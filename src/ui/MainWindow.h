#pragma once
#include <QMainWindow>
#include <QJsonDocument>
#include <QJsonObject>

class DrawingCanvas;
class QNetworkAccessManager;
class QUndoStack;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    // toolbar/menu actions
    void changeFillPattern(int idx); 
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

    // Layers (list-based)
    void addLayer();
    void removeSelectedLayer();
    void setCurrentLayerFromTree(QTreeWidgetItem* it, QTreeWidgetItem* prev);
    void layerItemChanged(QTreeWidgetItem* it, int column);

private:
    // UI builders
    void setupCentralWithRulers();
    void setupToolPanel();
    void setupMenus();
    void setupLayersDock();

    DrawingCanvas*          m_canvas { nullptr };
    QNetworkAccessManager*  m_net    { nullptr };
    QUndoStack*             m_undo   { nullptr };

    // QListWidget*            m_layerList { nullptr };
    // int                     m_nextLayerId { 1 };

    QTreeWidget*            m_layerTree { nullptr };
    int                     m_nextLayerId { 1 }; // 0 already exists
};

