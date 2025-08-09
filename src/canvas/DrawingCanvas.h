#pragma once
#include <QPolygonF>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUndoStack>              
#include <optional>                
#include <QPointer>                
#include <QVector>
#include <QHash>   
#include <QResizeEvent>
#include <Qt>
#include <QBrush>   

class QUndoStack;

class DrawingCanvas : public QGraphicsView {
    Q_OBJECT
public:
    enum class Tool { Select, Line, Rect, Ellipse, Polygon };
    explicit DrawingCanvas(QWidget* parent = nullptr);

    // Settings
    void setLayerVisibility(int layer, bool on);
    void setLayerLocked(int layer, bool on);
    bool isLayerVisible(int layer) const { return m_layerVisible.value(layer, true); }
    bool isLayerLocked (int layer) const { return m_layerLocked .value(layer, false); }

    void setCurrentTool (Tool t)           { m_tool  = t; }
    void setCurrentColor(const QColor&  c) { m_color = c; }
    void setFillColor   (const QColor&  c) { m_fill  = c; }
    void setLineWidth   (double w)         { m_lineWidth = std::max(0.0, w); }
    void setCurrentLayer(int layer)        { m_layer = layer; }
    void toggleGrid()                      { m_showGrid = !m_showGrid; viewport()->update(); }
    void setFillPattern(Qt::BrushStyle s)  { m_brushStyle = s; } 

    // Undo stack injection (from MainWindow)
    void setUndoStack(QUndoStack* s) { m_undo = s; }  // NEW

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
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent  (QMouseEvent* e) override;
    void mouseMoveEvent   (QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent       (QWheelEvent* e) override;
    void drawBackground   (QPainter* painter, const QRectF& rect) override;
    void keyPressEvent    (QKeyEvent* e) override;                // NEW

signals:
    // NEW: rulers listen to these to repaint
    void viewChanged(); 

private:
    // drawing helpers
    QPointF snap(const QPointF& scenePos) const;
    QPen   currentPen()   const { QPen p(m_color); p.setWidthF(m_lineWidth); return p; }
    QBrush currentBrush() const { QBrush b(m_fill); b.setStyle(m_brushStyle); return b; } // <-- CHANGE


    // --- Undo commands helpers (implemented in .cpp) --- NEW
    void pushAddCmd(QGraphicsItem* item, const QString& text = "Add");    // NEW
    void pushMoveCmd(QGraphicsItem* item, const QPointF& from, const QPointF& to,
                     const QString& text = "Move");                        // NEW
    void pushDeleteCmd(const QList<QGraphicsItem*>& items,
                       const QString& text = "Delete");                    // NEW

    // --- Handles (resize/rotate) --- NEW
    struct Handle {
        enum Type { TL, TM, TR, ML, MR, BL, BM, BR, ROT } type;
        QGraphicsRectItem* item { nullptr };
    };
    void applyLayerState(int layer); // NEW
    void createHandlesForSelected();   // NEW
    void clearHandles();               // NEW
    void layoutHandles();              // NEW
    bool handleMousePress(const QPointF& scenePos, Qt::MouseButton btn);   // NEW
    bool handleMouseMove (const QPointF& scenePos);                         // NEW
    bool handleMouseRelease(const QPointF& scenePos);                       // NEW

    QHash<int,bool> m_layerVisible;  // default true
    QHash<int,bool> m_layerLocked;
    QVector<QPointF> collectSnapPoints(QGraphicsItem* it) const;           // NEW
    void updateSnapIndicator(const QPointF& p) const;                       // NEW

private:
    QGraphicsScene* m_scene { nullptr };
    Tool   m_tool      { Tool::Select };
    QColor m_color     { Qt::black };        // stroke
    QColor m_fill      { Qt::transparent };  // fill
    double m_lineWidth { 1.0 };
    int    m_layer     { 0 };

    Qt::BrushStyle m_brushStyle { Qt::NoBrush }; 

    QGraphicsItem* m_tempItem { nullptr };
    QPointF        m_startPos;

    // polygon tool state
    bool      m_polyActive { false };
    QPolygonF m_poly;

    // grid
    bool   m_showGrid { true };
    double m_gridSize { 25.0 };

    // --- Undo --- NEW
    QUndoStack* m_undo { nullptr };
    QGraphicsItem* m_moveTarget { nullptr };     // for select-move tracking
    QPointF        m_moveStartPos;               // start pos for move

    // --- Snapping indicator --- NEW
    mutable QGraphicsItemGroup* m_snapIndicator { nullptr };

    // --- Handles state --- NEW
    QVector<QGraphicsItem*> m_moveItems;
    QVector<QPointF>        m_moveOldPos;
    QVector<QPointF>        m_moveNewPos;

    QVector<Handle> m_handles;
    QGraphicsEllipseItem* m_rotDot { nullptr };
    std::optional<Handle::Type> m_activeHandle;
    QGraphicsItem* m_target { nullptr };
    QPointF  m_handleStartScene;
    QRectF   m_targetStartRect;
    QLineF   m_targetStartLine;
    qreal    m_targetStartRotation {0};
    QPointF  m_targetCenter;
};
