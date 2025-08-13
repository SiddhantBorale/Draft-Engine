#pragma once
#include <QGraphicsItem>
#include <QPen>
#include <QFont>
#include <QPainterPath>
#include <functional>

struct DimStyle {
    QPen   pen        = QPen(Qt::darkGray, 0); // cosmetic
    QFont  font       = QFont("Menlo", 9);
    qreal  arrowSize  = 8.0;
    int    precision  = 2;
    QString unit      = QStringLiteral("mm");
    bool   showUnits  = true;
};

class LinearDimItem : public QGraphicsItem
{
public:
    explicit LinearDimItem(QPointF a = {}, QPointF b = {}, QGraphicsItem* parent = nullptr);

    // configuration
    void setEndpoints(const QPointF& a, const QPointF& b);
    void setOffset(qreal o);
    void setStyle(const DimStyle& s);
    void setFormatter(std::function<QString(double)> f); // px -> label

    // QGraphicsItem
    QRectF boundingRect() const override;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget* widget) override;

    // accessors
    const DimStyle& style() const { return m_style; }

private:
    void updatePath();

    QPointF m_a{};
    QPointF m_b{};
    qreal   m_offset{16.0};

    DimStyle m_style{};
    std::function<QString(double)> m_format; // optional: provided by canvas

    QPainterPath m_path;
    QRectF       m_bounds;
};
