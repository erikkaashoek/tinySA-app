#include "levelunit.h"
#include <cmath>

static const char *const kNames[] = {"dBm", "dBmV", "dBµV", "dBW", "W", "mW", "V", "mV"};

QStringList levelUnitNames()
{
    QStringList l;
    for (const char *n : kNames)
        l << QString::fromUtf8(n);
    return l;
}

QString levelUnitName(LevelUnit u)
{
    return QString::fromUtf8(kNames[int(u)]);
}

LevelUnit levelUnitFromName(const QString &name)
{
    if (name == "dBuV")
        return LevelUnit::dBuV;
    const int i = levelUnitNames().indexOf(name);
    return i < 0 ? LevelUnit::dBm : LevelUnit(i);
}

bool isDbUnit(LevelUnit u)
{
    return u == LevelUnit::dBm || u == LevelUnit::dBmV || u == LevelUnit::dBuV || u == LevelUnit::dBW;
}

bool isVoltUnit(LevelUnit u)
{
    return u == LevelUnit::V || u == LevelUnit::mV;
}

// 1 mW in 50 ohm: sqrt(1e-3 * 50) = 0.2236 V = 46.99 dBmV = 106.99 dBµV
static double dbOffset(LevelUnit u)
{
    switch (u) {
    case LevelUnit::dBmV: return 10 * std::log10(50e3);   // 20 log10(sqrt(50 * 1e-3) / 1e-3)
    case LevelUnit::dBuV: return 10 * std::log10(50e9);   // 20 log10(sqrt(50 * 1e-3) / 1e-6)
    case LevelUnit::dBW: return -30;
    default: return 0;
    }
}

double levelFromDbm(double dBm, LevelUnit u, bool normalized)
{
    if (isDbUnit(u))
        return normalized ? dBm : dBm + dbOffset(u);
    if (normalized)  // a ratio: power or voltage
        return std::pow(10.0, dBm / (isVoltUnit(u) ? 20 : 10));
    const double w = std::pow(10.0, (dBm - 30) / 10);
    switch (u) {
    case LevelUnit::mW: return w * 1e3;
    case LevelUnit::V: return std::sqrt(w * 50);
    case LevelUnit::mV: return std::sqrt(w * 50) * 1e3;
    default: return w;
    }
}

QString levelAxisUnit(LevelUnit u, bool normalized)
{
    if (normalized)
        return isDbUnit(u) ? "dB" : isVoltUnit(u) ? "voltage ratio" : "power ratio";
    return levelUnitName(u);
}

QString formatLevel(double dBm, LevelUnit u, bool normalized)
{
    if (std::isnan(dBm))
        return "-";
    const double v = levelFromDbm(dBm, u, normalized);
    if (isDbUnit(u))
        return QString("%1 %2").arg(v, 0, 'f', 1).arg(levelAxisUnit(u, normalized));
    if (normalized)
        return QString("x %1").arg(v, 0, 'g', 3);
    if (u == LevelUnit::mW || u == LevelUnit::mV)  // fixed unit, three significant digits
        return QString("%1 %2").arg(v, 0, 'g', 3).arg(levelUnitName(u));
    static const struct { double unit; const char *prefix; } kPrefixes[] = {
        {1, ""}, {1e-3, "m"}, {1e-6, "µ"}, {1e-9, "n"}, {1e-12, "p"}, {1e-15, "f"}, {1e-18, "a"}};
    for (const auto &p : kPrefixes)
        if (v >= p.unit || p.unit == 1e-18)
            return QString("%1 %2%3").arg(v / p.unit, 0, 'g', 3).arg(QString::fromUtf8(p.prefix), levelUnitName(u));
    return {};
}
