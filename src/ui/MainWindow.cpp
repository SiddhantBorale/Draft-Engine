#include "MainWindow.h"
#include "canvas/DrawingCanvas.h"
#include "ui/RulerWidget.h"

#include <QtWidgets>
#include <QShortcut>
#include <QColorDialog>
#include <QFileDialog>
#include <QJsonDocument>
#include <QMenuBar>
#include <QUndoStack>
#include <QListWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QMessageBox>
#include <QJsonObject>
#include <QJsonArray>


// Network
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_canvas(new DrawingCanvas(this)),
      m_net(new QNetworkAccessManager(this)),
      m_undo(new QUndoStack(this))
{
    setCentralWidget(m_canvas);
    setupCentralWithRulers(); 
    setupToolPanel();
    setupMenus();
    setupLayersDock();

    m_canvas->setFocus();

    m_canvas->setUndoStack(m_undo);

    qApp->installEventFilter(this);

    resize(1200, 800);
}

void MainWindow::setupCentralWithRulers()
{
    auto* central = new QWidget(this);
    auto* grid    = new QGridLayout(central);
    grid->setContentsMargins(0,0,0,0);
    grid->setSpacing(0);

    auto* topRuler = new RulerWidget(m_canvas, RulerWidget::Orientation::Horizontal, central);
    auto* leftRulr = new RulerWidget(m_canvas, RulerWidget::Orientation::Vertical, central);

    // small corner square where rulers meet
    auto* corner = new QWidget(central);
    corner->setFixedSize(24,24);
    corner->setAutoFillBackground(true);
    corner->setBackgroundRole(QPalette::Base);

    grid->addWidget(corner,   0,0);
    grid->addWidget(topRuler, 0,1);
    grid->addWidget(leftRulr, 1,0);
    grid->addWidget(m_canvas, 1,1);

    setCentralWidget(central);
}


