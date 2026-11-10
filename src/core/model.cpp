#include "model.h"
#include <QRegularExpression>

// Nominal limits (the firmware clamps further at run time, e.g. Ultra harmonic range).
// Generator levels: sa_core.c level_min()/level_max(); frequencies: the unit's menu texts.
static const GenDesc kBasicGen{
    10e3, 350e6, -76, -7, 240e6, 960e6, -38, 9, false, false, false,
    {GenType::CW, GenType::Sweep, GenType::AM, GenType::NFM, GenType::WFM, GenType::External}, 5000, 70};

static const GenDesc kUltraGen{
    10e3, 5.327e9, -115, -18.5, 0, 0, 0, 0, true, true, true,
    {GenType::CW, GenType::Sweep, GenType::AM, GenType::FM}, 3500, 90};

static const ModelDesc kBasic{
    "tinySA", {320, 240}, 128, false, true,
    0, 350e6, 240e6, 960e6,
    {3, 10, 30, 100, 300, 600}, 30, kBasicGen};

static const ModelDesc kUltra{
    "tinySA Ultra", {480, 320}, 174, true, false,
    0, 9.9e9, 0, 0,
    {0.2, 1, 3, 10, 30, 100, 300, 600, 850}, 30, kUltraGen};

static const ModelDesc kUltraPlus{
    "tinySA Ultra+", {480, 320}, 174, true, false,
    0, 9.9e9, 0, 0,
    {0.2, 1, 3, 10, 30, 100, 300, 600, 850}, 30, kUltraGen};

const ModelDesc *detectModel(const QString &info, const QString &version)
{
    const QString first = info.section('\n', 0, 0).trimmed();
    if (first.contains("tinySA ULTRA+", Qt::CaseInsensitive))
        return &kUltraPlus;
    if (first.contains("tinySA ULTRA", Qt::CaseInsensitive))
        return &kUltra;
    if (first.startsWith("tinySA", Qt::CaseInsensitive))
        return &kBasic;
    if (version.startsWith("tinySA4"))
        return &kUltra;
    if (version.startsWith("tinySA"))
        return &kBasic;
    return nullptr;
}

QString DeviceInfo::title() const
{
    QString t = model ? model->name : QString("unknown");
    if (!hardware.isEmpty() && !t.contains(hardware))
        t += " " + hardware;
    return version.isEmpty() ? t : t + " - " + version;
}

bool DeviceInfo::directCommandRace() const
{
    static const QRegularExpression re(R"(v(\d+)\.(\d+)-(\d+))");  // tinySA4_v1.4-220-gbc09086
    const auto m = re.match(version);
    if (!m.hasMatch())
        return true;
    const int v = m.captured(1).toInt() * 1000000 + m.captured(2).toInt() * 1000 + m.captured(3).toInt();
    return v < 1004266;
}

QStringList parseHelp(const QStringList &lines)
{
    QStringList cmds;
    for (const QString &l : lines) {
        const int at = l.indexOf("commands:");
        if (at >= 0)
            cmds += l.mid(at + 9).split(' ', Qt::SkipEmptyParts);
    }
    return cmds;
}

int parseZero(const QStringList &lines, int fallback)
{
    static const QRegularExpression re(R"(^\s*(-?\d+)\s*dBm\s*$)");
    for (const QString &l : lines) {
        const auto m = re.match(l);
        if (m.hasMatch())
            return m.captured(1).toInt();
    }
    return fallback;
}
