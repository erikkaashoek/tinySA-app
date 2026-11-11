#pragma once
// Device models. Everything model-specific is one row in the table in model.cpp;
// code asks the descriptor instead of testing the model type.
#include "units.h"
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

enum class InputMode { Low, High };  // tinySA basic: LOW (0-350 MHz) / HIGH (240-960 MHz) input

// Signal generator ("output mode"). Basic: LOW and HIGH output are separate connectors.
enum class GenPath { Low, High };
// The signal: a carrier, a frequency/level sweep or one modulation (the firmware cannot
// sweep and modulate at once). FM is the Ultra's adjustable FM; NFM/WFM/External the basic's;
// Burst keys the output on and off (Ultra firmware with burst, DeviceInfo::hasBurst).
enum class GenType { CW, Sweep, AM, FM, NFM, WFM, External, Burst };

struct GenDesc {
    double minHz, maxHz;            // LOW output; the Ultra's maximum depends on the hardware (genLimits)
    double minDbm, maxDbm;          // LOW output, nominal port level (the unit adds its calibration)
    double highMinHz, highMaxHz;    // HIGH output (basic), 0 = none
    double highMinDbm, highMaxDbm;
    bool hasMixer;                  // `output normal|mixer` (Ultra)
    bool needsSweepGo;              // a sweep only runs after `sweep go` (Ultra)
    bool adjustableModulation;      // `modulation depth|deviation` (Ultra)
    QVector<GenType> types;
    int maxModHz;
    double maxLevelChange;          // dB, LOW output only
};

struct ModelDesc {
    QString name;               // "tinySA", "tinySA Ultra", "tinySA Ultra+"
    QSize lcd;                  // screen size for capture / remote touch
    int defaultZero;            // scanraw dBm offset when the `zero` command is missing
    bool hasLna, hasInputMode;
    double minHz, maxHz;        // LOW input (or the only input)
    double highMinHz, highMaxHz;  // HIGH input, if hasInputMode
    QVector<double> rbwKHz;     // selectable RBW values ("auto" is implicit)
    int maxAtten;               // dB
    GenDesc gen;

    double lowLimit(InputMode m) const { return m == InputMode::High && hasInputMode ? highMinHz : minHz; }
    double highLimit(InputMode m) const { return m == InputMode::High && hasInputMode ? highMaxHz : maxHz; }
};

// Model from the first `info` line ("tinySA v0.3ZS304", "tinySA ULTRA ZS405",
// "tinySA ULTRA+ ZS407"), falling back on the `version` reply ("tinySA4_v1.4-..").
// Returns nullptr for anything that is not a tinySA (e.g. a NanoVNA on 0483:5740).
const ModelDesc *detectModel(const QString &info, const QString &version);

// The unit's generator at connect time, as far as the shell reports it (output on/off,
// level and modulation cannot be read). Frequency and span: DeviceInfo::deviceGrid.
struct UnitGenerator {
    bool active = false;  // in output mode
    bool high = false;    // basic HIGH output
    std::optional<double> extGainDb, sweepS;
};

// What the handshake learned about the connected unit.
struct DeviceInfo {
    const ModelDesc *model = nullptr;
    QString port;
    QString version;     // first `version` line
    QString hardware;    // "ZS407" or the Ultra's "HW Version:" text
    QString info;        // full `info` reply
    QStringList commands;  // from `help`
    int zeroDbm = 0;     // scanraw offset: dBm = raw/32 - zeroDbm
    ScanGrid deviceGrid;  // the unit's own sweep settings at connect time
    int vbatOffset = 0;   // mV, from `vbat_offset`
    bool hasBurst = false;  // `modulation` lists burst (tinySA Ultra firmware from v1.4-305)
    UnitGenerator gen;
    QString title() const;  // "tinySA Ultra+ ZS407 - tinySA4_v1.4-..."
    bool has(const QString &cmd) const { return commands.contains(cmd); }
    // Firmware before v1.4-266 (or a version that does not parse): a command sent right after
    // `output`, `modulation`, `levelchange` or `ext_gain` can hang in output mode, until
    // "Command timeout" (v1.4-252..265) or for good (older; changelog V1.4266, V1.4252).
    bool directCommandRace() const;
};

QStringList parseHelp(const QStringList &lines);   // tokens after every "commands:"
int parseZero(const QStringList &lines, int fallback);  // "...\r\n174dBm" -> 174
