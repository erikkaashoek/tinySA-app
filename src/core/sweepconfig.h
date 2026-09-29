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
    bool operator==(const SweepConfig &) const = default;
};
Q_DECLARE_METATYPE(SweepConfig)

// Device-side state as far as the link knows it; nullopt = unknown -> resend.
struct AppliedConfig {
    std::optional<InputMode> input;
    std::optional<bool> lna;
    std::optional<int> atten;
    std::optional<double> rbwKHz;
    std::optional<std::pair<qint64, qint64>> sweep;  // device display range
};

// Commands that bring the device from `have` to `want`, in firmware-safe order
// (input mode first: it resets rbw/atten). Updates `have`.
QByteArrayList configCommands(const SweepConfig &want, AppliedConfig &have, const ModelDesc &m);

// Clamp a config to what the model supports (range per input mode, RBW list, atten).
SweepConfig clampConfig(SweepConfig c, const ModelDesc &m);
