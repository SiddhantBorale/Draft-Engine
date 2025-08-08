#include "RulerWidget.h"
#include "canvas/DrawingCanvas.h"
#include <QPainter>
#include <QPaintEvent>
#include <QTransform>

RulerWidget::RulerWidget(DrawingCanvas* view, Orientation o, QWidget* parent)
    : QWidget(parent), m_view(view), m_orient(o)
{
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
    // Repaint when view changes (zoom/scroll/resize)
    QObject::connect(m_view, &DrawingCanvas::viewChanged, this, &RulerWidget::refresh);
}

void RulerWidget::paintEvent(QPaintEvent*)
{
    if (!m_view) return;
    QPainter p(this);
    p.fillRect(rect(), palette().base());

    // scale per axis (assumes no rotation/skew)
    const QTransform t = m_view->transform();
    const double sx = t.m11();
    const double sy = t.m22();

    // map viewport edges to scene
    const QPointF s0 = m_view->mapToScene(0,0);
    const QPointF s1 = m_view->mapToScene(width(), height());

    // choose approx 50 px spacing in *view* coords
    const int tickPx = 50;
    const QPen tickPen(palette().color(QPalette::Mid));
    p.setPen(tickPen);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    auto drawH = [&]{
        // scene units per pixel horizontally
        const double scenePerPx = 1.0 / (sx == 0 ? 1.0 : sx);
        const double startSceneX = s0.x();
        // align first tick to nearest tickPx in *view* space
        int first = (-(int)m_view->mapFromScene(QPointF(startSceneX,0)).x()) % tickPx;
        if (first < 0) first += tickPx;

        for (int x = first; x < width(); x += tickPx) {
            const QPointF scenePt = m_view->mapToScene(x, 0);
            const double val = scenePt.x(); // label in scene units (pixels for now)
            // big tick
            p.drawLine(x, height(), x, height()-8);
            p.drawText(x+2, height()-10, QString::number(val, 'f', 0));
            // minor ticks
            for (int m=1; m<5; ++m) {
                int mx = x + m*(tickPx/5);
                if (mx >= width()) break;
                int len = (m==5)?8:4;
                p.drawLine(mx, height(), mx, height()-len);
            }
        }
    };

    auto drawV = [&]{
        const double scenePerPx = 1.0 / (sy == 0 ? 1.0 : sy);
        const double startSceneY = s0.y();
        int first = (-(int)m_view->mapFromScene(QPointF(0,startSceneY)).y()) % tickPx;
        if (first < 0) first += tickPx;

        for (int y = first; y < height(); y += tickPx) {
            const QPointF scenePt = m_view->mapToScene(0, y);
            const double val = scenePt.y();
            p.drawLine(width()-8, y, width(), y);
            p.save();
            p.translate(0, y);
            p.rotate(-90);
            p.drawText(-height()+2, width()-10, QString::number(val, 'f', 0));
            p.restore();
            // minor ticks
            for (int m=1; m<5; ++m) {
                int my = y + m*(tickPx/5);
                if (my >= height()) break;
                int len = (m==5)?8:4;
                p.drawLine(width()-len, my, width(), my);
            }
        }
    };

    if (m_orient == Orientation::Horizontal) drawH();
    else                                      drawV();

    // corner shading line
    p.setPen(palette().color(QPalette::Dark));
    if (m_orient == Orientation::Horizontal)
        p.drawLine(0, height()-1, width(), height()-1);
    else
        p.drawLine(width()-1, 0, width()-1, height());
}
