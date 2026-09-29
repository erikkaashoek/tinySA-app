#pragma once
// Device models. Everything model-specific is one row in the table in model.cpp;
// code asks the descriptor instead of testing the model type.
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

enum class InputMode { Low, High };  // tinySA basic: LOW (0-350 MHz) / HIGH (240-960 MHz) input

struct ModelDesc {
    QString name;               // "tinySA", "tinySA Ultra", "tinySA Ultra+"
    QSize lcd;                  // screen size for capture / remote touch
    int defaultZero;            // scanraw dBm offset when the `zero` command is missing
    bool hasLna, hasInputMode;
    double minHz, maxHz;        // LOW input (or the only input)
    double highMinHz, highMaxHz;  // HIGH input, if hasInputMode
    QVector<double> rbwKHz;     // selectable RBW values ("auto" is implicit)
    int maxAtten;               // dB

    double lowLimit(InputMode m) const { return m == InputMode::High && hasInputMode ? highMinHz : minHz; }
    double highLimit(InputMode m) const { return m == InputMode::High && hasInputMode ? highMaxHz : maxHz; }
};

// Model from the first `info` line ("tinySA v0.3ZS304", "tinySA ULTRA ZS405",
// "tinySA ULTRA+ ZS407"), falling back on the `version` reply ("tinySA4_v1.4-..").
// Returns nullptr for anything that is not a tinySA (e.g. a NanoVNA on 0483:5740).
const ModelDesc *detectModel(const QString &info, const QString &version);

// What the handshake learned about the connected unit.
struct DeviceInfo {
    const ModelDesc *model = nullptr;
    QString port;
    QString version;     // first `version` line
    QString hardware;    // "ZS407" or the Ultra's "HW Version:" text
    QString info;        // full `info` reply
    QStringList commands;  // from `help`
    int zeroDbm = 0;     // scanraw offset: dBm = raw/32 - zeroDbm
    QString title() const;  // "tinySA Ultra+ ZS407 - tinySA4_v1.4-..."
    bool has(const QString &cmd) const { return commands.contains(cmd); }
};

QStringList parseHelp(const QStringList &lines);   // tokens after every "commands:"
int parseZero(const QStringList &lines, int fallback);  // "...\r\n174dBm" -> 174
