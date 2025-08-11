#pragma once
#include <QGraphicsItem>
#include <QPen>
#include <QFont>
#include <QString>
#include <QPainterPath>

// Simple dimension style you can tweak from the canvas/UI
struct DimStyle {
    QPen    pen        { Qt::black, 0.0 };   // cosmetic (0 width)
    QFont   font       { "Menlo", 9 };
    qreal   arrowSize  { 8.0 };
    int     precision  { 2 };
    QString unit       { "mm" };
    bool    showUnits  { true };
};

// Very lightweight linear dimension graphic: offset dim line between A and B,
// arrows at ends, and a length label.
class LinearDimItem : public QGraphicsItem
{
public:
    LinearDimItem(QPointF a = {}, QPointF b = {}, QGraphicsItem* parent = nullptr);

    void setEndpoints(const QPointF& a, const QPointF& b);
    void setOffset(qreal o);
    void setStyle(const DimStyle& s) { m_style = s; prepareGeometryChange(); updatePath(); update(); }
    const DimStyle& style() const { return m_style; }

    QRectF boundingRect() const override;
    void   paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget* w) override;

private:
    void updatePath();

    QPointF     m_a;
    QPointF     m_b;
    qreal       m_offset { 20.0 };
    DimStyle    m_style;

    QPainterPath m_path;
    QRectF       m_bounds;
};
