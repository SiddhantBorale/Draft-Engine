#include "MainWindow.h"

#include "canvas/DrawingCanvas.h"
#include "ui/RulerWidget.h"

#include <QtWidgets>
#include <QShortcut>
#include <QColorDialog>
#include <QFileDialog>
#include <QMenuBar>
#include <QUndoStack>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDialogButtonBox>
#include <QInputDialog>

// Network
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>

// 3D
#include "3d/SceneView3d.h"



static constexpr double kWallHeightM  = 3.0;
static constexpr double kWallThickM   = 0.15;
static constexpr bool   kIncludeFloor = true;

/* ***************************************************************** */
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_canvas(new DrawingCanvas(this))
    , m_scene3d(new Scene3DView(this))          // embedded 3D view (hidden via stack by default)
    , m_viewStack(new QStackedWidget(this))
    , m_undo(new QUndoStack(this))
    , m_net(new QNetworkAccessManager(this))
{
    setupCentralWithRulers();
    setupToolPanel();
    setupMenus();
    setupLayersDock();

    m_canvas->setFocus();
    m_canvas->setUndoStack(m_undo);

    qApp->installEventFilter(this);
    resize(1200, 800);
}

/* ***************************************************************** */
void MainWindow::setupCentralWithRulers()
{
    auto* central = new QWidget(this);
    auto* grid    = new QGridLayout(central);
    grid->setContentsMargins(0,0,0,0);
    grid->setSpacing(0);

    // rulers
    auto* topRuler  = new RulerWidget(m_canvas, RulerWidget::Orientation::Horizontal, central);
    auto* leftRuler = new RulerWidget(m_canvas, RulerWidget::Orientation::Vertical, central);

    auto* corner = new QWidget(central);
    corner->setFixedSize(24,24);
    corner->setAutoFillBackground(true);
    corner->setBackgroundRole(QPalette::Base);

    // stack: [0] canvas, [1] 3D
    m_viewStack->addWidget(m_canvas);
    m_viewStack->addWidget(m_scene3d);
    m_viewStack->setCurrentWidget(m_canvas);

    // hide rulers when 3D is shown (simple heuristic)
    connect(m_viewStack, &QStackedWidget::currentChanged, this, [=](int idx){
        bool is3D = (m_viewStack->widget(idx) == m_scene3d);
        topRuler->setVisible(!is3D);
        leftRuler->setVisible(!is3D);
        corner->setVisible(!is3D);
    });

    grid->addWidget(corner,     0,0);
    grid->addWidget(topRuler,   0,1);
    grid->addWidget(leftRuler,  1,0);
    grid->addWidget(m_viewStack,1,1);

    setCentralWidget(central);
}

