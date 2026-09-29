// SpectrumView markers: one overlay layer draws user markers, peak markers and the
// optional readout block in a single pass.
#include "markers.h"
#include "spectrumview.h"
#include "units.h"
#include <cmath>

class MarkerOverlay : public QCPLayerable {
public:
    explicit MarkerOverlay(SpectrumView *v) : QCPLayerable(v, "markers"), m_view(v) {}

protected:
    void applyDefaultAntialiasingHint(QCPPainter *p) const override
    {
        applyAntialiasingHint(p, true, QCP::aeItems);
    }
    void draw(QCPPainter *p) override { m_view->drawMarkers(p); }
    QRect clipRect() const override { return m_view->axisRect()->rect(); }

private:
    SpectrumView *m_view;
};

QCPLayerable *createMarkerOverlay(SpectrumView *v)
{
    v->addLayer("markers", v->layer("main"), QCustomPlot::limAbove);
    return new MarkerOverlay(v);
}

void SpectrumView::setMarkers(const QList<MarkerMark> &marks)
{
    m_marks = marks;
    replot(rpQueuedReplot);
}

QPointF SpectrumView::peak(bool max) const
{
    return m_graph[0]->visible() ? tracePeak(m_trace[0], max) : QPointF(qQNaN(), qQNaN());
}

int SpectrumView::markerAt(QPointF p) const
{
    int best = -1;
    double bestDist = 6;  // px
    for (int i = 0; i < m_marks.size(); ++i) {
        const double d = std::abs(xAxis->coordToPixel(m_marks[i].hz) - p.x());
        if (d <= bestDist)
            best = i, bestDist = d;
    }
    return best;
}

// Triangle with its tip at (x, y), pointing down (above the trace) or up.
static void triangle(QCPPainter *p, double x, double y, bool down, const QColor &c, bool filled)
{
    const double h = down ? -11 : 11;
    const QPolygonF tri{{x, y}, {x - 6, y + h}, {x + 6, y + h}};
    p->setPen(QPen(c, 1.2));
    p->setBrush(filled ? QBrush(c) : Qt::NoBrush);
    p->drawPolygon(tri);
}

void SpectrumView::drawMarkers(QCPPainter *p)
{
    const QRect r = axisRect()->rect();
    const QFontMetrics fm(font());
    auto label = [&](const QString &text, double x, double y, bool above, const QColor &c) {
        const QRectF box = fm.boundingRect(text).adjusted(-3, -1, 3, 1);
        QRectF at(0, 0, box.width(), box.height());
        at.moveCenter({x, above ? y - box.height() / 2 : y + box.height() / 2});
        at.moveLeft(std::clamp(at.left(), double(r.left()), double(r.right()) - at.width()));
        at.moveTop(std::clamp(at.top(), double(r.top()), double(r.bottom()) - at.height()));
        QColor bg = m_theme.background;
        bg.setAlpha(170);
        p->setPen(Qt::NoPen);
        p->setBrush(bg);
        p->drawRect(at);
        p->setPen(c);
        p->drawText(at, Qt::AlignCenter, text);
    };

    // user markers: dotted line, triangle on the trace, name above it
    for (const MarkerMark &m : m_marks) {
        const double x = xAxis->coordToPixel(m.hz);
        if (x < r.left() || x > r.right())
            continue;
        const QColor c = m.selected ? m_theme.selected : m_theme.marker;
        p->setPen(QPen(c, 1, Qt::DotLine));
        p->drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        double y = r.top() + 14;
        if (!std::isnan(m.dBm)) {
            y = std::clamp(yAxis->coordToPixel(plotValue(m.dBm)), r.top() + 14.0, double(r.bottom()));
            triangle(p, x, y, true, c, true);
        }
        label(m.label, x, y - 13, true, c);
    }

    // peak markers of the live trace, interpolated between points
    for (bool max : {true, false}) {
        if (!(max ? m_view.peakMax : m_view.peakMin))
            continue;
        const QPointF pk = peak(max);
        if (std::isnan(pk.x()))
            continue;
        const double x = xAxis->coordToPixel(pk.x());
        const double y = std::clamp(yAxis->coordToPixel(plotValue(pk.y())), double(r.top()), double(r.bottom()));
        const QColor c = m_theme.trace[0];
        triangle(p, x, y, max, c, false);
        const QString text = QString("%1 %2 %3").arg(max ? "max" : "min",
            formatHz(pk.x(), std::max(1.0, m_trace[0].grid.step() / 100)), levelText(pk.y()));
        label(text, x, max ? y - 13 : y + 13, max, c);
    }

    // readout block, top right
    if (m_view.markerText && !m_marks.isEmpty()) {
        QStringList lines;
        for (const MarkerMark &m : m_marks)
            lines << m.readout;
        const int lh = fm.height();
        int w = 0;
        for (const QString &l : lines)
            w = std::max(w, fm.horizontalAdvance(l));
        const int top = r.top() + 6 + (legend->visible() ? legend->outerRect().height() + 6 : 0);
        const QRect box(r.right() - w - 14, top, w + 10, lh * int(lines.size()) + 6);
        QColor bg = m_theme.background;
        bg.setAlpha(200);
        p->setPen(QPen(m_theme.grid));
        p->setBrush(bg);
        p->drawRect(box);
        for (int i = 0; i < lines.size(); ++i) {
            p->setPen(m_marks[i].selected ? m_theme.selected : m_theme.text);
            p->drawText(box.left() + 5, box.top() + 3 + lh * i + fm.ascent(), lines[i]);
        }
    }
}
