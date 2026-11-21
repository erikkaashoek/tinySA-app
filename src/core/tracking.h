#pragma once
// Tracking generator: a second tinySA whose output follows the analyzer, point by point. For
// every point the generator unit gets `sweep cw f`, then (after the settle time) the analyzer
// measures that frequency alone (`scanraw f f 1`). Both screens are off where the firmware has
// `ui`, so neither unit waits for screen redraws (DeviceLink, linktrack.cpp).
#include "genconfig.h"
#include "sweepconfig.h"

struct TrackConfig {
    bool on = false;        // sweep with the tracking generator, when one is connected
    double levelDbm = -30;  // its output level
    int settleMs = -1;      // wait between tuning and measuring; -1 = auto (trackSettleMs)
    bool mixer = true;      // Ultra output mode, as GenConfig: accurate (mixer) / cleanest path,
    bool harmonic = false;  //   "Ultra" also above the path's maximum (ultraOutputs())
    // The generator at the analyzer's frequency + offsetHz (+ or -): a mixer under test with an
    // external LO converts it to the analyzer's frequency (RF = IF + offset). With an offset the
    // generator's frequency error is not measured (the analyzer does not see the generator).
    double offsetHz = 0;
    bool operator==(const TrackConfig &) const = default;
};
Q_DECLARE_METATYPE(TrackConfig)

// Tracking sweeps measure single points (`scanraw f f 1`). With RBW auto the firmware measures
// one point with a wide RBW that reads 20 to 40 dB low around 3 GHz (ZS405, v1.4-316; fine
// with any fixed RBW), so RBW auto means this while tracking.
constexpr double kTrackAutoRbwKHz = 30;

// The generator unit's frequencies and levels as tracking generator: LOW output, on the
// Ultra the output mode of t. No model: wide open.
GenLimits trackerLimits(const DeviceInfo &tg, const TrackConfig &t);
TrackConfig clampTrack(TrackConfig t, const DeviceInfo &tg);

// The analyzer's sweep, clamped to the analyzer and to the generator unit's frequencies (minus
// the offset: the generator sweeps start + offset .. stop + offset).
SweepConfig clampToTracker(SweepConfig c, const ModelDesc &analyzer, const DeviceInfo &tg, const TrackConfig &t);

// The generator's frequency for the analyzer's hz (+ offset).
inline double trackerFreq(const TrackConfig &t, double hz) { return hz + t.offsetHz; }

// The generator unit's output path at hz: the Ultra makes a sine below 823 MHz, a square wave
// up to 1.13 GHz, the chosen path above (0, 1, 2) and the LO's harmonic above that path's
// maximum (3); the basic's LOW output is one path (0).
int trackerPath(const DeviceInfo &tg, const TrackConfig &t, double hz);

// What changed on the generator unit since the last point.
enum class TrackChange { None, Path, RfOn };  // a step / into another output path / RF just on

// Auto settle after the generator's prompt (ZS407, v1.4-316..342): screen off (`ui`) 10 ms. A
// step into another output path or RF just on is first waited for with a bare `sweep`, which
// the unit answers once its sweep thread has set the output (DeviceLink::tune, sync): 29 ms
// across 1.13 GHz on the mixer path, 180-190 ms into the cleanest path's leakage output (the
// Si4468 is reset, SI4463_init_rx); a fixed 32 ms lost that point. Screen on: 150 ms, the unit
// redraws its menu first; 300 ms with RF just on.
int trackSettleMs(const TrackConfig &t, bool screenOff, TrackChange change = TrackChange::None);

// What the generator unit makes as tracking generator: a carrier at hz, levelDbm, RF on/off,
// in the output mode of t.
GenConfig trackerGen(GenConfig g, const DeviceInfo &tg, const TrackConfig &t, double hz, double levelDbm, bool rf);
