#include "SceneView3d.h"
#include "canvas/DrawingCanvas.h"

#include <Qt3DExtras/Qt3DWindow>
#include <Qt3DExtras/QOrbitCameraController>
#include <Qt3DExtras/QPhongMaterial>
#include <Qt3DExtras/QPlaneMesh>
#include <Qt3DExtras/QCuboidMesh>
#include <Qt3DCore/QEntity>
#include <Qt3DCore/QTransform>
#include <Qt3DRender/QCamera>
#include <Qt3DRender/QCameraLens>

#include <QVBoxLayout>
#include <QTimer>
#include <QEvent>
#include <QResizeEvent>
#include <QtMath>

static Qt3DExtras::QPhongMaterial* makeMat(Qt3DCore::QEntity* parent, const QColor& c)
{
    auto* m = new Qt3DExtras::QPhongMaterial(parent);
    m->setDiffuse(c);
    return m;
}

Scene3DView::Scene3DView(QWidget* parent)
    : QWidget(parent)
    , m_view(new Qt3DExtras::Qt3DWindow())
    , m_container(QWidget::createWindowContainer(m_view, this))
    , m_root(new Qt3DCore::QEntity())
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0,0,0,0);
    lay->addWidget(m_container);

    if (m_view->width() == 0 || m_view->height() == 0)
        m_view->resize(640, 480);

    m_view->setRootEntity(m_root);

    auto* cam = m_view->camera();
    cam->lens()->setPerspectiveProjection(45.f, 16.f/9.f, 0.05f, 5000.f);
    cam->setUpVector({0.f, 1.f, 0.f});
    cam->setPosition({8.f, 6.f, 10.f});
    cam->setViewCenter({0.f, 0.f, 0.f});

    m_orbit = new Qt3DExtras::QOrbitCameraController(m_root);
    m_orbit->setCamera(cam);

    m_container->installEventFilter(this);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
}

Scene3DView::~Scene3DView() = default;

