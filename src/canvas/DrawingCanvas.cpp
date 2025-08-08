#include "DrawingCanvas.h"

#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsItemGroup>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPainter>
#include <QApplication>               // keyboardModifiers

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>

#include <QtSvg/QSvgRenderer>
#include <QtSvg/QSvgGenerator>
#include <QtSvgWidgets/QGraphicsSvgItem>
#include <QUndoCommand>               // NEW

#include <cmath>  // std::round
#include <limits>

// ─── helpers for color serialization ─────────────────────────
static QString colorToHex(const QColor& c) { return c.name(QColor::HexArgb); }
static QColor  hexToColor(const QString& s){ QColor c(s); return c.isValid()? c : QColor(Qt::black); }

// ─── ctor ────────────────────────────────────────────────────
DrawingCanvas::DrawingCanvas(QWidget* parent)
    : QGraphicsView(parent),
      m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::RubberBandDrag);

    // Rebuild resize/rotate handles on selection change  // NEW
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this]{
        clearHandles();
        createHandlesForSelected();
    });
}

// ─── Grid + object Snap helper (Shift enables object snap) ───
QPointF DrawingCanvas::snap(const QPointF& scenePos) const
{
    // grid
    const double gx = std::round(scenePos.x() / m_gridSize) * m_gridSize;
    const double gy = std::round(scenePos.y() / m_gridSize) * m_gridSize;
    QPointF best(gx, gy);
    double best2 = std::numeric_limits<double>::max();

    // if Shift not held → grid only
    if (!(QApplication::keyboardModifiers() & Qt::ShiftModifier))
        return best;

    // nearby items (scene-space bounding box query)
    const qreal px = 12.0;
    QRectF query(scenePos - QPointF(px,px), QSizeF(2*px, 2*px));
    const auto items = m_scene->items(query);

    for (auto* it : items) {
        if (!it->isVisible()) continue;
        for (const auto& s : collectSnapPoints(it)) {
            double d = QLineF(scenePos, s).length();
            double d2 = d*d;
            if (d2 < best2) { best2 = d2; best = s; }
        }
    }
    if (best2 < px*px) updateSnapIndicator(best);
    else updateSnapIndicator(QPointF()); // hide
    return (best2 < px*px) ? best : QPointF(gx, gy);
}

// ─── Draw background grid ────────────────────────────────────
void DrawingCanvas::drawBackground(QPainter* p, const QRectF& rect)
{
    if (!m_showGrid) {
        QGraphicsView::drawBackground(p, rect);
        return;
    }

    const QPen gridPen(QColor(230, 230, 230));
    p->setPen(gridPen);

    const double left = std::floor(rect.left() /  m_gridSize) * m_gridSize;
    const double top  = std::floor(rect.top()  /  m_gridSize) * m_gridSize;

    for (double x = left; x < rect.right(); x += m_gridSize)
        p->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));

    for (double y = top; y < rect.bottom(); y += m_gridSize)
        p->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
}

// ─── Mouse overrides ─────────────────────────────────────────
void DrawingCanvas::mousePressEvent(QMouseEvent* e)
{
    const QPointF sceneP = mapToScene(e->pos());

    // Try handle drag first (resize/rotate) // NEW
    if (handleMousePress(sceneP, e->button())) {
        return;
    }

    if (m_tool == Tool::Select) {
        // Track move start for undo // NEW
        auto sel = m_scene->selectedItems();
        if (sel.size() == 1) {
            m_moveTarget  = sel.first();
            m_moveStartPos = m_moveTarget->pos();
        } else {
            m_moveTarget = nullptr;
        }
        setDragMode(QGraphicsView::RubberBandDrag);
        QGraphicsView::mousePressEvent(e);
        return;
    }
    setDragMode(QGraphicsView::NoDrag);

    const QPointF pos = snap(sceneP);

    switch (m_tool) {
    case Tool::Line: {
        m_startPos = pos;
        auto* item = m_scene->addLine(QLineF(pos, pos), currentPen());
        item->setData(0, m_layer);
        item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        m_tempItem = item;
        break;
    }
    case Tool::Rect: {
        m_startPos = pos;
        auto* item = m_scene->addRect(QRectF(pos, pos), currentPen(), currentBrush());
        item->setData(0, m_layer);
        item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        m_tempItem = item;
        break;
    }
    case Tool::Ellipse: {
        m_startPos = pos;
        auto* item = m_scene->addEllipse(QRectF(pos, pos), currentPen(), currentBrush());
        item->setData(0, m_layer);
        item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        m_tempItem = item;
        break;
    }
    case Tool::Polygon: {
        // Left click = add point, Right click = finish
        if (e->button() == Qt::RightButton) {
            if (m_polyActive && m_poly.size() > 2) {
                // finalize polygon → push Add undo
                if (m_tempItem && m_undo) pushAddCmd(m_tempItem, "Add Polygon");
            }
            m_polyActive = false;
            m_poly.clear();
            m_tempItem = nullptr;
            return;
        }
        const QPointF p = pos;
        if (!m_polyActive) {
            m_polyActive = true;
            m_poly.clear();
            m_poly << p;

            auto* item = m_scene->addPolygon(m_poly, currentPen(), currentBrush());
            item->setData(0, m_layer);
            item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
            m_tempItem = item;
        } else {
            // add a vertex
            m_poly << p;
            if (auto* polyItem = qgraphicsitem_cast<QGraphicsPolygonItem*>(m_tempItem))
                polyItem->setPolygon(m_poly);
        }
        break;
    }
    default:
        QGraphicsView::mousePressEvent(e);
    }
}

