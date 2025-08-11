#include "DrawingCanvas.h"

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

#include <QtSvg/QSvgRenderer>
#include <QtSvg/QSvgGenerator>
#include <QtSvgWidgets/QGraphicsSvgItem>

#include <QUndoStack>
#include <QUndoCommand>

#include <cmath>
#include <limits>

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

    // Selection mode: try handles first, then normal selection
    if (m_tool == Tool::Select) {
        // Try to grab a handle (resize/rotate/bend)
        if (handleMousePress(sceneP, e->button())) {
            e->accept();
            return;
        }

        // Track positions for move undo
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

    // Drawing tools
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
        // Right click finishes
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
        if (h.item) { m_scene->removeItem(h.item); delete h.item; }
    }
    m_handles.clear();
    if (m_rotDot) { m_scene->removeItem(m_rotDot); delete m_rotDot; m_rotDot = nullptr; }
    if (m_bendPreview) { m_scene->removeItem(m_bendPreview); delete m_bendPreview; m_bendPreview = nullptr; }
    m_activeHandle.reset();
    m_target = nullptr;
}



void DrawingCanvas::createHandlesForSelected()
{
    clearHandles();

    const auto sel = m_scene->selectedItems();
    if (sel.size() != 1) return;

    m_target = sel.first();

    // Only support basic primitives for now
    if (!qgraphicsitem_cast<QGraphicsLineItem*>(m_target) &&
        !qgraphicsitem_cast<QGraphicsRectItem*>(m_target) &&
        !qgraphicsitem_cast<QGraphicsEllipseItem*>(m_target) &&
        !qgraphicsitem_cast<QGraphicsPolygonItem*>(m_target)) {
        m_target = nullptr;
        return;
    }

    constexpr qreal hs = 8.0;
    auto addHandle = [&](Handle::Type t){
        auto* r = m_scene->addRect(QRectF(-hs/2, -hs/2, hs, hs),
                                   QPen(Qt::blue, 0), QBrush(Qt::white));
        r->setZValue(1e6);
        r->setFlag(QGraphicsItem::ItemIgnoresTransformations, true); // stays same size on screen
        m_handles.push_back(Handle{t, r});
    };
    using T = Handle::Type;
    addHandle(T::TL); addHandle(T::TM); addHandle(T::TR);
    addHandle(T::ML);                 addHandle(T::MR);
    addHandle(T::BL); addHandle(T::BM); addHandle(T::BR);

    // rotate dot
    m_rotDot = m_scene->addEllipse(QRectF(-hs/2, -hs/2, hs, hs),
                                   QPen(Qt::darkGreen, 0), QBrush(Qt::green));
    m_rotDot->setZValue(1e6);
    m_rotDot->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

    // optional bend handle (for lines)
    if (qgraphicsitem_cast<QGraphicsLineItem*>(m_target)) {
        auto* r = m_scene->addRect(QRectF(-hs/2, -hs/2, hs, hs),
                                   QPen(Qt::darkMagenta, 0), QBrush(Qt::magenta));
        r->setZValue(1e6);
        r->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        m_handles.push_back(Handle{Handle::BEND, r});
    }

    layoutHandles();
}


void DrawingCanvas::layoutHandles()
{
    if (!m_target) return;
    QRectF br = m_target->sceneBoundingRect();
    if (br.isEmpty()) return;

    m_targetCenter = br.center();

    auto posFor = [&](Handle::Type t){
        switch (t) {
        case Handle::TL: return QPointF(br.left(),  br.top());
        case Handle::TM: return QPointF(br.center().x(), br.top());
        case Handle::TR: return QPointF(br.right(), br.top());
        case Handle::ML: return QPointF(br.left(),  br.center().y());
        case Handle::MR: return QPointF(br.right(), br.center().y());
        case Handle::BL: return QPointF(br.left(),  br.bottom());
        case Handle::BM: return QPointF(br.center().x(), br.bottom());
        case Handle::BR: return QPointF(br.right(), br.bottom());
        case Handle::ROT: // not used here
        case Handle::BEND: // handled below
            break;
        }
        return QPointF();
    };

    for (auto& h : m_handles) {
        if (h.type == Handle::BEND) continue; // set below for lines
        if (h.type == Handle::ROT)   continue;
        h.item->setPos(posFor(h.type));
    }

    if (m_rotDot) {
        m_rotDot->setPos(QPointF(br.center().x(), br.top() - 20.0));
    }

    // bend handle for line: midpoint of the line’s scene endpoints
    if (auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(m_target)) {
        const QLineF Ls = QLineF(m_target->mapToScene(ln->line().p1()),
                                 m_target->mapToScene(ln->line().p2()));
        const QPointF mid = (Ls.p1() + Ls.p2()) / 2.0;
        for (auto& h : m_handles) {
            if (h.type == Handle::BEND) {
                h.item->setPos(mid);
            }
        }
    }
}


