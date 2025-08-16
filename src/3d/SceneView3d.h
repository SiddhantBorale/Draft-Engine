#pragma once
#include <QWidget>
#include <QPointer>

class DrawingCanvas;

namespace Qt3DCore { class QEntity; class QTransform; }
namespace Qt3DExtras { class Qt3DWindow; class QOrbitCameraController; class QPhongMaterial; }

class Scene3DView : public QWidget
{
    Q_OBJECT
public:
    explicit Scene3DView(QWidget* parent = nullptr);
    ~Scene3DView() override;

    // Build 3D scene from the 2D canvas
    void buildFromCanvas(const DrawingCanvas* canvas,
                         double wallHeightM,
                         double wallThicknessM,
                         bool includeFloor);

    // Camera/view modes (Blender-ish)
    enum class ViewMode { OrthoTop, OrthoFront, OrthoRight, Perspective };
    void setMode(ViewMode m);
    void setTopOrtho();
    void setFrontOrtho();
    void setRightOrtho();
    void setPerspective();
    void toggleOrthoPerspective();

    // Sync top-ortho with 2D canvas
    void setSync2D(bool on);
    bool sync2D() const { return m_sync2D; }
    void connectCanvas(DrawingCanvas* canvas);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void ensureNonZeroSize();
    void syncCameraTo2D();

    // Camera presets
    void applyTopOrtho(float widthMeters);
    void applyFrontOrtho(float widthMeters);
    void applyRightOrtho(float widthMeters);
    void applyPerspectiveDefault();

    // Build helpers
    void clearGeometry();
    void addWallFromSegment(const QPointF& aPx,
                            const QPointF& bPx,
                            double pxToM,
                            double wallHeightM,
                            double wallThicknessM);
    void addFloorQuad(const QRectF& boundsPx, double pxToM);

    void frameCameraToBounds(const QRectF& boundsPx, double pxToM);

private:
    Qt3DExtras::Qt3DWindow*              m_view       { nullptr };
    QWidget*                             m_container  { nullptr };
    Qt3DCore::QEntity*                   m_root       { nullptr }; // scene root (kept)
    Qt3DCore::QEntity*                   m_geomRoot   { nullptr }; // regenerated geometry
    Qt3DExtras::QOrbitCameraController*  m_orbit      { nullptr };

    ViewMode                             m_mode       { ViewMode::Perspective };
    bool                                 m_sync2D     { true };
    QPointer<DrawingCanvas>              m_canvas;
};
