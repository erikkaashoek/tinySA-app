#include "units.h"
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

void FreqRange::setLimits(double minHz, double maxHz, double minSpanHz)
{
    m_min = minHz;
    m_max = std::max(maxHz, minHz);
    m_minSpan = std::min(minSpanHz, m_max - m_min);
    set(m_start, m_stop);
}

void FreqRange::place(double start, double span)
{
    span = std::clamp(span, m_minSpan, m_max - m_min);
    m_start = std::clamp(start, m_min, m_max - span);
    m_stop = m_start + span;
}

void FreqRange::set(double a, double b)
{
    if (b < a)
        std::swap(a, b);
    a = std::clamp(a, m_min, m_max);
    b = std::clamp(b, m_min, m_max);
    if (b - a < m_minSpan)
        place((a + b - m_minSpan) / 2, m_minSpan);
    else
        m_start = a, m_stop = b;
}

void FreqRange::setStart(double hz)
{
    set(std::min(hz, m_stop - m_minSpan), m_stop);
}

void FreqRange::setStop(double hz)
{
    set(m_start, std::max(hz, m_start + m_minSpan));
}

void FreqRange::setCenter(double hz)
{
    place(hz - span() / 2, span());
}

void FreqRange::setSpan(double hz)
{
    const double c = center();
    hz = std::clamp(hz, m_minSpan, m_max - m_min);
    place(c - hz / 2, hz);
}

void FreqRange::zoom(double factor, double aboutHz)
{
    const double s = std::clamp(span() * factor, m_minSpan, m_max - m_min);
    place(aboutHz - (aboutHz - m_start) * s / span(), s);
}

void FreqRange::pan(double deltaHz)
{
    place(m_start + deltaHz, span());
}

int ScanGrid::nearest(double hz) const
{
    if (points < 2 || step() <= 0)
        return 0;
    return std::clamp(int(std::lround((hz - startHz) / step())), 0, points - 1);
}

qint64 ScanGrid::scanrawStop() const
{
    if (points < 2)
        return qint64(std::llround(stopHz));
    return qint64(std::llround(startHz + (stopHz - startHz) * points / (points - 1)));
}

double roundTo(double v, double step)
{
    return step > 0 ? std::round(v / step) * step : v;
}

double niceStep(double minStep)
{
    if (!(minStep > 0))
        return 1;
    const double decade = std::pow(10.0, std::floor(std::log10(minStep)));
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (m * decade >= minStep * (1 - 1e-9))
            return m * decade;
    return 10 * decade;
}

QString formatHz(double hz, double resolutionHz)
{
    const double a = std::abs(hz);
    double mult = 1;
    const char *unit = "Hz";
    if (a >= 1e9) mult = 1e9, unit = "GHz";
    else if (a >= 1e6) mult = 1e6, unit = "MHz";
    else if (a >= 1e3) mult = 1e3, unit = "kHz";
    resolutionHz = std::max(resolutionHz, 1.0);
    const int decimals = std::clamp(int(std::ceil(std::log10(mult / resolutionHz) - 1e-9)), 0, 9);
    return QString("%1 %2").arg(hz / mult, 0, 'f', decimals).arg(unit);
}

QString formatMHz(double hz)
{
    QString s = QString::number(hz / 1e6, 'f', 6);
    while (s.endsWith('0'))
        s.chop(1);
    if (s.endsWith('.'))
        s.chop(1);
    return s;
}

std::optional<double> parseHz(QString text, double defaultUnitHz)
{
    static const QRegularExpression re(
        R"(^\s*([-+]?\d*(?:[.,]\d*)?(?:[eE][-+]?\d+)?)\s*([kKmMgG]?)\s*(?:[hH][zZ])?\s*$)");
    const auto m = re.match(text);
    if (!m.hasMatch() || m.captured(1).isEmpty() || m.captured(1) == "." || m.captured(1) == ",")
        return std::nullopt;
    bool ok = false;
    const double v = QString(m.captured(1)).replace(',', '.').toDouble(&ok);
    if (!ok)
        return std::nullopt;
    const QChar s = m.captured(2).isEmpty() ? QChar() : m.captured(2)[0].toLower();
    if (s == 'k') return v * 1e3;
    if (s == 'm') return v * 1e6;
    if (s == 'g') return v * 1e9;
    // a bare "Hz" suffix means plain hertz
    return v * (text.contains("hz", Qt::CaseInsensitive) ? 1 : defaultUnitHz);
}

QString formatSeconds(double s)
{
    const double a = std::abs(s);
    if (a >= 1 || a == 0)
        return QString::number(s, 'g', 6) + " s";
    if (a >= 1e-3)
        return QString::number(s * 1e3, 'g', 6) + " ms";
    return QString::number(s * 1e6, 'g', 6) + QString::fromUtf8(" µs");
}

std::optional<double> parseSeconds(const QString &text)
{
    static const QRegularExpression re(QString::fromUtf8(
        R"(^\s*([-+]?\d*(?:[.,]\d*)?(?:[eE][-+]?\d+)?)\s*([mun]|\x{00b5}|\x{03bc})?\s*s?\s*$)"));
    const auto m = re.match(text);
    if (!m.hasMatch() || m.captured(1).isEmpty() || m.captured(1) == "." || m.captured(1) == ",")
        return std::nullopt;
    bool ok = false;
    const double v = QString(m.captured(1)).replace(',', '.').toDouble(&ok);
    if (!ok)
        return std::nullopt;
    const QString p = m.captured(2);  // divided: "100u" is exactly 100e-6
    return p == "m" ? v / 1e3 : p == "n" ? v / 1e9 : p.isEmpty() ? v : v / 1e6;  // u, micro sign, mu
}
