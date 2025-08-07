// ─────────────────────────────────────────────────────────────
//  DrawingCanvas.cpp
//  Phase-2 drafting canvas (grid, snap, DXF import, SVG export)
// ─────────────────────────────────────────────────────────────
#include "DrawingCanvas.h"

#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPolygonItem>
#include <QMouseEvent>
#include <QPainter>
#include <QtSvg/QSvgGenerator>
#include <QMessageBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <libdxfrw.h>       // umbrella header (gives dxfRW, entities, interface)
#include <drw_interface.h>
#include <drw_entities.h>

#include <cmath>            // std::round

// ─── ctor ────────────────────────────────────────────────────
DrawingCanvas::DrawingCanvas(QWidget* parent)
    : QGraphicsView(parent),
      m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::RubberBandDrag);
}

// ─── Grid snap helper ────────────────────────────────────────
QPointF DrawingCanvas::snap(const QPointF& scenePos) const
{
    const double x = std::round(scenePos.x() / m_gridSize) * m_gridSize;
    const double y = std::round(scenePos.y() / m_gridSize) * m_gridSize;
    // TODO: extend to object-snap (endpoints/midpoints)
    return {x, y};
}

// ─── Draw background grid ────────────────────────────────────
void DrawingCanvas::drawBackground(QPainter* p, const QRectF& rect)
{
    if (!m_showGrid) {
        QGraphicsView::drawBackground(p, rect);
        return;
    }

    const QPen gridPen(QColor(230, 230, 230));
    p->setPen(gridPen);

    const double left = std::floor(rect.left() /  m_gridSize) * m_gridSize;
    const double top  = std::floor(rect.top()  /  m_gridSize) * m_gridSize;

    for (double x = left; x < rect.right(); x += m_gridSize)
        p->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));

    for (double y = top; y < rect.bottom(); y += m_gridSize)
        p->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
}

// ─── Mouse overrides (snap demo only) ────────────────────────
void DrawingCanvas::mousePressEvent(QMouseEvent* e)
{
    if (m_tool == Tool::Select) {
        QGraphicsView::mousePressEvent(e);
        return;
    }
    m_startPos = snap(mapToScene(e->pos()));
    // … add drawing-tool logic here …
}

void DrawingCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_tempItem || m_tool == Tool::Polygon) {
        QGraphicsView::mouseMoveEvent(e);
        return;
    }
    const QPointF current = snap(mapToScene(e->pos()));
    // … update m_tempItem geometry …
}

// ─── SVG export ──────────────────────────────────────────────
bool DrawingCanvas::exportSvg(const QString& filePath)
{
    QSvgGenerator gen;
    gen.setFileName(filePath);
    gen.setSize(QSize(1600, 1200));
    gen.setViewBox(scene()->itemsBoundingRect());

    QPainter painter(&gen);
    m_scene->render(&painter);
    return painter.isActive();
}

// ─── Simple DXF reader (LINE only for now) ───────────────────
class SimpleDxfReader : public DRW_Interface
{
public:
    explicit SimpleDxfReader(QGraphicsScene* s) : m_scene(s) {}

    // only entity we currently import
    void addLine(const DRW_Line& l) override
    {
        auto* ln = m_scene->addLine(l.basePoint.x,
                                    -l.basePoint.y,
                                    l.secPoint.x,
                                    -l.secPoint.y);
        ln->setPen(QPen(Qt::black, 0));
    }

