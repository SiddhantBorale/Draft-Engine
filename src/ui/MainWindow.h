#pragma once
#include <QMainWindow>
#include <QJsonDocument>
#include <QJsonObject>

class DrawingCanvas;
class QNetworkAccessManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    // toolbar/menu actions
   private slots:
    void chooseColor();
    void chooseFillColor();   // NEW
    void changeLineWidth(double w); // NEW
    void toggleGrid();
    void newScene();
    void openJson();
    void saveJson();
    void importSvg();
    void exportSvg();
    void runBluePrintAI();

    // Zoom
    void zoomIn();
    void zoomOut();
    void zoomReset();
    
private:
    // helpers that build the UI
    void setupToolPanel();
    void setupMenus();

    DrawingCanvas*          m_canvas { nullptr };
    QNetworkAccessManager*  m_net    { nullptr };
};
