#pragma once
// Declarative device settings: the GUI edits one SweepConfig value; the link keeps
// what it last applied and, before every scan, sends only the difference.
#include "model.h"
#include "units.h"
#include <QByteArrayList>
#include <QMetaType>
#include <optional>

struct SweepConfig {
    ScanGrid grid{88e6, 108e6, 450};
    double rbwKHz = 0;  // 0 = auto
    int atten = -1;     // dB, -1 = auto
    bool lna = false;   // models with hasLna
    InputMode input = InputMode::Low;  // models with hasInputMode
    // external amplifier (+) or attenuator (-): the unit's `ext_gain`, which shifts the levels
    // it reports. One value for analyzer and generator, as on the unit (kept over mode switches
    // and restarts); the session keeps GenConfig::extGainDb equal to it.
    double extGainDb = 0;
    bool operator==(const SweepConfig &) const = default;
};
Q_DECLARE_METATYPE(SweepConfig)

// Device-side state as far as the link knows it; nullopt = unknown -> resend.
struct AppliedConfig {
    std::optional<InputMode> input;
    std::optional<bool> lna;
    std::optional<int> atten;
    std::optional<double> rbwKHz;
    std::optional<double> extGainDb;  // unknown after a mode switch: the generator sets its own
    std::optional<std::pair<qint64, qint64>> sweep;  // device display range
};

// Commands that bring the device from `have` to `want`, in firmware-safe order
// (input mode first, on every model while unknown: it resets rbw/atten). Updates `have`.
QByteArrayList configCommands(const SweepConfig &want, AppliedConfig &have, const ModelDesc &m);

// Clamp a config to what the model supports (range per input mode, RBW list, atten).
SweepConfig clampConfig(SweepConfig c, const ModelDesc &m);