/* ***************************************************************** */
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
    addToolBtn("Dim (D)"    , DrawingCanvas::Tool::DimLinear);

    // Dim precision
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

    // Refine button
    auto* refineBtn = new QPushButton("Refine Vector", toolWidget);
    connect(refineBtn, &QPushButton::clicked, this, &MainWindow::refineVector);
    v->addWidget(refineBtn);

    // Units (menu populated in setupMenus, this is kept here for context)
    auto* strokeBtn = new QPushButton("Stroke Color", toolWidget);
    connect(strokeBtn, &QPushButton::clicked, this, &MainWindow::chooseColor);
    v->addWidget(strokeBtn);

    auto* fillBtn = new QPushButton("Fill Color", toolWidget);
    connect(fillBtn, &QPushButton::clicked, this, &MainWindow::chooseFillColor);
    v->addWidget(fillBtn);

    // Hatch / shading
    auto* hatchRow = new QWidget(toolWidget);
    auto* hatchLay = new QHBoxLayout(hatchRow);
    hatchLay->setContentsMargins(0,0,0,0);
    hatchLay->addWidget(new QLabel("Hatch:", hatchRow));
    auto* hatchBox = new QComboBox(hatchRow);
    hatchBox->addItem("None");
    hatchBox->addItem("Horizontal");
    hatchBox->addItem("Vertical");
    hatchBox->addItem("Diag \\ (Left)");
    hatchBox->addItem("Diag / (Right)");
    hatchBox->addItem("Cross");
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

    // Grid + zoom
    auto* gridBtn  = new QPushButton("Toggle Grid (G)", toolWidget);
    connect(gridBtn, &QPushButton::clicked, this, &MainWindow::toggleGrid);
    v->addWidget(gridBtn);

    auto* gb = new QGroupBox("Geometry", toolWidget);
    auto* g  = new QFormLayout(gb);

    // Corner radius
    m_cornerSpin = new QDoubleSpinBox(gb);
    m_cornerSpin->setRange(0.0, 1e6);
    m_cornerSpin->setDecimals(2);
    m_cornerSpin->setSingleStep(2.0);
    m_cornerSpin->setValue(10.0);
    auto* cornerBtn = new QPushButton("Apply Corner Radius", gb);
    connect(cornerBtn, &QPushButton::clicked, this, &MainWindow::applyCornerRadius);

    // Bend
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

    // Zoom
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

    // Join/apply fill
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

    // Tools dock
    auto* dock = new QDockWidget("Tools", this);
    dock->setWidget(toolWidget);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    // Shortcuts
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

