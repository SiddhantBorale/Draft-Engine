#pragma once
#include <QWidget>
#include <QRectF>
#include <QPointF>

namespace Qt3DCore { class QEntity; class QTransform; }
namespace Qt3DRender { class QCamera; class QDirectionalLight; }
namespace Qt3DExtras { class Qt3DWindow; class QPhongMaterial; class QCuboidMesh; class QPlaneMesh; }

class DrawingCanvas;

class Scene3DView : public QWidget
{
    Q_OBJECT
public:
    explicit Scene3DView(QWidget* parent = nullptr);
    ~Scene3DView() override;

    // Build 3D scene from the current 2D canvas lines (walls)
    void buildFromCanvas(const DrawingCanvas* canvas,
                         double wallHeightMeters = 3.0,
                         double wallThicknessMeters = 0.15,
                         bool includeFloor = true);

    void clearScene();

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    void ensureNonZeroSize();

    Qt3DCore::QEntity* addWall(const QPointF& a_px,
                               const QPointF& b_px,
                               double height_m,
                               double thick_m,
                               double px_to_m,
                               const QPointF& origin_px,
                               Qt3DCore::QEntity* parent);

    Qt3DCore::QEntity* addFloor(const QRectF& bounds_px,
                                double thick_m,
                                double px_to_m,
                                const QPointF& origin_px,
                                Qt3DCore::QEntity* parent);

private:
    Qt3DExtras::Qt3DWindow* m_view = nullptr;  // the Qt3D window
    QWidget*                m_container = nullptr; // widget wrapper for m_view
    Qt3DCore::QEntity*      m_root = nullptr;  // persistent root
    Qt3DCore::QEntity*      m_content = nullptr; // current content subtree
};
