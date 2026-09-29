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

    bool operator==(const FreqRange &o) const { return m_start == o.m_start && m_stop == o.m_stop; }

private:
    void place(double start, double span);  // clamp span, then shift into the limits

    double m_start, m_stop;
    double m_min = 0, m_max = 100e9, m_minSpan = 10e3;
};

// Sweep grid. Firmware scanraw steps by (stop-start)/points (sa_cmd.c cmd_scanraw),
// so the last point is one step short of the requested stop. To get points exactly
// at start..stop we request stop' = start + span*N/(N-1): see scanrawStop().
struct ScanGrid {
    double startHz = 0, stopHz = 0;
    int points = 0;

    double step() const { return points > 1 ? (stopHz - startHz) / (points - 1) : 0; }
    double freq(int i) const { return startHz + step() * i; }
    int nearest(double hz) const;  // index of the point closest to hz, clamped
    qint64 scanrawStop() const;
    bool operator==(const ScanGrid &o) const = default;
};

double roundTo(double v, double step);
double niceStep(double minStep);  // smallest {1, 2, 2.5, 5} x 10^n >= minStep

// "433.920 MHz"; decimals < 0 picks enough digits to resolve resolutionHz
QString formatHz(double hz, double resolutionHz = 1);
// Parses "433.92", "433,92", "433.92M", "1.2 GHz", "100k". Numbers without a
// suffix are in units of defaultUnitHz (the edits take MHz).
std::optional<double> parseHz(QString text, double defaultUnitHz = 1e6);