/* ***************************************************************** */
void MainWindow::setupLayersDock()
{
    auto* pane = new QWidget(this);
    auto* lay  = new QVBoxLayout(pane);
    lay->setContentsMargins(6,6,6,6);

    m_layerTree = new QTreeWidget(pane);
    m_layerTree->setColumnCount(3);
    m_layerTree->setHeaderLabels(QStringList{"Layer","👁","🔒"});
    m_layerTree->setRootIsDecorated(false);
    m_layerTree->setSelectionMode(QAbstractItemView::SingleSelection);

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

    // seed layer 0
    auto* it0 = new QTreeWidgetItem(m_layerTree);
    it0->setText(0, "Layer 0");
    it0->setData(0, Qt::UserRole, 0);
    it0->setCheckState(1, Qt::Checked);
    it0->setCheckState(2, Qt::Unchecked);
    m_layerTree->setCurrentItem(it0);
    m_canvas->setCurrentLayer(0);

    connect(m_layerTree, &QTreeWidget::currentItemChanged,
            this, &MainWindow::setCurrentLayerFromTree);
    connect(m_layerTree, &QTreeWidget::itemChanged,
            this, &MainWindow::layerItemChanged);
    connect(addBtn, &QPushButton::clicked, this, &MainWindow::addLayer);
    connect(delBtn, &QPushButton::clicked, this, &MainWindow::removeSelectedLayer);

    auto* dock = new QDockWidget("Layers", this);
    dock->setWidget(pane);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

/* ***************************************************************** */
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

    // Edit
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

    // Tools
    auto* tools = menuBar()->addMenu("&Tools");
    m_actSetScale = tools->addAction(tr("Set &Scale…"));
    m_actSetScale->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(m_actSetScale, &QAction::triggered, this, &MainWindow::setScaleInteractive);

    // AI
    auto* ai = menuBar()->addMenu("&AI");
    ai->addAction("Blueprint → Vectorise…", QKeySequence("Ctrl+Shift+V"), this, &MainWindow::runBluePrintAI);
    ai->addAction("Refine Vector (light overlaps)…", QKeySequence("Ctrl+Shift+L"), this, &MainWindow::refineOverlapsLight);

    // Auto-rooms
    QAction* actRoomsController = ai->addAction(tr("Auto-rooms (Preview)…"));
    actRoomsController->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    connect(actRoomsController, &QAction::triggered, this, &MainWindow::openAutoRoomsDialog);

    QAction* actRoomsApply = ai->addAction(tr("Apply Auto-rooms"));
    actRoomsApply->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));
    connect(actRoomsApply, &QAction::triggered, this, [this]{
        if (!m_canvas) return;
        const int added = m_canvas->applyRoomsPreview();
        if (statusBar()) statusBar()->showMessage(tr("Auto-rooms: %1 items added").arg(added), 3000);
    });

    QAction* actRoomsCancel = ai->addAction(tr("Cancel Auto-rooms Preview"));
    actRoomsCancel->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(actRoomsCancel, &QAction::triggered, this, [this]{
        if (!m_canvas) return;
        m_canvas->cancelRoomsPreview();
    });

    // View (2D/3D inside the same window)
    auto* view = menuBar()->addMenu("&View");

    // Units submenu, for convenience here
    auto* unitsMenu = menuBar()->addMenu("Units");
    auto addDisp = [&](const QString& name, DrawingCanvas::Unit u){
        QAction* a = new QAction(name, this);
        connect(a, &QAction::triggered, this, [this,u](){ m_canvas->setDisplayUnit(u); });
        unitsMenu->addAction(a);
    };
    addDisp("Display: Millimeter", DrawingCanvas::Unit::Millimeter);
    addDisp("Display: Centimeter", DrawingCanvas::Unit::Centimeter);
    addDisp("Display: Meter",      DrawingCanvas::Unit::Meter);
    addDisp("Display: Inch",       DrawingCanvas::Unit::Inch);
    addDisp("Display: Foot",       DrawingCanvas::Unit::Foot);

    // Zoom
    view->addAction("Zoom In",     QKeySequence::ZoomIn,  this, &MainWindow::zoomIn);
    view->addAction("Zoom Out",    QKeySequence::ZoomOut, this, &MainWindow::zoomOut);
    view->addAction("Reset Zoom",  QKeySequence("Ctrl+0"), this, &MainWindow::zoomReset);
    view->addAction("Zoom to Fit", QKeySequence('F'),      this, &MainWindow::zoomToFit);
    view->addSeparator();

    // 2D / 3D switching (Blender-ish)
    auto* act2D = view->addAction(tr("Canvas (2D)"));
    act2D->setShortcut(QKeySequence("Ctrl+2"));
    connect(act2D, &QAction::triggered, this, &MainWindow::switchTo2D);

    auto* view3D = view->addMenu(tr("3D View (embedded)"));
    auto* actTop    = view3D->addAction(tr("Top (Ortho)"));
    auto* actFront  = view3D->addAction(tr("Front (Ortho)"));
    auto* actRight  = view3D->addAction(tr("Right (Ortho)"));
    auto* actPersp  = view3D->addAction(tr("Perspective"));

    actTop->setShortcut(QKeySequence("Ctrl+7"));
    actFront->setShortcut(QKeySequence("Ctrl+1"));
    actRight->setShortcut(QKeySequence("Ctrl+3"));
    actPersp->setShortcut(QKeySequence("Ctrl+5"));

    connect(actTop,   &QAction::triggered, this, &MainWindow::switch3DTop);
    connect(actFront, &QAction::triggered, this, &MainWindow::switch3DFront);
    connect(actRight, &QAction::triggered, this, &MainWindow::switch3DRight);
    connect(actPersp, &QAction::triggered, this, &MainWindow::switch3DPerspective);
}

/* ============================ actions ============================ */

void MainWindow::setScaleInteractive()
{
    statusBar()->showMessage(tr("Set Scale: click first point, then second point…"));
    m_canvas->startSetScaleMode();
}

void MainWindow::layerItemChanged(QTreeWidgetItem* it, int column)
{
    if (!it) return;
    const int id = it->data(0, Qt::UserRole).toInt();

    if (column == 1) {
        m_canvas->setLayerVisibility(id, it->checkState(1) == Qt::Checked);
    } else if (column == 2) {
        m_canvas->setLayerLocked(id, it->checkState(2) == Qt::Checked);
    }
}

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

