#pragma once
// Level units for showing measured values (the tinySA's own UNIT menu: dBm, dBmV, dBuV, V, W,
// plus dBW, mW and mV). Everything inside the app stays dBm; these only convert for display.
// Voltages are across 50 ohm. Normalised traces are dB relative to the reference: a dB unit
// shows them as dB, a power unit as a power ratio, a voltage unit as a voltage ratio.
#include <QString>
#include <QStringList>

enum class LevelUnit { dBm, dBmV, dBuV, dBW, W, mW, V, mV };

QStringList levelUnitNames();                     // "dBm", "dBmV", "dBµV", "dBW", "W", "mW", "V", "mV"
QString levelUnitName(LevelUnit u);
LevelUnit levelUnitFromName(const QString &name);  // unknown: dBm (also "dBuV" for dBµV)
bool isDbUnit(LevelUnit u);                        // logarithmic (dBm, dBmV, dBµV, dBW)
bool isVoltUnit(LevelUnit u);                      // V, mV

// dBm (or dB when normalised) -> the value in u (dB offset, power or voltage, or a ratio)
double levelFromDbm(double dBm, LevelUnit u, bool normalized = false);
// "-30.0 dBm", "17.0 dBmV", "1 µW", "0.001 mW", "7.07 mV", "x 2" (a ratio); 3 significant digits
QString formatLevel(double dBm, LevelUnit u, bool normalized = false);
// The unit's name on the axis and in dialogs: "dBµV", "W", "dB" / "ratio" when normalised.
QString levelAxisUnit(LevelUnit u, bool normalized);
