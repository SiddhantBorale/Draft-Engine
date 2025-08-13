#pragma once
#include <QMainWindow>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDoubleSpinBox>

class DrawingCanvas;
class QNetworkAccessManager;
class QUndoStack;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QDoubleSpinBox;
class QPushButton;
class QNetworkReply;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void applyCornerRadius();   // NEW
    void applyLineBend();

    // toolbar/menu actions
    void setDimPrecision(int p);
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
    void onVectoriseFinished(); 
    void changeCornerRadius(double r);

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

    void refineVector();  // auto-clean pass
    void refineOverlapsLight();  // new



private:
    // UI builders
    QDoubleSpinBox* m_cornerSpin { nullptr }; // NEW
    QDoubleSpinBox* m_bendSpin   { nullptr }; // NEW

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
