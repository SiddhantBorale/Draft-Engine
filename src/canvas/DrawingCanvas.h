#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QColor>
#include <memory>

class DrawingCanvas : public QGraphicsView
{
    Q_OBJECT
public:
    enum class Tool { None, Line, Rect, Ellipse, Polygon, Select };

    explicit DrawingCanvas(QWidget* parent = nullptr);

    void setCurrentTool(Tool tool) { m_tool = tool; }
    void setCurrentColor(const QColor& c) { m_color = c; }
    void setCurrentLayer(int layer) { m_layer = layer; }

    // JSON helpers
    QJsonDocument saveToJson() const;
    void loadFromJson(const QJsonDocument& doc);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    QGraphicsScene* m_scene;
    Tool m_tool { Tool::Select };
    QColor m_color { Qt::black };
    int m_layer { 0 };

    // temp item while drawing
    QGraphicsItem* m_tempItem { nullptr };
    QPointF m_startPos;
};