/* ─────────────────────────  Tool panel  ───────────────────────── */
void MainWindow::setupToolPanel()
{
    auto* toolWidget = new QWidget;
    auto* v = new QVBoxLayout(toolWidget);

    auto addToolBtn = [&](const QString& text, DrawingCanvas::Tool t){
        auto* b = new QPushButton(text, toolWidget);
        connect(b, &QPushButton::clicked, this, [=]{ m_canvas->setCurrentTool(t); });
        v->addWidget(b);
    };

    addToolBtn("Select (S)" , DrawingCanvas::Tool::Select);
    addToolBtn("Line (L)"   , DrawingCanvas::Tool::Line);
    addToolBtn("Rect (R)"   , DrawingCanvas::Tool::Rect);
    addToolBtn("Ellipse (C)", DrawingCanvas::Tool::Ellipse);
    addToolBtn("Polygon (P)", DrawingCanvas::Tool::Polygon);
    addToolBtn("Dim (D)", DrawingCanvas::Tool::DimLinear);

    auto* precRow = new QWidget(toolWidget);
    auto* precLay = new QHBoxLayout(precRow);
    precLay->setContentsMargins(0,0,0,0);
    precLay->addWidget(new QLabel("Dim precision:", precRow));
    auto* precSpin = new QSpinBox(precRow);
    precSpin->setRange(0, 6);
    precSpin->setValue(2);
    connect(precSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::setDimPrecision);
    precLay->addWidget(precSpin);
    v->addWidget(precRow);

    // Tools dock (somewhere you add your buttons)
    auto* refineBtn = new QPushButton("Refine Vector", toolWidget);
    connect(refineBtn, &QPushButton::clicked, this, &MainWindow::refineVector);
    v->addWidget(refineBtn);

    // Or menu:
    auto* ai = menuBar()->addMenu("&AI");
    ai->addAction("Refine Vector", QKeySequence("Ctrl+Shift+R"), this, &MainWindow::refineVector);


    // Stroke color
    auto* strokeBtn = new QPushButton("Stroke Color", toolWidget);
    connect(strokeBtn, &QPushButton::clicked, this, &MainWindow::chooseColor);
    v->addWidget(strokeBtn);

    // Fill color
    auto* fillBtn = new QPushButton("Fill Color", toolWidget);
    connect(fillBtn, &QPushButton::clicked, this, &MainWindow::chooseFillColor);
    v->addWidget(fillBtn);

    // Hatch / line shading
    auto* hatchRow = new QWidget(toolWidget);
    auto* hatchLay = new QHBoxLayout(hatchRow);
    hatchLay->setContentsMargins(0,0,0,0);
    hatchLay->addWidget(new QLabel("Hatch:", hatchRow));

    auto* hatchBox = new QComboBox(hatchRow);
    hatchBox->addItem("None");          // 0
    hatchBox->addItem("Horizontal");    // 1
    hatchBox->addItem("Vertical");      // 2
    hatchBox->addItem("Diag \\ (Left)");// 3  (Qt::BDiagPattern)
    hatchBox->addItem("Diag / (Right)");// 4  (Qt::FDiagPattern)
    hatchBox->addItem("Cross");         // 5  (optional bonus)
    connect(hatchBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::changeFillPattern);
    hatchLay->addWidget(hatchBox);
    v->addWidget(hatchRow);

    // Line width
    auto* lwRow = new QWidget(toolWidget);
    auto* lwLay = new QHBoxLayout(lwRow);
    lwLay->setContentsMargins(0,0,0,0);
    lwLay->addWidget(new QLabel("Line Width:", lwRow));
    auto* lwSpin = new QDoubleSpinBox(lwRow);
    lwSpin->setRange(0.0, 50.0);
    lwSpin->setSingleStep(0.5);
    lwSpin->setValue(1.0);
    connect(lwSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MainWindow::changeLineWidth);
    lwLay->addWidget(lwSpin);
    v->addWidget(lwRow);

    // Grid toggle
    auto* gridBtn  = new QPushButton("Toggle Grid (G)", toolWidget);
    connect(gridBtn, &QPushButton::clicked, this, &MainWindow::toggleGrid);
    v->addWidget(gridBtn);

    auto* gb = new QGroupBox("Geometry", toolWidget);
    auto* g  = new QFormLayout(gb);

    // Corner radius (for rects & polygons)
    m_cornerSpin = new QDoubleSpinBox(gb);
    m_cornerSpin->setRange(0.0, 1e6);
    m_cornerSpin->setDecimals(2);
    m_cornerSpin->setSingleStep(2.0);
    m_cornerSpin->setValue(10.0);
    auto* cornerBtn = new QPushButton("Apply Corner Radius", gb);
    connect(cornerBtn, &QPushButton::clicked, this, &MainWindow::applyCornerRadius);

    // Bend sagitta (for a selected line)
    m_bendSpin = new QDoubleSpinBox(gb);
    m_bendSpin->setRange(-1e6, 1e6);
    m_bendSpin->setDecimals(2);
    m_bendSpin->setSingleStep(2.0);
    m_bendSpin->setValue(20.0);
    auto* bendBtn = new QPushButton("Bend Line to Arc", gb);
    connect(bendBtn, &QPushButton::clicked, this, &MainWindow::applyLineBend);

    g->addRow("Corner radius:", m_cornerSpin);
    g->addRow(cornerBtn);
    g->addRow("Sagitta:", m_bendSpin);
    g->addRow(bendBtn);

    v->addWidget(gb);

    // Zoom controls
    auto* zoomRow = new QWidget(toolWidget);
    auto* zoomLay = new QHBoxLayout(zoomRow);
    zoomLay->setContentsMargins(0,0,0,0);

    auto* zoomInBtn    = new QPushButton("Zoom In (+)", zoomRow);
    auto* zoomOutBtn   = new QPushButton("Zoom Out (-)", zoomRow);
    auto* zoomResetBtn = new QPushButton("Reset (0)", zoomRow);
    auto* zoomFitBtn   = new QPushButton("Fit (F)", zoomRow);
    connect(zoomInBtn,    &QPushButton::clicked, this, &MainWindow::zoomIn);
    connect(zoomOutBtn,   &QPushButton::clicked, this, &MainWindow::zoomOut);
    connect(zoomResetBtn, &QPushButton::clicked, this, &MainWindow::zoomReset);
    connect(zoomFitBtn,   &QPushButton::clicked, this, &MainWindow::zoomToFit);
    zoomLay->addWidget(zoomInBtn);
    zoomLay->addWidget(zoomOutBtn);
    zoomLay->addWidget(zoomResetBtn);
    zoomLay->addWidget(zoomFitBtn);
    v->addWidget(zoomRow);

    v->addStretch();

    

    auto* joinBtn = new QPushButton("Join Lines → Shape", toolWidget);
    connect(joinBtn, &QPushButton::clicked, this, [this]{
        if (!m_canvas->joinSelectedLinesToPolygon(2.0)) {
            QMessageBox::information(this, "Join Lines", "Select 3+ connected lines that form a closed loop.");
        }
    });
    v->addWidget(joinBtn);

    auto* applyFillBtn = new QPushButton("Apply Fill to Selection", toolWidget);
    connect(applyFillBtn, &QPushButton::clicked, this, [this]{ m_canvas->applyFillToSelection(); });
    v->addWidget(applyFillBtn);

    auto* dock = new QDockWidget("Tools", this);
    dock->setWidget(toolWidget);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    // keyboard shortcuts
    auto makeSC = [&](QKeySequence ks, auto slot){
        auto* sc = new QShortcut(ks, this);
        connect(sc, &QShortcut::activated, slot);
    };
    makeSC(QKeySequence('S'), [this]{ m_canvas->setCurrentTool(DrawingCanvas::Tool::Select); });
    makeSC(QKeySequence('L'), [this]{ m_canvas->setCurrentTool(DrawingCanvas::Tool::Line);   });
    makeSC(QKeySequence('R'), [this]{ m_canvas->setCurrentTool(DrawingCanvas::Tool::Rect);   });
    makeSC(QKeySequence('C'), [this]{ m_canvas->setCurrentTool(DrawingCanvas::Tool::Ellipse);} );
    makeSC(QKeySequence('P'), [this]{ m_canvas->setCurrentTool(DrawingCanvas::Tool::Polygon);} );
    makeSC(QKeySequence('G'), [this]{ toggleGrid(); });

    // Zoom shortcuts (Cmd/CTRL +/-, 0, F)
#ifdef Q_OS_MAC
    makeSC(QKeySequence("Meta++"), [this]{ zoomIn(); });
    makeSC(QKeySequence("Meta+-"), [this]{ zoomOut(); });
    makeSC(QKeySequence("Meta+0"), [this]{ zoomReset(); });
    makeSC(QKeySequence('F'),      [this]{ zoomToFit(); });
#else
    makeSC(QKeySequence("Ctrl++"), [this]{ zoomIn(); });
    makeSC(QKeySequence("Ctrl+-"), [this]{ zoomOut(); });
    makeSC(QKeySequence("Ctrl+0"), [this]{ zoomReset(); });
    makeSC(QKeySequence('F'),      [this]{ zoomToFit(); });
#endif
}