void DrawingCanvas::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF sceneP = mapToScene(e->pos());

    // Handle drag (resize/rotate) // NEW
    if (handleMouseMove(sceneP)) {
        layoutHandles();
        return;
    }

    if (m_tool == Tool::Select) {
        QGraphicsView::mouseMoveEvent(e);
        return;
    }

    const QPointF cur = snap(sceneP);

    switch (m_tool) {
    case Tool::Line: {
        if (auto* it = qgraphicsitem_cast<QGraphicsLineItem*>(m_tempItem)) {
            it->setLine(QLineF(m_startPos, cur));
        }
        break;
    }
    case Tool::Rect: {
        if (auto* it = qgraphicsitem_cast<QGraphicsRectItem*>(m_tempItem)) {
            it->setRect(QRectF(m_startPos, cur).normalized());
        }
        break;
    }
    case Tool::Ellipse: {
        if (auto* it = qgraphicsitem_cast<QGraphicsEllipseItem*>(m_tempItem)) {
            it->setRect(QRectF(m_startPos, cur).normalized());
        }
        break;
    }
    case Tool::Polygon: {
        if (m_polyActive) {
            // live preview: last point follows cursor
            QPolygonF preview = m_poly;
            if (!preview.isEmpty()) {
                if (preview.size() == 1) preview << cur;
                else preview[preview.size()-1] = cur;
            }
            if (auto* it = qgraphicsitem_cast<QGraphicsPolygonItem*>(m_tempItem)) {
                it->setPolygon(preview);
            }
        }
        break;
    }
    default:
        break;
    }
}

void DrawingCanvas::mouseReleaseEvent(QMouseEvent* e)
{
    const QPointF sceneP = mapToScene(e->pos());

    // Handle drag (resize/rotate) // NEW
    if (handleMouseRelease(sceneP)) {
        // TODO: push a transform undo here (can add later as TransformItemCmd)
        return;
    }

    if (m_tool == Tool::Select) {
        QGraphicsView::mouseReleaseEvent(e);
        // Select → if moved, push undo
        if (m_moveTarget && m_undo) {
            QPointF endPos = m_moveTarget->pos();
            if ((endPos - m_moveStartPos).manhattanLength() > 0.5)
                pushMoveCmd(m_moveTarget, m_moveStartPos, endPos, "Move");
        }
        m_moveTarget = nullptr;
        return;
    }

    if (m_tool == Tool::Polygon) {
        // polygon is committed per-click; nothing on release
        return;
    }

    // finalize preview for single-shot tools → push Add cmd // NEW
    if (m_tempItem && m_undo) {
        QString what = (m_tool==Tool::Line) ? "Add Line" :
                       (m_tool==Tool::Rect) ? "Add Rect" :
                       (m_tool==Tool::Ellipse) ? "Add Ellipse" : "Add";
        pushAddCmd(m_tempItem, what);
    }
    m_tempItem = nullptr;
}

/* ─── Delete key → Delete with undo ───────────────────────── */ // NEW
void DrawingCanvas::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        auto items = m_scene->selectedItems();
        if (!items.isEmpty() && m_undo)
            pushDeleteCmd(items, "Delete");
        e->accept();
        return;
    }
    QGraphicsView::keyPressEvent(e);
}

