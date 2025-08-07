#include "DrawingCanvas.h"
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPolygonItem>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDebug>

DrawingCanvas::DrawingCanvas(QWidget* parent)
    : QGraphicsView(parent),
      m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::RubberBandDrag);
}

// ─── Mouse Events ──────────────────────────────
void DrawingCanvas::mousePressEvent(QMouseEvent* e)
{
    if (m_tool == Tool::Select) {
        QGraphicsView::mousePressEvent(e);
        return;
    }

    m_startPos = mapToScene(e->pos());

    switch (m_tool) {
    case Tool::Line: {
        auto* item = new QGraphicsLineItem(QLineF(m_startPos, m_startPos));
        item->setPen(QPen(m_color, 2));
        item->setData(0, m_layer);
        m_scene->addItem(item);
        m_tempItem = item;
        break;
    }
    case Tool::Rect: {
        auto* item = new QGraphicsRectItem(QRectF(m_startPos, m_startPos));
        item->setPen(QPen(m_color, 2));
        item->setData(0, m_layer);
        m_scene->addItem(item);
        m_tempItem = item;
        break;
    }
    case Tool::Ellipse: {
        auto* item = new QGraphicsEllipseItem(QRectF(m_startPos, m_startPos));
        item->setPen(QPen(m_color, 2));
        item->setData(0, m_layer);
        m_scene->addItem(item);
        m_tempItem = item;
        break;
    }
    case Tool::Polygon: {
        // First click → start new polygon
        if (!m_tempItem) {
            auto* poly = new QGraphicsPolygonItem(QPolygonF({ m_startPos }));
            poly->setPen(QPen(m_color, 2));
            poly->setData(0, m_layer);
            m_scene->addItem(poly);
            m_tempItem = poly;
        } else {
            auto* poly = qgraphicsitem_cast<QGraphicsPolygonItem*>(m_tempItem);
            auto p = poly->polygon();
            p << m_startPos;
            poly->setPolygon(p);
        }
        break;
    }
    default:
        break;
    }
}

void DrawingCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_tempItem || m_tool == Tool::Polygon) {
        QGraphicsView::mouseMoveEvent(e);
        return;
    }

    QPointF current = mapToScene(e->pos());

    if (auto* line = qgraphicsitem_cast<QGraphicsLineItem*>(m_tempItem)) {
        line->setLine(QLineF(m_startPos, current));
    } else if (auto* rect = qgraphicsitem_cast<QGraphicsRectItem*>(m_tempItem)) {
        rect->setRect(QRectF(m_startPos, current).normalized());
    } else if (auto* ell = qgraphicsitem_cast<QGraphicsEllipseItem*>(m_tempItem)) {
        ell->setRect(QRectF(m_startPos, current).normalized());
    }
}

void DrawingCanvas::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_tool != Tool::Polygon)
        m_tempItem = nullptr;

    QGraphicsView::mouseReleaseEvent(e);
}

void DrawingCanvas::wheelEvent(QWheelEvent* e)
{
    constexpr double scaleFactor = 1.15;
    if (e->angleDelta().y() > 0)
        scale(scaleFactor, scaleFactor);
    else
        scale(1.0 / scaleFactor, 1.0 / scaleFactor);
}

// ─── JSON save/load ───────────────────────────
static QJsonObject itemToJson(QGraphicsItem* it)
{
    QJsonObject obj;
    obj["layer"] = it->data(0).toInt();
    if (auto* ln = qgraphicsitem_cast<QGraphicsLineItem*>(it)) {
        obj["type"] = "line";
        obj["x1"] = ln->line().x1();
        obj["y1"] = ln->line().y1();
        obj["x2"] = ln->line().x2();
        obj["y2"] = ln->line().y2();
        obj["color"] = static_cast<int>(ln->pen().color().rgba());
    } else if (auto* rc = qgraphicsitem_cast<QGraphicsRectItem*>(it)) {
        obj["type"] = "rect";
        obj["x"] = rc->rect().x();
        obj["y"] = rc->rect().y();
        obj["w"] = rc->rect().width();
        obj["h"] = rc->rect().height();
        obj["color"] = static_cast<int>(rc->pen().color().rgba());
    } else if (auto* el = qgraphicsitem_cast<QGraphicsEllipseItem*>(it)) {
        obj["type"] = "ellipse";
        obj["x"] = el->rect().x();
        obj["y"] = el->rect().y();
        obj["w"] = el->rect().width();
        obj["h"] = el->rect().height();
        obj["color"] = static_cast<int>(el->pen().color().rgba());
    } else if (auto* pg = qgraphicsitem_cast<QGraphicsPolygonItem*>(it)) {
        obj["type"] = "polygon";
        QJsonArray points;
        for (const QPointF& p : pg->polygon()) {
            QJsonArray pt; pt << p.x() << p.y();
            points.append(pt);
        }
        obj["points"] = points;
        obj["color"] = static_cast<int>(pg->pen().color().rgba());
    }
    return obj;
}

static QGraphicsItem* jsonToItem(const QJsonObject& obj)
{
    QString type = obj["type"].toString();
    QColor c; c.setRgba(obj["color"].toInt());
    if (type == "line") {
        auto* ln = new QGraphicsLineItem(obj["x1"].toDouble(), obj["y1"].toDouble(),
                                         obj["x2"].toDouble(), obj["y2"].toDouble());
        ln->setPen(QPen(c, 2));
        return ln;
    } else if (type == "rect") {
        auto* rc = new QGraphicsRectItem(obj["x"].toDouble(), obj["y"].toDouble(),
                                         obj["w"].toDouble(), obj["h"].toDouble());
        rc->setPen(QPen(c, 2));
        return rc;
    } else if (type == "ellipse") {
        auto* el = new QGraphicsEllipseItem(obj["x"].toDouble(), obj["y"].toDouble(),
                                            obj["w"].toDouble(), obj["h"].toDouble());
        el->setPen(QPen(c, 2));
        return el;
    } else if (type == "polygon") {
        QPolygonF poly;
        for (const auto& pt : obj["points"].toArray()) {
            QJsonArray a = pt.toArray();
            poly << QPointF(a[0].toDouble(), a[1].toDouble());
        }
        auto* pg = new QGraphicsPolygonItem(poly);
        pg->setPen(QPen(c, 2));
        return pg;
    }
    return nullptr;
}

QJsonDocument DrawingCanvas::saveToJson() const
{
    QJsonArray arr;
    for (QGraphicsItem* it : m_scene->items()) {
        if (it->flags() & QGraphicsItem::ItemIsSelectable)
            arr.append(itemToJson(it));
    }
    return QJsonDocument(arr);
}

void DrawingCanvas::loadFromJson(const QJsonDocument& doc)
{
    m_scene->clear();
    for (const auto& v : doc.array()) {
        if (auto* item = jsonToItem(v.toObject()))
            m_scene->addItem(item);
    }
}