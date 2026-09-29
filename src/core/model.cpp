#include "model.h"
#include <QRegularExpression>

// Nominal limits (the firmware clamps further at run time, e.g. Ultra harmonic range).
static const ModelDesc kBasic{
    "tinySA", {320, 240}, 128, false, true,
    0, 350e6, 240e6, 960e6,
    {3, 10, 30, 100, 300, 600}, 30, true};

static const ModelDesc kUltra{
    "tinySA Ultra", {480, 320}, 174, true, false,
    0, 9.9e9, 0, 0,
    {0.2, 1, 3, 10, 30, 100, 300, 600, 850}, 30, false};

static const ModelDesc kUltraPlus{
    "tinySA Ultra+", {480, 320}, 174, true, false,
    0, 9.9e9, 0, 0,
    {0.2, 1, 3, 10, 30, 100, 300, 600, 850}, 30, false};

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
