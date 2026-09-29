#pragma once
// The spectrum graph: the one implementation of all graph drawing and mouse
// interaction. It shows traces and emits intents (range requests, image requests);
// it never talks to the device. Promoted from a plain QWidget in mainwindow.ui.
//
// Mouse: hover shows a readout. Frequency strip below the plot: drag the left /
// middle / right third to move start / pan / move stop; wheel zooms. Level strip
// left of the plot: top / middle / bottom third moves top / pans / moves bottom;
// wheel zooms. Wheel in the plot zooms the span about the cursor. Right click: menu.
#include "appsettings.h"
#include "traces.h"
#include "qcustomplot.h"

class SpectrumView : public QCustomPlot {
    Q_OBJECT
public:
    enum class RangeOp { Set, Start, Stop, Center, Zoom };  // Set a..b | a = Hz | Zoom: factor a about b Hz
    static constexpr int kTraces = 4;                       // live, M1, M2, M3

    explicit SpectrumView(QWidget *parent = nullptr);

    void setTheme(const Theme &t);
    void setView(const ViewSettings &v);
    const ViewSettings &view() const { return m_view; }
    void setTrace(int i, const Trace &t);  // already normalised when normalising
    void setShown(int i, bool on);
    void setNormalized(bool on);           // levels are dB relative to the reference
    void setLinear(bool on);
    void setSpan(double startHz, double stopHz);
    void autoScale();                      // once, from the visible traces
    QImage image(const QString &header);   // graph as shown (without hover), header on top

    // scripted tests
    const Trace &trace(int i) const { return m_trace[i]; }
    QPointF pointFor(const QString &zone, double hz, double level) const;  // zone: plot/xaxis/yaxis
    QList<QAction *> menuActions(QPointF pos, QObject *owner);             // the context menu
    QString hoverText() const;

signals:
    void rangeRequested(SpectrumView::RangeOp op, double a, double b);
    void viewChanged();  // levels or options changed by the user
    void saveImageRequested();
    void copyImageRequested();
    void stopRequested();  // Esc

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;  // PgUp/PgDn zoom, Esc stops
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    enum class Zone { None, Plot, XStart, XPan, XStop, YMax, YPan, YMin };
    Zone zoneAt(QPointF p) const;
    void redraw(int i);        // trace i -> its graph (scale, spline)
    void applyLevels();        // y range from the view settings, auto ends from the data
    void storeLevels();        // y range -> view settings
    double plotValue(double dBm) const;
    QString levelText(double dBm) const;
    void updateHover(QPointF p);
    void hideHover();

    Theme m_theme;
    ViewSettings m_view;
    bool m_normalized = false;
    Trace m_trace[kTraces];
    QCPGraph *m_graph[kTraces];
    QCPItemStraightLine *m_hoverLine;
    QCPItemText *m_hoverText;
    Zone m_drag = Zone::None;
    QPointF m_pressPos;
    QCPRange m_pressX, m_pressY;
};

// Axis labels with SI prefixes: 433.92M (Hz axis), 2.5µ (W axis)
class SiTicker : public QCPAxisTicker {
protected:
    double getTickStep(const QCPRange &range) override;
    QString getTickLabel(double tick, const QLocale &locale, QChar formatChar, int precision) override;

private:
    double m_step = 1, m_unit = 1;
    QString m_prefix;
};