void MainWindow::changeFillPattern(int idx)
{
    // Map combo index → Qt brush style
    Qt::BrushStyle s = Qt::NoBrush;
    switch (idx) {
        case 1: s = Qt::HorPattern;   break; // horizontal
        case 2: s = Qt::VerPattern;   break; // vertical ("straight")
        case 3: s = Qt::BDiagPattern; break; // "\" (left diagonal)
        case 4: s = Qt::FDiagPattern; break; // "/" (right diagonal)
        case 5: s = Qt::CrossPattern; break; // optional cross
        default: s = Qt::NoBrush;     break;
    }
    m_canvas->setFillPattern(s);
}


/* ───────────────────────────  Layers dock  ───────────────────────── */
void MainWindow::setupLayersDock()
{
    auto* pane = new QWidget(this);
    auto* lay  = new QVBoxLayout(pane);
    lay->setContentsMargins(6,6,6,6);

    m_layerTree = new QTreeWidget(pane);
    m_layerTree->setColumnCount(3);
    QStringList headers{"Layer","👁","🔒"};
    m_layerTree->setHeaderLabels(headers);
    m_layerTree->setRootIsDecorated(false);
    m_layerTree->setSelectionMode(QAbstractItemView::SingleSelection);

    // controls row
    auto* row = new QWidget(pane);
    auto* rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(0,0,0,0);
    auto* addBtn = new QPushButton("+", row);
    auto* delBtn = new QPushButton("–", row);
    rowLay->addWidget(addBtn);
    rowLay->addWidget(delBtn);
    rowLay->addStretch();

    lay->addWidget(new QLabel("Layers", pane));
    lay->addWidget(m_layerTree);
    lay->addWidget(row);

    // Seed Layer 0
    auto* it0 = new QTreeWidgetItem(m_layerTree);
    it0->setText(0, "Layer 0");
    it0->setData(0, Qt::UserRole, 0);
    it0->setCheckState(1, Qt::Checked);   // visible
    it0->setCheckState(2, Qt::Unchecked); // unlocked
    m_layerTree->setCurrentItem(it0);

    connect(m_layerTree, &QTreeWidget::currentItemChanged,
            this, &MainWindow::setCurrentLayerFromTree);
    connect(m_layerTree, &QTreeWidget::itemChanged,
            this, &MainWindow::layerItemChanged);
    connect(addBtn, &QPushButton::clicked, this, &MainWindow::addLayer);
    connect(delBtn, &QPushButton::clicked, this, &MainWindow::removeSelectedLayer);

    auto* dock = new QDockWidget("Layers", this);
    dock->setWidget(pane);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // ensure canvas starts on layer 0
    m_canvas->setCurrentLayer(0);
}
/* ───────────────────────────  Menus  ─────────────────────────── */
void MainWindow::setupMenus()
{
    // File
    auto* file = menuBar()->addMenu("&File");
    file->addAction("New",         QKeySequence::New,  this, &MainWindow::newScene);
    file->addAction("Open JSON…",  QKeySequence::Open, this, &MainWindow::openJson);
    file->addAction("Save JSON…",  QKeySequence::Save, this, &MainWindow::saveJson);
    file->addSeparator();
    file->addAction("Import SVG…", QKeySequence("Ctrl+I"), this, &MainWindow::importSvg);
    file->addAction("Export SVG…", QKeySequence("Ctrl+E"), this, &MainWindow::exportSvg);
    file->addSeparator();
    file->addAction("E&xit", QKeySequence::Quit, qApp, &QCoreApplication::quit);

    // Edit (Undo/Redo)
    auto* edit = menuBar()->addMenu("&Edit");
    edit->addAction("Undo", QKeySequence::Undo, m_undo, &QUndoStack::undo);
    edit->addAction("Redo", QKeySequence::Redo, m_undo, &QUndoStack::redo);
    edit->addSeparator();
    edit->addAction("Join Lines → Shape", this, [this]{ 
        if (!m_canvas->joinSelectedLinesToPolygon(2.0)) {
            QMessageBox::information(this, "Join Lines", "Select 3+ connected lines that form a closed loop.");
        }
    });
    edit->addAction("Apply Fill to Selection", this, [this]{ m_canvas->applyFillToSelection(); });

    // View (Zoom)
    auto* view = menuBar()->addMenu("&View");
    view->addAction("Zoom In",    QKeySequence::ZoomIn,  this, &MainWindow::zoomIn);
    view->addAction("Zoom Out",   QKeySequence::ZoomOut, this, &MainWindow::zoomOut);
    view->addAction("Reset Zoom", QKeySequence("Ctrl+0"), this, &MainWindow::zoomReset);
    view->addAction("Zoom to Fit", QKeySequence('F'), this, &MainWindow::zoomToFit);

    // AI
    auto* ai = menuBar()->addMenu("&AI");
    ai->addAction("Blueprint → Vectorise…",
                  QKeySequence("Ctrl+Shift+V"),
                  this, &MainWindow::runBluePrintAI);
}