/* ─── wheel zoom ─────────────────────────────────────────── */
void DrawingCanvas::wheelEvent(QWheelEvent* e)
{
    const double  factor = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    scale(factor, factor);
}

// ─── SVG export ──────────────────────────────────────────────
bool DrawingCanvas::exportSvg(const QString& filePath)
{
    if (filePath.isEmpty()) return false;

    QSvgGenerator gen;
    gen.setFileName(filePath);
    gen.setSize(QSize(1600, 1200));
    gen.setViewBox(scene()->itemsBoundingRect());

    QPainter painter(&gen);
    m_scene->render(&painter);
    return painter.isActive();
}

// ─── SVG import (as a single QGraphicsSvgItem) ──────────────
bool DrawingCanvas::importSvg(const QString& filePath)
{
    if (filePath.isEmpty()) return false;

    auto* item = new QGraphicsSvgItem(filePath);
    if (!item->renderer()->isValid()) {
        delete item;
        return false;
    }

    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    m_scene->addItem(item);

    // Fit the view around imported art
    const auto br = item->boundingRect();
    if (!br.isEmpty()) {
        m_scene->setSceneRect(br.marginsAdded(QMarginsF(50,50,50,50)));
        fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    }
    return true;
}

// ─── JSON save/load (unchanged except style) ─────────────────
QJsonDocument DrawingCanvas::saveToJson() const
{
    QJsonArray arr;

    for (auto* it : m_scene->items()) {
        if (auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(it)) {
            QJsonObject o{
                {"type","line"},
                {"x1", ln->line().x1()}, {"y1", ln->line().y1()},
                {"x2", ln->line().x2()}, {"y2", ln->line().y2()},
                {"color", colorToHex(ln->pen().color())},
                {"width", ln->pen().widthF()},
                {"layer", it->data(0).toInt()}
            };
            arr.append(o);
        } else if (auto* rc = qgraphicsitem_cast<QGraphicsRectItem*>(it)) {
            const auto r = rc->rect();
            QJsonObject o{
                {"type","rect"},
                {"x", r.x()}, {"y", r.y()},
                {"w", r.width()}, {"h", r.height()},
                {"color", colorToHex(rc->pen().color())},
                {"width", rc->pen().widthF()},
                {"fill", colorToHex(rc->brush().color())},
                {"layer", it->data(0).toInt()}
            };
            arr.append(o);
        } else if (auto* el = qgraphicsitem_cast<QGraphicsEllipseItem*>(it)) {
            const auto r = el->rect();
            QJsonObject o{
                {"type","ellipse"},
                {"x", r.x()}, {"y", r.y()},
                {"w", r.width()}, {"h", r.height()},
                {"color", colorToHex(el->pen().color())},
                {"width", el->pen().widthF()},
                {"fill", colorToHex(el->brush().color())},
                {"layer", it->data(0).toInt()}
            };
            arr.append(o);
        } else if (auto* pg = qgraphicsitem_cast<QGraphicsPolygonItem*>(it)) {
            QJsonArray pts;
            for (const auto& p : pg->polygon())
                pts.append(QJsonObject{{"x", p.x()}, {"y", p.y()}});
            QJsonObject o{
                {"type","polygon"},
                {"points", pts},
                {"color", colorToHex(pg->pen().color())},
                {"width", pg->pen().widthF()},
                {"fill", colorToHex(pg->brush().color())},
                {"layer", it->data(0).toInt()}
            };
            arr.append(o);
        }
    }

    QJsonObject root{{"items", arr}};
    return QJsonDocument(root);
}

