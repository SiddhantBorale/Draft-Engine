#pragma once
#include <QMainWindow>
#include <QColor>
#include <memory>
class DrawingCanvas;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void chooseColor();
    void saveJson();
    void openJson();

private:
    void setupUI();
    DrawingCanvas* m_canvas { nullptr };
    QColor m_currentColor { Qt::black };
};