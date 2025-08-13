#include "DrawingCanvas.h"
#include "dim/AnchorPoint.h"
#include "dim/LinearDimItem.h"

#include <QScrollBar>
#include <QKeyEvent>
#include <QApplication>
#include <QPainter>

#include <QAbstractGraphicsShapeItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsItemGroup>
#include <QGraphicsScene>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QSet>


#include <QtSvg/QSvgRenderer>
#include <QtSvg/QSvgGenerator>
#include <QtSvgWidgets/QGraphicsSvgItem>

#include <QUndoStack>
#include <QUndoCommand>

#include <algorithm>
#include <cmath>
#include <limits>

// --- Shim wrappers so legacy free-function calls still work ---
// ---- Private helper definitions for DrawingCanvas (declared in .h) ----



namespace {
    constexpr int kCornerRadiusRole = 0xDA15C0; // any unique int

    static QPainterPath makeRoundRectPath(const QRectF& r, double rad)
    {
        QPainterPath p;
        if (rad <= 0.0) {
            p.addRect(r);
        } else {
            const qreal rx = std::clamp<qreal>(rad, 0, std::min(r.width(), r.height())/2.0);
            p.addRoundedRect(r, rx, rx);
        }
        return p;
    }
    // Convert rect->path on first rounding; otherwise return the existing path item.
    // Keeps flags, z, layer data, selection, pen/brush, position/transform.
    static QGraphicsPathItem* ensureRoundedPathItem(QGraphicsScene* scene, QGraphicsItem*& it)
    {
        if (auto* pathIt = qgraphicsitem_cast<QGraphicsPathItem*>(it))
            return pathIt;

        auto* rectIt = qgraphicsitem_cast<QGraphicsRectItem*>(it);
        if (!rectIt) return nullptr;

        // capture state
        const QRectF r    = rectIt->rect();
        const QPen pen    = rectIt->pen();
        const QBrush br   = rectIt->brush();
        const auto flags  = rectIt->flags();
        const qreal z     = rectIt->zValue();
        const QVariant layerData = rectIt->data(0);
        const bool wasSelected   = rectIt->isSelected();
        const QTransform xf      = rectIt->transform();
        const QPointF pos        = rectIt->pos();
        const qreal rot          = rectIt->rotation();
        const QPointF origin     = rectIt->transformOriginPoint();

        // create path item with same geometry (no rounding yet)
        auto* path = new QGraphicsPathItem(makeRoundRectPath(r, 0.0));
        path->setPen(pen);
        path->setBrush(br);
        path->setFlags(flags);
        path->setZValue(z);
        path->setData(0, layerData);
        path->setTransformOriginPoint(origin);
        path->setTransform(xf);
        path->setPos(pos);
        path->setRotation(rot);

        // replace in the scene
        scene->addItem(path);
        scene->removeItem(rectIt);
        delete rectIt;

        it = path; // update caller's reference
        path->setSelected(wasSelected);
        return path;
    }
}


// color serialize helpers
static QString colorToHex(const QColor& c) { return c.name(QColor::HexArgb); }
static QColor  hexToColor(const QString& s){ QColor c(s); return c.isValid()? c : QColor(Qt::black); }


//--------------------------------------------------------------
// ctor
//--------------------------------------------------------------
DrawingCanvas::DrawingCanvas(QWidget* parent)
    : QGraphicsView(parent),
      m_scene(new QGraphicsScene(this))

{
    setScene(m_scene);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this]{
    clearHandles();
    createHandlesForSelected();
    });
    setRenderHint(QPainter::Antialiasing, true);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setDragMode(QGraphicsView::RubberBandDrag);
    
    m_dimStyle.pen = QPen(Qt::darkGray, 0);
    m_dimStyle.font = QFont("Menlo", 9);
    m_dimStyle.arrowSize = 8.0;
    m_dimStyle.precision = 2;
    m_dimStyle.unit = "mm";
    m_dimStyle.showUnits = true;

    setFocusPolicy(Qt::StrongFocus);
    viewport()->setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    // rulers: notify when scrolled
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]{ emit viewChanged(); });
    connect(verticalScrollBar(),   &QScrollBar::valueChanged, this, [this]{ emit viewChanged(); });
}

//--------------------------------------------------------------
// small helpers
//--------------------------------------------------------------
bool DrawingCanvas::almostEqual(const QPointF& a, const QPointF& b, double tol) {
    return QLineF(a, b).length() <= tol;
}
QPointF DrawingCanvas::snapTol(const QPointF& p, double tol) {
    auto q = [&](double v){ return std::round(v / tol) * tol; };
    return QPointF(q(p.x()), q(p.y()));
}

void DrawingCanvas::ensureLayer(int id) {
    if (!m_layers.contains(id)) m_layers.insert(id, LayerState{true, false});
}
void DrawingCanvas::applyLayerStateToItem(QGraphicsItem* it, int id) {
    ensureLayer(id);
    const auto st = m_layers.value(id);
    it->setVisible(st.visible);
    it->setFlag(QGraphicsItem::ItemIsSelectable, !st.locked);
    it->setFlag(QGraphicsItem::ItemIsMovable,    !st.locked);
    it->setOpacity(st.locked ? 0.6 : 1.0);
}

void DrawingCanvas::setCurrentLayer(int layer) {
    m_layer = layer;
    ensureLayer(layer);
}

//--------------------------------------------------------------
// apply fill preset to current selection
//--------------------------------------------------------------
void DrawingCanvas::applyFillToSelection()
{
    const QBrush br = currentBrush();
    for (QGraphicsItem* it : m_scene->selectedItems()) {
        if (auto* s = qgraphicsitem_cast<QAbstractGraphicsShapeItem*>(it)) {
            s->setBrush(br);
        }
    }
}

//--------------------------------------------------------------
// join N>=3 selected lines to polygon
//--------------------------------------------------------------
bool DrawingCanvas::joinSelectedLinesToPolygon(double tol)
{
    if (tol <= 0) tol = 1e-3;

    QList<QGraphicsLineItem*> lines;
    for (QGraphicsItem* it : m_scene->selectedItems()) {
        if (auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(it)) {
            if (QLineF(ln->line().p1(), ln->line().p2()).length() > 1e-9)
                lines << ln;
        }
    }
    if (lines.size() < 3) return false;

    struct Seg { QPointF a; QPointF b; QGraphicsLineItem* item; };
    QVector<Seg> segs; segs.reserve(lines.size());
    for (auto* ln : lines) segs.push_back({ ln->line().p1(), ln->line().p2(), ln });

    using Key = QPair<qint64, qint64>;
    auto keyOf = [&](const QPointF& p)->Key {
        return Key{ llround(p.x() / tol), llround(p.y() / tol) };
    };

    QHash<Key, QVector<int>> adj;
    QHash<Key, QPointF>      repr;

    for (int i = 0; i < segs.size(); ++i) {
        const Key ka = keyOf(segs[i].a);
        const Key kb = keyOf(segs[i].b);
        adj[ka].push_back(i);
        adj[kb].push_back(i);
        if (!repr.contains(ka)) repr.insert(ka, segs[i].a);
        if (!repr.contains(kb)) repr.insert(kb, segs[i].b);
    }
    if (adj.isEmpty()) return false;

    auto pickStart = [&]() -> Key {
        for (auto it = adj.constBegin(); it != adj.constEnd(); ++it)
            if (it.value().size() == 1) return it.key();
        return adj.constBegin().key();
    };

    Key startKey = pickStart();
    Key curKey   = startKey;

    QPolygonF ordered;
    ordered << repr.value(curKey, QPointF());

    QHash<int, bool> usedSeg;

    auto keysEqual = [&](const Key& k1, const Key& k2) { return k1 == k2; };
    auto closeEnough = [&](const QPointF& a, const QPointF& b) {
        return QLineF(a, b).length() <= tol;
    };

    for (;;) {
        const auto options = adj.value(curKey);
        int nextIdx = -1;
        Key nextKey;
        for (int si : options) {
            if (usedSeg.value(si)) continue;
            const auto& s  = segs[si];
            const Key ka   = keyOf(s.a);
            const Key kb   = keyOf(s.b);
            if (keysEqual(ka, curKey)) { nextIdx = si; nextKey = kb; break; }
            if (keysEqual(kb, curKey)) { nextIdx = si; nextKey = ka; break; }
        }
        if (nextIdx < 0) break;

        usedSeg[nextIdx] = true;
        const QPointF nextPt = repr.value(nextKey, QPointF());
        if (ordered.isEmpty() || !closeEnough(ordered.back(), nextPt))
            ordered << nextPt;

        curKey = nextKey;
        if (ordered.size() >= 4 && keysEqual(curKey, startKey))
            break;
    }

    if (ordered.size() < 4) return false;
    if (!closeEnough(ordered.front(), ordered.back())) {
        if (QLineF(ordered.front(), ordered.back()).length() <= tol)
            ordered.back() = ordered.front();
        else
            return false;
    }

    QPen   pen = lines.front()->pen();
    QBrush br  = currentBrush();

    auto* polyItem = m_scene->addPolygon(ordered, pen, br);
    polyItem->setData(0, lines.front()->data(0));
    polyItem->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);

    for (auto* ln : lines) {
        m_scene->removeItem(ln);
        delete ln;
    }

    polyItem->setSelected(true);
    return true;
}

