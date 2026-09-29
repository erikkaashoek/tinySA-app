#include "markers.h"
#include <cmath>

QPointF interpolatePeak(const Trace &t, int k)
{
    const int n = t.dBm.size();
    const double f = t.grid.freq(k), l0 = t.dBm[k];
    if (k <= 0 || k >= n - 1 || std::isnan(t.dBm[k - 1]) || std::isnan(t.dBm[k + 1]))
        return {f, l0};
    const double lm = t.dBm[k - 1], lp = t.dBm[k + 1], den = lm - 2 * l0 + lp;
    if (std::abs(den) < 1e-12)
        return {f, l0};
    const double d = std::clamp(0.5 * (lm - lp) / den, -0.5, 0.5);  // vertex offset in points
    return {f + d * t.grid.step(), l0 - 0.25 * (lm - lp) * d};
}

QPointF tracePeak(const Trace &t, bool max)
{
    int best = -1;
    for (int i = 0; i < t.dBm.size(); ++i)
        if (!std::isnan(t.dBm[i]) && (best < 0 || (max ? t.dBm[i] > t.dBm[best] : t.dBm[i] < t.dBm[best])))
            best = i;
    return best < 0 ? QPointF(qQNaN(), qQNaN()) : interpolatePeak(t, best);
}

int MarkerSet::add(double hz)
{
    m_list.append({hz, false});
    return m_selected = int(m_list.size()) - 1;
}

void MarkerSet::remove(int i)
{
    if (i < 0 || i >= count())
        return;
    m_list.removeAt(i);
    if (m_selected == i)
        m_selected = -1;
    else if (m_selected > i)
        --m_selected;  // the same marker, one place up
}

void MarkerSet::clear()
{
    m_list.clear();
    m_selected = -1;
}

void MarkerSet::move(int i, double hz)
{
    if (i >= 0 && i < count())
        m_list[i].hz = hz;
}

void MarkerSet::setDelta(int i, bool on)
{
    if (i >= 0 && i < count())
        m_list[i].delta = on;
}

void MarkerSet::select(int i)
{
    m_selected = i >= 0 && i < count() ? i : -1;
}

int MarkerSet::reference() const
{
    for (int i = 0; i < count(); ++i)
        if (!m_list[i].delta)
            return i;
    return -1;
}

int MarkerSet::near(double hz, double tolHz) const
{
    int best = -1;
    for (int i = 0; i < count(); ++i)
        if (std::abs(m_list[i].hz - hz) <= tolHz
            && (best < 0 || std::abs(m_list[i].hz - hz) < std::abs(m_list[best].hz - hz)))
            best = i;
    return best;
}

MarkerReading MarkerSet::read(int i, const Trace &t) const
{
    MarkerReading r;
    const Marker &m = m_list[i];
    r.name = (m.delta ? "D" : "") + QString::number(i + 1);
    r.hz = m.hz;
    if (!t.isEmpty() && m.hz >= t.grid.startHz && m.hz <= t.grid.stopHz) {
        const int k = t.grid.nearest(m.hz);
        r.hz = t.grid.freq(k);
        r.dBm = t.dBm[k];
    }
    const int ref = reference();
    if (m.delta && ref >= 0 && ref != i) {
        const MarkerReading base = read(ref, t);
        r.dHz = r.hz - base.hz;
        r.dDb = r.dBm - base.dBm;
    }
    return r;
}

QString MarkerSet::serialize() const
{
    QStringList parts;
    for (const Marker &m : m_list)
        parts << (m.delta ? "D:" : "") + QString::number(m.hz, 'f', 0);
    return parts.join(';');
}

void MarkerSet::deserialize(const QString &s)
{
    clear();
    for (const QString &p : s.split(';', Qt::SkipEmptyParts)) {
        const bool delta = p.startsWith("D:");
        bool ok = false;
        const double hz = (delta ? p.mid(2) : p).toDouble(&ok);
        if (ok)
            m_list.append({hz, delta});
    }
}