void MainWindow::layerItemChanged(QTreeWidgetItem* it, int column)
{
    if (!it) return;
    const int id = it->data(0, Qt::UserRole).toInt();

    if (column == 1) { // visibility
        const bool vis = (it->checkState(1) == Qt::Checked);
        m_canvas->setLayerVisibility(id, vis);
    } else if (column == 2) { // lock
        const bool lock = (it->checkState(2) == Qt::Checked);
        m_canvas->setLayerLocked(id, lock);
    }
}

/* ───────────────────────  Slots / actions  ───────────────────── */
void MainWindow::chooseColor()
{
    const QColor c = QColorDialog::getColor(m_canvas->palette().color(QPalette::Text), this);
    if (c.isValid()) m_canvas->setCurrentColor(c);
}

void MainWindow::chooseFillColor()
{
    const QColor c = QColorDialog::getColor(Qt::transparent, this, "Fill Color",
                                            QColorDialog::ShowAlphaChannel);
    if (c.isValid()) m_canvas->setFillColor(c);
}

void MainWindow::changeLineWidth(double w)
{
    m_canvas->setLineWidth(w);
}

// Zoom passthroughs
void MainWindow::zoomIn()     { m_canvas->zoomIn(); }
void MainWindow::zoomOut()    { m_canvas->zoomOut(); }
void MainWindow::zoomReset()  { m_canvas->zoomReset(); }
void MainWindow::zoomToFit()  { m_canvas->fitInView(m_canvas->scene()->itemsBoundingRect().marginsAdded(QMarginsF(50,50,50,50)), Qt::KeepAspectRatio); }

