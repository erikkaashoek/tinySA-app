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

void FreqRange::zoomLog(double factor, double aboutHz)
{
    if (m_start <= 0 || aboutHz <= 0)
        return zoom(factor, aboutHz);
    const double a = aboutHz * std::pow(m_start / aboutHz, factor), b = aboutHz * std::pow(m_stop / aboutHz, factor);
    if (b - a < m_minSpan)
        return place(std::sqrt(a * b) - m_minSpan / 2, m_minSpan);
    // kept inside the limits as one ratio: shifted (in ratio) rather than squeezed
    const double ratio = std::min(b / a, m_min > 0 ? m_max / m_min : b / a);
    double lo = a;
    if (lo * ratio > m_max)
        lo = m_max / ratio;
    if (lo < m_min)
        lo = m_min;
    set(lo, lo * ratio);
}

void FreqRange::setCenterLog(double hz)
{
    if (m_start <= 0 || hz <= 0)
        return setCenter(hz);
    const double half = std::sqrt(m_stop / m_start);
    double lo = hz / half;
    if (lo * half * half > m_max)
        lo = m_max / (half * half);
    set(std::max(lo, m_min), std::max(lo, m_min) * half * half);
}

static bool isLog(const ScanGrid &g)
{
    return g.log && g.startHz > 0 && g.stopHz > g.startHz && g.points > 1;
}

int logPieces(double startHz, double stopHz, int points)
{
    if (startHz <= 0 || stopHz <= startHz || points < 5)
        return 1;
    const int octaves = int(std::ceil(std::log2(stopHz / startHz) - 1e-9));
    return std::clamp(octaves, 1, (points - 1) / 2);
}

int ScanGrid::pieceStart(int k) const
{
    const int n = pieceCount();
    return k >= n ? points : int(std::lround(double(k) * (points - 1) / n));
}

double ScanGrid::step() const
{
    if (points < 2)
        return 0;
    return isLog(*this) ? freq(1) - freq(0) : (stopHz - startHz) / (points - 1);
}

double ScanGrid::freqAt(double x) const
{
    if (points < 2)
        return startHz;
    const int last = points - 1;
    if (!isLog(*this))
        return startHz + (stopHz - startHz) / last * x;
    auto exact = [&](double i) { return startHz * std::pow(stopHz / startHz, i / last); };
    if (pieces <= 0)
        return exact(x);
    // the piece holding x (the last one past the end), linear between its exact ends
    const int n = pieceCount();
    int k = std::clamp(int(std::floor(x * n / last)), 0, n - 1);
    while (k > 0 && x < pieceStart(k))
        --k;
    while (k < n - 1 && x >= pieceStart(k + 1))
        ++k;
    const int a = pieceStart(k), b = std::min(pieceStart(k + 1), last);
    const double fa = exact(a), fb = exact(b);
    return fa + (fb - fa) * (x - a) / (b - a);
}

double ScanGrid::index(double hz) const
{
    if (points < 2)
        return 0;
    const int last = points - 1;
    if (!isLog(*this))
        return stopHz > startHz ? (hz - startHz) / (stopHz - startHz) * last : 0;
    if (hz <= 0)
        return -1e9;
    const double x = std::log(hz / startHz) / std::log(stopHz / startHz) * last;  // exact log
    if (pieces <= 0)
        return x;
    // the pieces start at exact log frequencies: x lies in the same piece
    const int n = pieceCount();
    int k = std::clamp(int(std::floor(x * n / last)), 0, n - 1);
    while (k > 0 && x < pieceStart(k))
        --k;
    while (k < n - 1 && x >= pieceStart(k + 1))
        ++k;
    const int a = pieceStart(k), b = std::min(pieceStart(k + 1), last);
    const double fa = freqAt(a), fb = freqAt(b);
    return a + (hz - fa) / (fb - fa) * (b - a);
}

int ScanGrid::nearest(double hz) const
{
    if (points < 2 || stopHz <= startHz)
        return 0;
    if (!isLog(*this))
        return std::clamp(int(std::lround(index(hz))), 0, points - 1);
    const int i = std::clamp(int(std::floor(index(hz))), 0, points - 1);  // the closer neighbour
    return i < points - 1 && std::abs(freq(i + 1) - hz) < std::abs(hz - freq(i)) ? i + 1 : i;
}

ScanGrid ScanGrid::piece(int k) const
{
    if (!isLog(*this))
        return {startHz, stopHz, points};
    const int a = pieceStart(k), b = pieceStart(k + 1) - 1;  // its points a..b
    return {freq(a), freq(b), b - a + 1};
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
