#include "MainWindow.h"
#include "canvas/DrawingCanvas.h"
#include "ui/RulerWidget.h"
#include "3d/SceneView3d.h"

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

#include <QDialog>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>


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

void MainWindow::refineOverlapsLight()
{
    // gentle defaults: ~2 px tolerance, 80% overlap, ≤3° angle diff
    const int n = m_canvas->refineOverlapsLight(2.0, 0.80, 3.0);
    statusBar()->showMessage(QString("Overlap cleanup: %1 merged").arg(n), 3000);
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

    // Tools
    auto* tools = menuBar()->addMenu(tr("&Tools"));
    m_actSetScale = tools->addAction(tr("Set &Scale…"));
    m_actSetScale->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(m_actSetScale, &QAction::triggered, this, &MainWindow::setScaleInteractive);

    // View (Zoom)
    auto* view = menuBar()->addMenu("&View");
// ...
    // QAction* act3D = view->addAction(tr("Open 3D Preview"));
    auto* act3D = view->addAction(tr("Open 3D View…"));
    connect(act3D, &QAction::triggered, this, [this]{
        auto* win = new Scene3DView(this);
        win->setAttribute(Qt::WA_DeleteOnClose);
        win->resize(900, 700);
        win->show();

        // tweak if you like (meters):
        constexpr double kWallH = 2.7;
        constexpr double kWallT = 0.12;
        win->buildFromCanvas(m_canvas, kWallH, kWallT, /*includeFloor*/true);
    });

    
    connect(act3D, &QAction::triggered, this, &MainWindow::open3DPreview);
    view->addAction("Zoom In",    QKeySequence::ZoomIn,  this, &MainWindow::zoomIn);
    view->addAction("Zoom Out",   QKeySequence::ZoomOut, this, &MainWindow::zoomOut);
    view->addAction("Reset Zoom", QKeySequence("Ctrl+0"), this, &MainWindow::zoomReset);
    view->addAction("Zoom to Fit", QKeySequence('F'),     this, &MainWindow::zoomToFit);

    // AI
    auto* ai = menuBar()->addMenu("&AI");
    ai->addAction("Blueprint → Vectorise…",
                  QKeySequence("Ctrl+Shift+V"),
                  this, &MainWindow::runBluePrintAI);

    ai->addAction("Refine Vector (light overlaps)…",
                  QKeySequence("Ctrl+Shift+L"),
                  this, &MainWindow::refineOverlapsLight);

    // In MainWindow::setupMenus()
    auto* view3dAct = ai->addAction(tr("Open 3D Preview…")); // or put under View menu
    connect(view3dAct, &QAction::triggered, this, [this]{
        bool okH=false, okT=false;
        double h = QInputDialog::getDouble(this, tr("Wall height (m)"), tr("Height:"), 3.0, 0.5, 100.0, 2, &okH);
        if (!okH) return;
        double t = QInputDialog::getDouble(this, tr("Wall thickness (m)"), tr("Thickness:"), 0.15, 0.01, 2.0, 3, &okT);
        if (!okT) return;

        if (!m_scene3d) {
            m_scene3d = new Scene3DView;
            m_scene3d->setAttribute(Qt::WA_DeleteOnClose, true);
            // keep pointer safe if user closes the window
            connect(m_scene3d, &QObject::destroyed, this, [this]{ m_scene3d = nullptr; });
        }

        m_scene3d->buildFromCanvas(m_canvas, h, t, /*includeFloor*/true);
        m_scene3d->resize(900, 600);
        m_scene3d->show();
        m_scene3d->raise();
        m_scene3d->activateWindow();
    });


    // Refine (Preview)… dialog
    QAction* actRefinePreview = ai->addAction("Refine (Preview)...");
    connect(actRefinePreview, &QAction::triggered, this, [this]{
        if (!m_canvas) return;

        QDialog dlg(this);
        dlg.setWindowTitle("Refine (Preview)");

        // ---- Form ----
        auto* form = new QFormLayout;

        // Core
        auto* weld  = new QDoubleSpinBox; weld->setRange(0.1, 50.0); weld->setValue(6.0);  weld->setDecimals(1);
        auto* close = new QDoubleSpinBox; close->setRange(0.1, 50.0); close->setValue(8.0); close->setDecimals(1);
        auto* axis  = new QDoubleSpinBox; axis->setRange(0.0, 45.0);  axis->setValue(6.0);  axis->setDecimals(1);
        auto* minl  = new QDoubleSpinBox; minl->setRange(0.0, 50.0);  minl->setValue(10.0); minl->setDecimals(1);

        form->addRow("Weld tolerance (px):", weld);
        form->addRow("Close gap (px):",      close);
        form->addRow("Axis snap (deg):",     axis);
        form->addRow("Min length (px):",     minl);

        // Parallel stack thinning
        auto* thinLbl = new QLabel("<b>Parallel stack thinning</b>");
        form->addRow(thinLbl);

        auto* thinEnable = new QCheckBox("Enable stack thinning");
        thinEnable->setChecked(true);
        form->addRow(QString(), thinEnable);

        auto* sep   = new QDoubleSpinBox; sep->setRange(0.0, 20.0);   sep->setValue(3.0);  sep->setDecimals(1);
        auto* ang   = new QDoubleSpinBox; ang->setRange(0.0, 15.0);   ang->setValue(3.0);  ang->setDecimals(1);
        auto* ovlap = new QDoubleSpinBox; ovlap->setRange(0.0, 200.0); ovlap->setValue(30.0); ovlap->setDecimals(0);

        form->addRow("Max separation (px):", sep);
        form->addRow("Max angle Δ (deg):",   ang);
        form->addRow("Min overlap (px):",    ovlap);

        // Buttons
        auto* btnPreview = new QPushButton("Preview");
        auto* btnApply   = new QPushButton("Apply");
        auto* btnClose   = new QPushButton("Close");

        auto* h = new QHBoxLayout;
        h->addStretch(1);
        h->addWidget(btnPreview);
        h->addWidget(btnApply);
        h->addWidget(btnClose);

        auto* v = new QVBoxLayout;
        v->addLayout(form);
        v->addLayout(h);
        dlg.setLayout(v);

        // Send params to canvas
        auto sendParams = [this, weld, close, axis, minl, thinEnable, sep, ang, ovlap](){
            DrawingCanvas::RefineParams p;
            p.weldTolPx   = weld->value();
            p.closeTolPx  = close->value();
            p.axisSnapDeg = axis->value();
            p.minLenPx    = minl->value();

            p.stackEnabled    = thinEnable->isChecked();
            p.stackSepPx      = sep->value();
            p.stackAngleDeg   = ang->value();
            p.stackMinOverlap = ovlap->value();

            m_canvas->updateRefinePreview(p);
        };

        // Wire buttons
        connect(btnPreview, &QPushButton::clicked, &dlg, [sendParams]{ sendParams(); });
        connect(btnApply,   &QPushButton::clicked, &dlg, [this]{
            const int edits = m_canvas->applyRefinePreview();
            if (statusBar()) statusBar()->showMessage(QString("Refine applied: %1 edits").arg(edits), 3000);
        });
        connect(btnClose,   &QPushButton::clicked, &dlg, &QDialog::accept);

        // Live preview
        auto live = [&dlg, sendParams](QDoubleSpinBox* sp){
            QObject::connect(sp, &QDoubleSpinBox::valueChanged, &dlg, [sendParams](double){ sendParams(); });
        };
        live(weld); live(close); live(axis); live(minl); live(sep); live(ang); live(ovlap);
        QObject::connect(thinEnable, &QCheckBox::toggled, &dlg, [sendParams](bool){ sendParams(); });

        // Kick an initial preview
        sendParams();
        dlg.exec();

        // Ensure overlay removed if user closes without Apply
        m_canvas->cancelRefinePreview();
    });

    // === Auto-rooms actions ===
    // --- Auto-rooms (Controller) ---
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
}


