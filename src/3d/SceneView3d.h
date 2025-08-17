#pragma once
#include <QWidget>
#include <QRectF>
#include <QPoint>

// Pull in the full type so moc/clang see QEntity methods.
#include <Qt3DCore/QEntity>

namespace Qt3DCore  { class QTransform; }
namespace Qt3DExtras { class Qt3DWindow; class QOrbitCameraController; }
class DrawingCanvas;

class Scene3DView : public QWidget
{
    Q_OBJECT
public:
    enum class ViewMode { OrthoTop, OrthoFront, OrthoRight, Perspective };

    explicit Scene3DView(QWidget* parent = nullptr);
    ~Scene3DView() override;

    void buildFromCanvas(const DrawingCanvas* canvas,
                         double wallHeightM,
                         double wallThicknessM,
                         bool includeFloor);

    void setMode(ViewMode m);
    void toggleOrthoPerspective();

    void setSync2D(bool on);
    void connectCanvas(DrawingCanvas* canvas);

    // simple toggles (moved to .cpp to avoid inline use of incomplete types)
    void setGridVisible(bool on);
    void setFloorVisible(bool on);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    // build helpers
    void clearGeometry();
    void addWallFromSegment(const QPointF& aPx, const QPointF& bPx,
                            double pxToM, double wallHeightM, double wallThicknessM);
    Qt3DCore::QEntity* addFloorQuad(const QRectF& boundsPx, double pxToM);

    // camera
    void setTopOrtho();
    void setFrontOrtho();
    void setRightOrtho();
    void setPerspective();
    void applyTopOrtho(float widthMeters);
    void applyFrontOrtho(float widthMeters);
    void applyRightOrtho(float widthMeters);
    void applyPerspectiveDefault();
    void frameCameraToBounds(const QRectF& boundsPx, double pxToM);
    void syncCameraTo2D();
    void ensureNonZeroSize();

    // ortho interaction
    void beginPan(const QPoint& p);
    void updatePan(const QPoint& p);
    void endPan();
    void orthoZoom(float factor, const QPoint& mousePx);

private:
    Qt3DExtras::Qt3DWindow*               m_view       = nullptr;
    QWidget*                               m_container  = nullptr;
    Qt3DCore::QEntity*                     m_root       = nullptr;
    Qt3DCore::QEntity*                     m_geomRoot   = nullptr;
    Qt3DExtras::QOrbitCameraController*    m_orbit      = nullptr;
    DrawingCanvas*                         m_canvas     = nullptr;

    // grid+floor holders so we can toggle visibility
    Qt3DCore::QEntity* m_gridEntity  = nullptr;
    Qt3DCore::QEntity* m_floorEntity = nullptr;
    bool m_gridVisible  = true;
    bool m_floorVisible = true;

    ViewMode m_mode = ViewMode::Perspective;
    bool     m_sync2D = true;

    // panning state
    bool   m_panning = false;
    QPoint m_lastMouse;
    // current ortho extents (half widths in world units)
    float m_halfW = 10.f;
    float m_halfH = 10.f;
};
