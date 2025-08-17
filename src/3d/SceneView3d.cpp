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
#include <Qt3DRender/QDirectionalLight>

#include <QVBoxLayout>
#include <QTimer>
#include <QEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>

// ---------- small helpers ----------
static Qt3DExtras::QPhongMaterial* makeMat(Qt3DCore::QEntity* parent, const QColor& c)
{
    auto* m = new Qt3DExtras::QPhongMaterial(parent);
    m->setDiffuse(c);
    return m;
}

// grid made from thin cuboids (works on stock Qt3DExtras)
static Qt3DCore::QEntity* makeGroundGridBars(Qt3DCore::QEntity* parent,
                                             float halfSize,
                                             float step,
                                             const QVector3D& center,
                                             float zOffset = 0.002f)
{
    auto* root = new Qt3DCore::QEntity(parent);
    auto* minorMat = makeMat(root, QColor(185,185,185));
    auto* majorMat = makeMat(root, QColor(110,110,110));

    const int n = int(std::ceil(halfSize/step));
    const float barT = std::max(0.0025f, step * 0.02f);  // thickness
    const float barH = std::max(0.0008f, step * 0.006f); // height

    for (int i=-n; i<=n; ++i) {
        const float v = i*step;
        const bool major = (i%10)==0;
        auto* mat = major ? majorMat : minorMat;

        // X line (vary Y)
        {
            auto* e  = new Qt3DCore::QEntity(root);
            auto* m  = new Qt3DExtras::QCuboidMesh(e);
            m->setXExtent(2.f*halfSize);
            m->setYExtent(barT);
            m->setZExtent(barH);
            auto* tr = new Qt3DCore::QTransform(e);
            tr->setTranslation(QVector3D(center.x(), center.y()+v, zOffset));
            e->addComponent(m); e->addComponent(tr); e->addComponent(mat);
        }
        // Y line (vary X)
        {
            auto* e  = new Qt3DCore::QEntity(root);
            auto* m  = new Qt3DExtras::QCuboidMesh(e);
            m->setXExtent(barT);
            m->setYExtent(2.f*halfSize);
            m->setZExtent(barH);
            auto* tr = new Qt3DCore::QTransform(e);
            tr->setTranslation(QVector3D(center.x()+v, center.y(), zOffset));
            e->addComponent(m); e->addComponent(tr); e->addComponent(mat);
        }
    }
    return root;
}

// ---------- class ----------
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

    // Camera (Z-up)
    auto* cam = m_view->camera();
    cam->lens()->setPerspectiveProjection(45.f, 16.f/9.f, 0.1f, 5000.f);
    cam->setUpVector({0.f, 0.f, 1.f});
    cam->setPosition({20.f, 16.f, 12.f});
    cam->setViewCenter({0.f, 0.f, 0.f});

    m_orbit = new Qt3DExtras::QOrbitCameraController(m_root);
    m_orbit->setCamera(cam);

    // Light
    {
        auto* le = new Qt3DCore::QEntity(m_root);
        auto* dl = new Qt3DRender::QDirectionalLight(le);
        dl->setWorldDirection(QVector3D(-0.4f, -0.5f, -0.8f));
        le->addComponent(dl);
    }

    m_container->installEventFilter(this);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
}

Scene3DView::~Scene3DView() = default;

