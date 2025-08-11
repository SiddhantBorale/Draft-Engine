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

void LinearDimItem::updatePath()
{
    m_path = QPainterPath();

    // vector a->b
    QPointF v = m_b - m_a;
    qreal len = std::hypot(v.x(), v.y());
    if (len < 1e-6) {
        m_bounds = QRectF(m_a, QSizeF(1,1)).normalized();
        return;
    }

    // unit normal
    QPointF n(-v.y()/len, v.x()/len);
    QPointF a2 = m_a + n * m_offset;
    QPointF b2 = m_b + n * m_offset;

    // dim line
    m_path.moveTo(a2);
    m_path.lineTo(b2);

    // arrows
    const qreal s = m_style.arrowSize;
    QPointF dir = v / len;

    auto addArrow = [&](const QPointF& tip, int sign){
        QPointF base = tip - dir * s * sign;
        // arrow width based on normal
        QPointF wvec = n * (s * 0.4);
        QPainterPath tri;
        tri.moveTo(tip);
        tri.lineTo(base + wvec);
        tri.lineTo(base - wvec);
        tri.closeSubpath();
        m_path.addPath(tri);
    };

    addArrow(a2, -1);
    addArrow(b2, +1);

    m_bounds = m_path.boundingRect().marginsAdded(QMarginsF(4,4,4,4));
}

QRectF LinearDimItem::boundingRect() const
{
    return m_bounds;
}

void LinearDimItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    p->setRenderHint(QPainter::Antialiasing, true);

    // line + arrows
    p->setPen(m_style.pen);
    p->setBrush(m_style.pen.color());
    p->drawPath(m_path);

    // length text
    qreal len = QLineF(m_a, m_b).length();
    QString text = QString::number(len, 'f', m_style.precision);
    if (m_style.showUnits && !m_style.unit.isEmpty())
        text += " " + m_style.unit;

    // place text near the midpoint offset along normal
    QPointF v = m_b - m_a;
    qreal L = std::hypot(v.x(), v.y());
    if (L < 1e-6) return;
    QPointF n(-v.y()/L, v.x()/L);
    QPointF mid = (m_a + m_b) * 0.5 + n * m_offset;

    p->setPen(m_style.pen);
    p->setFont(m_style.font);

    // simple centered label
    QRectF box(mid - QPointF(100, 10), QSizeF(200, 20));
    p->drawText(box, Qt::AlignCenter, text);
}
