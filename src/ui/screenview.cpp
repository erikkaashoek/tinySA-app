#include "screenview.h"
#include <QMouseEvent>
#include <QPainter>

ScreenView::ScreenView(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(160, 120);
    setCursor(Qt::PointingHandCursor);
}

void ScreenView::setImage(const QImage &img)
{
    const bool resized = img.size() != m_img.size();
    m_img = img;
    if (resized)
        updateGeometry();
    update();
}

void ScreenView::setFit(bool fit)
{
    m_fit = fit;
    updateGeometry();
    update();
}

QSize ScreenView::sizeHint() const
{
    return m_img.isNull() ? QSize(480, 320) : m_img.size();
}

QRectF ScreenView::target() const
{
    if (m_img.isNull())
        return rect();
    QSizeF s = m_img.size();
    if (m_fit)
        s.scale(size(), Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2), s);
}

QPoint ScreenView::toLcd(QPointF p) const
{
    const QRectF t = target();
    const double x = (p.x() - t.left()) * m_img.width() / t.width();
    const double y = (p.y() - t.top()) * m_img.height() / t.height();
    return {std::clamp(int(x), 0, m_img.width() - 1), std::clamp(int(y), 0, m_img.height() - 1)};
}

QPointF ScreenView::widgetPoint(QPoint lcd) const
{
    const QRectF t = target();
    return {t.left() + (lcd.x() + 0.5) * t.width() / m_img.width(),
            t.top() + (lcd.y() + 0.5) * t.height() / m_img.height()};
}

void ScreenView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (m_img.isNull()) {
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignCenter, "No screen captured yet");
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(target(), m_img);
}

void ScreenView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_img.isNull() || !target().contains(e->position()))
        return;
    m_down = true;
    emit touched(toLcd(e->position()), true);
}

void ScreenView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_down)
        emit touched(toLcd(e->position()), true);
}

void ScreenView::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_down || e->button() != Qt::LeftButton)
        return;
    m_down = false;
    emit touched(toLcd(e->position()), false);
}
