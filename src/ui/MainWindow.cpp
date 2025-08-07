#include "MainWindow.h"
#include "canvas/DrawingCanvas.h"
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QJsonDocument>
#include <QColorDialog>
#include <QMessageBox>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setupUI();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUI()
{
    setWindowTitle("Drafting Software – Phase 1");
    resize(1024, 768);

    m_canvas = new DrawingCanvas(this);
    setCentralWidget(m_canvas);

    auto* tb = addToolBar("Tools");
    QAction* lineAct = tb->addAction("Line");
    QAction* rectAct = tb->addAction("Rectangle");
    QAction* ellAct  = tb->addAction("Circle");
    QAction* polyAct = tb->addAction("Polygon");
    tb->addSeparator();
    QAction* pickColorAct = tb->addAction("Color");

    connect(lineAct, &QAction::triggered, [=]{ m_canvas->setCurrentTool(DrawingCanvas::Tool::Line); });
    connect(rectAct, &QAction::triggered, [=]{ m_canvas->setCurrentTool(DrawingCanvas::Tool::Rect); });
    connect(ellAct,  &QAction::triggered, [=]{ m_canvas->setCurrentTool(DrawingCanvas::Tool::Ellipse); });
    connect(polyAct, &QAction::triggered, [=]{ m_canvas->setCurrentTool(DrawingCanvas::Tool::Polygon); });
    connect(pickColorAct, &QAction::triggered, this, &MainWindow::chooseColor);

    // File menu
    auto* fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction("Open", this, &MainWindow::openJson, QKeySequence::Open);
    fileMenu->addAction("Save", this, &MainWindow::saveJson, QKeySequence::Save);

    statusBar()->showMessage("Ready");
}

void MainWindow::chooseColor()
{
    QColor chosen = QColorDialog::getColor(m_currentColor, this, "Pick Color");
    if (chosen.isValid()) {
        m_currentColor = chosen;
        m_canvas->setCurrentColor(chosen);
    }
}

void MainWindow::saveJson()
{
    QString file = QFileDialog::getSaveFileName(this, "Save JSON", {}, "JSON Files (*.json)");
    if (file.isEmpty()) return;

    QFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Error", "Unable to open file for writing");
        return;
    }
    f.write(m_canvas->saveToJson().toJson(QJsonDocument::Indented));
}

void MainWindow::openJson()
{
    QString file = QFileDialog::getOpenFileName(this, "Open JSON", {}, "JSON Files (*.json)");
    if (file.isEmpty()) return;

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Error", "Unable to open file for reading");
        return;
    }
    QByteArray data = f.readAll();
    m_canvas->loadFromJson(QJsonDocument::fromJson(data));
}