//--------------------------------------------------------------
// tool switching
//--------------------------------------------------------------
void DrawingCanvas::setCurrentTool(Tool t)
{
    if (m_polyActive && t != Tool::Polygon) {
        m_polyActive = false;
        m_poly.clear();
        m_tempItem = nullptr;
    }
    m_tool = t;
    setDragMode(t == Tool::Select ? QGraphicsView::RubberBandDrag
                                  : QGraphicsView::NoDrag);
}

//--------------------------------------------------------------
// Snap (Shift enables object snaps)
//--------------------------------------------------------------
QPointF DrawingCanvas::snap(const QPointF& scenePos) const
{
    const double gx = std::round(scenePos.x() / m_gridSize) * m_gridSize;
    const double gy = std::round(scenePos.y() / m_gridSize) * m_gridSize;
    QPointF best(gx, gy);
    double best2 = std::numeric_limits<double>::max();

    if (!(QApplication::keyboardModifiers() & Qt::ShiftModifier))
        return best;

    const qreal px = 12.0;
    QRectF query(scenePos - QPointF(px,px), QSizeF(2*px, 2*px));
    const auto items = m_scene->items(query);

    for (auto* it : items) {
        if (!it->isVisible()) continue;
        for (const auto& s : collectSnapPoints(it)) {
            const double d  = QLineF(scenePos, s).length();
            const double d2 = d * d;
            if (d2 < best2) { best2 = d2; best = s; }
        }
    }
    if (best2 < px*px) updateSnapIndicator(best);
    else updateSnapIndicator(QPointF());
    return (best2 < px*px) ? best : QPointF(gx, gy);
}

//--------------------------------------------------------------
// background grid
//--------------------------------------------------------------
void DrawingCanvas::drawBackground(QPainter* p, const QRectF& rect)
{
    if (!m_showGrid) { QGraphicsView::drawBackground(p, rect); return; }

    const QPen gridPen(QColor(230, 230, 230));
    p->setPen(gridPen);

    const double left = std::floor(rect.left() /  m_gridSize) * m_gridSize;
    const double top  = std::floor(rect.top()  /  m_gridSize) * m_gridSize;

    for (double x = left; x < rect.right(); x += m_gridSize)
        p->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    for (double y = top; y < rect.bottom(); y += m_gridSize)
        p->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
}

//--------------------------------------------------------------
// mouse events
//--------------------------------------------------------------
void DrawingCanvas::mousePressEvent(QMouseEvent* e)
{
    if (m_spacePanning) {
        QGraphicsView::mousePressEvent(e);
        return;
    }

    const QPointF sceneP  = mapToScene(e->pos());
    const QPointF snapped = snap(sceneP);

    /* ───────────── Linear Dimension (three clicks) ───────────── */
    if (m_tool == Tool::DimLinear) {
    const QPointF snapP = snapped;

    // First click → create first anchor (parent to hit item if any)
    if (!m_dimA) {
        QGraphicsItem* hit = nullptr;
        const QRectF pick(snapP - QPointF(3,3), QSizeF(6,6));
        for (QGraphicsItem* it : m_scene->items(pick)) { hit = it; break; }

        m_dimA = new AnchorPoint(hit);
        if (!hit) {
            m_dimA->setPos(snapP);
            m_scene->addItem(m_dimA);           // only add if no parent
        }
        e->accept();
        return;
    }

    // Second click → create second anchor
    if (!m_dimB) {
        QGraphicsItem* hit = nullptr;
        const QRectF pick(snapP - QPointF(3,3), QSizeF(6,6));
        for (QGraphicsItem* it : m_scene->items(pick)) { hit = it; break; }

        m_dimB = new AnchorPoint(hit);
        if (!hit) {
            m_dimB->setPos(snapP);
            m_scene->addItem(m_dimB);           // only add if no parent
        }
        e->accept();
        return;
    }

    // Third click → compute perpendicular offset and place dimension
    const QPointF a = m_dimA->scenePos();
    const QPointF b = m_dimB->scenePos();

    const QPointF d = b - a;
    const double  len = std::hypot(d.x(), d.y());
    qreal off = 0.0;
    if (len > 1e-6) {
        const QPointF n(-d.y()/len, d.x()/len); // left normal
        const QPointF mid = (a + b) * 0.5;
        const QPointF v = snapP - mid;
        off = v.x()*n.x() + v.y()*n.y();        // signed offset
    }
    m_dimOffset = off;

    auto* dim = new LinearDimItem(a, b);
    dim->setOffset(m_dimOffset);
    dim->setStyle(m_dimStyle);
    dim->setData(0, m_layer);
    dim->setFlags(QGraphicsItem::ItemIsSelectable);
    m_scene->addItem(dim);                      // add ONCE

    // Clean up anchors: remove only if they were free anchors (no parent)
    if (m_dimA) {
        if (!m_dimA->parentItem() && m_dimA->scene() == m_scene)
            m_scene->removeItem(m_dimA);
        delete m_dimA; m_dimA = nullptr;
    }
    if (m_dimB) {
        if (!m_dimB->parentItem() && m_dimB->scene() == m_scene)
            m_scene->removeItem(m_dimB);
        delete m_dimB; m_dimB = nullptr;
    }

    e->accept();
    return;
}

    /* ───────────── Select tool (handles & move tracking) ───────────── */
    if (m_tool == Tool::Select) {
        // Try to grab a handle (resize/rotate/bend)
        if (handleMousePress(sceneP, e->button())) {
            e->accept();
            return;
        }

        // Track old positions for move-undo
        m_moveItems.clear();
        m_moveOldPos.clear();
        for (QGraphicsItem* it : m_scene->selectedItems()) {
            m_moveItems.push_back(it);
            m_moveOldPos.push_back(it->pos());
        }

        setDragMode(QGraphicsView::RubberBandDrag);
        QGraphicsView::mousePressEvent(e);

        // Selection may have changed → refresh handles
        clearHandles();
        createHandlesForSelected();
        return;
    }

    /* ───────────── Drawing tools ───────────── */
    setDragMode(QGraphicsView::NoDrag);

    switch (m_tool) {
    case Tool::Line: {
        m_startPos = snapped;
        auto* item = new QGraphicsLineItem(QLineF(snapped, snapped));
        item->setPen(currentPen());
        item->setData(0, m_layer);
        applyLayerStateToItem(item, m_layer);
        item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        m_scene->addItem(item);
        m_tempItem = item;
        break;
    }
    case Tool::Rect: {
        m_startPos = snapped;
        auto* item = new QGraphicsRectItem(QRectF(snapped, snapped));
        item->setPen(currentPen());
        item->setBrush(currentBrush());
        item->setData(0, m_layer);
        applyLayerStateToItem(item, m_layer);
        item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        m_scene->addItem(item);
        m_tempItem = item;
        break;
    }
    case Tool::Ellipse: {
        m_startPos = snapped;
        auto* item = new QGraphicsEllipseItem(QRectF(snapped, snapped));
        item->setPen(currentPen());
        item->setBrush(currentBrush());
        item->setData(0, m_layer);
        applyLayerStateToItem(item, m_layer);
        item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        m_scene->addItem(item);
        m_tempItem = item;
        break;
    }
    case Tool::Polygon: {
        if (e->button() == Qt::RightButton) {
            if (m_polyActive && m_poly.size() > 2) {
                if (auto* polyItem = qgraphicsitem_cast<QGraphicsPolygonItem*>(m_tempItem)) {
                    polyItem->setPolygon(m_poly);
                    pushAddCmd(polyItem, "Add Polygon");
                }
            }
            m_polyActive = false;
            m_poly.clear();
            m_tempItem = nullptr;
            return;
        }

        const QPointF p = snapped;
        if (!m_polyActive) {
            m_polyActive = true;
            m_poly.clear();
            m_poly << p;

            auto* item = new QGraphicsPolygonItem(m_poly);
            item->setPen(currentPen());
            item->setBrush(currentBrush());
            item->setData(0, m_layer);
            applyLayerStateToItem(item, m_layer);
            item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
            m_scene->addItem(item);
            m_tempItem = item;
        } else {
            m_poly << p;
            if (auto* polyItem = qgraphicsitem_cast<QGraphicsPolygonItem*>(m_tempItem))
                polyItem->setPolygon(m_poly);
        }
        break;
    }
    default:
        QGraphicsView::mousePressEvent(e);
        break;
    }
}



void DrawingCanvas::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF sceneP = mapToScene(e->pos());
    if (m_spacePanning) { QGraphicsView::mouseMoveEvent(e); return; }

    // handles drag
    if (handleMouseMove(sceneP)) { layoutHandles(); return; }

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
    default: break;
    }
}

void DrawingCanvas::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_spacePanning) { QGraphicsView::mouseReleaseEvent(e); return; }

    if (m_tool == Tool::Select) {
        if (handleMouseRelease(mapToScene(e->pos()))) { layoutHandles(); return; }

        QGraphicsView::mouseReleaseEvent(e);

        if (!m_moveItems.isEmpty()) {
            m_moveNewPos.clear();
            m_moveNewPos.reserve(m_moveItems.size());
            bool changed = false;
            for (int i = 0; i < m_moveItems.size(); ++i) {
                const QPointF np = m_moveItems[i]->pos();
                m_moveNewPos.push_back(np);
                if (!qFuzzyCompare(np.x(), m_moveOldPos[i].x()) ||
                    !qFuzzyCompare(np.y(), m_moveOldPos[i].y())) {
                    changed = true;
                }
            }
            if (changed && m_undo) {
                // push one command per item for now
                for (int i = 0; i < m_moveItems.size(); ++i)
                    pushMoveCmd(m_moveItems[i], m_moveOldPos[i], m_moveNewPos[i], "Move");
            }
            m_moveItems.clear(); m_moveOldPos.clear(); m_moveNewPos.clear();

            // refresh handles for new selection (if single)
            clearHandles();
            createHandlesForSelected();
            layoutHandles();
        }
        return;
    }

    if (m_tool == Tool::Polygon) return;

    if (m_tempItem) {
        pushAddCmd(m_tempItem, "Add");
    }
    m_tempItem = nullptr;
}

