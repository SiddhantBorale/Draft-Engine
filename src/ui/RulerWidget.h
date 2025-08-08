#pragma once
#include <QWidget>

class DrawingCanvas;

class RulerWidget : public QWidget {
    Q_OBJECT
public:
    enum class Orientation { Horizontal, Vertical };
    RulerWidget(DrawingCanvas* view, Orientation o, QWidget* parent=nullptr);

    QSize sizeHint() const override { return (m_orient==Orientation::Horizontal)
                                              ? QSize(200, 24) : QSize(24, 200); }

public slots:
    void refresh() { update(); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    DrawingCanvas* m_view { nullptr };
    Orientation    m_orient;
};
