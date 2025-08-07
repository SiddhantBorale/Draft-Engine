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
    /* toolbar/menu actions */
    void chooseColor();
    void toggleGrid();
    void newScene();
    void openJson();
    void saveJson();
    void openDxf();
    void exportSvg();
    void runBluePrintAI();          // POST to FastAPI stub

private:
    /* helpers that build the UI */
    void setupToolPanel();
    void setupMenus();

    DrawingCanvas*          m_canvas { nullptr };
    QNetworkAccessManager*  m_net    { nullptr };
};
