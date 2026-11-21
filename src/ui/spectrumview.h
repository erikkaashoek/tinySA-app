#pragma once
// The spectrum graph: the one implementation of all graph drawing and mouse
// interaction. It shows traces and emits intents (range requests, image requests);
// it never talks to the device. Promoted from a plain QWidget in mainwindow.ui.
//
// Mouse: hover shows a readout (top left, else bottom left or top centre: the first
// place clear of the marker labels and the readout block). Frequency strip below the
// plot: drag the left / middle / right third to move start / pan / move stop; wheel
// zooms. Level strip left of the plot: top / middle / bottom third moves top / pans /
// moves bottom; wheel zooms. Wheel in the plot zooms the span about the cursor.
// Right click: menu.
// Markers: click selects, drag moves (drop outside the plot deletes), double click
// adds or deletes, Left/Right step the selected marker, Del deletes it.
#include "appsettings.h"
#include "traces.h"
#include "qcustomplot.h"
#include <cmath>

// A user marker as the controller wants it drawn.
struct MarkerMark {
    double hz = 0, dBm = qQNaN();  // dBm NaN: vertical line only (no trace value)
    QString label;                 // "1", "D2"
    QString readout;               // line for the on-graph readout block
    bool selected = false;
};

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
    void setUnit(LevelUnit u);             // the levels' unit: axis, readouts
    void setExtGain(double db);            // named on the level axis while not 0
    void setSpan(double startHz, double stopHz, bool log = false);  // log: logarithmic axis
    bool logFrequency() const { return m_logX; }
    void setMarkers(const QList<MarkerMark> &marks);
    void autoScale();                      // once, from the visible traces
    QImage image(const QString &header);   // graph as shown (without hover), header on top

    // scripted tests
    const Trace &trace(int i) const { return m_trace[i]; }
    QPointF pointFor(const QString &zone, double hz, double level) const;  // zone: plot/xaxis/yaxis
    QList<QAction *> menuActions(QPointF pos, QObject *owner);             // the context menu
    QString hoverText() const;
    QPointF hoverPlace() const { return m_hoverText->position->coords(); }  // axis rect ratio
    bool hoverCoversMark() const;  // the hover readout overlaps a marker label or the readout block
    QPointF peak(bool max) const;  // peak marker (Hz, dBm) of the shown live trace

signals:
    void rangeRequested(SpectrumView::RangeOp op, double a, double b);
    void viewChanged();  // levels or options changed by the user
    void saveImageRequested();
    void copyImageRequested();
    void stopRequested();  // Esc
    void markerAddRequested(double hz);
    void markerRemoveRequested(int i);
    void markerMoveRequested(int i, double hz);
    void markerSelectRequested(int i);  // -1 = none
    void markerDeltaRequested(int i);   // toggle normal/delta
    void markerStepRequested(int points);
    void markerPeakRequested();         // selected marker (or a new one) to the max peak

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;  // PgUp/PgDn zoom, Esc stops, markers
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    friend class MarkerOverlay;
    enum class Zone { None, Plot, XStart, XPan, XStop, YMax, YPan, YMin };
    Zone zoneAt(QPointF p) const;
    void redraw(int i);        // trace i -> its graph (scale, spline)
    void applyLevels();        // y range from the view settings, auto ends from the data
    void storeLevels();        // y range -> view settings
    double plotValue(double dBm) const;
    double axisLevel(double kept) const;  // a graph level as kept in m_view -> the axis value
    double keptLevel(double axis) const;  // and back
    // the frequency axis as drawn: linear, or ln(Hz) on the log axis
    double toAxis(double hz) const { return m_logX ? std::log(hz) : hz; }
    double fromAxis(double u) const { return m_logX ? std::exp(u) : u; }
    QString levelText(double dBm) const;
    void updateHover(QPointF p);
    void hideHover();
    int markerAt(QPointF p) const;  // user marker under the mouse, -1 if none
    // what drawMarkers() paints: a triangle at tip (none for UserLine), a label in box
    struct Mark { QString text; QRectF box; QColor colour; QPointF tip; enum { User, UserLine, Max, Min } kind; };
    QVector<Mark> markLayout() const;
    QRect readoutRect() const;                    // marker readout block, empty when not shown
    bool marksIntersect(const QRectF &box) const;  // box overlaps a mark's label or the readout block
    void placeHover();                            // after the layout of every replot
    void drawMarkers(QCPPainter *p);

    Theme m_theme;
    ViewSettings m_view;
    bool m_normalized = false;
    bool m_logX = false;  // logarithmic frequency axis
    double m_extGainDb = 0;
    Trace m_trace[kTraces];
    QCPGraph *m_graph[kTraces];
    QCPItemStraightLine *m_hoverLine;
    QCPItemText *m_hoverText;
    Zone m_drag = Zone::None;
    QPointF m_pressPos;
    QCPRange m_pressX, m_pressY;
    QList<MarkerMark> m_marks;
    int m_dragMarker = -1;
    QCPLayerable *m_overlay;
};

// Axis labels with SI prefixes: 433.92M (Hz axis), 2.5µ (W axis)
class SiTicker : public QCPAxisTicker {
protected:
    double getTickStep(const QCPRange &range) override;
    QString getTickLabel(double tick, const QLocale &locale, QChar formatChar, int precision) override;

    double m_step = 1, m_unit = 1;
    QString m_prefix;
};

// The log frequency axis: ticks at 1, 2, 5 x 10^n (up to 3 decades) or 10^n, each labelled
// with its own prefix (10k, 1M, 2G); spans under half a decade get linear ticks.
class LogSiTicker : public SiTicker {
protected:
    double getTickStep(const QCPRange &range) override;
    int getSubTickCount(double tickStep) override;
    QString getTickLabel(double tick, const QLocale &locale, QChar formatChar, int precision) override;
    QVector<double> createTickVector(double tickStep, const QCPRange &range) override;

private:
    bool m_linear = false;
};
