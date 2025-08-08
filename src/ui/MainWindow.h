#pragma once
#include <QMainWindow>
#include <QJsonDocument>
#include <QJsonObject>

class DrawingCanvas;
class QNetworkAccessManager;
class QUndoStack;
class QListWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    // for Spacebar pan toggle
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    // toolbar/menu actions
    void chooseColor();
    void chooseFillColor();
    void changeLineWidth(double w);
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
    void zoomToFit();

    // Edit (Undo/Redo)
    void undo();
    void redo();

    // Layers
    void addLayer();
    void removeSelectedLayer();
    void setCurrentLayerFromList();

private:
    // helpers that build the UI
    void setupToolPanel();
    void setupMenus();
    void setupLayersDock();

    DrawingCanvas*          m_canvas { nullptr };
    QNetworkAccessManager*  m_net    { nullptr };
    QUndoStack*             m_undo   { nullptr };

    // simple layers UI (names only, no visibility/lock yet)
    QListWidget*            m_layerList { nullptr };
    int                     m_nextLayerId { 1 }; // start with layer 0 existing
};
