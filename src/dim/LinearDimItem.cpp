#include "LinearDimItem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

LinearDimItem::LinearDimItem(QPointF a, QPointF b, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_a(a), m_b(b)
{
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setAcceptedMouseButtons(Qt::NoButton);
    updatePath();
}

void LinearDimItem::setEndpoints(const QPointF& a, const QPointF& b)
{
    if (a == m_a && b == m_b) return;
    prepareGeometryChange();
    m_a = a; m_b = b;
    updatePath();
    update();
}

void LinearDimItem::setOffset(qreal o)
{
    if (qFuzzyCompare(o, m_offset)) return;
    prepareGeometryChange();
    m_offset = o;
    updatePath();
    update();
}

void LinearDimItem::setStyle(const DimStyle& s)
{
    if (m_style.pen == s.pen &&
        m_style.font == s.font &&
        qFuzzyCompare(m_style.arrowSize, s.arrowSize) &&
        m_style.precision == s.precision &&
        m_style.unit == s.unit &&
        m_style.showUnits == s.showUnits) {
        return;
    }
    prepareGeometryChange();
    m_style = s;
    updatePath();
    update();
}

void LinearDimItem::setFormatter(std::function<QString(double)> f)
{
    m_format = std::move(f);
    update(); // label may change formatting
}

void LinearDimItem::updatePath()
{
    m_path = QPainterPath();

    // vector a->b
    const QPointF v = m_b - m_a;
    const qreal   len = std::hypot(v.x(), v.y());
    if (len < 1e-6) {
        m_bounds = QRectF(m_a, QSizeF(1, 1)).normalized().marginsAdded(QMarginsF(8, 8, 8, 8));
        return;
    }

    // unit normal
    const QPointF n(-v.y()/len, v.x()/len);
    const QPointF a2 = m_a + n * m_offset;
    const QPointF b2 = m_b + n * m_offset;

    // dimension line
    m_path.moveTo(a2);
    m_path.lineTo(b2);

    // arrows
    const qreal s = m_style.arrowSize;
    const QPointF dir = v / len;

    auto addArrow = [&](const QPointF& tip, int sign){
        const QPointF base = tip - dir * s * sign;
        const QPointF wvec = n * (s * 0.4);
        QPainterPath tri;
        tri.moveTo(tip);
        tri.lineTo(base + wvec);
        tri.lineTo(base - wvec);
        tri.closeSubpath();
        m_path.addPath(tri);
    };

    addArrow(a2, -1);
    addArrow(b2, +1);

    // conservative bounds: path + text margin
    m_bounds = m_path.boundingRect().marginsAdded(QMarginsF(24, 24, 24, 24));
}

QRectF LinearDimItem::boundingRect() const
{
    return m_bounds;
}

void LinearDimItem::paint(QPainter* p, const QStyleOptionGraphicsItem* /*opt*/, QWidget* /*widget*/)
{
    p->setRenderHint(QPainter::Antialiasing, true);

    // line + arrows
    p->setPen(m_style.pen);
    p->setBrush(m_style.pen.color());
    p->drawPath(m_path);

    // length text
    const qreal pxLen = QLineF(m_a, m_b).length();

    QString text;
    if (m_format) {
        // Canvas provided: converts px -> human label with units
        text = m_format(pxLen);
    } else {
        // Fallback: show pixels with optional suffix
        text = QString::number(pxLen, 'f', m_style.precision);
        if (m_style.showUnits && !m_style.unit.isEmpty())
            text += " " + m_style.unit;
    }

    // place text near the midpoint offset along normal
    const QPointF v = m_b - m_a;
    const qreal L = std::hypot(v.x(), v.y());
    if (L < 1e-6) return;

    const QPointF n(-v.y()/L, v.x()/L);
    const QPointF mid = (m_a + m_b) * 0.5 + n * m_offset;

    p->setPen(m_style.pen);
    p->setFont(m_style.font);

    // center the label on 'mid' using font metrics
    const QFontMetricsF fm(m_style.font);
    const QRectF tight = fm.boundingRect(QStringLiteral("  %1  ").arg(text)); // small padding
    const QRectF box(mid - QPointF(tight.width()/2.0, tight.height()/2.0),
                     QSizeF(tight.width(), tight.height()));

    p->drawText(box, Qt::AlignCenter, text);
}