static QPen previewPen(const QPen& base)
{
    QPen p = base;
    p.setStyle(Qt::DashLine);
    p.setCosmetic(true);
    return p;
}



bool DrawingCanvas::handleMousePress(const QPointF& scenePos, Qt::MouseButton btn)
{
    Q_UNUSED(btn);
    if (!m_target) return false;

    for (auto& h : m_handles) {
        if (h.item->sceneBoundingRect().contains(scenePos)) {
            m_activeHandle = h.type;
            m_handleStartScene = scenePos;

            if (*m_activeHandle == DrawingCanvas::Handle::ROT) {
                m_targetCenter = m_target->sceneBoundingRect().center();
                m_targetStartRotation = m_target->rotation();
                return true;
            }

            if (*m_activeHandle == Handle::BEND &&
                qgraphicsitem_cast<QGraphicsLineItem*>(m_target)) {
                if (!m_bendPreview) {
                    m_bendPreview = m_scene->addPath(QPainterPath(), QPen(Qt::darkYellow, 0));
                    m_bendPreview->setZValue(1e6);
                    m_bendPreview->setPen(previewPen(m_bendPreview->pen()));
                }
                return true;
            }

            // record starting geometry for resize on rect/ellipse/line P1/P2
            m_targetCenter = m_target->sceneBoundingRect().center();
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
        m_activeHandle = DrawingCanvas::Handle::ROT;
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

    // -------- BEND live preview on lines --------
    if (type == Handle::BEND) {
        auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(m_target);
        if (!ln) return false;

        QPointF A = m_target->mapToScene(ln->line().p1());
        QPointF B = m_target->mapToScene(ln->line().p2());
        if (QLineF(A,B).length() < 1e-6) return true;

        QLineF dir(A, B); dir.setLength(1.0);
        QPointF n(-dir.dy(), dir.dx());
        QPointF M = m_bendMidScene;

        QPointF d = scenePos - M;
        double sagitta = d.x()*n.x() + d.y()*n.y();

        QPainterPath path;
        path.moveTo(A);
        path.quadTo(M + n * sagitta, B);

        if (!m_bendPreview) {
            m_bendPreview = m_scene->addPath(path, previewPen(ln->pen()));
            m_bendPreview->setZValue(1e6);
        } else {
            m_bendPreview->setPath(path);
            QPen pen = previewPen(ln->pen());
            m_bendPreview->setPen(pen);
        }
        return true;
    }

    if (type == Handle::ROT) {
        QLineF a(m_targetCenter, m_handleStartScene);
        QLineF b(m_targetCenter, scenePos);
        qreal delta = b.angleTo(a); // CCW positive
        m_target->setTransformOriginPoint(m_target->mapFromScene(m_targetCenter));
        m_target->setRotation(m_targetStartRotation + delta);
        return true;
    }

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
        case Handle::TL: L.setP1(L.p1()+delta); break; // reusing TL as P1
        case Handle::BR: L.setP2(L.p2()+delta); break; // reusing BR as P2
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

    if (*m_activeHandle == Handle::BEND) {
        auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(m_target);
        if (ln) {
            // recompute sagitta from preview path’s control point if available
            double sag = 0.0;

            QPointF A = m_target->mapToScene(ln->line().p1());
            QPointF B = m_target->mapToScene(ln->line().p2());
            QLineF dir(A, B); dir.setLength(1.0);
            QPointF n(-dir.dy(), dir.dx());
            QPointF M = m_bendMidScene;

            if (m_bendPreview && !m_bendPreview->path().isEmpty()
                && m_bendPreview->path().elementCount() >= 3) {
                auto e1 = m_bendPreview->path().elementAt(1); // quad control
                QPointF C(e1.x, e1.y);
                QPointF dm = C - M;
                sag = dm.x()*n.x() + dm.y()*n.y();
            }

            if (m_bendPreview) { m_scene->removeItem(m_bendPreview); delete m_bendPreview; m_bendPreview = nullptr; }

            // Select the line and convert using your existing API
            m_target->setSelected(true);
            bendSelectedLine(sag);
        }
        m_activeHandle.reset();
        clearHandles();
        createHandlesForSelected();
        return true;
    }

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
