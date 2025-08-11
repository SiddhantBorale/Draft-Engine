#pragma once
#include <QGraphicsObject>

/* A tiny, invisible object you can parent to any QGraphicsItem.
 * Because it’s a QGraphicsObject, it emits xChanged()/yChanged()
 * so our dimension can update live when the parent item moves. */
class AnchorPoint : public QGraphicsObject {
    Q_OBJECT
public:
    explicit AnchorPoint(QGraphicsItem* parent = nullptr)
        : QGraphicsObject(parent) {
        setFlag(QGraphicsItem::ItemIgnoresTransformations, false);
    }
    QRectF boundingRect() const override { return QRectF(-0.5, -0.5, 1.0, 1.0); }
    void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override {}
};