bool Scene3DView::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_container) {
        switch (ev->type()) {
        case QEvent::Show:
        case QEvent::Resize:
            QTimer::singleShot(0, this, [this]{ ensureNonZeroSize(); });
            break;
        case QEvent::MouseButtonPress:
            if (m_mode != ViewMode::Perspective) beginPan(static_cast<QMouseEvent*>(ev)->pos());
            break;
        case QEvent::MouseMove:
            if (m_mode != ViewMode::Perspective) updatePan(static_cast<QMouseEvent*>(ev)->pos());
            break;
        case QEvent::MouseButtonRelease:
            if (m_mode != ViewMode::Perspective) endPan();
            break;
        case QEvent::Wheel:
            if (m_mode != ViewMode::Perspective) {
                auto* we = static_cast<QWheelEvent*>(ev);
                // 120 units per notch; positive => zoom in
                const int steps = we->angleDelta().y() / 120;
                const float factor = std::pow(0.9f, steps); // 0.9 per notch
                orthoZoom(factor, we->position().toPoint());
                return true;
            }
            break;
        default: break;
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
    if (m_geomRoot) { delete m_geomRoot; m_geomRoot = nullptr; }
    m_geomRoot = new Qt3DCore::QEntity(m_root);
    m_gridEntity  = nullptr;
    m_floorEntity = nullptr;
}

Qt3DCore::QEntity* Scene3DView::addFloorQuad(const QRectF& boundsPx, double pxToM)
{
    const float w = float(boundsPx.width()  * pxToM);
    const float h = float(boundsPx.height() * pxToM);
    if (w <= 0 || h <= 0) return nullptr;

    auto* e = new Qt3DCore::QEntity(m_geomRoot);
    // auto* mesh = new Qt3DExtras::QPlaneMesh(e);  // XY plane, +Z normal
    // mesh->setWidth(w);
    // mesh->setHeight(h);

    const float cx = float(boundsPx.center().x() * pxToM);
    const float cy = float(boundsPx.center().y() * pxToM);

    auto* tr = new Qt3DCore::QTransform(e);
    tr->setTranslation(QVector3D(cx, cy, 0.f));

    // darker neutral, tweak as desired
    auto* mat = makeMat(e, QColor(210, 210, 210));

    // e->addComponent(mesh);
    e->addComponent(tr);
    e->addComponent(mat);
    return e;
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
    if (len <= 1e-4f) return;

    auto* e = new Qt3DCore::QEntity(m_geomRoot);

    auto* mesh = new Qt3DExtras::QCuboidMesh(e);
    mesh->setXExtent(len);
    mesh->setYExtent(float(wallThicknessM));
    mesh->setZExtent(float(wallHeightM));

    auto* tr = new Qt3DCore::QTransform(e);
    const QVector3D M = (A + B) * 0.5f;
    const float angRad = std::atan2(D.y(), D.x());
    const QQuaternion rot =
        QQuaternion::fromAxisAndAngle(QVector3D(0,0,1), qRadiansToDegrees(angRad));

    tr->setTranslation(QVector3D(M.x(), M.y(), float(wallHeightM * 0.5f)));
    tr->setRotation(rot);

    auto* mat = makeMat(e, QColor(200, 200, 210));

    e->addComponent(mesh); e->addComponent(tr); e->addComponent(mat);
}

void Scene3DView::setGridVisible(bool on)
{
    m_gridVisible = on;
    if (m_gridEntity) m_gridEntity->setEnabled(on);
}

void Scene3DView::setFloorVisible(bool on)
{
    m_floorVisible = on;
    if (m_floorEntity) m_floorEntity->setEnabled(on);
}


void Scene3DView::buildFromCanvas(const DrawingCanvas* canvas,
                                  double wallHeightM,
                                  double wallThicknessM,
                                  bool includeFloor)
{
    if (!canvas || !canvas->scene()) return;

    const QRectF brPx = canvas->scene()->itemsBoundingRect().normalized();
    const double spanPx = std::max(brPx.width(), brPx.height());
    const double pxToM = (spanPx > 0.0) ? (20.0 / spanPx) : 0.01;  // fit ~20 m across

    clearGeometry();

    // Floor + grid
    if (includeFloor && brPx.isValid()) {
        m_floorEntity = addFloorQuad(brPx, pxToM); // will be hidden in ortho
        const float half = float(0.6 * std::max(brPx.width(), brPx.height()) * pxToM);
        const QVector3D center(float(brPx.center().x() * pxToM),
                               float(brPx.center().y() * pxToM), 0.f);
        m_gridEntity = makeGroundGridBars(m_geomRoot, half, /*step*/0.25f, center, /*z*/0.002f);
        if (m_gridEntity)  m_gridEntity->setEnabled(m_gridVisible);
    }

    // Filter short segments
    const double kMinLenPx = 20.0;

    const auto items = canvas->scene()->items();
    for (QGraphicsItem* it : items) {
        if (!it->isVisible()) continue;

        if (auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(it)) {
            QLineF L = ln->line();
            QPointF A = ln->mapToScene(L.p1());
            QPointF B = ln->mapToScene(L.p2());
            if (QLineF(A,B).length() < kMinLenPx) continue;
            addWallFromSegment(A, B, pxToM, wallHeightM, wallThicknessM);
        } else if (auto* rc = qgraphicsitem_cast<QGraphicsRectItem*>(it)) {
            const QPolygonF poly = rc->mapToScene(QPolygonF(rc->rect()));
            for (int i = 0; i < poly.size(); ++i) {
                const QPointF A = poly[i], B = poly[(i+1) % poly.size()];
                if (QLineF(A,B).length() < kMinLenPx) continue;
                addWallFromSegment(A, B, pxToM, wallHeightM, wallThicknessM);
            }
        } else if (auto* pg = qgraphicsitem_cast<QGraphicsPolygonItem*>(it)) {
            const QPolygonF poly = pg->mapToScene(pg->polygon());
            for (int i = 0; i < poly.size(); ++i) {
                const QPointF A = poly[i], B = poly[(i+1) % poly.size()];
                if (QLineF(A,B).length() < kMinLenPx) continue;
                addWallFromSegment(A, B, pxToM, wallHeightM, wallThicknessM);
            }
        } else if (auto* path = qgraphicsitem_cast<QGraphicsPathItem*>(it)) {
            const QPainterPath sc = path->mapToScene(path->path());
            const auto polys = sc.toSubpathPolygons();
            for (const QPolygonF& lp : polys) {
                for (int i = 0; i < lp.size(); ++i) {
                    const QPointF A = lp[i], B = lp[(i+1) % lp.size()];
                    if (QLineF(A,B).length() < kMinLenPx) continue;
                    addWallFromSegment(A, B, pxToM, wallHeightM, wallThicknessM);
                }
            }
        }
    }

    // show/hide floor depending on mode
    if (m_floorEntity)
        m_floorEntity->setEnabled(m_floorVisible && (m_mode == ViewMode::Perspective));

    frameCameraToBounds(brPx, pxToM);
    if (m_mode != ViewMode::Perspective && m_sync2D) syncCameraTo2D();
}

/* ======================= CAMERA MODES (Z-up) ======================= */

void Scene3DView::setMode(ViewMode m)
{
    m_mode = m;
    switch (m) {
    case ViewMode::OrthoTop:    setTopOrtho();   break;
    case ViewMode::OrthoFront:  setFrontOrtho(); break;
    case ViewMode::OrthoRight:  setRightOrtho(); break;
    case ViewMode::Perspective: setPerspective(); break;
    }
    // floor visibility rule
    if (m_floorEntity)
        m_floorEntity->setEnabled(m_floorVisible && (m_mode == ViewMode::Perspective));
}

void Scene3DView::setTopOrtho()   { applyTopOrtho(10.f);   if (m_sync2D) syncCameraTo2D(); m_orbit->setEnabled(false); }
void Scene3DView::setFrontOrtho() { applyFrontOrtho(10.f); m_orbit->setEnabled(false); }
void Scene3DView::setRightOrtho() { applyRightOrtho(10.f); m_orbit->setEnabled(false); }
void Scene3DView::setPerspective(){ applyPerspectiveDefault(); m_orbit->setEnabled(true); }

void Scene3DView::toggleOrthoPerspective()
{
    if (m_mode == ViewMode::Perspective) setMode(ViewMode::OrthoTop);
    else setMode(ViewMode::Perspective);
}

void Scene3DView::applyTopOrtho(float widthMeters)
{
    auto* cam = m_view->camera();
    const float aspect = float(std::max(1, m_view->width())) / float(std::max(1, m_view->height()));
    m_halfW = widthMeters * 0.5f;
    m_halfH = m_halfW / aspect;

    cam->lens()->setOrthographicProjection(-m_halfW, +m_halfW, -m_halfH, +m_halfH, 0.1f, 5000.f);
    cam->setUpVector({0.f, 0.f, 1.f});
    cam->setPosition({0.f, 0.f, 50.f});
    cam->setViewCenter({0.f, 0.f, 0.f});
}

void Scene3DView::applyFrontOrtho(float widthMeters)
{
    auto* cam = m_view->camera();
    const float aspect = float(std::max(1, m_view->width())) / float(std::max(1, m_view->height()));
    m_halfW = widthMeters * 0.5f;
    m_halfH = m_halfW / aspect;

    cam->lens()->setOrthographicProjection(-m_halfW, +m_halfW, -m_halfH, +m_halfH, 0.1f, 5000.f);
    cam->setUpVector({0.f, 0.f, 1.f});
    cam->setPosition({0.f, 50.f, 2.f});     // looking -Y
    cam->setViewCenter({0.f, 0.f, 2.f});
}

void Scene3DView::applyRightOrtho(float widthMeters)
{
    auto* cam = m_view->camera();
    const float aspect = float(std::max(1, m_view->width())) / float(std::max(1, m_view->height()));
    m_halfW = widthMeters * 0.5f;
    m_halfH = m_halfW / aspect;

    cam->lens()->setOrthographicProjection(-m_halfW, +m_halfW, -m_halfH, +m_halfH, 0.1f, 5000.f);
    cam->setUpVector({0.f, 0.f, 1.f});
    cam->setPosition({50.f, 0.f, 2.f});     // looking -X
    cam->setViewCenter({0.f, 0.f, 2.f});
}

void Scene3DView::applyPerspectiveDefault()
{
    auto* cam = m_view->camera();
    cam->lens()->setPerspectiveProjection(45.f, 16.f/9.f, 0.1f, 5000.f);
    cam->setUpVector({0.f, 0.f, 1.f});
    cam->setPosition({20.f, 16.f, 12.f});
    cam->setViewCenter({0.f, 0.f, 0.f});
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

    const QRectF visPx =
        m_canvas->mapToScene(m_canvas->viewport()->geometry()).boundingRect();
    if (visPx.width() <= 0 || visPx.height() <= 0) return;

    const float w = float(visPx.width());
    const float h = float(visPx.height());
    const float aspect = float(std::max(1, m_view->width())) / float(std::max(1, m_view->height()));
    m_halfW = 0.5f * w;
    m_halfH = m_halfW / aspect;
    if (m_halfH < 0.5f * h) { m_halfH = 0.5f * h; m_halfW = m_halfH * aspect; }

    auto* cam = m_view->camera();
    cam->lens()->setOrthographicProjection(-m_halfW, +m_halfW, -m_halfH, +m_halfH, 0.1f, 5000.f);

    const QPointF c = visPx.center();
    const float cx = float(c.x());
    const float cy = float(c.y());

    cam->setUpVector({0.f, 0.f, 1.f});
    if (m_mode == ViewMode::OrthoTop) {
        cam->setPosition({cx, cy, 50.f});
        cam->setViewCenter({cx, cy, 0.f});
    } else if (m_mode == ViewMode::OrthoFront) {
        cam->setPosition({cx, 50.f, 2.f});
        cam->setViewCenter({cx, 0.f,  2.f});
    } else if (m_mode == ViewMode::OrthoRight) {
        cam->setPosition({50.f, cy, 2.f});
        cam->setViewCenter({0.f,  cy,  2.f});
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
    cam->setUpVector({0.f, 0.f, 1.f});
    cam->setViewCenter({cx, cy, 0.f});

    const float dist = std::max(5.f, r * 2.2f);
    cam->setPosition({cx + dist, cy + dist*0.7f, std::max(6.f, r*1.4f)});
}

/* ======================= ORTHO INTERACTION ======================= */

void Scene3DView::beginPan(const QPoint& p)
{
    m_panning = true;
    m_lastMouse = p;
}

void Scene3DView::updatePan(const QPoint& p)
{
    if (!m_panning) return;
    const QPoint d = p - m_lastMouse;
    m_lastMouse = p;

    auto* cam = m_view->camera();
    const float sx = (2.f * m_halfW) / std::max(1, m_view->width());
    const float sy = (2.f * m_halfH) / std::max(1, m_view->height());

    QVector3D delta;
    switch (m_mode) {
    case ViewMode::OrthoTop:   delta = QVector3D(-d.x()*sx, -d.y()*sy, 0.f); break;          // pan in XY
    case ViewMode::OrthoFront: delta = QVector3D(-d.x()*sx, 0.f,         +d.y()*sy); break;  // pan X/Z
    case ViewMode::OrthoRight: delta = QVector3D(0.f,       -d.x()*sx,   +d.y()*sy); break;  // pan Y/Z
    default: return;
    }

    cam->setPosition(cam->position() + delta);
    cam->setViewCenter(cam->viewCenter() + delta);
}

void Scene3DView::endPan()
{
    m_panning = false;
}

void Scene3DView::orthoZoom(float factor, const QPoint&)
{
    // clamp zoom
    const float minHalf = 0.5f;
    const float maxHalf = 1e6f;
    m_halfW = std::clamp(m_halfW * factor, minHalf, maxHalf);
    m_halfH = std::clamp(m_halfH * factor, minHalf, maxHalf);

    auto* cam = m_view->camera();
    cam->lens()->setOrthographicProjection(-m_halfW, +m_halfW, -m_halfH, +m_halfH, 0.1f, 5000.f);
}