void DrawingCanvas::loadFromJson(const QJsonDocument& doc)
{
    m_scene->clear();
    const auto root = doc.object();
    const auto arr = root.value("items").toArray();

    for (const auto& v : arr) {
        const auto o = v.toObject();
        const auto type = o.value("type").toString();

        auto mkPen  = [&](const QJsonObject& oo){
            QPen p(hexToColor(oo.value("color").toString("#ff000000")));
            p.setWidthF(oo.value("width").toDouble(1.0));
            return p;
        };
        auto mkBrush= [&](const QJsonObject& oo){
            const QString def = QColor(Qt::transparent).name(QColor::HexArgb);
            QColor fill = hexToColor(oo.value("fill").toString(def));
            return QBrush(fill);
        };
        int layer = o.value("layer").toInt(0);

        if (type == "line") {
            auto* it = m_scene->addLine(o["x1"].toDouble(), o["y1"].toDouble(),
                                        o["x2"].toDouble(), o["y2"].toDouble(),
                                        mkPen(o));
            it->setData(0, layer);
            it->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        } else if (type == "rect") {
            QRectF r(o["x"].toDouble(), o["y"].toDouble(),
                     o["w"].toDouble(), o["h"].toDouble());
            auto* it = m_scene->addRect(r, mkPen(o), mkBrush(o));
            it->setData(0, layer);
            it->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        } else if (type == "ellipse") {
            QRectF r(o["x"].toDouble(), o["y"].toDouble(),
                     o["w"].toDouble(), o["h"].toDouble());
            auto* it = m_scene->addEllipse(r, mkPen(o), mkBrush(o));
            it->setData(0, layer);
            it->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        } else if (type == "polygon") {
            QPolygonF poly;
            for (const auto& pv : o["points"].toArray()) {
                const auto po = pv.toObject();
                poly << QPointF(po["x"].toDouble(), po["y"].toDouble());
            }
            auto* it = m_scene->addPolygon(poly, mkPen(o), mkBrush(o));
            it->setData(0, layer);
            it->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        }
    }
}

/* ======================= Undo command impls ======================= */ // NEW
namespace {
    struct AddItemCmd : public QUndoCommand {
        QGraphicsScene* s; QGraphicsItem* it;
        AddItemCmd(QGraphicsScene* s_, QGraphicsItem* it_, const QString& text)
            : s(s_), it(it_) { setText(text); }
        void undo() override { s->removeItem(it); }
        void redo() override { if (!it->scene()) s->addItem(it); }
    };
    struct DeleteItemsCmd : public QUndoCommand {
        QGraphicsScene* s; QList<QGraphicsItem*> items;
        DeleteItemsCmd(QGraphicsScene* s_, QList<QGraphicsItem*> its, const QString& text)
            : s(s_), items(std::move(its)) { setText(text); }
        void undo() override { for (auto* it: items) if (!it->scene()) s->addItem(it); }
        void redo() override { for (auto* it: items) if ( it->scene()) s->removeItem(it); }
    };
    struct MoveItemCmd : public QUndoCommand {
        QGraphicsItem* it; QPointF a,b;
        MoveItemCmd(QGraphicsItem* i, QPointF from, QPointF to, const QString& text)
            : it(i), a(from), b(to) { setText(text); }
        void undo() override { it->setPos(a); }
        void redo() override { it->setPos(b); }
    };
}
void DrawingCanvas::pushAddCmd(QGraphicsItem* item, const QString& text) {
    if (m_undo) m_undo->push(new AddItemCmd(m_scene, item, text));
}
void DrawingCanvas::pushMoveCmd(QGraphicsItem* item, const QPointF& from, const QPointF& to, const QString& text) {
    if (m_undo) m_undo->push(new MoveItemCmd(item, from, to, text));
}
void DrawingCanvas::pushDeleteCmd(const QList<QGraphicsItem*>& items, const QString& text) {
    if (m_undo) m_undo->push(new DeleteItemsCmd(m_scene, items, text));
}

/* ======================= Object snap helpers ======================= */ // NEW
QVector<QPointF> DrawingCanvas::collectSnapPoints(QGraphicsItem* it) const
{
    QVector<QPointF> pts;
    if (auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(it)) {
        auto L = ln->line();
        pts << ln->mapToScene(L.p1()) << ln->mapToScene(L.p2())
            << ln->mapToScene((L.p1()+L.p2())/2.0);
    } else if (auto* rc = qgraphicsitem_cast<QGraphicsRectItem*>(it)) {
        auto r = rc->rect();
        QPolygonF v = rc->mapToScene(QPolygonF(r));
        for (const auto& p : v) pts << p;
        pts << rc->mapToScene(r.center());
    } else if (auto* el = qgraphicsitem_cast<QGraphicsEllipseItem*>(it)) {
        auto r = el->rect();
        pts << el->mapToScene(r.center());
        pts << el->mapToScene(QPointF(r.left(), r.center().y()));
        pts << el->mapToScene(QPointF(r.right(), r.center().y()));
        pts << el->mapToScene(QPointF(r.center().x(), r.top()));
        pts << el->mapToScene(QPointF(r.center().x(), r.bottom()));
    } else if (auto* pg = qgraphicsitem_cast<QGraphicsPolygonItem*>(it)) {
        auto poly = pg->polygon();
        for (const auto& p : poly) pts << pg->mapToScene(p);
        pts << pg->mapToScene(pg->boundingRect().center());
    }
    return pts;
}