void MainWindow::changeLineWidth(double w) { m_canvas->setLineWidth(w); }

void MainWindow::setDimPrecision(int p)
{
    if (!m_canvas) return;
    m_canvas->setDimPrecision(p);
}

void MainWindow::changeFillPattern(int idx)
{
    if (!m_canvas) return;

    // Map combo index → Qt brush style
    Qt::BrushStyle s = Qt::NoBrush;
    switch (idx) {
        case 1: s = Qt::HorPattern;   break; // horizontal
        case 2: s = Qt::VerPattern;   break; // vertical
        case 3: s = Qt::BDiagPattern; break; // "\" (left diagonal)
        case 4: s = Qt::FDiagPattern; break; // "/" (right diagonal)
        case 5: s = Qt::CrossPattern; break; // cross hatch
        default: s = Qt::NoBrush;     break;
    }

    m_canvas->setFillPattern(s);
}


void MainWindow::zoomIn()     { m_canvas->zoomIn(); }
void MainWindow::zoomOut()    { m_canvas->zoomOut(); }
void MainWindow::zoomReset()  { m_canvas->zoomReset(); }
void MainWindow::zoomToFit()  { m_canvas->fitInView(m_canvas->scene()->itemsBoundingRect().marginsAdded(QMarginsF(50,50,50,50)), Qt::KeepAspectRatio); }
void MainWindow::toggleGrid() { m_canvas->toggleGrid(); }
void MainWindow::newScene()   { m_canvas->scene()->clear(); }

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

/* AI vectorise */
void MainWindow::runBluePrintAI()
{
    const QString fn = QFileDialog::getOpenFileName(
        this, "Choose blueprint image", {},
        "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)");
    if (fn.isEmpty()) return;

    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);

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

    auto addField = [&](const char* name, const QByteArray& val){
        QHttpPart p;
        p.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QVariant(QString("form-data; name=\"%1\"").arg(name)));
        p.setBody(val);
        multi->append(p);
    };

    addField("min_line_len", QByteArray::number(36));
    addField("canny1",       QByteArray::number(70));
    addField("canny2",       QByteArray::number(160));
    addField("approx_eps",   QByteArray::number(2.0));
    addField("text_suppr",   QByteArray::number(1));
    addField("side_denoise_on", QByteArray::number(1));
    addField("use_mlsd",     QByteArray::number(1));
    addField("door_simpl",   QByteArray::number(1));
    addField("room_close",   QByteArray::number(1));

    QNetworkRequest req(QUrl("http://127.0.0.1:8000/vectorise"));
    auto* reply = m_net->post(req, multi);
    multi->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, &MainWindow::onVectoriseFinished);
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
        QMessageBox::warning(this, "Vectorise", QString("Bad JSON: %1").arg(perr.errorString()));
        return;
    }
    m_canvas->loadFromJson(doc);
    m_canvas->fitInView(m_canvas->scene()->itemsBoundingRect().marginsAdded(QMarginsF(50,50,50,50)),
                        Qt::KeepAspectRatio);
}

/* Undo/Redo */
void MainWindow::undo() { m_undo->undo(); }
void MainWindow::redo() { m_undo->redo(); }

/* Layers */
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
    m_canvas->moveItemsToLayer(id, 0);
    delete it;
    if (m_layerTree->topLevelItemCount() > 0)
        m_layerTree->setCurrentItem(m_layerTree->topLevelItem(0));
}
void MainWindow::setCurrentLayerFromTree(QTreeWidgetItem* it, QTreeWidgetItem*)
{
    if (!it) return;
    const int id = it->data(0, Qt::UserRole).toInt();
    m_canvas->setCurrentLayer(id);
}

