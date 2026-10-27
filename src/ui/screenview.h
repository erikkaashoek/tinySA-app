#pragma once
// Shows the tinySA screen (scaled to fit, or 1:1) and turns mouse presses and
// drags on it into touches at LCD pixel coordinates. Promoted in screenwindow.ui.
#include <QImage>
#include <QWidget>

class ScreenView : public QWidget {
    Q_OBJECT
public:
    explicit ScreenView(QWidget *parent = nullptr);
    void setImage(const QImage &img);
    const QImage &image() const { return m_img; }  // as the tinySA sent it
    const QImage &shownImage() const { return m_invert ? m_inverted : m_img; }  // shown, saved, copied
    void setInvert(bool on);  // inverted colours: light background, printer friendly
    void setFit(bool fit);
    QPointF widgetPoint(QPoint lcd) const;  // scripted tests

signals:
    void touched(QPoint lcd, bool down);  // press/drag (true), release (false)

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    QSize sizeHint() const override;

private:
    QRectF target() const;  // where the image is drawn
    QPoint toLcd(QPointF p) const;

    QImage m_img, m_inverted;
    bool m_fit = true, m_down = false, m_invert = false;
};