void MainWindow::toggleGrid()  { m_canvas->toggleGrid(); }
void MainWindow::newScene()    { m_canvas->scene()->clear(); }

/* JSON */
void MainWindow::openJson()
{
    const QString fn = QFileDialog::getOpenFileName(this, "Open JSON", {}, "JSON (*.json)");
    if (fn.isEmpty()) return;
    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly)) return;
    m_canvas->loadFromJson(QJsonDocument::fromJson(f.readAll()));
}

void MainWindow::saveJson()
{
    const QString fn = QFileDialog::getSaveFileName(this, "Save JSON", "scene.json", "JSON (*.json)");
    if (fn.isEmpty()) return;
    QFile f(fn);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(m_canvas->saveToJson().toJson());
}

/* SVG */
void MainWindow::importSvg()
{
    const QString fn = QFileDialog::getOpenFileName(this, "Import SVG", {}, "SVG (*.svg)");
    if (!fn.isEmpty()) m_canvas->importSvg(fn);
}

void MainWindow::exportSvg()
{
    const QString fn = QFileDialog::getSaveFileName(this, "Export SVG", "scene.svg", "SVG (*.svg)");
    if (!fn.isEmpty()) m_canvas->exportSvg(fn);
}

void MainWindow::runBluePrintAI()
{
    const QString fn = QFileDialog::getOpenFileName(
        this, "Choose blueprint image", {},
        "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)");
    if (fn.isEmpty()) return;

    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    // image part
    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QVariant("form-data; name=\"image\"; filename=\"" + QFileInfo(fn).fileName() + "\""));
    const QString ext = QFileInfo(fn).suffix().toLower();
    const QString mime = (ext == "jpg" || ext == "jpeg") ? "image/jpeg" :
                         (ext == "png") ? "image/png" :
                         (ext == "bmp") ? "image/bmp" : "application/octet-stream";
    imagePart.setHeader(QNetworkRequest::ContentTypeHeader, mime);
    auto* file = new QFile(fn);
    if (!file->open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Vectorise", "Could not open file.");
        file->deleteLater();
        multi->deleteLater();
        return;
    }
    imagePart.setBodyDevice(file);
    file->setParent(multi);
    multi->append(imagePart);

    // helper to add numeric fields
    auto addField = [&](const char* name, const QByteArray& val){
        QHttpPart p;
        p.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QVariant(QString("form-data; name=\"%1\"").arg(name)));
        p.setBody(val);
        multi->append(p);
    };

    // tuned defaults for floor plans (feel free to expose as UI later)
    addField("min_line_len", QByteArray::number(55));   // longer segments
    addField("canny1",       QByteArray::number(60));
    addField("canny2",       QByteArray::number(140));
    addField("approx_eps",   QByteArray::number(0.012)); // 1.2% of perimeter
    addField("close_radius", QByteArray::number(3));     // px
    addField("min_poly_area",QByteArray::number(300));   // px^2
    addField("merge_angle",  QByteArray::number(6.0));   // deg
    addField("merge_dist",   QByteArray::number(6.0));   // px

    QNetworkRequest req(QUrl("http://127.0.0.1:8000/vectorise"));
    auto* reply = m_net->post(req, multi);
    multi->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, &MainWindow::onVectoriseFinished);
    statusBar()->showMessage("Vectorising…");
}




void MainWindow::onVectoriseFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    reply->deleteLater();

    if (status < 200 || status >= 300) {
        QMessageBox::warning(this, "Vectorise",
                             QString("Server error (%1): %2").arg(status).arg(QString::fromUtf8(data)));
        return;
    }

    QJsonParseError perr{};
    QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (perr.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, "Vectorise",
                             QString("Bad JSON: %1").arg(perr.errorString()));
        return;
    }

    // Load into canvas (replaces scene). If you prefer merging, say the word and I'll switch it.
    m_canvas->loadFromJson(doc);

    // Fit view nicely
    m_canvas->fitInView(m_canvas->scene()->itemsBoundingRect().marginsAdded(QMarginsF(50,50,50,50)),
                        Qt::KeepAspectRatio);
}


