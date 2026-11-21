#pragma once
// Frequency range model, sweep grid and number formatting. Every UI path that
// changes the sweep (edits, axis drags, wheel, keys, context menu) goes through
// FreqRange, so limit/min-span clamping lives in exactly one place.
#include <QString>
#include <QtGlobal>
#include <optional>

class FreqRange {
public:
    FreqRange(double startHz = 0, double stopHz = 0) : m_start(startHz), m_stop(stopHz) {}

    double start() const { return m_start; }
    double stop() const { return m_stop; }
    double center() const { return (m_start + m_stop) / 2; }
    double span() const { return m_stop - m_start; }
    double minHz() const { return m_min; }
    double maxHz() const { return m_max; }

    void setLimits(double minHz, double maxHz, double minSpanHz = 10e3);
    void set(double startHz, double stopHz);
    void setStart(double hz);                  // keeps stop
    void setStop(double hz);                   // keeps start
    void setCenter(double hz);                 // keeps span
    void setSpan(double hz);                   // keeps center
    void zoom(double factor, double aboutHz);  // span *= factor; aboutHz stays in place
    void pan(double deltaHz);
    // log axis (start > 0): the same in ratios. zoomLog: stop/start to the power factor;
    // setCenterLog: hz in the middle of the axis, stop/start kept
    void zoomLog(double factor, double aboutHz);
    void setCenterLog(double hz);

    bool operator==(const FreqRange &o) const { return m_start == o.m_start && m_stop == o.m_stop; }

private:
    void place(double start, double span);  // clamp span, then shift into the limits

    double m_start, m_stop;
    double m_min = 0, m_max = 100e9, m_minSpan = 10e3;
};

// Sweep grid. Firmware scanraw steps by (stop-start)/points (sa_cmd.c cmd_scanraw),
// so the last point is one step short of the requested stop. To get points exactly
// at start..stop we request stop' = start + span*N/(N-1): see scanrawStop().
// The points of a sweep: linear, or logarithmic (startHz > 0). The firmware's `scanraw` only
// sweeps linearly, so an analyzer sweep makes a log grid of `pieces` linear pieces, one
// `scanraw` each: the pieces start at exact log frequencies (piece k at point
// round(k (points - 1) / pieces)), the points in between are linear. pieces = 0 is exact log
// (a tracking sweep measures point by point; a SweepConfig asks for a log sweep with 0 and the
// link chooses). freq() is always what is measured.
struct ScanGrid {
    double startHz = 0, stopHz = 0;
    int points = 0;
    bool log = false;
    int pieces = 0;  // log only: 0 = exact log, else linear pieces (logPieces())

    double step() const;  // linear: the step; log: the smallest step (the first)
    double stepAt(int i) const { return points < 2 ? 0 : i < points - 1 ? freq(i + 1) - freq(i) : freq(i) - freq(i - 1); }
    double freq(int i) const { return freqAt(i); }
    double freqAt(double x) const;  // at a fractional point index (interpolation, peaks)
    double index(double hz) const;  // the fractional point index of hz (freqAt's inverse), unclamped
    int nearest(double hz) const;   // index of the point closest to hz, clamped
    qint64 scanrawStop() const;     // linear grid: the stop that `scanraw` needs for it
    int pieceCount() const { return log && pieces > 0 ? pieces : 1; }
    int pieceStart(int k) const;    // first point of piece k (pieceCount(): points)
    ScanGrid piece(int k) const;    // piece k as a linear grid (the whole grid when linear)
    bool operator==(const ScanGrid &o) const = default;
};

constexpr double kLogMinHz = 10e3;  // a log sweep starts here at the lowest
// Linear pieces of a log sweep: one per octave (the step grows at most 2x from one piece to the
// next), at least 2 points each.
int logPieces(double startHz, double stopHz, int points);

double roundTo(double v, double step);
double niceStep(double minStep);  // smallest {1, 2, 2.5, 5} x 10^n >= minStep

// "433.920 MHz"; decimals < 0 picks enough digits to resolve resolutionHz
QString formatHz(double hz, double resolutionHz = 1);
// MHz for the edits, to the Hz and without trailing zeros: "433.92", "100", "0.0001"
QString formatMHz(double hz);
// Times for the edits: "100 µs", "1.5 ms", "2 s"
QString formatSeconds(double s);
// "100u", "100us", "100 µs", "1.5m", "1.5 ms", "2", "2 s" -> seconds (no prefix: seconds)
std::optional<double> parseSeconds(const QString &text);
// Parses "433.92", "433,92", "433.92M", "1.2 GHz", "100k". Numbers without a
// suffix are in units of defaultUnitHz (the edits take MHz).
std::optional<double> parseHz(QString text, double defaultUnitHz = 1e6);
