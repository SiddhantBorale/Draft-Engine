#include "MainWindow.h"
#include "canvas/DrawingCanvas.h"

#include <QtWidgets>
#include <QShortcut>
#include <QColorDialog>
#include <QFileDialog>
#include <QJsonDocument>
#include <QMenuBar>
#include <QUndoStack>
#include <QListWidget>

// Network
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_canvas(new DrawingCanvas(this)),
      m_net(new QNetworkAccessManager(this)),
      m_undo(new QUndoStack(this))
{
    setCentralWidget(m_canvas);
    setupToolPanel();
    setupMenus();
    setupLayersDock();

    // enable Spacebar pan via eventFilter
    qApp->installEventFilter(this);

    resize(1200, 800);
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

    // Stroke color
    auto* strokeBtn = new QPushButton("Stroke Color", toolWidget);
    connect(strokeBtn, &QPushButton::clicked, this, &MainWindow::chooseColor);
    v->addWidget(strokeBtn);

    // Fill color
    auto* fillBtn = new QPushButton("Fill Color", toolWidget);
    connect(fillBtn, &QPushButton::clicked, this, &MainWindow::chooseFillColor);
    v->addWidget(fillBtn);

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

/* ───────────────────────────  Layers dock  ───────────────────────── */
void MainWindow::setupLayersDock()
{
    auto* pane = new QWidget(this);
    auto* lay  = new QVBoxLayout(pane);
    lay->setContentsMargins(4,4,4,4);

    m_layerList = new QListWidget(pane);
    m_layerList->setSelectionMode(QAbstractItemView::SingleSelection);

    auto* row = new QWidget(pane);
    auto* rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(0,0,0,0);
    auto* addBtn = new QPushButton("+", row);
    auto* delBtn = new QPushButton("–", row);
    rowLay->addWidget(addBtn);
    rowLay->addWidget(delBtn);

    lay->addWidget(new QLabel("Layers", pane));
    lay->addWidget(m_layerList);
    lay->addWidget(row);

    // seed with Layer 0
    m_layerList->addItem("Layer 0");
    m_layerList->setCurrentRow(0);
    connect(m_layerList, &QListWidget::currentRowChanged, this, &MainWindow::setCurrentLayerFromList);
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

/* Blueprint-to-vector stub */
void MainWindow::runBluePrintAI()
{
    const QJsonObject payload{{"msg", "hello from CAD"}};
    QNetworkRequest  req(QUrl("http://127.0.0.1:8000/vectorise"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    m_net->post(req, QJsonDocument(payload).toJson());
}

/* ───────────────────────  Undo/Redo stubs  ───────────────────── */
// These are placeholders; actual commands will be pushed from DrawingCanvas
void MainWindow::undo() { m_undo->undo(); }
void MainWindow::redo() { m_undo->redo(); }

/* ───────────────────────  Layers actions  ───────────────────── */
void MainWindow::addLayer()
{
    m_layerList->addItem(QString("Layer %1").arg(m_nextLayerId));
    m_layerList->setCurrentRow(m_layerList->count() - 1);
    ++m_nextLayerId;
}

void MainWindow::removeSelectedLayer()
{
    const int row = m_layerList->currentRow();
    if (row <= 0) return; // keep Layer 0
    delete m_layerList->takeItem(row);
    m_layerList->setCurrentRow(std::max(0, row - 1));
}

void MainWindow::setCurrentLayerFromList()
{
    int row = m_layerList->currentRow();
    if (row < 0) row = 0;
    m_canvas->setCurrentLayer(row);
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