void DrawingCanvas::updateSnapIndicator(const QPointF& p) const
{
    // Hide if invalid (NaN check via isNull on lines below)
    if (!m_snapIndicator && !p.isNull()) {
        auto* cross = new QGraphicsItemGroup;
        auto* h = m_scene->addLine(QLineF(p.x()-5, p.y(), p.x()+5, p.y()), QPen(Qt::red, 0));
        auto* v = m_scene->addLine(QLineF(p.x(), p.y()-5, p.x(), p.y()+5), QPen(Qt::red, 0));
        cross->addToGroup(h); cross->addToGroup(v);
        cross->setZValue(1e6);
        m_snapIndicator = cross;
    } else if (m_snapIndicator && !p.isNull()) {
        // Move children to new p — simplest is recreate:
        auto kids = m_snapIndicator->childItems();
        for (auto* k : kids) m_scene->removeItem(k), delete k;
        auto* h = m_scene->addLine(QLineF(p.x()-5, p.y(), p.x()+5, p.y()), QPen(Qt::red, 0));
        auto* v = m_scene->addLine(QLineF(p.x(), p.y()-5, p.x(), p.y()+5), QPen(Qt::red, 0));
        m_snapIndicator->addToGroup(h);
        m_snapIndicator->addToGroup(v);
        m_snapIndicator->setZValue(1e6);
    } else if (m_snapIndicator && p.isNull()) {
        m_scene->removeItem(m_snapIndicator);
        delete m_snapIndicator;
        m_snapIndicator = nullptr;
    }
}

/* =================== Resize/Rotate handles =================== */ // NEW
void DrawingCanvas::clearHandles()
{
    for (auto& h : m_handles) {
        if (h.item) { m_scene->removeItem(h.item); delete h.item; }
    }
    m_handles.clear();
    if (m_rotDot) { m_scene->removeItem(m_rotDot); delete m_rotDot; m_rotDot = nullptr; }
    m_activeHandle.reset();
    m_target = nullptr;
}

void DrawingCanvas::createHandlesForSelected()
{
    auto sel = m_scene->selectedItems();
    if (sel.size() != 1) return;
    m_target = sel.first();

    // Only basic primitives (line/rect/ellipse/polygon) for now
    if (!qgraphicsitem_cast<QGraphicsLineItem*>(m_target) &&
        !qgraphicsitem_cast<QGraphicsRectItem*>(m_target) &&
        !qgraphicsitem_cast<QGraphicsEllipseItem*>(m_target) &&
        !qgraphicsitem_cast<QGraphicsPolygonItem*>(m_target)) return;

    constexpr qreal hs = 8.0;
    auto addHandle = [&](Handle::Type t){
        auto* r = m_scene->addRect(QRectF(-hs/2, -hs/2, hs, hs), QPen(Qt::blue, 0), QBrush(Qt::white));
        r->setZValue(1e6);
        m_handles.push_back(Handle{t, r});
    };
    using T = Handle::Type;
    addHandle(T::TL); addHandle(T::TM); addHandle(T::TR);
    addHandle(T::ML);               addHandle(T::MR);
    addHandle(T::BL); addHandle(T::BM); addHandle(T::BR);

    // rotate dot above top
    m_rotDot = m_scene->addEllipse(QRectF(-hs/2,-hs/2,hs,hs), QPen(Qt::darkGreen,0), QBrush(Qt::green));
    m_rotDot->setZValue(1e6);

    layoutHandles();
}

void DrawingCanvas::layoutHandles()
{
    if (!m_target) return;
    QRectF br = m_target->sceneBoundingRect();
    m_targetCenter = br.center();

    auto posFor = [&](Handle::Type t){
        if (t == Handle::TL) return QPointF(br.left(),  br.top());
        if (t == Handle::TM) return QPointF(br.center().x(), br.top());
        if (t == Handle::TR) return QPointF(br.right(), br.top());
        if (t == Handle::ML) return QPointF(br.left(),  br.center().y());
        if (t == Handle::MR) return QPointF(br.right(), br.center().y());
        if (t == Handle::BL) return QPointF(br.left(),  br.bottom());
        if (t == Handle::BM) return QPointF(br.center().x(), br.bottom());
        if (t == Handle::BR) return QPointF(br.right(), br.bottom());
        return QPointF();
    };

    for (auto& h : m_handles) h.item->setPos(posFor(h.type));
    if (m_rotDot) {
        QPointF top = QPointF(br.center().x(), br.top() - 20.0);
        m_rotDot->setPos(top);
    }
}

