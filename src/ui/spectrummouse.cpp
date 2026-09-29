// SpectrumView mouse and wheel handling: hover readout, axis-strip drags, zoom.
#include "spectrumview.h"
#include <cmath>

SpectrumView::Zone SpectrumView::zoneAt(QPointF p) const
{
    const QRect r = axisRect()->rect();
    if (r.contains(p.toPoint()))
        return Zone::Plot;
    if (p.x() >= r.left() && p.x() <= r.right() && p.y() > r.bottom() && p.y() < r.bottom() + 40) {
        const double f = (p.x() - r.left()) / r.width();
        return f < 1.0 / 3 ? Zone::XStart : f > 2.0 / 3 ? Zone::XStop : Zone::XPan;
    }
    if (p.y() >= r.top() && p.y() <= r.bottom() && p.x() < r.left() && p.x() > r.left() - 70) {
        const double f = (p.y() - r.top()) / r.height();
        return f < 1.0 / 3 ? Zone::YMax : f > 2.0 / 3 ? Zone::YMin : Zone::YPan;
    }
    return Zone::None;
}

void SpectrumView::mousePressEvent(QMouseEvent *e)
{
    const Zone z = zoneAt(e->position());
    if (e->button() == Qt::LeftButton && z == Zone::Plot) {  // select / start dragging a marker
        m_dragMarker = markerAt(e->position());
        emit markerSelectRequested(m_dragMarker);
    }
    if (e->button() == Qt::LeftButton && z != Zone::None && z != Zone::Plot) {
        m_drag = z;
        m_pressPos = e->position();
        m_pressX = xAxis->range();
        m_pressY = yAxis->range();
        hideHover();
        return;
    }
    QCustomPlot::mousePressEvent(e);
}

void SpectrumView::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF p = e->position();
    if (m_dragMarker >= 0 && (e->buttons() & Qt::LeftButton)) {
        emit markerMoveRequested(m_dragMarker, xAxis->pixelToCoord(p.x()));
        return;
    }
    if (m_drag != Zone::None) {  // grab and drag: the grabbed value follows the mouse
        const QRect r = axisRect()->rect();
        const double dx = (p.x() - m_pressPos.x()) * m_pressX.size() / r.width();
        const double dy = (p.y() - m_pressPos.y()) * m_pressY.size() / r.height();
        const double minX = m_pressX.size() * 0.01, minY = m_pressY.size() * 0.01;
        const QCPRange &x = m_pressX, &y = m_pressY;
        switch (m_drag) {
        case Zone::XStart: xAxis->setRange(std::min(x.lower - dx, x.upper - minX), x.upper); break;
        case Zone::XStop: xAxis->setRange(x.lower, std::max(x.upper - dx, x.lower + minX)); break;
        case Zone::XPan: xAxis->setRange(x.lower - dx, x.upper - dx); break;
        case Zone::YMax: yAxis->setRange(y.lower, std::max(y.upper + dy, y.lower + minY)); break;
        case Zone::YMin: yAxis->setRange(std::min(y.lower + dy, y.upper - minY), y.upper); break;
        case Zone::YPan: yAxis->setRange(y.lower + dy, y.upper + dy); break;
        default: break;
        }
        replot(rpQueuedReplot);
        return;
    }
    switch (zoneAt(p)) {
    case Zone::XStart: case Zone::XPan: case Zone::XStop: setCursor(Qt::SizeHorCursor); break;
    case Zone::YMax: case Zone::YPan: case Zone::YMin: setCursor(Qt::SizeVerCursor); break;
    case Zone::Plot: setCursor(Qt::CrossCursor); break;
    case Zone::None: unsetCursor(); break;
    }
    if (zoneAt(p) == Zone::Plot)
        updateHover(p);
    else
        hideHover();
    QCustomPlot::mouseMoveEvent(e);
}

void SpectrumView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_dragMarker >= 0) {  // dropped outside the plot: delete
        const int i = std::exchange(m_dragMarker, -1);
        if (!axisRect()->rect().contains(e->position().toPoint()))
            emit markerRemoveRequested(i);
        return;
    }
    const Zone z = m_drag;
    if (z == Zone::None)
        return QCustomPlot::mouseReleaseEvent(e);
    m_drag = Zone::None;
    if (z == Zone::XStart || z == Zone::XPan || z == Zone::XStop) {
        emit rangeRequested(RangeOp::Set, xAxis->range().lower, xAxis->range().upper);
    } else {
        if (z != Zone::YMin)
            m_view.autoMax = false;
        if (z != Zone::YMax)
            m_view.autoMin = false;
        storeLevels();
        emit viewChanged();
    }
}

void SpectrumView::wheelEvent(QWheelEvent *e)
{
    const double steps = e->angleDelta().y() / 120.0;
    const Zone z = zoneAt(e->position());
    if (steps == 0 || z == Zone::None)
        return QCustomPlot::wheelEvent(e);
    if (z == Zone::YMax || z == Zone::YPan || z == Zone::YMin) {
        const double half = yAxis->range().size() * std::pow(1.1, -steps) / 2;
        const double c = yAxis->range().center();
        yAxis->setRange(c - half, c + half);
        m_view.autoMin = m_view.autoMax = false;
        storeLevels();
        replot(rpQueuedReplot);
        emit viewChanged();
    } else {  // plot or frequency strip: zoom the span about the cursor
        emit rangeRequested(RangeOp::Zoom, std::pow(1.2, -steps), xAxis->pixelToCoord(e->position().x()));
    }
    e->accept();
}

void SpectrumView::keyPressEvent(QKeyEvent *e)
{
    const double c = xAxis->range().center();
    switch (e->key()) {
    case Qt::Key_PageUp: emit rangeRequested(RangeOp::Zoom, 1 / 1.5, c); break;
    case Qt::Key_PageDown: emit rangeRequested(RangeOp::Zoom, 1.5, c); break;
    case Qt::Key_Escape: emit stopRequested(); break;
    case Qt::Key_Left: emit markerStepRequested(-1); break;
    case Qt::Key_Right: emit markerStepRequested(1); break;
    case Qt::Key_Delete:
        for (int i = 0; i < m_marks.size(); ++i)
            if (m_marks[i].selected)
                emit markerRemoveRequested(i);
        break;
    default: return QCustomPlot::keyPressEvent(e);
    }
    e->accept();
}

void SpectrumView::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || zoneAt(e->position()) != Zone::Plot)
        return QCustomPlot::mouseDoubleClickEvent(e);
    const int i = markerAt(e->position());
    if (i >= 0)
        emit markerRemoveRequested(i);
    else
        emit markerAddRequested(xAxis->pixelToCoord(e->position().x()));
}

void SpectrumView::leaveEvent(QEvent *e)
{
    hideHover();
    QCustomPlot::leaveEvent(e);
}