    // ─── empty stubs for required pure-virtuals ─────────────
    void addHeader      (const DRW_Header*)               override {}
    void addLType       (const DRW_LType&)                override {}
    void addLayer       (const DRW_Layer&)                override {}
    void addDimStyle    (const DRW_Dimstyle&)             override {}
    void addVport       (const DRW_Vport&)                override {}
    void addTextStyle   (const DRW_Textstyle&)            override {}
    void addAppId       (const DRW_AppId&)                override {}
    void addBlock       (const DRW_Block&)                override {}
    void setBlock       (int)                             override {}
    void endBlock       ()                                override {}
    void addPoint       (const DRW_Point&)                override {}
    void addRay         (const DRW_Ray&)                  override {}
    void addXline       (const DRW_Xline&)                override {}
    void addArc         (const DRW_Arc&)                  override {}
    void addCircle      (const DRW_Circle&)               override {}
    void addEllipse     (const DRW_Ellipse&)              override {}
    void addLWPolyline  (const DRW_LWPolyline&)           override {}
    void addPolyline    (const DRW_Polyline&)             override {}
    void addSpline      (const DRW_Spline*)               override {}
    void addKnot        (const DRW_Entity&)               override {}
    void addInsert      (const DRW_Insert&)               override {}
    void addTrace       (const DRW_Trace&)                override {}
    void add3dFace      (const DRW_3Dface&)               override {}
    void addSolid       (const DRW_Solid&)                override {}
    void addMText       (const DRW_MText&)                override {}
    void addText        (const DRW_Text&)                 override {}
    void addDimAlign    (const DRW_DimAligned*)           override {}
    void addDimLinear   (const DRW_DimLinear*)            override {}
    void addDimRadial   (const DRW_DimRadial*)            override {}
    void addDimDiametric(const DRW_DimDiametric*)         override {}
    void addDimAngular  (const DRW_DimAngular*)           override {}
    void addDimAngular3P(const DRW_DimAngular3p*)         override {}
    void addDimOrdinate (const DRW_DimOrdinate*)          override {}
    void addLeader      (const DRW_Leader*)               override {}
    void addHatch       (const DRW_Hatch*)                override {}
    void addViewport    (const DRW_Viewport&)             override {}
    void addImage       (const DRW_Image*)                override {}
    void linkImage      (const DRW_ImageDef*)             override {}
    void addComment     (const char*)                     override {}
    void addPlotSettings(const DRW_PlotSettings*)         override {}

    // writer stubs
    void writeHeader       (DRW_Header&) override {}
    void writeBlocks       ()            override {}
    void writeBlockRecords ()            override {}
    void writeEntities     ()            override {}
    void writeLTypes       ()            override {}
    void writeLayers       ()            override {}
    void writeTextstyles   ()            override {}
    void writeVports       ()            override {}
    void writeDimstyles    ()            override {}
    void writeObjects      ()            override {}
    void writeAppId        ()            override {}

private:
    QGraphicsScene* m_scene;
};

// ─── DXF loader wrapper ──────────────────────────────────────
bool DrawingCanvas::openDxf(const QString& path)
{
    SimpleDxfReader reader(m_scene);

    dxfRW dxf(path.toStdString().c_str());          // ctor: filename only
    if (!dxf.read(&reader, /*readAllSections=*/true)) {
        QMessageBox::warning(this, "DXF",
                             "Failed to read LX entities from file.");
        return false;
    }
    return true;
}

// convenience wrapper retained for menu action
bool DrawingCanvas::importDxf(const QString& path)
{
    return openDxf(path);
}


/* ─── wheel zoom ─────────────────────────────────────────── */
void DrawingCanvas::wheelEvent(QWheelEvent* e)
{
    const double  factor = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    scale(factor, factor);
}

/* ─── mouse release (finish drawing) ─────────────────────── */
void DrawingCanvas::mouseReleaseEvent(QMouseEvent* e)
{
    QGraphicsView::mouseReleaseEvent(e);
    m_tempItem = nullptr;          // stop rubber-band preview
}

/* ─── JSON I/O (no-op placeholders) ──────────────────────── */
QJsonDocument DrawingCanvas::saveToJson() const
{
    // TODO: serialize items properly
    return QJsonDocument{ QJsonObject{{"todo", "implement save"}} };
}

void DrawingCanvas::loadFromJson(const QJsonDocument& /*doc*/)
{
    scene()->clear();
    // TODO: recreate items
}