/* Spacebar pan on the canvas only */
bool MainWindow::eventFilter(QObject* obj, QEvent* ev)
{
    Q_UNUSED(obj);
    if (!m_canvas) return false;

    if (ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Space && !ke->isAutoRepeat()) {
            m_canvas->setDragMode(QGraphicsView::ScrollHandDrag);
            return false;
        }
    } else if (ev->type() == QEvent::KeyRelease) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Space && !ke->isAutoRepeat()) {
            m_canvas->setDragMode(QGraphicsView::RubberBandDrag);
            return false;
        }
    }
    return false;
}

/* Geometry helpers */
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
void MainWindow::changeCornerRadius(double r)
{
    auto sel = m_canvas->scene()->selectedItems();
    if (sel.size() != 1) return;
    if (auto* rr = qgraphicsitem_cast<RoundedRectItem*>(sel.first())) {
        rr->setRadius(r, r);
        m_canvas->viewport()->update();
        m_canvas->setSelectedCornerRadius(r);
        m_canvas->refreshHandles();
    }
}

/* Refine */
void MainWindow::refineVector()
{
    DrawingCanvas::RefineParams p;
    p.gapPx       = 12.0;
    p.axisSnapDeg = 7.5;
    p.mergePx     = 10;
    p.extendPx    = 10.0;
    p.minLenPx    = 1.0;

    const int edits = m_canvas->refineVector(p);
    statusBar()->showMessage(QString("Refine complete — %1 edits").arg(edits), 4000);
}
void MainWindow::refineOverlapsLight()
{
    const int n = m_canvas->refineOverlapsLight(2.0, 0.80, 3.0);
    statusBar()->showMessage(QString("Overlap cleanup: %1 merged").arg(n), 3000);
}

/* Auto-rooms dialog (preview + apply) */
void MainWindow::openAutoRoomsDialog()
{
    if (!m_canvas) return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Auto-rooms — Live Preview"));

    auto* form = new QFormLayout;

    auto* spWeldTolPx   = new QDoubleSpinBox; spWeldTolPx->setRange(0.1, 50.0);  spWeldTolPx->setDecimals(1); spWeldTolPx->setValue(8.0);
    auto* spAxisSnapDeg = new QDoubleSpinBox; spAxisSnapDeg->setRange(0.0, 20.0); spAxisSnapDeg->setDecimals(1); spAxisSnapDeg->setValue(8.0);
    auto* spMinAreaM2   = new QDoubleSpinBox; spMinAreaM2->setRange(0.0, 500.0); spMinAreaM2->setDecimals(2); spMinAreaM2->setValue(0.30);
    auto* spMinSidePx   = new QDoubleSpinBox; spMinSidePx->setRange(0.0, 500.0); spMinSidePx->setDecimals(0); spMinSidePx->setValue(35.0);
    auto* spMinWallPx   = new QDoubleSpinBox; spMinWallPx->setRange(0.0, 200.0); spMinWallPx->setDecimals(0); spMinWallPx->setValue(12.0);
    auto* spRailFrac    = new QDoubleSpinBox; spRailFrac->setRange(0.0, 1.0);    spRailFrac->setSingleStep(0.05); spRailFrac->setDecimals(2); spRailFrac->setValue(0.70);
    auto* spDoorGapPx   = new QDoubleSpinBox; spDoorGapPx->setRange(0.0, 80.0);  spDoorGapPx->setDecimals(0); spDoorGapPx->setValue(18.0);
    auto* spMinStrong   = new QSpinBox;       spMinStrong->setRange(0, 4);       spMinStrong->setValue(3);

    form->addRow(tr("<b>Geometry</b>"), new QLabel);
    form->addRow(tr("Weld tolerance (px):"), spWeldTolPx);
    form->addRow(tr("Axis snap (deg):"),     spAxisSnapDeg);
    form->addRow(tr("Min area (m²):"),       spMinAreaM2);
    form->addRow(tr("Min side (px):"),       spMinSidePx);
    form->addRow(tr("Min wall segment (px):"), spMinWallPx);

    form->addRow(tr("<b>Wall coverage</b>"), new QLabel);
    form->addRow(tr("Strong coverage fraction:"), spRailFrac);
    form->addRow(tr("Max door gap (px):"),       spDoorGapPx);
    form->addRow(tr("Min strong sides (0–4):"),  spMinStrong);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, &dlg);

    auto* vbox = new QVBoxLayout;
    vbox->addLayout(form);
    vbox->addWidget(btns);
    dlg.setLayout(vbox);

    auto sendParams = [this,
                       spWeldTolPx, spMinAreaM2, spAxisSnapDeg,
                       spMinSidePx, spMinWallPx, spRailFrac, spDoorGapPx, spMinStrong]()
    {
        m_canvas->updateRoomsPreview(
            spWeldTolPx->value(),
            spMinAreaM2->value(),
            spAxisSnapDeg->value(),
            spMinSidePx->value(),
            spMinWallPx->value(),
            spRailFrac->value(),
            spDoorGapPx->value(),
            spMinStrong->value()
        );
    };

    auto connectD = [&](QDoubleSpinBox* sp){
        QObject::connect(sp, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
                         &dlg, [sendParams](double){ sendParams(); });
    };
    auto connectI = [&](QSpinBox* sp){
        QObject::connect(sp, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                         &dlg, [sendParams](int){ sendParams(); });
    };

    connectD(spWeldTolPx);
    connectD(spAxisSnapDeg);
    connectD(spMinAreaM2);
    connectD(spMinSidePx);
    connectD(spMinWallPx);
    connectD(spRailFrac);
    connectD(spDoorGapPx);
    connectI(spMinStrong);

    QObject::connect(btns->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dlg, [this]{
        if (!m_canvas) return;
        const int added = m_canvas->applyRoomsPreview();
        if (statusBar()) statusBar()->showMessage(tr("Auto-rooms: %1 room(s) added").arg(added), 3000);
    });
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);

    sendParams();
    dlg.exec();

    if (m_canvas) m_canvas->cancelRoomsPreview();
}

