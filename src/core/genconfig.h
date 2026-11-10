#pragma once
// Signal generator settings, declarative like SweepConfig: the GUI edits one GenConfig,
// the link keeps what it last applied (AppliedGen) and sends only the difference, in an
// order that is safe for the firmware (level before `output on`, `level` again after a
// frequency change, never sweep and modulation at once).
#include "model.h"
#include <QByteArrayList>
#include <QMetaType>
#include <optional>

struct GenConfig {
    GenPath path = GenPath::Low;  // basic: LOW / HIGH output connector
    bool mixer = true;            // Ultra: `output mixer` (accurate, the unit's default) / `normal` (cleanest)
    double freqHz = 433.92e6;     // carrier, the centre of a sweep
    double levelDbm = -40;        // at the DUT: port level + extGainDb
    bool levelKnown = true;       // false: taken over from a unit (the level cannot be read back);
                                  // levelDbm is then only where an edit starts from
    double extGainDb = 0;         // external amplifier (+) or attenuator (-) between unit and DUT
    std::optional<bool> on;       // RF output; nullopt = unknown (taken over from a generating unit)
    std::optional<GenType> type;  // nullopt = unknown (set on the unit)
    double spanHz = 10e6;         // Sweep
    double sweepS = 2;            // Sweep time, 0 = as fast as possible
    double levelChangeDb = 0;     // Sweep: level change over the sweep (LOW output)
    int modHz = 1000;             // modulation frequency
    int amDepth = 80;             // Ultra AM, %
    int fmDevHz = 3000;           // Ultra FM deviation
    bool operator==(const GenConfig &) const = default;
};
Q_DECLARE_METATYPE(GenConfig)

// Generator state of the unit as far as the link knows it; nullopt = unknown -> resend.
struct AppliedGen {
    std::optional<GenPath> mode;  // the unit is in this output mode
    std::optional<bool> mixer, on, sweeping;
    std::optional<double> extGainDb, levelDbm, levelChangeDb, sweepS;
    std::optional<std::pair<qint64, qint64>> freq;  // centre, span
    std::optional<QByteArray> modulation;
    std::optional<int> modHz, amDepth, fmDevHz;
};

struct GenLimits {
    double minHz, maxHz;
    double minDbm, maxDbm;  // at the DUT (port level + external gain)
};
GenLimits genLimits(const GenConfig &c, const DeviceInfo &d);  // no model: wide open

// Commands that bring the unit from `have` to `want`. Updates `have`. Unknown wants
// (on, type) send nothing.
QByteArrayList genCommands(const GenConfig &want, AppliedGen &have, const ModelDesc &m);

// A command that may not have reached the unit (no reply): forget what it set, so the next
// diff sends it again (an `output off` must not get lost).
void forgetGenCommand(const QByteArray &cmd, AppliedGen &have);

// Clamp to the connected unit: frequency/level limits of the path, types of the model.
GenConfig clampGen(GenConfig c, const DeviceInfo &d);

// The unit was already generating at connect: take over what it reports (mode, frequency,
// span, external gain, sweep time) and mark RF, level and signal type unknown. `have` then
// matches the result, so nothing is sent until the user changes something.
GenConfig adoptUnitGenerator(GenConfig c, const DeviceInfo &d, AppliedGen *have);

// What the unit's OUTPUT line shows for this setting: "Sine", "Square wave", "Mixer".
QString outputPathText(const GenConfig &c, const DeviceInfo &d);

QString genTypeName(GenType t);  // "CW", "Sweep", "AM", "FM", "NFM", "WFM", "Ext"
std::optional<GenType> genTypeFromName(const QString &name);
