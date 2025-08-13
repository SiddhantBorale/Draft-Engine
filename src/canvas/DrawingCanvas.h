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
#include "dim/LinearDimItem.h"


class QUndoStack;
class AnchorPoint;
class LinearDimItem;


/* Lightweight rounded-rect item we can edit later */
class RoundedRectItem : public QGraphicsPathItem {
public:
    RoundedRectItem(const QRectF& r = QRectF(), qreal rx = 0, qreal ry = 0)
        : m_rect(r), m_rx(rx), m_ry(ry) { updatePath(); }
    void setRect(const QRectF& r) { m_rect = r; updatePath(); }
    void setRadius(qreal rx, qreal ry) {
        m_rx = qMax<qreal>(0, rx);
        m_ry = qMax<qreal>(0, ry);
        // clamp to half size
        m_rx = qMin(m_rx, m_rect.width()  * 0.5);
        m_ry = qMin(m_ry, m_rect.height() * 0.5);
        updatePath();
    }
    QRectF rect() const { return m_rect; }
    qreal  rx()   const { return m_rx; }
    qreal  ry()   const { return m_ry;  }
    
private:
    void updatePath() {
        QPainterPath p;
        p.addRoundedRect(m_rect.normalized(), m_rx, m_ry);
        setPath(p);
    }

    QRectF m_rect;
    qreal  m_rx {0};
    qreal  m_ry {0};
};

class DrawingCanvas : public QGraphicsView {
    Q_OBJECT
public:
    struct RefineParams {
    // distances (pixels)
    double gapPx            = 1.0;   // close endpoints if within this
    double mergePx          = 1.0;   // treat endpoints as same if within this
    double extendPx         = 1.0;   // extend endpoints to hit a nearby segment
    double minLenPx         = 20.0;   // delete segments shorter than this
    double collinearOverlapPx = 2.0; // need at least this 1-D overlap to merge

    // angles
    double axisSnapDeg      = 85.0;   // snap to 0/90 only if within this
    double axisSnapMinLen   = 50.0;  // but only if the segment is at least this long
    double extendAngleDeg   = 85.0;  // allow extending only if near 90°±this to the target

    // (You can make presets by copying and tweaking these numbers.)
};

    


    enum class Tool { Select, Line, Rect, Ellipse, Polygon, DimLinear };
    explicit DrawingCanvas(QWidget* parent = nullptr);

    // Layer controls
    int refineOverlapsLight(double tolPx = 1.5, double coverage = 0.95, double axisSnapDeg = 3);

    int refineVector();
    int refineVector(const RefineParams& p);
    void setDimUnits(const QString& u) { m_dimStyle.unit = u; }
    void setDimPrecision(int p) { m_dimStyle.precision = std::clamp(p,0,6); }
    void refreshHandles();                  // NEW: safe public wrapper
    void setSelectedCornerRadius(double r);
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
    bool roundSelectedShape(double radius);   // (optional external control)
    bool bendSelectedLine(double sagitta);    // (you already have bend support)
    void setCurrentTool(Tool t);
    Tool currentTool() const { return m_tool; }
    void setCurrentColor(const QColor&  c) { m_color = c; }
    void setFillColor   (const QColor&  c) { m_fill  = c; }
    void setLineWidth   (double w)         { m_lineWidth = std::max(0.0, w); }
    void setCurrentLayer(int layer);        // defined in .cpp
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
    void viewChanged(); // rulers listen

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

    // Handles (resize/rotate + bend + radius)
    struct Handle {
        enum Type {
            TL, TM, TR, ML, MR, BL, BM, BR, // resize
            ROT,                            // rotate
            BEND,                           // bend (for lines)
            RAD_TL, RAD_TR, RAD_BR, RAD_BL  // NEW: corner-radius handles for rects
        } type;
        QGraphicsRectItem* item { nullptr };
    };

    using Key = QPair<qint64,qint64>;
    static Key keyOf(const QPointF& p, double tol) {
        return Key{ llround(p.x()/tol), llround(p.y()/tol) };
    }
    static double deg(double rad) { return rad * 180.0 / M_PI; }
    static double rad(double deg) { return deg * M_PI / 180.0; }

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

    static inline double sqr(double v) { return v*v; }

    // geometry helpers used by refineVector
    // static double  dist2(const QPointF& a, const QPointF& b);
    static QPointF projectPointOnSegment(const QPointF& p,
                                     const QPointF& a,
                                     const QPointF& b,
                                     double* tOut = nullptr);


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

    // bend preview
    QGraphicsPathItem* m_bendPreview { nullptr };
    QPointF  m_bendMidScene;

    // --- Dimensions (simple 3-click placement: A, B, then offset) ---
    DimStyle   m_dimStyle;           // style for dimensions
    AnchorPoint* m_dimA { nullptr }; // first endpoint anchor (handle item, optional parent)
    AnchorPoint* m_dimB { nullptr }; // second endpoint anchor
    qreal        m_dimOffset { 20.0 };
    bool         m_dimPending { false }; // if you run a multi-click flow

};
