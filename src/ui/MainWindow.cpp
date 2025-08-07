#include "MainWindow.h"
#include "canvas/DrawingCanvas.h"

#include <QtWidgets>                     // pulls in all widget headers
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>

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
        connect(b, &QPushButton::clicked,
                this, [=]{ m_canvas->setCurrentTool(t); });
        v->addWidget(b);
    };

    addToolBtn("Select (S)" , DrawingCanvas::Tool::Select);
    addToolBtn("Line (L)"   , DrawingCanvas::Tool::Line);
    addToolBtn("Rect (R)"   , DrawingCanvas::Tool::Rect);
    addToolBtn("Ellipse (C)", DrawingCanvas::Tool::Ellipse);
    addToolBtn("Polygon (P)", DrawingCanvas::Tool::Polygon);

    /* colour picker */
    auto* colorBtn = new QPushButton("Color", toolWidget);
    connect(colorBtn, &QPushButton::clicked, this, &MainWindow::chooseColor);
    v->addWidget(colorBtn);

    /* grid toggle */
    auto* gridBtn  = new QPushButton("Toggle Grid (G)", toolWidget);
    connect(gridBtn, &QPushButton::clicked, this, &MainWindow::toggleGrid);
    v->addWidget(gridBtn);

    v->addStretch();

    auto* dock = new QDockWidget("Tools", this);
    dock->setWidget(toolWidget);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    /* keyboard shortcuts */
    auto makeSC = [&](QKeySequence ks, auto slot){
        auto* sc = new QShortcut(ks, this);
        connect(sc, &QShortcut::activated, slot);
    };

    makeSC('S', [this]{ m_canvas->setCurrentTool(DrawingCanvas::Tool::Select); });
    makeSC('L', [this]{ m_canvas->setCurrentTool(DrawingCanvas::Tool::Line);   });
    makeSC('R', [this]{ m_canvas->setCurrentTool(DrawingCanvas::Tool::Rect);   });
    makeSC('C', [this]{ m_canvas->setCurrentTool(DrawingCanvas::Tool::Ellipse);} );
    makeSC('P', [this]{ m_canvas->setCurrentTool(DrawingCanvas::Tool::Polygon);} );
    makeSC('G', [this]{ toggleGrid(); });
}

/* ───────────────────────────  Menus  ─────────────────────────── */
void MainWindow::setupMenus()
{
    auto* file = menuBar()->addMenu("&File");
    file->addAction("New",         this, &MainWindow::newScene,  QKeySequence::New);
    file->addAction("Open JSON…",  this, &MainWindow::openJson,  QKeySequence::Open);
    file->addAction("Save JSON…",  this, &MainWindow::saveJson,  QKeySequence::Save);
    file->addSeparator();
    file->addAction("Import DXF…", this, &MainWindow::openDxf,   QKeySequence("Ctrl+D"));
    file->addAction("Export SVG…", this, &MainWindow::exportSvg, QKeySequence("Ctrl+E"));
    file->addSeparator();
    file->addAction("E&xit", qApp, &QCoreApplication::quit, QKeySequence::Quit);

    auto* ai = menuBar()->addMenu("&AI");
    ai->addAction("Blueprint → Vectorise…",
                  this, &MainWindow::runBluePrintAI,
                  QKeySequence("Ctrl+Shift+V"));
}

/* ───────────────────────  Slots / actions  ───────────────────── */
void MainWindow::chooseColor()
{
    const QColor c = QColorDialog::getColor(m_canvas->palette().color(QPalette::Text),
                                            this);
    if (c.isValid()) m_canvas->setCurrentColor(c);
}
void MainWindow::toggleGrid()  { m_canvas->toggleGrid();              }
void MainWindow::newScene()    { m_canvas->scene()->clear();          }

/* JSON */
void MainWindow::openJson()
{
    const QString fn = QFileDialog::getOpenFileName(this, "Open JSON", {}, "*.json");
    if (fn.isEmpty()) return;
    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly)) return;
    m_canvas->loadFromJson(QJsonDocument::fromJson(f.readAll()));
}
void MainWindow::saveJson()
{
    const QString fn = QFileDialog::getSaveFileName(this, "Save JSON", "scene.json", "*.json");
    if (fn.isEmpty()) return;
    QFile f(fn);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(m_canvas->saveToJson().toJson());
}

/* DXF / SVG */
void MainWindow::openDxf()
{
    const QString fn = QFileDialog::getOpenFileName(this, "Import DXF", {}, "*.dxf");
    if (!fn.isEmpty()) m_canvas->importDxf(fn);
}
void MainWindow::exportSvg()
{
    const QString fn = QFileDialog::getSaveFileName(this, "Export SVG", "scene.svg", "*.svg");
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
