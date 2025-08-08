#include "MainWindow.h"
#include "canvas/DrawingCanvas.h"

#include <QtWidgets>                 // QWidget, QDockWidget, QPushButton, etc.
#include <QShortcut>
#include <QColorDialog>
#include <QFileDialog>
#include <QJsonDocument>
#include <QMenuBar>

// Network
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_canvas(new DrawingCanvas(this)),
      m_net(new QNetworkAccessManager(this))
{
    setCentralWidget(m_canvas);
    setupToolPanel();
    setupMenus();
    resize(1200, 800);
}

/* ─────────────────────────  Tool panel  ───────────────────────── */
void MainWindow::setupToolPanel()
{
    auto* toolWidget = new QWidget;
    auto* v          = new QVBoxLayout(toolWidget);

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

    // colour picker
    auto* colorBtn = new QPushButton("Color", toolWidget);
    connect(colorBtn, &QPushButton::clicked, this, &MainWindow::chooseColor);
    v->addWidget(colorBtn);

    // grid toggle
    auto* gridBtn  = new QPushButton("Toggle Grid (G)", toolWidget);
    connect(gridBtn, &QPushButton::clicked, this, &MainWindow::toggleGrid);
    v->addWidget(gridBtn);

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
}

/* ───────────────────────────  Menus  ─────────────────────────── */
void MainWindow::setupMenus()
{
    auto* file = menuBar()->addMenu("&File");
    file->addAction("New",        QKeySequence::New,  this, &MainWindow::newScene);
    file->addAction("Open JSON…", QKeySequence::Open, this, &MainWindow::openJson);
    file->addAction("Save JSON…", QKeySequence::Save, this, &MainWindow::saveJson);
    file->addSeparator();
    file->addAction("Import SVG…", QKeySequence("Ctrl+I"), this, &MainWindow::importSvg);
    file->addAction("Export SVG…", QKeySequence("Ctrl+E"), this, &MainWindow::exportSvg);
    file->addSeparator();
    file->addAction("E&xit", QKeySequence::Quit, qApp, &QCoreApplication::quit);

    auto* ai = menuBar()->addMenu("&AI");
    ai->addAction("Blueprint → Vectorise…",
                  this, &MainWindow::runBluePrintAI,
                  QKeySequence("Ctrl+Shift+V"));
}

/* ───────────────────────  Slots / actions  ───────────────────── */
void MainWindow::chooseColor()
{
    const QColor c = QColorDialog::getColor(m_canvas->palette().color(QPalette::Text), this);
    if (c.isValid()) m_canvas->setCurrentColor(c);
}

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