//--------------------------------------------------------------
// keys: space pan, delete, escape
//--------------------------------------------------------------
void DrawingCanvas::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spacePanning = true;
        setDragMode(QGraphicsView::ScrollHandDrag);
        viewport()->setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }

    if (e->key() == Qt::Key_Escape) {
        if (m_polyActive) {
            m_polyActive = false;
            m_poly.clear();
            m_tempItem = nullptr;
        }
        setCurrentTool(Tool::Select);
        e->accept();
        return;
    }

    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && !e->isAutoRepeat()) {
        const auto sel = m_scene->selectedItems();
        if (!sel.isEmpty()) pushDeleteCmd(sel, "Delete");
        e->accept();
        return;
    }
    QGraphicsView::keyPressEvent(e);
}

void DrawingCanvas::keyReleaseEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Space && m_spacePanning && !e->isAutoRepeat()) {
        m_spacePanning = false;
        setDragMode(m_tool == Tool::Select ? QGraphicsView::RubberBandDrag
                                           : QGraphicsView::NoDrag);
        viewport()->unsetCursor();
        e->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(e);
}

//--------------------------------------------------------------
// wheel zoom
//--------------------------------------------------------------
void DrawingCanvas::wheelEvent(QWheelEvent* e)
{
    const double  factor = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    scale(factor, factor);
    layoutHandles();
    emit viewChanged();
    layoutHandles();
}

//--------------------------------------------------------------
// SVG export/import
//--------------------------------------------------------------
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

bool DrawingCanvas::importSvg(const QString& filePath)
{
    if (filePath.isEmpty()) return false;

    auto* item = new QGraphicsSvgItem(filePath);
    if (!item->renderer()->isValid()) { delete item; return false; }

    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    m_scene->addItem(item);

    const auto br = item->boundingRect();
    if (!br.isEmpty()) {
        m_scene->setSceneRect(br.marginsAdded(QMarginsF(50,50,50,50)));
        fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    }
    return true;
}

//--------------------------------------------------------------
// JSON save/load
//--------------------------------------------------------------
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
                {"fillStyle", int(rc->brush().style())},
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
                {"fillStyle", int(el->brush().style())},
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
                {"fillStyle", int(pg->brush().style())},
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

    auto mkPen  = [&](const QJsonObject& oo){
        QPen p(hexToColor(oo.value("color").toString("#ff000000")));
        p.setWidthF(oo.value("width").toDouble(1.0));
        return p;
    };
    auto mkBrush = [&](const QJsonObject& oo){
        const QString def = QColor(Qt::transparent).name(QColor::HexArgb);
        QColor fill = hexToColor(oo.value("fill").toString(def));
        QBrush br(fill);
        br.setStyle(static_cast<Qt::BrushStyle>(oo.value("fillStyle").toInt(int(Qt::NoBrush))));
        return br;
    };

    for (const auto& v : arr) {
        const auto o = v.toObject();
        const auto type = o.value("type").toString();
        int layer = o.value("layer").toInt(0);

        if (type == "line") {
            auto* it = m_scene->addLine(o["x1"].toDouble(), o["y1"].toDouble(),
                                        o["x2"].toDouble(), o["y2"].toDouble(),
                                        mkPen(o));
            it->setData(0, layer);
            it->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
            applyLayerStateToItem(it, layer);
        } else if (type == "rect") {
            QRectF r(o["x"].toDouble(), o["y"].toDouble(),
                     o["w"].toDouble(), o["h"].toDouble());
            auto* it = m_scene->addRect(r, mkPen(o), mkBrush(o));
            it->setData(0, layer);
            it->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
            applyLayerStateToItem(it, layer);
        } else if (type == "ellipse") {
            QRectF r(o["x"].toDouble(), o["y"].toDouble(),
                     o["w"].toDouble(), o["h"].toDouble());
            auto* it = m_scene->addEllipse(r, mkPen(o), mkBrush(o));
            it->setData(0, layer);
            it->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
            applyLayerStateToItem(it, layer);
        } else if (type == "polygon") {
            QPolygonF poly;
            for (const auto& pv : o["points"].toArray()) {
                const auto po = pv.toObject();
                poly << QPointF(po["x"].toDouble(), po["y"].toDouble());
            }
            auto* it = m_scene->addPolygon(poly, mkPen(o), mkBrush(o));
            it->setData(0, layer);
            it->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
            applyLayerStateToItem(it, layer);
        }
    }
}

//--------------------------------------------------------------
// Undo command impls (simple in-file commands)
//--------------------------------------------------------------
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