/* ============================ 3D switching ============================ */

void MainWindow::prepare3D(Scene3DView::ViewMode mode)
{
    if (!m_scene3d || !m_canvas) return;

    // rebuild from current 2D geometry each time we switch in
    m_scene3d->buildFromCanvas(m_canvas, kWallHeightM, kWallThickM, kIncludeFloor);
    m_scene3d->setMode(mode);

    m_viewStack->setCurrentWidget(m_scene3d);
}

void MainWindow::switchTo2D()
{
    if (!m_viewStack) return;
    m_viewStack->setCurrentWidget(m_canvas);
}

void MainWindow::switch3DTop()         { prepare3D(Scene3DView::ViewMode::OrthoTop); }
void MainWindow::switch3DFront()       { prepare3D(Scene3DView::ViewMode::OrthoFront); }
void MainWindow::switch3DRight()       { prepare3D(Scene3DView::ViewMode::OrthoRight); }
void MainWindow::switch3DPerspective() { prepare3D(Scene3DView::ViewMode::Perspective); }

/* ============================ misc ============================ */

void MainWindow::promptForProjectUnits()
{
    QStringList opts{ "Millimeter", "Centimeter", "Meter", "Inch", "Foot" };
    bool ok = false;
    const QString sel = QInputDialog::getItem(this, "Project Units",
                                              "Choose base units:", opts, 0, false, &ok);
    if (!ok) return;

    auto parse = [](const QString& s){
        using U = DrawingCanvas::Unit;
        if (s=="Centimeter") return U::Centimeter;
        if (s=="Meter")      return U::Meter;
        if (s=="Inch")       return U::Inch;
        if (s=="Foot")       return U::Foot;
        return U::Millimeter;
    };
    auto u = parse(sel);
    m_canvas->setProjectUnit(u);
    m_canvas->setDisplayUnit(u);
    m_canvas->setUnitPrecision((u==DrawingCanvas::Unit::Meter || u==DrawingCanvas::Unit::Foot) ? 3 : 1);
}