bool Scene3DView::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_container) {
        if (ev->type() == QEvent::Show || ev->type() == QEvent::Resize) {
            QTimer::singleShot(0, this, [this]{ ensureNonZeroSize(); });
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void Scene3DView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    ensureNonZeroSize();
    if (m_mode != ViewMode::Perspective && m_sync2D) syncCameraTo2D();
}

void Scene3DView::ensureNonZeroSize()
{
    if (!m_container || !m_view) return;
    int w = m_container->width();
    int h = m_container->height();
    if (w <= 0) w = 320;
    if (h <= 0) h = 240;
    if (w != m_view->width() || h != m_view->height())
        m_view->resize(w, h);
}

/* ======================= BUILD FROM CANVAS ======================= */

void Scene3DView::clearGeometry()
{
    if (m_geomRoot) {
        delete m_geomRoot;
        m_geomRoot = nullptr;
    }
    m_geomRoot = new Qt3DCore::QEntity(m_root);
}

void Scene3DView::addWallFromSegment(const QPointF& aPx,
                                     const QPointF& bPx,
                                     double pxToM,
                                     double wallHeightM,
                                     double wallThicknessM)
{
    const QVector3D A(aPx.x()*pxToM, aPx.y()*pxToM, 0.f);
    const QVector3D B(bPx.x()*pxToM, bPx.y()*pxToM, 0.f);

    const QVector3D D = B - A;
    const float len = D.length();
    if (len <= 1e-6f) return;

    auto* e = new Qt3DCore::QEntity(m_geomRoot);

    auto* mesh = new Qt3DExtras::QCuboidMesh(e);
    mesh->setXExtent(len);
    mesh->setYExtent(float(wallThicknessM));
    mesh->setZExtent(float(wallHeightM));

    auto* tr = new Qt3DCore::QTransform(e);

    // Midpoint
    const QVector3D M = (A + B) * 0.5f;

    // Rotate around Z to align with segment (in XY plane)
    const float angRad = std::atan2(D.y(), D.x());
    const QQuaternion rot = QQuaternion::fromAxisAndAngle(QVector3D(0,0,1), qRadiansToDegrees(angRad));

    // Raise by half height so it sits on floor (z=0)
    tr->setTranslation(QVector3D(M.x(), M.y(), float(wallHeightM*0.5)));
    tr->setRotation(rot);

    auto* mat = makeMat(e, QColor(200, 200, 210));

    e->addComponent(mesh);
    e->addComponent(tr);
    e->addComponent(mat);
}

void Scene3DView::addFloorQuad(const QRectF& boundsPx, double pxToM)
{
    const float w = float(boundsPx.width()  * pxToM);
    const float h = float(boundsPx.height() * pxToM);
    if (w <= 0 || h <= 0) return;

    auto* e = new Qt3DCore::QEntity(m_geomRoot);
    auto* mesh = new Qt3DExtras::QPlaneMesh(e);
    mesh->setWidth(w);
    mesh->setHeight(h);

    // Position plane at the center of bounds, z=0
    const float cx = float(boundsPx.center().x() * pxToM);
    const float cy = float(boundsPx.center().y() * pxToM);

    auto* tr = new Qt3DCore::QTransform(e);
    tr->setTranslation(QVector3D(cx, cy, 0.f));
    // QPlaneMesh is in XY plane by default with normal +Z — perfect for a floor

    auto* mat = makeMat(e, QColor(220, 235, 220));

    e->addComponent(mesh);
    e->addComponent(tr);
    e->addComponent(mat);
}

void Scene3DView::buildFromCanvas(const DrawingCanvas* canvas,
                                  double wallHeightM,
                                  double wallThicknessM,
                                  bool includeFloor)
{
    if (!canvas || !canvas->scene()) return;

    // 1) Use the 2D scene bounds to derive a sane px→meters scale
    const QRectF brPx = canvas->scene()->itemsBoundingRect().normalized();
    const double spanPx = std::max(brPx.width(), brPx.height());
    // Aim to fit largest dimension into ~20 meters if no explicit scale
    const double pxToM = (spanPx > 0.0) ? (20.0 / spanPx) : 0.01; // fallback 1cm/px

    // 2) Clear old geometry container
    clearGeometry();

    // 3) Optionally add a floor patch covering the drawing bounds
    if (includeFloor && brPx.isValid())
        addFloorQuad(brPx, pxToM);

    // 4) Collect wall segments from lines/rects/polys
    const auto items = canvas->scene()->items();
    for (QGraphicsItem* it : items) {
        if (!it->isVisible()) continue;

        if (auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(it)) {
            const QLineF L = ln->line();
            const QPointF A = ln->mapToScene(L.p1());
            const QPointF B = ln->mapToScene(L.p2());
            addWallFromSegment(A, B, pxToM, wallHeightM, wallThicknessM);
        } else if (auto* rc = qgraphicsitem_cast<QGraphicsRectItem*>(it)) {
            const QPolygonF poly = rc->mapToScene(QPolygonF(rc->rect()));
            for (int i = 0; i < poly.size(); ++i) {
                const QPointF A = poly[i];
                const QPointF B = poly[(i+1) % poly.size()];
                addWallFromSegment(A, B, pxToM, wallHeightM, wallThicknessM);
            }
        } else if (auto* pg = qgraphicsitem_cast<QGraphicsPolygonItem*>(it)) {
            const QPolygonF poly = pg->mapToScene(pg->polygon());
            if (poly.size() >= 2) {
                for (int i = 0; i < poly.size(); ++i) {
                    const QPointF A = poly[i];
                    const QPointF B = poly[(i+1) % poly.size()];
                    addWallFromSegment(A, B, pxToM, wallHeightM, wallThicknessM);
                }
            }
        } else if (auto* path = qgraphicsitem_cast<QGraphicsPathItem*>(it)) {
            const QPainterPath sc = path->mapToScene(path->path());
            const auto polys = sc.toSubpathPolygons();
            for (const QPolygonF& lp : polys) {
                for (int i = 0; i < lp.size(); ++i) {
                    const QPointF A = lp[i];
                    const QPointF B = lp[(i+1) % lp.size()];
                    addWallFromSegment(A, B, pxToM, wallHeightM, wallThicknessM);
                }
            }
        }
    }

    // 5) Frame camera to content
    frameCameraToBounds(brPx, pxToM);

    // 6) If in ortho + sync, align to the 2D camera
    if (m_mode != ViewMode::Perspective && m_sync2D) syncCameraTo2D();
}

/* ======================= CAMERA MODES ======================= */

void Scene3DView::setMode(ViewMode m)
{
    m_mode = m;
    switch (m) {
    case ViewMode::OrthoTop:   setTopOrtho();   break;
    case ViewMode::OrthoFront: setFrontOrtho(); break;
    case ViewMode::OrthoRight: setRightOrtho(); break;
    case ViewMode::Perspective: setPerspective(); break;
    }
}

void Scene3DView::setTopOrtho()   { applyTopOrtho(10.f);   if (m_sync2D) syncCameraTo2D(); }
void Scene3DView::setFrontOrtho() { applyFrontOrtho(10.f); }
void Scene3DView::setRightOrtho() { applyRightOrtho(10.f); }
void Scene3DView::setPerspective(){ applyPerspectiveDefault(); }

void Scene3DView::toggleOrthoPerspective()
{
    if (m_mode == ViewMode::Perspective) setMode(ViewMode::OrthoTop);
    else setMode(ViewMode::Perspective);
}

void Scene3DView::applyTopOrtho(float widthMeters)
{
    auto* cam = m_view->camera();
    const float aspect = float(std::max(1, m_view->width())) / float(std::max(1, m_view->height()));
    const float halfW  = widthMeters * 0.5f;
    const float halfH  = halfW / aspect;

    cam->lens()->setOrthographicProjection(-halfW, +halfW, -halfH, +halfH, -5000.f, 5000.f);
    cam->setPosition({0.f, 0.f, 50.f});
    cam->setViewCenter({0.f, 0.f, 0.f});
    cam->setUpVector({0.f, 1.f, 0.f});
    m_orbit->setEnabled(false);
    m_mode = ViewMode::OrthoTop;
}

void Scene3DView::applyFrontOrtho(float widthMeters)
{
    auto* cam = m_view->camera();
    const float aspect = float(std::max(1, m_view->width())) / float(std::max(1, m_view->height()));
    const float halfW  = widthMeters * 0.5f;
    const float halfH  = halfW / aspect;

    cam->lens()->setOrthographicProjection(-halfW, +halfW, -halfH, +halfH, -5000.f, 5000.f);
    cam->setPosition({0.f, 50.f, 0.f});
    cam->setViewCenter({0.f, 0.f, 0.f});
    cam->setUpVector({0.f, 0.f, 1.f});
    m_orbit->setEnabled(false);
    m_mode = ViewMode::OrthoFront;
}

void Scene3DView::applyRightOrtho(float widthMeters)
{
    auto* cam = m_view->camera();
    const float aspect = float(std::max(1, m_view->width())) / float(std::max(1, m_view->height()));
    const float halfW  = widthMeters * 0.5f;
    const float halfH  = halfW / aspect;

    cam->lens()->setOrthographicProjection(-halfW, +halfW, -halfH, +halfH, -5000.f, 5000.f);
    cam->setPosition({50.f, 0.f, 0.f});
    cam->setViewCenter({0.f, 0.f, 0.f});
    cam->setUpVector({0.f, 0.f, 1.f});
    m_orbit->setEnabled(false);
    m_mode = ViewMode::OrthoRight;
}

void Scene3DView::applyPerspectiveDefault()
{
    auto* cam = m_view->camera();
    cam->lens()->setPerspectiveProjection(45.f, 16.f/9.f, 0.05f, 5000.f);
    cam->setUpVector({0.f, 1.f, 0.f});
    cam->setPosition({8.f, 6.f, 10.f});
    cam->setViewCenter({0.f, 0.f, 0.f});
    m_orbit->setEnabled(true);
    m_mode = ViewMode::Perspective;
}

/* ======================= 2D SYNC / FRAMING ======================= */

void Scene3DView::setSync2D(bool on)
{
    m_sync2D = on;
    if (on && m_mode != ViewMode::Perspective) syncCameraTo2D();
}

void Scene3DView::connectCanvas(DrawingCanvas* canvas)
{
    if (m_canvas == canvas) return;
    if (m_canvas) disconnect(m_canvas, nullptr, this, nullptr);
    m_canvas = canvas;
    if (!m_canvas) return;

    connect(m_canvas, &DrawingCanvas::viewChanged, this, [this]{
        if (m_sync2D && m_mode != ViewMode::Perspective) syncCameraTo2D();
    });
}

void Scene3DView::syncCameraTo2D()
{
    if (!m_canvas || m_mode == ViewMode::Perspective) return;

    const QRectF visPx = m_canvas->mapToScene(m_canvas->viewport()->geometry()).boundingRect();
    if (visPx.width() <= 0 || visPx.height() <= 0) return;

    // Just map px→world 1:1 here; buildFromCanvas already scaled content
    const double pxToM = 1.0;

    const float w = float(visPx.width()  * pxToM);
    const float h = float(visPx.height() * pxToM);
    const float aspect = float(std::max(1, m_view->width())) / float(std::max(1, m_view->height()));
    float halfW = 0.5f * w, halfH = 0.5f * h;
    if (halfW / halfH > aspect) halfH = halfW / aspect; else halfW = halfH * aspect;

    auto* cam = m_view->camera();
    cam->lens()->setOrthographicProjection(-halfW, +halfW, -halfH, +halfH, -5000.f, 5000.f);

    const QPointF c = visPx.center();
    const float cx = float(c.x() * pxToM);
    const float cy = float(c.y() * pxToM);

    if (m_mode == ViewMode::OrthoTop) {
        cam->setPosition({cx, cy, 50.f});
        cam->setViewCenter({cx, cy, 0.f});
        cam->setUpVector({0.f, 1.f, 0.f});
    } else if (m_mode == ViewMode::OrthoFront) {
        cam->setPosition({cx, 50.f, cy});
        cam->setViewCenter({cx, 0.f,  cy});
        cam->setUpVector({0.f, 0.f, 1.f});
    } else if (m_mode == ViewMode::OrthoRight) {
        cam->setPosition({50.f, cy, cx});
        cam->setViewCenter({0.f,  cy, cx});
        cam->setUpVector({0.f, 0.f, 1.f});
    }
}

void Scene3DView::frameCameraToBounds(const QRectF& boundsPx, double pxToM)
{
    if (!boundsPx.isValid()) { applyPerspectiveDefault(); return; }

    const float cx = float(boundsPx.center().x() * pxToM);
    const float cy = float(boundsPx.center().y() * pxToM);
    const float wx = float(boundsPx.width()  * pxToM);
    const float wy = float(boundsPx.height() * pxToM);
    const float r  = 0.5f * std::max(wx, wy);

    auto* cam = m_view->camera();
    cam->setUpVector({0.f, 1.f, 0.f});
    cam->setViewCenter({cx, cy, 0.f});

    // Back off proportionally to the radius
    const float dist = std::max(5.f, r * 2.2f);
    cam->setPosition({cx + dist, cy + dist*0.6f, std::max(6.f, r*1.4f)});
}