//--------------------------------------------------------------
// snap points + indicator
//--------------------------------------------------------------
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
    if (!m_snapIndicator && !p.isNull()) {
        auto* cross = new QGraphicsItemGroup;
        auto* h = m_scene->addLine(QLineF(p.x()-5, p.y(), p.x()+5, p.y()), QPen(Qt::red, 0));
        auto* v = m_scene->addLine(QLineF(p.x(), p.y()-5, p.x(), p.y()+5), QPen(Qt::red, 0));
        cross->addToGroup(h); cross->addToGroup(v);
        cross->setZValue(1e6);
        m_snapIndicator = cross;
    } else if (m_snapIndicator && !p.isNull()) {
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

//--------------------------------------------------------------
// handles (resize/rotate)
//--------------------------------------------------------------
void DrawingCanvas::resizeEvent(QResizeEvent* e) {
    QGraphicsView::resizeEvent(e);
    emit viewChanged();
}
void DrawingCanvas::clearHandles()
{
    for (auto& h : m_handles) {
        if (h.item) {
            if (h.item->scene())
                h.item->scene()->removeItem(h.item);
            delete h.item;
            h.item = nullptr;
        }
    }
    m_handles.clear();

    if (m_rotDot) {
        if (m_rotDot->scene())
            m_rotDot->scene()->removeItem(m_rotDot);
        delete m_rotDot;
        m_rotDot = nullptr;
    }

    m_activeHandle.reset();
    m_target = nullptr;
}




void DrawingCanvas::createHandlesForSelected()
{
    clearHandles();

    const auto sel = m_scene->selectedItems();
    if (sel.size() != 1) return;
    m_target = sel.first();

    // Only basic primitives for now
    if (!qgraphicsitem_cast<QGraphicsLineItem*>(m_target) &&
        !qgraphicsitem_cast<QGraphicsRectItem*>(m_target) &&
        !qgraphicsitem_cast<QGraphicsEllipseItem*>(m_target) &&
        !qgraphicsitem_cast<QGraphicsPolygonItem*>(m_target) &&
        !qgraphicsitem_cast<RoundedRectItem*>(m_target)) {
        m_target = nullptr;
        return;
    }

    constexpr qreal hs = 8.0;
    auto addHandle = [&](Handle::Type t, const QColor& c = Qt::blue){
        auto* r = m_scene->addRect(QRectF(-hs/2, -hs/2, hs, hs),
                                   QPen(c, 0), QBrush(Qt::white));
        r->setZValue(1e6);
        m_handles.push_back(Handle{t, r});
    };

    using T = Handle::Type;

    // resize handles (8)
    addHandle(T::TL); addHandle(T::TM); addHandle(T::TR);
    addHandle(T::ML);                 addHandle(T::MR);
    addHandle(T::BL); addHandle(T::BM); addHandle(T::BR);

    // rotate dot
    m_rotDot = m_scene->addEllipse(QRectF(-hs/2,-hs/2,hs,hs),
                                   QPen(Qt::darkGreen,0), QBrush(Qt::green));
    m_rotDot->setZValue(1e6);

    // bend handle for lines
    if (qgraphicsitem_cast<QGraphicsLineItem*>(m_target)) {
        // we reuse TL for P1 and BR for P2 in your code; bending uses a mid handle:
        addHandle(T::BEND, QColor(160, 0, 160));
    }

    // NEW: corner-radius handles for rects or rounded-rects
    if (qgraphicsitem_cast<QGraphicsRectItem*>(m_target) ||
        qgraphicsitem_cast<RoundedRectItem*>(m_target)) {
        addHandle(T::RAD_TL, Qt::red);
        addHandle(T::RAD_TR, Qt::red);
        addHandle(T::RAD_BR, Qt::red);
        addHandle(T::RAD_BL, Qt::red);
    }

    layoutHandles();
}



void DrawingCanvas::layoutHandles()
{
    if (!m_target) return;

    const QRectF br = m_target->sceneBoundingRect();
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

    // convenience for radius-handle positions (a bit inside from each corner)
    auto radPos = [&](Handle::Type t){
        // Work in scene coordinates using a small inset from the corner,
        // or if it's RoundedRectItem, place on the arc's 45deg point.
        const qreal inset = qMin(br.width(), br.height()) * 0.12; // visual distance
        if (auto* rr = qgraphicsitem_cast<RoundedRectItem*>(m_target)) {
            const qreal rx = rr->rx();
            const qreal ry = rr->ry();
            const qreal dx = (rx > 0 ? rx : inset);
            const qreal dy = (ry > 0 ? ry : inset);
            if (t == Handle::RAD_TL) return QPointF(br.left()  + dx, br.top()    + dy);
            if (t == Handle::RAD_TR) return QPointF(br.right() - dx, br.top()    + dy);
            if (t == Handle::RAD_BR) return QPointF(br.right() - dx, br.bottom() - dy);
            if (t == Handle::RAD_BL) return QPointF(br.left()  + dx, br.bottom() - dy);
        } else {
            if (t == Handle::RAD_TL) return QPointF(br.left()  + inset, br.top()    + inset);
            if (t == Handle::RAD_TR) return QPointF(br.right() - inset, br.top()    + inset);
            if (t == Handle::RAD_BR) return QPointF(br.right() - inset, br.bottom() - inset);
            if (t == Handle::RAD_BL) return QPointF(br.left()  + inset, br.bottom() - inset);
        }
        return QPointF();
    };

    for (auto& h : m_handles) {
        switch (h.type) {
        case Handle::TL: case Handle::TM: case Handle::TR:
        case Handle::ML: case Handle::MR:
        case Handle::BL: case Handle::BM: case Handle::BR:
            h.item->setPos(posFor(h.type));
            break;
        case Handle::RAD_TL: case Handle::RAD_TR:
        case Handle::RAD_BR: case Handle::RAD_BL:
            h.item->setBrush(QBrush(Qt::white));
            h.item->setPen(QPen(Qt::red, 0));
            h.item->setPos(radPos(h.type));
            break;
        case Handle::BEND:
            h.item->setBrush(QBrush(QColor(250,240,255)));
            h.item->setPen(QPen(QColor(160,0,160), 0));
            h.item->setPos(br.center());
            break;
        default: break;
        }
    }

    if (m_rotDot) {
        QPointF top = QPointF(br.center().x(), br.top() - 20.0);
        m_rotDot->setPos(top);
    }
}





static QPen previewPen(const QPen& base)
{
    QPen p = base;
    QColor c = p.color(); c.setAlpha(180);
    p.setColor(c);
    p.setStyle(Qt::DashLine);
    return p;
}



bool DrawingCanvas::handleMousePress(const QPointF& scenePos, Qt::MouseButton btn)
{
    Q_UNUSED(btn);
    if (!m_target) return false;

    // hit-test handles
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
            } else if (auto* rr = qgraphicsitem_cast<RoundedRectItem*>(m_target)) {
                m_targetStartRect = rr->rect();
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
    Handle::Type type = *m_activeHandle;

    // Rotation
    if (type == Handle::ROT) {
        QLineF a(m_targetCenter, m_handleStartScene);
        QLineF b(m_targetCenter, scenePos);
        qreal delta = b.angleTo(a); // CCW positive
        m_target->setTransformOriginPoint(m_target->mapFromScene(m_targetCenter));
        m_target->setRotation(m_targetStartRotation + delta);
        layoutHandles();
        return true;
    }

    // Local motion
    QPointF localStart = m_target->mapFromScene(m_handleStartScene);
    QPointF localNow   = m_target->mapFromScene(scenePos);
    QPointF delta      = localNow - localStart;

    // --- Corner radius editing (NEW) ---
    auto applyRadiusDrag = [&](Handle::Type cornerType){
        // Ensure we are operating on a RoundedRectItem.
        RoundedRectItem* rr = qgraphicsitem_cast<RoundedRectItem*>(m_target);
        if (!rr) {
            if (auto* rc = qgraphicsitem_cast<QGraphicsRectItem*>(m_target)) {
                // upgrade rect -> rounded rect, preserving style and transforms
                QRectF r = rc->rect();
                auto* newItem = new RoundedRectItem(r, 0, 0);
                newItem->setPen(rc->pen());
                newItem->setBrush(rc->brush());
                newItem->setPos(rc->pos());
                newItem->setRotation(rc->rotation());
                newItem->setScale(rc->scale());
                newItem->setTransform(rc->transform());
                newItem->setData(0, rc->data(0));
                newItem->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
                m_scene->addItem(newItem);
                newItem->setSelected(true);
                m_scene->removeItem(rc);
                delete rc;
                m_target = rr = newItem;
                // reset start rect reference
                m_targetStartRect = rr->rect();
            } else {
                return; // not a rect-like item
            }
        }

        const QRectF r0 = m_targetStartRect.normalized();
        // choose which corner we’re dragging
        QPointF corner;
        int sx = 1, sy = 1; // signs to measure dx,dy
        switch (cornerType) {
            case Handle::RAD_TL: corner = r0.topLeft();     sx = +1; sy = +1; break;
            case Handle::RAD_TR: corner = r0.topRight();    sx = -1; sy = +1; break;
            case Handle::RAD_BR: corner = r0.bottomRight(); sx = -1; sy = -1; break;
            case Handle::RAD_BL: corner = r0.bottomLeft();  sx = +1; sy = -1; break;
            default: return;
        }

        // Drag vector from corner
        QPointF localCorner = corner; // already in local coords
        QPointF v = localNow - localCorner;

        qreal rx = qMax<qreal>(0.0, sx * v.x());
        qreal ry = qMax<qreal>(0.0, sy * v.y());
        // Keep arcs “round-ish”: use the smaller of rx,ry for both (or keep independent if you want)
        qreal rad = qMin(rx, ry);

        // Clamp to half width/height
        rad = qMin(rad, r0.width()*0.5);
        rad = qMin(rad, r0.height()*0.5);

        rr->setRect(r0);           // keep rect
        rr->setRadius(rad, rad);   // set radius
        layoutHandles();
    };

    // Dispatch for radius handles
    if (type == Handle::RAD_TL || type == Handle::RAD_TR ||
        type == Handle::RAD_BR || type == Handle::RAD_BL) {
        applyRadiusDrag(type);
        return true;
    }

    // --- Resize rect/ellipse/line (existing behavior) ---
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
        layoutHandles();
        return true;
    }
    if (auto* rr = qgraphicsitem_cast<RoundedRectItem*>(m_target)) {
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
        r = r.normalized();
        rr->setRect(r);
        // Keep radius clamped to the new size
        rr->setRadius(rr->rx(), rr->ry());
        layoutHandles();
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
        layoutHandles();
        return true;
    }
    if (auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(m_target)) {
        QLineF L = m_targetStartLine;
        switch (type) {
        case Handle::TL: L.setP1(L.p1()+delta); break; // reuse TL for P1
        case Handle::BR: L.setP2(L.p2()+delta); break; // reuse BR for P2
        case Handle::BEND: {
            // (your existing bend-preview logic here if any)
            break;
        }
        default: break;
        }
        ln->setLine(L);
        layoutHandles();
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


//--------------------------------------------------------------
// Layers API
//--------------------------------------------------------------
void DrawingCanvas::setLayerVisibility(int layerId, bool visible) {
    ensureLayer(layerId);
    m_layers[layerId].visible = visible;

    for (QGraphicsItem* it : m_scene->items()) {
        if (it->data(0).toInt() == layerId) it->setVisible(visible);
    }
    viewport()->update();
}

void DrawingCanvas::setLayerLocked(int layerId, bool locked) {
    ensureLayer(layerId);
    m_layers[layerId].locked = locked;

    for (QGraphicsItem* it : m_scene->items()) {
        if (it->data(0).toInt() == layerId) {
            it->setFlag(QGraphicsItem::ItemIsSelectable, !locked);
            it->setFlag(QGraphicsItem::ItemIsMovable,    !locked);
            it->setOpacity(locked ? 0.6 : 1.0);
        }
    }
    viewport()->update();
}

void DrawingCanvas::moveItemsToLayer(int fromLayer, int toLayer) {
    ensureLayer(toLayer);
    for (QGraphicsItem* it : m_scene->items()) {
        if (it->data(0).toInt() == fromLayer) {
            it->setData(0, toLayer);
            applyLayerStateToItem(it, toLayer);
        }
    }
}

// ===================== Rounded corners / Fillet =====================
static QPainterPath makeRoundedPolygonPath(const QPolygonF& poly, double r)
{
    // Clamp + trivial
    if (poly.size() < 3 || r <= 0.0) {
        QPainterPath p; p.addPolygon(poly); p.closeSubpath(); return p;
    }

    const bool closed = (poly.first() == poly.last());
    // Build a working list of points (closed for math)
    QVector<QPointF> pts = poly.toVector();
    if (!closed) pts.push_back(poly.first()); // treat open as closed ring for turns

    QPainterPath path;
    auto N = pts.size();
    auto prevIdx = [&](int i){ return (i-1+N) % N; };
    auto nextIdx = [&](int i){ return (i+1) % N; };

    // Start from first corner; we’ll build with lineTo + quadTo(pivot, exit)
    // Use first corner inset as initial moveTo
    auto cornerPoint = [&](int i){ return pts[i]; };

    // Helper: inset point on edge (A->B) by distance d from corner at A
    auto inset = [](const QPointF& A, const QPointF& B, double d)->QPointF {
        QLineF L(A,B);
        if (L.length() < 1e-9) return A;
        L.setLength(d);
        return L.p2();
    };

    // Compute per-edge inset distances so we don’t overshoot short edges
    QVector<double> maxInset(N, r);
    for (int i = 0; i < N; ++i) {
        const QPointF P = pts[prevIdx(i)];
        const QPointF C = pts[i];
        const QPointF Nn= pts[nextIdx(i)];
        const double a = QLineF(P, C).length();
        const double b = QLineF(C, Nn).length();
        const double lim = 0.5 * std::min(a, b);
        maxInset[i] = std::min(r, lim);
    }

    // First corner moveTo:
    {
        const int i = 0;
        QPointF C = cornerPoint(i);
        QPointF P = cornerPoint(prevIdx(i));
        QPointF enter = inset(C, P, maxInset[i]); // from C towards P
        // start at enter point of first corner (close ring will connect)
        path.moveTo(enter);
    }

    // For each corner, create: lineTo(enter), quadTo(corner, exit)
    for (int i = 0; i < N; ++i) {
        const QPointF P = cornerPoint(prevIdx(i));
        const QPointF C = cornerPoint(i);
        const QPointF Nn= cornerPoint(nextIdx(i));

        const double d = maxInset[i];

        QPointF enter = inset(C, P, d); // along CP
        QPointF exit  = inset(C, Nn, d); // along CN

        path.lineTo(enter);
        path.quadTo(C, exit);
    }
    path.closeSubpath();
    return path;
}

bool DrawingCanvas::roundSelectedShape(double radius)
{
    if (radius <= 0.0) return false;

    const auto sel = m_scene->selectedItems();
    if (sel.size() != 1) return false;

    QGraphicsItem* it = sel.first();

    // RECT: replace with rounded rect path
    if (auto* rc = qgraphicsitem_cast<QGraphicsRectItem*>(it)) {
        const QRectF r = rc->rect();
        QPainterPath path;
        // clamp radius to rect size
        const qreal rad = std::min<qreal>(radius, std::min(r.width(), r.height())/2.0);
        path.addRoundedRect(r, rad, rad);

        auto* pi = new QGraphicsPathItem(path);
        pi->setPen(rc->pen());
        pi->setBrush(rc->brush());
        pi->setData(0, rc->data(0));
        pi->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        pi->setPos(rc->pos());
        pi->setRotation(rc->rotation());
        pi->setTransformOriginPoint(rc->transformOriginPoint());

        m_scene->addItem(pi);
        m_scene->removeItem(rc);
        delete rc;
        pi->setSelected(true);
        return true;
    }

    // POLYGON: build rounded polygon painter path
    if (auto* pg = qgraphicsitem_cast<QGraphicsPolygonItem*>(it)) {
        const QPolygonF poly = pg->polygon();
        QPainterPath path = makeRoundedPolygonPath(poly, radius);

        auto* pi = new QGraphicsPathItem(path);
        pi->setPen(pg->pen());
        pi->setBrush(pg->brush());
        pi->setData(0, pg->data(0));
        pi->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        pi->setPos(pg->pos());
        pi->setRotation(pg->rotation());
        pi->setTransformOriginPoint(pg->transformOriginPoint());

        m_scene->addItem(pi);
        m_scene->removeItem(pg);
        delete pg;
        pi->setSelected(true);
        return true;
    }

    // Lines/Ellipses/Paths not handled here
    return false;
}

// ======================= Bend line into arc =========================
bool DrawingCanvas::bendSelectedLine(double sagitta)
{
    // Sagitta can be positive/negative (which side of the line to bulge)
    const auto sel = m_scene->selectedItems();
    if (sel.size() != 1) return false;

    auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(sel.first());
    if (!ln) return false;

    const QLineF L = ln->line();
    if (L.length() < 1e-6) return false;

    const QPointF A = L.p1();
    const QPointF B = L.p2();

    // Midpoint
    const QPointF M = (A + B) * 0.5;

    // Unit normal (left-hand normal)
    QLineF dir(A, B);
    dir.setLength(1.0);
    // normal = rotate dir by +90deg
    QPointF n(-dir.dy(), dir.dx());

    // Control point = midpoint offset by sagitta along normal
    const QPointF C = M + n * sagitta;

    QPainterPath path;
    path.moveTo(A);
    path.quadTo(C, B);

    auto* pi = new QGraphicsPathItem(path);
    pi->setPen(ln->pen());
    pi->setBrush(Qt::NoBrush);
    pi->setData(0, ln->data(0));
    pi->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    pi->setPos(ln->pos());
    pi->setRotation(ln->rotation());
    pi->setTransformOriginPoint(ln->transformOriginPoint());

    m_scene->addItem(pi);
    m_scene->removeItem(ln);
    delete ln;

    pi->setSelected(true);
    return true;
}

void DrawingCanvas::setSelectedCornerRadius(double r)
{
    // Normalize negative inputs
    if (r < 0) r = 0;

    auto sel = m_scene->selectedItems();
    if (sel.isEmpty()) return;

    for (QGraphicsItem* it : sel) {
        // Get or convert to a path item
        QGraphicsPathItem* pathIt = ensureRoundedPathItem(m_scene, it);
        if (!pathIt) continue;

        // Determine rect to round:
        // If it used to be a rect we kept that rect in the path; otherwise derive from boundingRect.
        QRectF rLocal;
        // Try to recover a “base rect” from existing path if it’s a simple (rounded) rect
        // Fallback to local bounding rect
        rLocal = pathIt->path().boundingRect();

        // Build new path with desired radius
        QPainterPath newPath = makeRoundRectPath(rLocal, r);

        // Apply pen/brush unchanged; update path only
        pathIt->setPath(newPath);

        // Persist radius on the item so handles (or re-edits) can read/update it again
        pathIt->setData(kCornerRadiusRole, r);
    }

    // Re-create & place handles after geometry change so the radius knobs/handles stay in the right spot
    refreshHandles();

    // Repaint rulers if you have them
    emit viewChanged();
}

void DrawingCanvas::refreshHandles()
{
    clearHandles();
    createHandlesForSelected();
    layoutHandles();
    viewport()->update();
}


#include <QtMath>

static double angleDeg(const QLineF& L) {
    // range [0..180)
    double a = std::fmod(180.0 - L.angle(), 180.0);
    if (a < 0) a += 180.0;
    return a;
}
static bool near(double v, double target, double eps) {
    return std::abs(v - target) <= eps;
}
static QPointF average(const QVector<QPointF>& pts) {
    if (pts.isEmpty()) return {};
    qreal sx=0, sy=0;
    for (auto& p: pts){ sx+=p.x(); sy+=p.y(); }
    return QPointF(sx/pts.size(), sy/pts.size());
}


// ---- helpers (place near your other file-scope helpers) ----------------

QPointF DrawingCanvas::projectPointOnSegment(const QPointF& p,
                                             const QPointF& a,
                                             const QPointF& b,
                                             double* tOut)
{
    const double vx = b.x() - a.x();
    const double vy = b.y() - a.y();
    const double vv = vx*vx + vy*vy;

    double t = 0.0;
    if (vv > 1e-9) {
        t = ((p.x() - a.x())*vx + (p.y() - a.y())*vy) / vv;
        if (t < 0.0) t = 0.0;
        else if (t > 1.0) t = 1.0;
    }
    if (tOut) *tOut = t;

    return QPointF(a.x() + t*vx, a.y() + t*vy);
}
// Local helpers for geometry – do NOT use the class' private functions here.
namespace {
    inline double sqr(double v) { return v * v; }

    inline double dist2(const QPointF& a, const QPointF& b) {
        const double dx = a.x() - b.x();
        const double dy = a.y() - b.y();
        return dx*dx + dy*dy;
    }

    inline double segLen2(const QLineF& L) {
        return dist2(L.p1(), L.p2());
    }
    inline double length2(const QLineF& L){ return dist2(L.p1(), L.p2()); }

// signed angle difference in degrees in [0,90]
inline double angleDiffDeg(const QLineF& A, const QLineF& B){
    const double a1 = std::atan2(A.dy(), A.dx());
    const double a2 = std::atan2(B.dy(), B.dx());
    double d = std::fabs(a1 - a2);
    d = std::min(d, M_PI - d);
    return d * 180.0 / M_PI;
}

struct OverlapMerge {
    QLineF merged;
    bool   ok = false;
};

// Decide if A and B are near duplicates (parallel & overlapping) and produce an averaged line.
// tolPx      : max perpendicular distance between the two lines
// coverFrac  : required overlap as fraction of the shorter projected length
// axisSnapDeg: max angle difference allowed (deg)
inline OverlapMerge overlappedMostly(const QLineF& A, const QLineF& B,
                                     double tolPx, double coverFrac, double axisSnapDeg)
{
    OverlapMerge out;

    // 1) roughly parallel?
    if (angleDiffDeg(A, B) > axisSnapDeg) return out;

    // Use dominant orientation
    const bool horiz = std::fabs(A.dy()) < std::fabs(A.dx());

    // 2) perpendicular separation (average constant coord)
    auto avgY = [](const QLineF& L){ return 0.5*(L.y1()+L.y2()); };
    auto avgX = [](const QLineF& L){ return 0.5*(L.x1()+L.x2()); };

    if (horiz) {
        const double yA = avgY(A), yB = avgY(B);
        if (std::fabs(yA - yB) > tolPx) return out;

        // Project to X and test overlap
        const double ax1 = std::min(A.x1(), A.x2());
        const double ax2 = std::max(A.x1(), A.x2());
        const double bx1 = std::min(B.x1(), B.x2());
        const double bx2 = std::max(B.x1(), B.x2());

        const double overlap = std::max(0.0, std::min(ax2, bx2) - std::max(ax1, bx1));
        const double shortLen = std::min(ax2-ax1, bx2-bx1);
        if (shortLen <= 1e-6) return out;
        if (overlap < coverFrac * shortLen) return out;

        const double y = 0.5*(yA + yB);
        const double x1 = std::min(ax1, bx1);
        const double x2 = std::max(ax2, bx2);
        out.merged = QLineF(QPointF(x1, y), QPointF(x2, y));
        out.ok = true;
        return out;
    } else {
        const double xA = avgX(A), xB = avgX(B);
        if (std::fabs(xA - xB) > tolPx) return out;

        // Project to Y and test overlap
        const double ay1 = std::min(A.y1(), A.y2());
        const double ay2 = std::max(A.y1(), A.y2());
        const double by1 = std::min(B.y1(), B.y2());
        const double by2 = std::max(B.y1(), B.y2());

        const double overlap = std::max(0.0, std::min(ay2, by2) - std::max(ay1, by1));
        const double shortLen = std::min(ay2-ay1, by2-by1);
        if (shortLen <= 1e-6) return out;
        if (overlap < coverFrac * shortLen) return out;

        const double x = 0.5*(xA + xB);
        const double y1 = std::min(ay1, by1);
        const double y2 = std::max(ay2, by2);
        out.merged = QLineF(QPointF(x, y1), QPointF(x, y2));
        out.ok = true;
        return out;
    }
}
static bool computeMerged(const QLineF& A, const QLineF& B,
                          double tolPx, double coverFrac, double axisSnapDeg,
                          QLineF& out)
{
    if (angleDiffDeg(A, B) > axisSnapDeg) return false;

    const bool horiz = std::fabs(A.dy()) < std::fabs(A.dx());

    if (horiz) {
        const double yA = 0.5*(A.y1()+A.y2());
        const double yB = 0.5*(B.y1()+B.y2());
        if (std::fabs(yA - yB) > tolPx) return false;

        const double ax1 = std::min(A.x1(), A.x2());
        const double ax2 = std::max(A.x1(), A.x2());
        const double bx1 = std::min(B.x1(), B.x2());
        const double bx2 = std::max(B.x1(), B.x2());

        const double overlap = std::max(0.0, std::min(ax2, bx2) - std::max(ax1, bx1));
        const double shortLen = std::min(ax2 - ax1, bx2 - bx1);
        if (shortLen <= 1e-6 || overlap < coverFrac * shortLen) return false;

        const double y = 0.5*(yA + yB);
        out = QLineF(QPointF(std::min(ax1, bx1), y), QPointF(std::max(ax2, bx2), y));
        return true;
    } else {
        const double xA = 0.5*(A.x1()+A.x2());
        const double xB = 0.5*(B.x1()+B.x2());
        if (std::fabs(xA - xB) > tolPx) return false;

        const double ay1 = std::min(A.y1(), A.y2());
        const double ay2 = std::max(A.y1(), A.y2());
        const double by1 = std::min(B.y1(), B.y2());
        const double by2 = std::max(B.y1(), B.y2());

        const double overlap = std::max(0.0, std::min(ay2, by2) - std::max(ay1, by1));
        const double shortLen = std::min(ay2 - ay1, by2 - by1);
        if (shortLen <= 1e-6 || overlap < coverFrac * shortLen) return false;

        const double x = 0.5*(xA + xB);
        out = QLineF(QPointF(x, std::min(ay1, by1)), QPointF(x, std::max(ay2, by2)));
        return true;
    }
}

}


static double angleBetweenDeg(const QLineF& A, const QLineF& B){
    // acute angle between directions, in degrees
    double a1 = std::atan2(A.dy(), A.dx());
    double a2 = std::atan2(B.dy(), B.dx());
    double d = std::fabs(a1 - a2);
    if (d > M_PI) d = 2*M_PI - d;
    return d * 180.0 / M_PI;
}
static bool intervalsOverlap1D(double a1, double a2, double b1, double b2, double tol){
    if (a1 > a2) std::swap(a1, a2);
    if (b1 > b2) std::swap(b1, b2);
    return !(a2 < b1 - tol || b2 < a1 - tol);
}
static bool nearlyCollinear(const QLineF& A, const QLineF& B, double degTol){
    return angleBetweenDeg(A, B) <= degTol;
}
static bool nearLineDuplicate(const QLineF& A, const QLineF& B, double tolPx){
    // same (or reversed) line within tol, used to clean duplicates
    auto close = [&](const QPointF& u, const QPointF& v){ return dist2(u,v) <= sqr(tolPx); };
    return (close(A.p1(), B.p1()) && close(A.p2(), B.p2())) ||
           (close(A.p1(), B.p2()) && close(A.p2(), B.p1()));
}

static int removeDuplicateSegments(QGraphicsScene* scene, double tolPx)
{
    auto items = scene->items(); // Z-order doesn’t matter for us
    // Collect all lines with their pointers for safe deletion later
    struct LRef { QGraphicsLineItem* it; QLineF L; };
    QVector<LRef> lines; lines.reserve(items.size());
    for (QGraphicsItem* it : items) {
        if (auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(it)) {
            lines.push_back({ln, ln->line()});
        }
    }

    int removed = 0;
    QVector<bool> dead(lines.size(), false);

    for (int i = 0; i < lines.size(); ++i) {
        if (dead[i]) continue;
        for (int j = i + 1; j < lines.size(); ++j) {
            if (dead[j]) continue;
            if (nearLineDuplicate(lines[i].L, lines[j].L, tolPx)) {
                // Prefer keeping the thicker/visible one; simple rule: keep i, drop j
                scene->removeItem(lines[j].it);
                delete lines[j].it;
                dead[j] = true;
                ++removed;
            }
        }
    }
    return removed;
}

int DrawingCanvas::refineVector() {
    return refineVector(RefineParams{}); // defaults
}


// ---- main pass ----------------------------------------------------------
int DrawingCanvas::refineVector(const RefineParams& p)
{
    int fixes = 0;

    // 1) collect line items
    QList<QGraphicsLineItem*> lines;
    lines.reserve(m_scene->items().size());
    for (QGraphicsItem* it : m_scene->items()) {
        if (auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(it))
            lines << ln;
    }
    if (lines.isEmpty()) return 0;

    const double gap2    = p.gapPx    * p.gapPx;
    const double merge2  = p.mergePx  * p.mergePx;
    const double minLen2 = p.minLenPx * p.minLenPx;
    const double extend2 = p.extendPx * p.extendPx;

    // 2) Snap to axis (0°/90°) — only for long lines and small angular dev
    for (auto* ln : lines) {
        QLineF L = ln->line();
        if (segLen2(L) < sqr(p.axisSnapMinLen)) continue;

        const double ang = angleDeg(L); // [0,180)
        const double dev0  = std::fabs(ang - 0.0);
        const double dev90 = std::fabs(ang - 90.0);
        if (std::min(dev0, dev90) <= p.axisSnapDeg) {
            if (dev90 < dev0) {
                // vertical: lock x to mid x
                const double x = 0.5 * (L.x1() + L.x2());
                L.setP1(QPointF(x, L.y1()));
                L.setP2(QPointF(x, L.y2()));
            } else {
                // horizontal: lock y to mid y
                const double y = 0.5 * (L.y1() + L.y2());
                L.setP1(QPointF(L.x1(), y));
                L.setP2(QPointF(L.x2(), y));
            }
            ln->setLine(L);
            ++fixes;
        }
    }

    // 3) delete tiny segments
    for (auto*& ln : lines) {
        if (!ln) continue;
        if (dist2(ln->line().p1(), ln->line().p2()) < minLen2) {
            m_scene->removeItem(ln); delete ln; ln = nullptr; ++fixes;
        }
    }
    lines.erase(std::remove(lines.begin(), lines.end(), nullptr), lines.end());
    if (lines.isEmpty()) return fixes;

    // Helper to prefer moving the shorter segment
    auto segLength = [](const QLineF& L){ return std::sqrt(segLen2(L)); };

    // 4) close small endpoint gaps (bias: move the shorter one)
    struct End {
        QGraphicsLineItem* ln; bool p1;
        QPointF pt() const { return p1 ? ln->line().p1() : ln->line().p2(); }
        void set(const QPointF& p) {
            QLineF L = ln->line();
            if (p1) L.setP1(p); else L.setP2(p);
            ln->setLine(L);
        }
    };
    QVector<End> ends; ends.reserve(lines.size()*2);
    for (auto* ln : lines) { ends.push_back({ln,true}); ends.push_back({ln,false}); }

    QVector<bool> used(ends.size(), false);
    for (int i=0;i<ends.size();++i) if (!used[i]) {
        int best = -1; double best2 = gap2;
        for (int j=i+1;j<ends.size();++j) if (!used[j]) {
            double d2 = dist2(ends[i].pt(), ends[j].pt());
            if (d2 < best2) { best2 = d2; best = j; }
        }
        if (best >= 0) {
            // choose which endpoint to move: move endpoint of the **shorter segment** to the longer one
            QGraphicsLineItem* A = ends[i].ln;
            QGraphicsLineItem* B = ends[best].ln;
            const double lenA = segLength(A->line());
            const double lenB = segLength(B->line());
            if (lenA < lenB) {
                ends[i].set(ends[best].pt());
            } else {
                ends[best].set(ends[i].pt());
            }
            used[i]=used[best]=true; ++fixes;
        }
    }

    // 5) merge collinear segments that *share an endpoint* and overlap on 1D axis
    const double dirTolDeg = p.axisSnapDeg; // keep same small tolerance
    for (int i=0;i<lines.size();++i) for (int j=i+1;j<lines.size();++j) {
        auto* A = lines[i]; auto* B = lines[j];
        if (!A || !B) continue;
        QLineF La = A->line(), Lb = B->line();

        // Must share an endpoint
        bool share =
            dist2(La.p1(), Lb.p1()) <= merge2 || dist2(La.p1(), Lb.p2()) <= merge2 ||
            dist2(La.p2(), Lb.p1()) <= merge2 || dist2(La.p2(), Lb.p2()) <= merge2;
        if (!share || !nearlyCollinear(La, Lb, dirTolDeg)) continue;

        // Check 1D overlap along dominant axis
        const bool horiz = std::fabs(La.dy()) < std::fabs(La.dx());
        const double a1 = horiz ? La.x1() : La.y1();
        const double a2 = horiz ? La.x2() : La.y2();
        const double b1 = horiz ? Lb.x1() : Lb.y1();
        const double b2 = horiz ? Lb.x2() : Lb.y2();
        if (!intervalsOverlap1D(a1, a2, b1, b2, p.collinearOverlapPx))
            continue;

        // Merge to extremes
        QVector<QPointF> pts{La.p1(), La.p2(), Lb.p1(), Lb.p2()};
        std::sort(pts.begin(), pts.end(), [&](const QPointF& u, const QPointF& v){
            return horiz ? (u.x() < v.x()) : (u.y() < v.y());
        });
        A->setLine(QLineF(pts.front(), pts.back()));
        m_scene->removeItem(B); delete B; lines[j] = nullptr; ++fixes;
    }
    lines.erase(std::remove(lines.begin(), lines.end(), nullptr), lines.end());
    if (lines.isEmpty()) return fixes;

    // 6) extend endpoints to meet nearby segments (prefer near-perpendicular T)
    for (auto* ln : lines) {
        QLineF L = ln->line();
        for (auto* other : lines) {
            if (ln == other) continue;
            QLineF M = other->line();

            // angle gate: close to 90° (T) OR very close to 0° (collinear)
            const double ab = angleBetweenDeg(L, M);
            const bool okAngle =
                std::fabs(ab - 90.0) <= p.extendAngleDeg || std::fabs(ab - 0.0) <= p.axisSnapDeg;
            if (!okAngle) continue;

            for (int k=0;k<2;++k) {
                QPointF P = (k==0 ? L.p1() : L.p2());
                double t=0;
                QPointF Q = projectPointOnSegment(P, M.p1(), M.p2(), &t);
                if (t > 0.0 && t < 1.0 && dist2(P, Q) <= extend2) {
                    if (k==0) L.setP1(Q); else L.setP2(Q);
                    ++fixes;
                }
            }
        }
        ln->setLine(L);
    }

    // 7) drop near-duplicates (rare but can appear after merges/extends)
    for (int i=0;i<lines.size();++i) for (int j=i+1;j<lines.size();++j) {
        if (!lines[i] || !lines[j]) continue;
        if (nearLineDuplicate(lines[i]->line(), lines[j]->line(), /*tolPx*/ 1.0)) {
            m_scene->removeItem(lines[j]); delete lines[j]; lines[j] = nullptr; ++fixes;
        }
    }

    scene()->update();
    viewport()->update();
    return fixes;
}


int DrawingCanvas::refineOverlapsLight(double tolPx, double coverage, double axisSnapDeg)
{
    // 1) Snapshot all line items (no scene mutation while we inspect).
    struct Rec { QGraphicsLineItem* it; QLineF L; };
    QVector<Rec> recs;
    recs.reserve(scene()->items().size());
    for (QGraphicsItem* it : scene()->items()) {
        if (auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(it)) {
            recs.push_back({ ln, ln->line() });
        }
    }
    if (recs.size() < 2) return 0;

    QVector<bool> alive(recs.size(), true);

    // For each pair, if they are near-duplicates:
    //   - choose a survivor (longer segment),
    //   - update survivor geometry to the union/average,
    //   - mark the other for deletion (but don't delete yet).
    QSet<QGraphicsLineItem*> toDelete;

    for (int i = 0; i < recs.size(); ++i) {
        if (!alive[i] || !recs[i].it) continue;

        for (int j = i + 1; j < recs.size(); ++j) {
            if (!alive[j] || !recs[j].it) continue;

            QLineF merged;
            if (!computeMerged(recs[i].L, recs[j].L, tolPx, coverage, axisSnapDeg, merged))
                continue;

            // keep longer one
            const double li2 = length2(recs[i].L);
            const double lj2 = length2(recs[j].L);
            const int keep = (li2 >= lj2) ? i : j;
            const int drop = (keep == i) ? j : i;

            // Update survivor geometry now (so it can merge with others later)
            recs[keep].L = merged;
            if (recs[keep].it) recs[keep].it->setLine(merged);

            // Mark the other for deletion after we finish scanning
            alive[drop] = false;
            if (recs[drop].it) toDelete.insert(recs[drop].it);
        }
    }

    // 3) Apply deletions safely.
    int removed = 0;
    for (QGraphicsLineItem* item : toDelete) {
        if (!item) continue;
        if (item->scene() == scene()) scene()->removeItem(item);
        delete item;
        ++removed;
    }

    if (removed) {
        scene()->update();
        viewport()->update();
    }
    return removed;
}

// ---------- Refine (preview) utilities ----------

QList<QGraphicsLineItem*> DrawingCanvas::collectLineItems() const
{
    QList<QGraphicsLineItem*> out;
    for (QGraphicsItem* it : m_scene->items()) {
        if (auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(it)) {
            const int layerId = it->data(0).toInt();
            const bool vis    = isLayerVisible(layerId);
            const bool locked = isLayerLocked(layerId);
            if (vis && !locked) out << ln;
        }
    }
    return out;
}

static inline double dcAngleDeg(const QLineF& L) {
    double a = std::atan2(L.dy(), L.dx()) * 180.0 / M_PI; // [-180,180)
    if (a < 0) a += 180.0;                                // [0,180)
    return a;
}
static inline void dcAxisSnap(QLineF& L, double axisSnapDeg)
{
    const double ang = dcAngleDeg(L);
    const double d0  = std::fabs(ang - 0.0);
    const double d90 = std::fabs(ang - 90.0);
    if (std::min(d0, d90) > axisSnapDeg) return;

    if (d90 < d0) { // vertical
        const double x = 0.5*(L.x1()+L.x2());
        L.setP1(QPointF(x, L.y1()));
        L.setP2(QPointF(x, L.y2()));
    } else {        // horizontal
        const double y = 0.5*(L.y1()+L.y2());
        L.setP1(QPointF(L.x1(), y));
        L.setP2(QPointF(L.x2(), y));
    }
}

void DrawingCanvas::computeRefinePreview(const QList<QGraphicsLineItem*>& lines,
                                         const RefineParams& p,
                                         QVector<QLineF>& outNew,
                                         QVector<QLineF>& outClosures,
                                         QVector<int>& outDeleteIdx)
{
    outNew.clear();
    outClosures.clear();
    outDeleteIdx.clear();
    if (lines.isEmpty()) return;

    // ------- 1) Weld endpoints using a small grid bucket -------
    struct End { int li; bool p1; QPointF pos; int vid = -1; };
    QVector<End> ends; ends.reserve(lines.size()*2);
    for (int i=0;i<lines.size();++i) {
        QLineF L = lines[i]->line();
        ends.push_back({i,true,  L.p1(), -1});
        ends.push_back({i,false, L.p2(), -1});
    }
    const double tol  = std::max(0.5, p.weldTolPx);
    const double cell = std::max(1.0, tol);

    auto cellKey = [&](const QPointF& q)->qint64 {
        const qint64 gx = static_cast<qint64>(std::floor(q.x()/cell));
        const qint64 gy = static_cast<qint64>(std::floor(q.y()/cell));
        return (gx<<32) ^ (gy & 0xffffffff);
    };

    QHash<qint64, QVector<int>> buckets;
    QVector<QPointF> vSum;  // running sum per vertex
    QVector<int>     vCnt;

    auto nearbyVerts = [&](const QPointF& q)->QVector<int> {
        QVector<int> idxs;
        const qint64 gx = static_cast<qint64>(std::floor(q.x()/cell));
        const qint64 gy = static_cast<qint64>(std::floor(q.y()/cell));
        for (qint64 dy=-1; dy<=1; ++dy)
            for (qint64 dx=-1; dx<=1; ++dx) {
                const qint64 key = ((gx+dx)<<32) ^ ((gy+dy)&0xffffffff);
                if (buckets.contains(key)) idxs += buckets[key];
            }
        return idxs;
    };

    for (int ei=0; ei<ends.size(); ++ei) {
        const QPointF q = ends[ei].pos;
        const QVector<int> cand = nearbyVerts(q);
        int attach = -1;
        for (int ci : cand) {
            const QPointF mean = vSum[ci] / double(std::max(1, vCnt[ci]));
            if (dist2D(mean, q) <= tol*tol) { attach = ci; break; }
        }
        if (attach < 0) {
            attach = vSum.size();
            vSum.push_back(q);
            vCnt.push_back(1);
        } else {
            vSum[attach] += q;
            vCnt[attach] += 1;
        }
        ends[ei].vid = attach;
        buckets[cellKey(q)].push_back(attach);
    }

    QVector<QPointF> vPos; vPos.reserve(vSum.size());
    for (int i=0;i<vSum.size();++i) vPos.push_back(vSum[i] / double(std::max(1, vCnt[i])));

    // ------- 2) New lines = welded + axis snapped -------
    outNew.resize(lines.size());
    for (int i=0;i<lines.size();++i) {
        const QPointF P = vPos[ ends[2*i + 0].vid ];
        const QPointF Q = vPos[ ends[2*i + 1].vid ];
        QLineF L(P, Q);
        if (L.length() >= p.minLenPx)
            dcAxisSnap(L, p.axisSnapDeg);
        outNew[i] = L;
    }

    // ------- 3) Add micro-closures between free endpoints -------
    const double close2 = p.closeTolPx * p.closeTolPx;
    QVector<QVector<int>> incident(vPos.size());
    for (int i=0;i<lines.size();++i) {
        incident[ ends[2*i+0].vid ].push_back(i);
        incident[ ends[2*i+1].vid ].push_back(i);
    }
    QVector<int> freeVerts;
    for (int vid=0; vid<vPos.size(); ++vid)
        if (incident[vid].size()==1) freeVerts.push_back(vid);

    for (int a=0; a<freeVerts.size(); ++a) {
        for (int b=a+1; b<freeVerts.size(); ++b) {
            const int va = freeVerts[a], vb = freeVerts[b];
            const QPointF P = vPos[va], Q = vPos[vb];
            if (dist2D(P,Q) > close2) continue;

            QLineF L(P,Q);
            if (L.length() < p.minLenPx) continue;
            dcAxisSnap(L, p.axisSnapDeg);

            // avoid duplicating existing connection
            bool dup=false;
            for (int li : incident[va]) {
                int other = (ends[2*li+0].vid==va)? ends[2*li+1].vid : ends[2*li+0].vid;
                if (other==vb) { dup=true; break; }
            }
            if (!dup) outClosures.push_back(L);
        }
    }

    // ------- 4) Parallel stack thinning (gentle, width-aware) -------
    if (p.stackEnabled) {
        const double angTolRad = p.stackAngleDeg * M_PI / 180.0;
        const double sepTol    = std::max(0.0, p.stackSepPx);
        const double minOv     = std::max(0.0, p.stackMinOverlap);

        const int n = outNew.size();
        QVector<bool> killed(n,false);

        auto dirAngle = [](const QLineF& L)->double {
            double a = std::atan2(L.dy(), L.dx());
            if (a < 0) a += M_PI;           // [0,π)
            return a;
        };

        for (int i=0;i<n;++i) {
            if (killed[i]) continue;
            QLineF A = outNew[i];
            if (A.length() < p.minLenPx) continue;

            // reference axis from A
            const QPointF O( 0.25*(A.x1()+A.x2()), 0.25*(A.y1()+A.y2()) ); // some origin (quarter of sum)
            const double lenA = std::hypot(A.dx(), A.dy());
            if (lenA <= 1e-9) continue;
            const QPointF u( A.dx()/lenA, A.dy()/lenA );     // axis unit
            const QPointF nrm(-u.y(), u.x());                // perp

            auto projS = [&](const QPointF& P){ return dot2D(QPointF(P.x()-O.x(), P.y()-O.y()), u); };
            auto projT = [&](const QPointF& P){ return dot2D(QPointF(P.x()-O.x(), P.y()-O.y()), nrm); };

            double sA1 = projS(A.p1()), sA2 = projS(A.p2());
            if (sA1 > sA2) std::swap(sA1, sA2);
            const double offA = 0.5*(projT(A.p1()) + projT(A.p2()));

            double sMin = sA1, sMax = sA2;
            double offSum = offA; int offCnt = 1;

            for (int j=i+1;j<n;++j) {
                if (killed[j]) continue;
                QLineF B = outNew[j];
                if (B.length() < p.minLenPx) continue;

                // angle similarity
                double dAng = std::fabs(dirAngle(A) - dirAngle(B));
                dAng = std::min(dAng, M_PI - dAng);
                if (dAng > angTolRad) continue;

                // separation (signed offset of midpoints)
                const double offB = 0.5*(projT(B.p1()) + projT(B.p2()));
                if (std::fabs(offA - offB) > sepTol) continue;

                // overlap along axis
                double sB1 = projS(B.p1()), sB2 = projS(B.p2());
                if (sB1 > sB2) std::swap(sB1, sB2);
                const double ov = std::max(0.0, std::min(sA2, sB2) - std::max(sA1, sB1));
                if (ov < minOv) continue;

                // accept into group
                sMin   = std::min(sMin, sB1);
                sMax   = std::max(sMax, sB2);
                offSum += offB; ++offCnt;
                killed[j] = true;
                outDeleteIdx.push_back(j);
            }

            const double offC = offSum / double(offCnt);
            const QPointF P = QPointF(O.x() + u.x()*sMin + nrm.x()*offC,
                                      O.y() + u.y()*sMin + nrm.y()*offC);
            const QPointF Q = QPointF(O.x() + u.x()*sMax + nrm.x()*offC,
                                      O.y() + u.y()*sMax + nrm.y()*offC);
            outNew[i] = QLineF(P,Q);
        }

        // Optional: ensure delete indices are unique & sorted (nice for apply)
        std::sort(outDeleteIdx.begin(), outDeleteIdx.end());
        outDeleteIdx.erase(std::unique(outDeleteIdx.begin(), outDeleteIdx.end()), outDeleteIdx.end());
    }
}

void DrawingCanvas::updateRefinePreview(const RefineParams& p)
{
    // 1) Drop ONLY the old overlay (if any). Keep any staged data intact.
    if (m_refinePreview) {
        if (m_refinePreview->scene() == m_scene)
            m_scene->removeItem(m_refinePreview);
        delete m_refinePreview;
        m_refinePreview = nullptr;
    }

    // 2) Collect lines and compute preview state
    const QList<QGraphicsLineItem*> lines = collectLineItems();

    m_refineSrc = QVector<QGraphicsLineItem*>::fromList(lines);
    m_refineNew.clear();
    m_refineClosures.clear();
    m_refineDeleteIdx.clear();

    computeRefinePreview(lines, p, m_refineNew, m_refineClosures, m_refineDeleteIdx);

    // 3) Build overlay
    m_refinePreview = new QGraphicsItemGroup();
    m_refinePreview->setHandlesChildEvents(false);
    m_refinePreview->setFlag(QGraphicsItem::ItemIsSelectable, false);
    m_refinePreview->setZValue(1e6);
    m_scene->addItem(m_refinePreview);

    QPen changedPen(Qt::blue);       changedPen.setCosmetic(true); changedPen.setStyle(Qt::DashLine);
    QPen addPen    (Qt::darkGreen);  addPen    .setCosmetic(true); addPen    .setStyle(Qt::DashDotLine);
    QPen delPen    (Qt::red);        delPen    .setCosmetic(true); delPen    .setStyle(Qt::DotLine);

    // Replacement geometry (blue)
    for (const QLineF& L : m_refineNew) {
        auto* ghost = new QGraphicsLineItem(L);
        ghost->setPen(changedPen);
        ghost->setZValue(1e6+1);
        m_refinePreview->addToGroup(ghost);
    }
    // New closures (green)
    for (const QLineF& L : m_refineClosures) {
        auto* ghost = new QGraphicsLineItem(L);
        ghost->setPen(addPen);
        ghost->setZValue(1e6+1);
        m_refinePreview->addToGroup(ghost);
    }
    // To-be-deleted originals (red at current positions)
    for (int idx : m_refineDeleteIdx) {
        if (idx >= 0 && idx < m_refineSrc.size() && m_refineSrc[idx]) {
            auto* ghost = new QGraphicsLineItem(m_refineSrc[idx]->line());
            ghost->setPen(delPen);
            ghost->setZValue(1e6+1);
            m_refinePreview->addToGroup(ghost);
        }
    }

    viewport()->update();
}

int DrawingCanvas::applyRefinePreview()
{
    if (!m_refinePreview) return 0;
    int edits = 0;

    const int n = std::min(m_refineSrc.size(), m_refineNew.size());
    QSet<int> toDelete = QSet<int>(m_refineDeleteIdx.begin(), m_refineDeleteIdx.end());

    // Apply geometry updates to survivors
    for (int i = 0; i < n; ++i) {
        if (!m_refineSrc[i]) continue;
        if (toDelete.contains(i)) continue; // will delete
        const QLineF cur = m_refineSrc[i]->line();
        const QLineF nxt = m_refineNew[i];
        if (cur.p1() != nxt.p1() || cur.p2() != nxt.p2()) {
            m_refineSrc[i]->setLine(nxt);
            ++edits;
        }
    }

    // Delete thinned duplicates
    for (int idx : toDelete) {
        if (idx >= 0 && idx < m_refineSrc.size() && m_refineSrc[idx]) {
            m_scene->removeItem(m_refineSrc[idx]);
            delete m_refineSrc[idx];
            ++edits;
        }
    }

    // Add closures
    for (const QLineF& L : m_refineClosures) {
        if (L.length() <= 0.0) continue;
        auto* ln = new QGraphicsLineItem(L);
        ln->setPen(currentPen());
        ln->setData(0, m_layer);
        applyLayerStateToItem(ln, m_layer);
        ln->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        m_scene->addItem(ln);
        ++edits;
    }

    // Clean up preview + staged data
    cancelRefinePreview();

    scene()->update();
    viewport()->update();
    return edits;
}

void DrawingCanvas::cancelRefinePreview()
{
    // Remove overlay if present
    if (m_refinePreview) {
        if (m_refinePreview->scene() == m_scene)
            m_scene->removeItem(m_refinePreview);
        delete m_refinePreview;
        m_refinePreview = nullptr;
    }

    // Clear staged arrays
    m_refineSrc.clear();
    m_refineNew.clear();
    m_refineClosures.clear();
    m_refineDeleteIdx.clear();
}


