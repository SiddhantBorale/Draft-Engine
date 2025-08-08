// ─────────────────────────────────────────────────────────────
//  DrawingCanvas.cpp
//  Phase-2 drafting canvas (grid, snap, SVG import/export, JSON I/O)
// ─────────────────────────────────────────────────────────────
#include "DrawingCanvas.h"

#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPainter>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>

#include <QtSvg/QSvgRenderer>   
#include <QtSvg/QSvgGenerator>
#include <QtSvgWidgets/QGraphicsSvgItem>

#include <cmath>  // std::round

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
}

// ─── Grid snap helper ────────────────────────────────────────
QPointF DrawingCanvas::snap(const QPointF& scenePos) const
{
    const double x = std::round(scenePos.x() / m_gridSize) * m_gridSize;
    const double y = std::round(scenePos.y() / m_gridSize) * m_gridSize;
    // TODO: extend to object-snap (endpoints/midpoints)
    return {x, y};
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

// ─── Mouse overrides (snap demo only) ────────────────────────
void DrawingCanvas::mousePressEvent(QMouseEvent* e)
{
    if (m_tool == Tool::Select) {
        setDragMode(QGraphicsView::RubberBandDrag);
        QGraphicsView::mousePressEvent(e);
        return;
    }
    setDragMode(QGraphicsView::NoDrag);

    const QPointF pos = snap(mapToScene(e->pos()));

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
        // Left click = add point, Right click or double-click = finish
        if (e->button() == Qt::RightButton) {
            if (m_polyActive && m_poly.size() > 2) {
                // finalize: keep as-is
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
            auto* polyItem = qgraphicsitem_cast<QGraphicsPolygonItem*>(m_tempItem);
            if (polyItem) polyItem->setPolygon(m_poly);
        }
        break;
    }
    default:
        QGraphicsView::mousePressEvent(e);
    }
}

void DrawingCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (m_tool == Tool::Select) {
        QGraphicsView::mouseMoveEvent(e);
        return;
    }

    const QPointF cur = snap(mapToScene(e->pos()));

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
    if (m_tool == Tool::Select) {
        QGraphicsView::mouseReleaseEvent(e);
        return;
    }

    if (m_tool == Tool::Polygon) {
        // polygon is committed per-click; nothing on release
        return;
    }

    // finalize preview for single-shot tools
    m_tempItem = nullptr;
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

// ─── JSON save/load ──────────────────────────────────────────
QJsonDocument DrawingCanvas::saveToJson() const
{
    QJsonArray arr;

    for (auto* it : m_scene->items()) {
        // Serialize top-level primitives
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
        // Note: SVG imports come in as QGraphicsSvgItem — skipped by design.
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
            p.setWidthF(oo.value("width").toDouble(0));
            return p;
        };
        auto mkBrush= [&](const QJsonObject& oo){
            QColor fill = hexToColor(
                oo.value("fill").toString(QColor(Qt::transparent).name(QColor::HexArgb))
            );
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
