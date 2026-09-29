#include "traces.h"
#include <cmath>

static double toLin(double dBm) { return std::pow(10.0, dBm / 10); }
static double toDb(double p) { return 10 * std::log10(p); }

double Trace::at(double hz) const
{
    const int n = dBm.size();
    if (n == 0 || hz < grid.startHz || hz > grid.stopHz)
        return qQNaN();
    if (n == 1)
        return dBm[0];
    const double x = (hz - grid.startHz) / grid.step();
    const int i = std::clamp(int(x), 0, n - 2);
    const double t = x - i;
    return dBm[i] * (1 - t) + dBm[i + 1] * t;
}

void TraceSet::setHold(Hold h)
{
    m_hold = h;
    m_holdVals.clear();
}

void TraceSet::setAverage(int sweeps)
{
    m_avgN = std::max(1, sweeps);
    m_ring.clear();
    m_sum.clear();
}

void TraceSet::restart()
{
    m_live = Trace{};
    m_holdVals.clear();
    m_ring.clear();
    m_sum.clear();
}

void TraceSet::add(const SweepFrame &f)
{
    const int n = f.grid.points;
    if (!(f.grid == m_live.grid) || m_live.dBm.size() != n) {
        restart();
        m_live = {f.grid, QVector<double>(n, qQNaN())};
    }
    if (m_sum.size() != n)
        m_sum = QVector<double>(n, 0.0);
    if (m_hold != Hold::Off && m_holdVals.size() != n)
        m_holdVals = QVector<double>(n, qQNaN());

    const int count = std::min(f.received, n);
    const double k = 1.0 / (m_ring.size() + 1);
    for (int i = 0; i < count; ++i) {
        double v = f.dBm[i];
        if (m_avgN > 1)
            v = toDb((m_sum[i] + toLin(v)) * k);
        if (m_hold != Hold::Off) {
            double &h = m_holdVals[i];
            h = std::isnan(h) ? v : m_hold == Hold::Max ? std::max(h, v) : std::min(h, v);
            v = h;
        }
        m_live.dBm[i] = v;
    }
    if (f.complete && m_avgN > 1) {
        QVector<double> p(n);
        for (int i = 0; i < n; ++i)
            m_sum[i] += p[i] = toLin(f.dBm[i]);
        m_ring.append(p);
        if (m_ring.size() > m_avgN - 1) {
            const QVector<double> &old = m_ring.first();
            for (int i = 0; i < n; ++i)
                m_sum[i] -= old[i];
            m_ring.removeFirst();
        }
    }
}

void TraceSet::store(int m)
{
    m_mem[m] = m_live;
}

void TraceSet::setMemory(int m, const Trace &t)
{
    m_mem[m] = t;
}

void TraceSet::setNormalize(bool on)
{
    m_norm = on ? m_live : Trace{};
}

Trace TraceSet::normalizedView(const Trace &t) const
{
    if (m_norm.isEmpty() || t.isEmpty())
        return t;
    Trace out = t;
    const bool same = t.grid == m_norm.grid;
    for (int i = 0; i < t.dBm.size(); ++i)  // reference interpolated after a range change
        out.dBm[i] -= same ? m_norm.dBm[i] : m_norm.at(t.grid.freq(i));
    return out;
}
