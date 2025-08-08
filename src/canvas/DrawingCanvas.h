#pragma once
#include <QPolygonF>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>

class DrawingCanvas : public QGraphicsView {
    Q_OBJECT
public:
    enum class Tool { Select, Line, Rect, Ellipse, Polygon };
    explicit DrawingCanvas(QWidget* parent = nullptr);

    // Settings
    void setCurrentTool (Tool t)           { m_tool  = t; }
    void setCurrentColor(const QColor&  c) { m_color = c; }
    void setFillColor   (const QColor&  c) { m_fill  = c; }
    void setLineWidth   (double w)         { m_lineWidth = std::max(0.0, w); }
    void setCurrentLayer(int layer)        { m_layer = layer; }
    void toggleGrid()                      { m_showGrid = !m_showGrid; viewport()->update(); }

    // Persistence
    QJsonDocument saveToJson() const;
    void          loadFromJson(const QJsonDocument& doc);

    // File I/O
    bool importSvg(const QString& filePath);
    bool exportSvg(const QString& filePath);

    // Zoom helpers
    void zoomIn()    { scale(1.15, 1.15); }
    void zoomOut()   { scale(1.0/1.15, 1.0/1.15); }
    void zoomReset() { resetTransform(); }

protected:
    void mousePressEvent  (QMouseEvent* e) override;
    void mouseMoveEvent   (QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent       (QWheelEvent* e) override;
    void drawBackground   (QPainter* painter, const QRectF& rect) override;

private:
    QPointF snap(const QPointF& scenePos) const;
    QPen   currentPen()   const { QPen p(m_color); p.setWidthF(m_lineWidth); return p; }
    QBrush currentBrush() const { return QBrush(m_fill); }

    QGraphicsScene* m_scene { nullptr };
    Tool   m_tool      { Tool::Select };
    QColor m_color     { Qt::black };        // stroke
    QColor m_fill      { Qt::transparent };  // fill
    double m_lineWidth { 1.0 };
    int    m_layer     { 0 };

    QGraphicsItem* m_tempItem { nullptr };
    QPointF        m_startPos;

    // polygon tool state
    bool      m_polyActive { false };
    QPolygonF m_poly;

    // grid
    bool   m_showGrid { true };
    double m_gridSize { 25.0 };
};