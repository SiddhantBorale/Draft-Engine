#include "SceneView3d.h"
#include "canvas/DrawingCanvas.h"

#include <Qt3DCore/QEntity>
#include <Qt3DCore/QTransform>

#include <Qt3DRender/QCamera>
#include <Qt3DRender/QDirectionalLight>

#include <Qt3DExtras/Qt3DWindow>
#include <Qt3DExtras/QOrbitCameraController>
#include <Qt3DExtras/QPhongMaterial>
#include <Qt3DExtras/QPlaneMesh>
#include <Qt3DExtras/QCuboidMesh>

#include <QGraphicsScene>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsPathItem>
#include <QVBoxLayout>
#include <QTimer>
#include <QtMath>

Scene3DView::Scene3DView(QWidget* parent)
    : QWidget(parent)
    , m_view(new Qt3DExtras::Qt3DWindow())
    , m_container(QWidget::createWindowContainer(m_view, this))
    , m_root(new Qt3DCore::QEntity)
{
    // container into layout
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0,0,0,0);
    lay->addWidget(m_container);

    // Strong size policy and non-zero minimums to avoid Metal swapchain 0-height
    m_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_container->setMinimumSize(16, 16);
    setMinimumSize(32, 32);

    // Watch container resizes to keep window > 0x0
    m_container->installEventFilter(this);

    // Initial guard (some platforms start at 0x0 on first show)
    ensureNonZeroSize();
    QTimer::singleShot(50, this, [this]{ ensureNonZeroSize(); });
    QTimer::singleShot(150, this, [this]{ ensureNonZeroSize(); });

    // Basic scene
    m_view->setRootEntity(m_root);

    // Camera
    auto* cam = m_view->camera();
    cam->lens()->setPerspectiveProjection(45.0f, 16.f/9.f, 0.01f, 1000.0f);
    cam->setPosition(QVector3D(0.0f, -8.0f, 4.0f));
    cam->setViewCenter(QVector3D(0.0f, 0.0f, 0.0f));

    // Orbit controller
    auto* camCtrl = new Qt3DExtras::QOrbitCameraController(m_root);
    camCtrl->setCamera(cam);

    // Light
    auto* lightEnt = new Qt3DCore::QEntity(m_root);
    auto* light    = new Qt3DRender::QDirectionalLight(lightEnt);
    light->setWorldDirection(QVector3D(-0.5f, -0.3f, -1.0f));
    lightEnt->addComponent(light);
}

Scene3DView::~Scene3DView()
{
    // m_root belongs to the Qt3DWindow; children are managed by parents
}

bool Scene3DView::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_container && ev->type() == QEvent::Resize) {
        ensureNonZeroSize();
    }
    return QWidget::eventFilter(obj, ev);
}

void Scene3DView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    ensureNonZeroSize();
}

void Scene3DView::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    // make sure after becoming visible we never leave the swapchain at 0 height
    ensureNonZeroSize();
    QTimer::singleShot(0, this, [this]{ ensureNonZeroSize(); });
}

void Scene3DView::ensureNonZeroSize()
{
    if (!m_container || !m_view) return;

    // 1) Never allow the container to be zero height
    int w = qMax(1, m_container->width());
    int h = qMax(1, m_container->height());
    if (m_container->width() != w || m_container->height() != h)
        m_container->resize(w, h);

    // 2) Mirror onto the QWindow (Metal swapchain is tied to this)
    if (m_view->width() <= 0 || m_view->height() <= 0) {
        m_view->resize(qMax(1, w), qMax(1, h));
    }
}

void Scene3DView::clearScene()
{
    if (!m_root) return;
    if (m_content) {
        delete m_content; // deletes subtree
        m_content = nullptr;
    }
}

static inline double radians(double deg){ return deg * M_PI / 180.0; }

Qt3DCore::QEntity* Scene3DView::addWall(const QPointF& a_px,
                                        const QPointF& b_px,
                                        double height_m,
                                        double thick_m,
                                        double px_to_m,
                                        const QPointF& origin_px,
                                        Qt3DCore::QEntity* parent)
{
    const QPointF A = a_px - origin_px;
    const QPointF B = b_px - origin_px;

    const double dx = (B.x() - A.x()) * px_to_m;
    const double dy = (B.y() - A.y()) * px_to_m;
    const double len_m = std::hypot(dx, dy);
    if (len_m <= 1e-6) return nullptr;

    auto* e   = new Qt3DCore::QEntity(parent);
    auto* m   = new Qt3DExtras::QCuboidMesh(e);
    auto* tr  = new Qt3DCore::QTransform(e);
    auto* mat = new Qt3DExtras::QPhongMaterial(e);

    m->setXExtent(len_m);
    m->setYExtent(thick_m);
    m->setZExtent(height_m);

    // midpoint and rotation (Qt3D cuboid is along +X by default)
    const double mx = (A.x() + B.x()) * 0.5 * px_to_m;
    const double my = (A.y() + B.y()) * 0.5 * px_to_m;
    const double angDeg = std::atan2(dy, dx) * 180.0 / M_PI;

    tr->setTranslation(QVector3D(mx, my, height_m * 0.5));
    tr->setRotation(QQuaternion::fromAxisAndAngle(QVector3D(0,0,1), angDeg));
    mat->setDiffuse(QColor(210, 210, 210));

    e->addComponent(m);
    e->addComponent(tr);
    e->addComponent(mat);
    return e;
}

