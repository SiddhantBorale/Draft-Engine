#pragma once
#include <QPolygonF>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QBrush>
#include <QResizeEvent>
#include <QHash>
#include <QPointer>
#include <QVector>
#include <optional>
#include <QGraphicsPathItem>
#include <QPainterPath>

class QUndoStack;

class DrawingCanvas : public QGraphicsView {
    Q_OBJECT
public:
    enum class Tool { Select, Line, Rect, Ellipse, Polygon };
    explicit DrawingCanvas(QWidget* parent = nullptr);

    // Layer controls
    void setLayerVisibility(int layerId, bool visible);
    void setLayerLocked(int layerId, bool locked);
    bool isLayerVisible(int layerId) const {
        auto st = m_layers.value(layerId, LayerState{true,false});
        return st.visible;
    }
    bool isLayerLocked(int layerId) const {
        auto st = m_layers.value(layerId, LayerState{true,false});
        return st.locked;
    }
    void moveItemsToLayer(int fromLayer, int toLayer);

    // Editing helpers
    bool joinSelectedLinesToPolygon(double tolerance = 1.5);
    void applyFillToSelection();

    // Settings / tools
    bool roundSelectedShape(double radius);   // <— smooth/rounded corners
    bool bendSelectedLine(double sagitta);
    void setCurrentTool(Tool t);
    Tool currentTool() const { return m_tool; }
    void setCurrentColor(const QColor&  c) { m_color = c; }
    void setFillColor   (const QColor&  c) { m_fill  = c; }
    void setLineWidth   (double w)         { m_lineWidth = std::max(0.0, w); }
    void setCurrentLayer(int layer); // declaration only (defined in .cpp)
    void toggleGrid()                      { m_showGrid = !m_showGrid; viewport()->update(); }
    void setFillPattern(Qt::BrushStyle s)  { m_brushStyle = s; }

    // Undo stack injection (from MainWindow)
    void setUndoStack(QUndoStack* s) { m_undo = s; }

    // Persistence
    QJsonDocument saveToJson() const;
    void          loadFromJson(const QJsonDocument& doc);

    // File I/O
    bool importSvg(const QString& filePath);
    bool exportSvg(const QString& filePath);

    // Zoom helpers
    void zoomIn()    { scale(1.15, 1.15); emit viewChanged(); }
    void zoomOut()   { scale(1.0/1.15, 1.0/1.15); emit viewChanged(); }
    void zoomReset() { resetTransform(); emit viewChanged(); }

signals:
    // rulers listen to this
    void viewChanged();

protected:
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent  (QMouseEvent* e) override;
    void mouseMoveEvent   (QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent       (QWheelEvent* e) override;
    void drawBackground   (QPainter* painter, const QRectF& rect) override;
    void keyPressEvent    (QKeyEvent* e) override;
    void keyReleaseEvent  (QKeyEvent* e) override;

private:
    // drawing helpers
    QPointF snap(const QPointF& scenePos) const;
    QPen   currentPen()   const { QPen p(m_color); p.setWidthF(m_lineWidth); return p; }
    QBrush currentBrush() const { QBrush b(m_fill); b.setStyle(m_brushStyle); return b; }

    // Undo helpers (push commands)
    void pushAddCmd(QGraphicsItem* item, const QString& text = "Add");
    void pushMoveCmd(QGraphicsItem* item, const QPointF& from, const QPointF& to,
                     const QString& text = "Move");
    void pushDeleteCmd(const QList<QGraphicsItem*>& items,
                       const QString& text = "Delete");

    // Handles (resize/rotate)
    struct Handle {
        enum Type { TL, TM, TR, ML, MR, BL, BM, BR, ROT } type;
        QGraphicsRectItem* item { nullptr };
    };
    void createHandlesForSelected();
    void clearHandles();
    void layoutHandles();
    bool handleMousePress  (const QPointF& scenePos, Qt::MouseButton btn);
    bool handleMouseMove   (const QPointF& scenePos);
    bool handleMouseRelease(const QPointF& scenePos);

    // Snap helpers
    static bool almostEqual(const QPointF& a, const QPointF& b, double tol);
    static QPointF snapTol(const QPointF& p, double tol);
    QVector<QPointF> collectSnapPoints(QGraphicsItem* it) const;
    void updateSnapIndicator(const QPointF& p) const;

    // Layers
    struct LayerState { bool visible = true; bool locked = false; };
    void ensureLayer(int id);
    void applyLayerStateToItem(QGraphicsItem* it, int id);

private:
    QGraphicsScene* m_scene { nullptr };
    Tool   m_tool      { Tool::Select };
    QColor m_color     { Qt::black };        // stroke
    QColor m_fill      { Qt::transparent };  // fill
    double m_lineWidth { 1.0 };
    int    m_layer     { 0 };
    Qt::BrushStyle m_brushStyle { Qt::NoBrush };

    // layer map
    QHash<int, LayerState> m_layers;

    QGraphicsItem* m_tempItem { nullptr };
    QPointF        m_startPos;

    // polygon tool state
    bool      m_polyActive { false };
    QPolygonF m_poly;

    // grid
    bool   m_showGrid { true };
    double m_gridSize { 25.0 };

    // Undo
    QUndoStack* m_undo { nullptr };
    QVector<QGraphicsItem*> m_moveItems;
    QVector<QPointF>        m_moveOldPos;
    QVector<QPointF>        m_moveNewPos;

    // snap indicator
    mutable QGraphicsItemGroup* m_snapIndicator { nullptr };

    // handles state
    QVector<Handle> m_handles;
    QGraphicsEllipseItem* m_rotDot { nullptr };
    std::optional<Handle::Type> m_activeHandle;
    QGraphicsItem* m_target { nullptr };
    QPointF  m_handleStartScene;
    QRectF   m_targetStartRect;
    QLineF   m_targetStartLine;
    qreal    m_targetStartRotation {0};
    QPointF  m_targetCenter;

    // panning state
    bool m_spacePanning { false };
};