bool DrawingCanvas::handleMousePress(const QPointF& scenePos, Qt::MouseButton btn)
{
    Q_UNUSED(btn);
    if (!m_target) return false;

    for (auto& h : m_handles) {
        if (h.item->sceneBoundingRect().contains(scenePos)) {
            m_activeHandle = h.type;
            m_handleStartScene = scenePos;
            m_targetCenter = m_target->sceneBoundingRect().center();
            m_targetStartRotation = m_target->rotation();

            if (auto* rc = qgraphicsitem_cast<QGraphicsRectItem*>(m_target)) {
                m_targetStartRect = rc->rect();
            } else if (auto* el = qgraphicsitem_cast<QGraphicsEllipseItem*>(m_target)) {
                m_targetStartRect = el->rect();
            } else if (auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(m_target)) {
                m_targetStartLine = ln->line();
            }
            return true;
        }
    }
    if (m_rotDot && m_rotDot->sceneBoundingRect().contains(scenePos)) {
        m_activeHandle = Handle::ROT;
        m_handleStartScene = scenePos;
        m_targetCenter = m_target->sceneBoundingRect().center();
        m_targetStartRotation = m_target->rotation();
        return true;
    }
    return false;
}

bool DrawingCanvas::handleMouseMove(const QPointF& scenePos)
{
    if (!m_activeHandle || !m_target) return false;
    auto type = *m_activeHandle;

    if (type == Handle::ROT) {
        QLineF a(m_targetCenter, m_handleStartScene);
        QLineF b(m_targetCenter, scenePos);
        qreal delta = b.angleTo(a); // CCW positive
        m_target->setTransformOriginPoint(m_target->mapFromScene(m_targetCenter));
        m_target->setRotation(m_targetStartRotation + delta);
        return true;
    }

    // local delta in item's coordinates
    QPointF localStart = m_target->mapFromScene(m_handleStartScene);
    QPointF localNow   = m_target->mapFromScene(scenePos);
    QPointF delta      = localNow - localStart;

    if (auto* rc = qgraphicsitem_cast<QGraphicsRectItem*>(m_target)) {
        QRectF r = m_targetStartRect;
        switch (type) {
        case Handle::TL: r.setTopLeft(r.topLeft()+delta); break;
        case Handle::TM: r.setTop(r.top()+delta.y());     break;
        case Handle::TR: r.setTopRight(r.topRight()+delta); break;
        case Handle::ML: r.setLeft(r.left()+delta.x());   break;
        case Handle::MR: r.setRight(r.right()+delta.x()); break;
        case Handle::BL: r.setBottomLeft(r.bottomLeft()+delta); break;
        case Handle::BM: r.setBottom(r.bottom()+delta.y()); break;
        case Handle::BR: r.setBottomRight(r.bottomRight()+delta); break;
        default: break;
        }
        rc->setRect(r.normalized());
        return true;
    }
    if (auto* el = qgraphicsitem_cast<QGraphicsEllipseItem*>(m_target)) {
        QRectF r = m_targetStartRect;
        switch (type) {
        case Handle::TL: r.setTopLeft(r.topLeft()+delta); break;
        case Handle::TM: r.setTop(r.top()+delta.y());     break;
        case Handle::TR: r.setTopRight(r.topRight()+delta); break;
        case Handle::ML: r.setLeft(r.left()+delta.x());   break;
        case Handle::MR: r.setRight(r.right()+delta.x()); break;
        case Handle::BL: r.setBottomLeft(r.bottomLeft()+delta); break;
        case Handle::BM: r.setBottom(r.bottom()+delta.y()); break;
        case Handle::BR: r.setBottomRight(r.bottomRight()+delta); break;
        default: break;
        }
        el->setRect(r.normalized());
        return true;
    }
    if (auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(m_target)) {
        QLineF L = m_targetStartLine;
        switch (type) {
        case Handle::TL: L.setP1(L.p1()+delta); break; // reuse TL for P1
        case Handle::BR: L.setP2(L.p2()+delta); break; // reuse BR for P2
        default: break;
        }
        ln->setLine(L);
        return true;
    }
    return false;
}

bool DrawingCanvas::handleMouseRelease(const QPointF& scenePos)
{
    Q_UNUSED(scenePos);
    if (!m_activeHandle) return false;
    m_activeHandle.reset();
    return true;
}
