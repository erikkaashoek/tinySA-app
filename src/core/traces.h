#pragma once
// Trace processing: live sweeps -> (average) -> (min/max hold) -> display trace,
// plus stored memories M1..M3 and the normalisation reference. Partial frames
// update only the points already measured; the rest keep the previous sweep, so
// the trace refreshes like a sweep cursor instead of flashing gaps.
#include "link.h"

enum class Hold { Off, Max, Min };

struct Trace {
    ScanGrid grid;
    QVector<double> dBm;  // NaN = no value
    bool isEmpty() const { return dBm.isEmpty(); }
    double at(double hz) const;  // linear interpolation, NaN outside the grid
};

class TraceSet {
public:
    static constexpr int kMemories = 3;

    void setHold(Hold h);          // restarts the hold
    Hold hold() const { return m_hold; }
    void setAverage(int sweeps);   // 1 = off, else a moving average over this many sweeps
    int average() const { return m_avgN; }
    void restart();                // forget history (new range, user request)

    void add(const SweepFrame &f);
    const Trace &live() const { return m_live; }  // processed, not normalised

    void store(int m);             // copy the live trace into memory m
    void setMemory(int m, const Trace &t);
    const Trace &memory(int m) const { return m_mem[m]; }

    void setNormalize(bool on);    // on: the current live trace becomes the reference
    bool normalized() const { return !m_norm.isEmpty(); }
    Trace normalizedView(const Trace &t) const;  // t minus the reference (dB), if normalising

private:
    Trace m_live;
    Hold m_hold = Hold::Off;
    int m_avgN = 1;
    QVector<double> m_holdVals;
    QList<QVector<double>> m_ring;  // linear power of the last m_avgN-1 complete sweeps
    QVector<double> m_sum;          // sum over m_ring
    Trace m_mem[kMemories];
    Trace m_norm;
};