/* ───────────────────────  Undo/Redo stubs  ───────────────────── */
// These are placeholders; actual commands will be pushed from DrawingCanvas
void MainWindow::undo() { m_undo->undo(); }
void MainWindow::redo() { m_undo->redo(); }

/* ───────────────────────  Layers actions  ───────────────────── */
void MainWindow::addLayer()
{
    auto* it = new QTreeWidgetItem(m_layerTree);
    it->setText(0, QString("Layer %1").arg(m_nextLayerId));
    it->setData(0, Qt::UserRole, m_nextLayerId);
    it->setFlags(it->flags() | Qt::ItemIsEditable);
    it->setCheckState(1, Qt::Checked);
    it->setCheckState(2, Qt::Unchecked);
    m_layerTree->setCurrentItem(it);
    m_nextLayerId++;
}


void MainWindow::removeSelectedLayer()
{
    auto* it = m_layerTree->currentItem();
    if (!it) return;

    const int id = it->data(0, Qt::UserRole).toInt();
    if (id == 0) return; // keep Layer 0

    // Move items from this layer to 0 (safer than delete)
    m_canvas->moveItemsToLayer(id, 0);

    delete it;
    // select something sensible
    if (m_layerTree->topLevelItemCount() > 0)
        m_layerTree->setCurrentItem(m_layerTree->topLevelItem(0));
}

// void MainWindow::setCurrentLayerFromList()
// {
//     int row = m_layerList->currentRow();
//     if (row < 0) row = 0;
//     m_canvas->setCurrentLayer(row);
// }

void MainWindow::setCurrentLayerFromTree(QTreeWidgetItem* it, QTreeWidgetItem* /*prev*/)
{
    if (!it) return;
    const int id = it->data(0, Qt::UserRole).toInt();
    m_canvas->setCurrentLayer(id);
}

/* ───────────────────────  Spacebar pan  ───────────────────── */
bool MainWindow::eventFilter(QObject* obj, QEvent* ev)
{
    Q_UNUSED(obj);
    if (!m_canvas) return false;

    if (ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Space && !ke->isAutoRepeat()) {
            m_canvas->setDragMode(QGraphicsView::ScrollHandDrag);
            return false; // let others see it too
        }
    } else if (ev->type() == QEvent::KeyRelease) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Space && !ke->isAutoRepeat()) {
            // restore normal mode (Select uses RubberBand)
            m_canvas->setDragMode(QGraphicsView::RubberBandDrag);
            return false;
        }
    }
    return false;
}

void MainWindow::applyCornerRadius()
{
    if (!m_canvas) return;
    const double r = m_cornerSpin ? m_cornerSpin->value() : 0.0;
    if (r <= 0.0) return;
    m_canvas->roundSelectedShape(r);
}

void MainWindow::applyLineBend()
{
    if (!m_canvas) return;
    const double s = m_bendSpin ? m_bendSpin->value() : 0.0;
    if (qFuzzyIsNull(s)) return;
    m_canvas->bendSelectedLine(s);
}

void MainWindow::changeCornerRadius(double r) {
    auto sel = m_canvas->scene()->selectedItems();
    if (sel.size() != 1) return;
    if (auto* rr = qgraphicsitem_cast<RoundedRectItem*>(sel.first())) {
        rr->setRadius(r, r);
        m_canvas->viewport()->update();
        m_canvas->setSelectedCornerRadius(r);
        m_canvas->refreshHandles(); // re-place radius knobs
    }
}

void MainWindow::setDimPrecision(int p) {
    m_canvas->setDimPrecision(p);
}

void MainWindow::refineVector()
{
    DrawingCanvas::RefineParams params;
    params.gapPx       = 20.0;  // snap nearby endpoints together (px)
    params.axisSnapDeg = 12;   // snap near-axis lines to 0° / 90°
    params.mergePx     = 10;   // merge collinear segments if gap ≤ mergePx
    params.extendPx    = 25.0;  // extend to intersect if close (px)
    params.minLenPx    = 1.0;   // drop tiny fragments

    const int edits = m_canvas->refineVector(params);
    statusBar()->showMessage(QString("Refine complete — %1 edits").arg(edits), 4000);
}