void MainWindow::setScaleInteractive()
{
    statusBar()->showMessage(tr("Set Scale: click first point, then second point…"));
    m_canvas->startSetScaleMode();
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

    auto addField = [&](const char* name, const QByteArray& val){
        QHttpPart p;
        p.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QVariant(QString("form-data; name=\"%1\"").arg(name)));
        p.setBody(val);
        multi->append(p);
    };

    // Line/edge params
    addField("min_line_len", QByteArray::number(36));   // a touch shorter to pick broken walls
    addField("canny1",       QByteArray::number(70));
    addField("canny2",       QByteArray::number(160));
    addField("approx_eps",   QByteArray::number(2.0));

    // NEW toggles
    addField("text_suppr",       "1");
    addField("side_denoise_on",  "1");
    addField("door_simpl",       "1");
    addField("room_close",       "1");
    addField("text_suppr",      QByteArray::number(1));
    addField("side_denoise_on", QByteArray::number(1));
    addField("use_mlsd",        QByteArray::number(1));       // turn M-LSD on

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
    params.gapPx       = 12.0;  // snap nearby endpoints together (px)
    params.axisSnapDeg = 7.5;   // snap near-axis lines to 0° / 90°
    params.mergePx     = 10;   // merge collinear segments if gap ≤ mergePx
    params.extendPx    = 10.0;  // extend to intersect if close (px)
    params.minLenPx    = 1.0;   // drop tiny fragments

    const int edits = m_canvas->refineVector(params);
    statusBar()->showMessage(QString("Refine complete — %1 edits").arg(edits), 4000);
}

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

    // Live preview sender
    auto sendParams = [this,
                       spWeldTolPx, spMinAreaM2, spAxisSnapDeg,
                       spMinSidePx, spMinWallPx, spRailFrac, spDoorGapPx, spMinStrong]()
    {
        m_canvas->updateRoomsPreview(
            /*weldTolPx*/      spWeldTolPx->value(),
            /*minArea_m2*/     spMinAreaM2->value(),
            /*axisSnapDeg*/    spAxisSnapDeg->value(),
            /*minSidePx*/      spMinSidePx->value(),
            /*minWallSegLen*/  spMinWallPx->value(),
            /*railCoverFrac*/  spRailFrac->value(),
            /*doorGapMaxPx*/   spDoorGapPx->value(),
            /*minStrongSides*/ spMinStrong->value()
        );
    };

    // ---- FIXED: typed connections (no const char* + lambda mix) ----
    auto connectD = [&](QDoubleSpinBox* sp){
        QObject::connect(sp,
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            &dlg, [sendParams](double){ sendParams(); });
    };
    auto connectI = [&](QSpinBox* sp){
        QObject::connect(sp,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
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

    // Apply commits shapes to the scene; keep dialog open to iterate
    QObject::connect(btns->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dlg, [this]{
        if (!m_canvas) return;
        const int added = m_canvas->applyRoomsPreview();
        if (statusBar()) statusBar()->showMessage(tr("Auto-rooms: %1 room(s) added").arg(added), 3000);
    });

    // Close stops preview
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);

    // Kick off initial preview
    sendParams();

    dlg.exec();

    // Ensure overlay removed when dialog closes (if user didn't Apply last)
    if (m_canvas) m_canvas->cancelRoomsPreview();
}


void MainWindow::open3DPreview()
{
    if (!m_3dDock) {
        m_3dDock = new QDockWidget(tr("3D Preview"), this);
        m_3dDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        m_3dView = new Scene3DView(m_3dDock);
        m_3dDock->setWidget(m_3dView);
        addDockWidget(Qt::RightDockWidgetArea, m_3dDock);
    }
    if (m_canvas && m_3dView) {
        // tweak thickness/height to taste (meters)
        m_3dView->buildFromCanvas(m_canvas, /*thick*/0.12, /*height*/2.7, /*floor*/true);
    }
    m_3dDock->show();
    m_3dDock->raise();
}