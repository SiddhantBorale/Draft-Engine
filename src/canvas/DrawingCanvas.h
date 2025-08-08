#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>

class DrawingCanvas : public QGraphicsView
{
    Q_OBJECT
public:
    enum class Tool { Select, Line, Rect, Ellipse, Polygon };

    explicit DrawingCanvas(QWidget* parent = nullptr);

    void setCurrentTool (Tool t)           { m_tool  = t; }
    void setCurrentColor(const QColor&  c) { m_color = c; }
    void setCurrentLayer(int layer)        { m_layer = layer; }
    void toggleGrid()                      { m_showGrid = !m_showGrid; viewport()->update(); }

    // Persistence
    QJsonDocument saveToJson() const;
    void          loadFromJson(const QJsonDocument& doc);

    // File I/O
    bool importSvg(const QString& filePath);   // NEW
    bool exportSvg(const QString& filePath);

protected:
    void mousePressEvent  (QMouseEvent* e) override;
    void mouseMoveEvent   (QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent       (QWheelEvent* e) override;
    void drawBackground   (QPainter* painter, const QRectF& rect) override;

private:
    QPointF snap(const QPointF& scenePos) const;

    QGraphicsScene* m_scene { nullptr };
    Tool            m_tool   { Tool::Select };
    QColor          m_color  { Qt::black };
    int             m_layer  { 0 };

    QGraphicsItem*  m_tempItem { nullptr };
    QPointF         m_startPos;

    bool    m_showGrid { true };
    double  m_gridSize { 25.0 };
};
