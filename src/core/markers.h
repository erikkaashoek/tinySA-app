#pragma once
// Markers: user markers (normal or delta) and peak finding. Pure logic, no drawing.
// User markers read the level at the nearest sweep point; peak markers are
// parabolic-interpolated between points (never snapped to a bin).
#include "traces.h"
#include <QList>
#include <QPointF>

// Strongest (max) or weakest (min) point of a trace, interpolated with the
// parabola through it and its neighbours: (Hz, dBm). NaN point if the trace is empty.
QPointF tracePeak(const Trace &t, bool max);
QPointF interpolatePeak(const Trace &t, int index);

struct Marker {
    double hz = 0;
    bool delta = false;  // relative to the reference (first normal) marker
};

struct MarkerReading {
    QString name;      // "1", "D2"
    double hz = 0;     // at the nearest sweep point
    double dBm = qQNaN();
    double dHz = 0, dDb = qQNaN();  // delta markers: difference to the reference
};

class MarkerSet {
public:
    const QList<Marker> &all() const { return m_list; }
    int count() const { return int(m_list.size()); }
    int add(double hz);  // selects and returns the new marker
    void remove(int i);
    void clear();
    void move(int i, double hz);
    void setDelta(int i, bool on);
    int selected() const { return m_selected; }  // -1 = none
    void select(int i);
    int reference() const;  // first normal marker, -1 if none
    int near(double hz, double tolHz) const;  // marker closest to hz within tol, else -1
    MarkerReading read(int i, const Trace &t) const;

    QString serialize() const;           // "100000000;D:433920000" for the settings file
    void deserialize(const QString &s);

private:
    QList<Marker> m_list;
    int m_selected = -1;
};