Qt3DCore::QEntity* Scene3DView::addFloor(const QRectF& bounds_px,
                                         double thick_m,
                                         double px_to_m,
                                         const QPointF& origin_px,
                                         Qt3DCore::QEntity* parent)
{
    if (bounds_px.isEmpty()) return nullptr;

    const double w_m = bounds_px.width()  * px_to_m;
    const double h_m = bounds_px.height() * px_to_m;

    auto* e   = new Qt3DCore::QEntity(parent);
    auto* m   = new Qt3DExtras::QPlaneMesh(e);
    auto* tr  = new Qt3DCore::QTransform(e);
    auto* mat = new Qt3DExtras::QPhongMaterial(e);

    m->setWidth(  float(w_m) );
    m->setHeight( float(h_m) );

    // plane is centered on its transform; place at z=0, centered
    const QPointF c = bounds_px.center() - origin_px;
    tr->setTranslation(QVector3D(c.x() * px_to_m, c.y() * px_to_m, 0.0f));

    mat->setDiffuse(QColor(230, 227, 215)); // light beige

    e->addComponent(m);
    e->addComponent(tr);
    e->addComponent(mat);

    if (thick_m > 1e-6) {
        // optional thin slab
        auto* slab = new Qt3DCore::QEntity(parent);
        auto* sm   = new Qt3DExtras::QCuboidMesh(slab);
        auto* st   = new Qt3DCore::QTransform(slab);
        auto* smat = new Qt3DExtras::QPhongMaterial(slab);

        sm->setXExtent(w_m);
        sm->setYExtent(h_m);
        sm->setZExtent(thick_m);
        st->setTranslation(QVector3D(c.x() * px_to_m, c.y() * px_to_m, thick_m * 0.5));
        smat->setDiffuse(QColor(210, 205, 195));

        slab->addComponent(sm);
        slab->addComponent(st);
        slab->addComponent(smat);
    }
    return e;
}

void Scene3DView::buildFromCanvas(const DrawingCanvas* canvas,
                                  double wallHeightMeters,
                                  double wallThicknessMeters,
                                  bool includeFloor)
{
    if (!canvas || !canvas->scene()) return;

    ensureNonZeroSize();

    clearScene();
    m_content = new Qt3DCore::QEntity(m_root);

    // ====== scale: PIXEL -> METER ======
    const double meters_per_px =
        canvas->convertUnits(canvas->toProjectUnitsPx(1.0),
                             canvas->projectUnit(),
                             DrawingCanvas::Unit::Meter);
    const double px_to_m = meters_per_px > 0 ? meters_per_px : 0.001; // fallback

    // center everything around origin so the camera can find it easily
    const QRectF bounds = canvas->scene()->itemsBoundingRect();
    const QPointF origin_px = bounds.center();

    // ====== FLOOR ======
    if (includeFloor) {
        addFloor(bounds, /*thickness*/0.05, px_to_m, origin_px, m_content);
    }

    // ====== WALLS: from QGraphicsLineItem (visible & unlocked only) ======
    for (QGraphicsItem* it : canvas->scene()->items()) {
        if (!it->isVisible()) continue;

        const int layerId = it->data(0).toInt();
        if (!canvas->isLayerVisible(layerId) || canvas->isLayerLocked(layerId))
            continue;

        if (auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(it)) {
            const QLineF L = ln->line();
            const QPointF p1s = ln->mapToScene(L.p1());
            const QPointF p2s = ln->mapToScene(L.p2());
            addWall(p1s, p2s,
                    wallHeightMeters, wallThicknessMeters,
                    px_to_m, origin_px, m_content);
        }
    }

    // reframe camera to bounds
    auto* cam = m_view->camera();
    const double w_m = bounds.width()  * px_to_m;
    const double h_m = bounds.height() * px_to_m;
    const double diag = std::sqrt(w_m*w_m + h_m*h_m) + 0.001;

    cam->setViewCenter(QVector3D(0,0,0));
    cam->setPosition(QVector3D(0, -float(diag*0.9), float(diag*0.5)));
}
