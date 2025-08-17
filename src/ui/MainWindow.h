#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QDoubleSpinBox>
#include <QTreeWidget>

// We must include the real header to use Scene3DView::ViewMode in declarations
#include "3d/SceneView3d.h"

class DrawingCanvas;
class QUndoStack;
class QNetworkAccessManager;
class QTreeWidgetItem;
class QAction;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    /* Toolbar / actions */
    void promptForProjectUnits();
    void setScaleInteractive();
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

    /* Zoom */
    void zoomIn();
    void zoomOut();
    void zoomReset();
    void zoomToFit();

    /* Edit */
    void undo();
    void redo();

    /* Layers */
    void addLayer();
    void removeSelectedLayer();
    void setCurrentLayerFromTree(QTreeWidgetItem* it, QTreeWidgetItem* prev);
    void layerItemChanged(QTreeWidgetItem* it, int column);

    /* Vector refine + rooms */
    void refineVector();
    void refineOverlapsLight();
    void openAutoRoomsDialog();

    /* View switching */
    void switchTo2D();
    void switch3DTop();
    void switch3DFront();
    void switch3DRight();
    void switch3DPerspective();

    void applyCornerRadius();
    void applyLineBend();

private:
    /* UI builders */
    void setupCentralWithRulers();
    void setupToolPanel();
    void setupMenus();
    void setupLayersDock();
    

    /* 3D helpers (embedded in the same central area) */
    void prepare3D(Scene3DView::ViewMode mode);

    /* Data */
    DrawingCanvas*         m_canvas  = nullptr;
    Scene3DView*           m_scene3d = nullptr;
    QStackedWidget*        m_viewStack = nullptr; // [0] 2D canvas, [1] 3D view

    QUndoStack*            m_undo = nullptr;
    QNetworkAccessManager* m_net  = nullptr;

    QTreeWidget*           m_layerTree = nullptr;
    int                    m_nextLayerId = 1;   // Layer 0 is seeded

    QAction*               m_actSetScale = nullptr;

    /* Small editors in the Tools panel */
    QDoubleSpinBox*        m_cornerSpin = nullptr;
    QDoubleSpinBox*        m_bendSpin   = nullptr;
};
