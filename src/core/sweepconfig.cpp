#include "sweepconfig.h"
#include <algorithm>
#include <cmath>

QByteArrayList configCommands(const SweepConfig &want, AppliedConfig &have, const ModelDesc &m)
{
    QByteArrayList cmds;
    // Unknown mode (after connecting or generating): always set it, the Ultra too. A unit
    // left in output mode answers scanraw with junk, and hangs in it with modulation on.
    const InputMode input = m.hasInputMode ? want.input : InputMode::Low;
    if (have.input != input) {
        cmds << (input == InputMode::High ? "mode high input" : "mode low input");
        const std::optional<double> extGain = have.extGainDb;  // kept by the unit (back from the
        have = AppliedConfig{input};  // generator the link forgets it); the rest is reloaded
        have.extGainDb = extGain;
    }
    if (m.hasLna && have.lna != want.lna) {
        cmds << (want.lna ? "lna on" : "lna off");
        have.lna = want.lna;
    }
    if (have.atten != want.atten) {
        cmds << (want.atten < 0 ? QByteArray("attenuate auto")
                                : "attenuate " + QByteArray::number(want.atten));
        have.atten = want.atten;
    }
    if (have.rbwKHz != want.rbwKHz) {
        cmds << (want.rbwKHz <= 0 ? QByteArray("rbw auto")
                                  : "rbw " + QByteArray::number(want.rbwKHz));
        have.rbwKHz = want.rbwKHz;
    }
    if (have.extGainDb != want.extGainDb) {
        cmds << "ext_gain " + QByteArray::number(want.extGainDb, 'g', 10);
        have.extGainDb = want.extGainDb;
    }
    const std::pair<qint64, qint64> sweep{std::llround(want.grid.startHz),
                                          std::llround(want.grid.stopHz)};
    if (have.sweep != sweep) {
        cmds << "sweep start " + QByteArray::number(sweep.first)
             << "sweep stop " + QByteArray::number(sweep.second);
        have.sweep = sweep;
    }
    return cmds;
}

SweepConfig clampConfig(SweepConfig c, const ModelDesc &m)
{
    if (!m.hasInputMode)
        c.input = InputMode::Low;
    if (!m.hasLna)
        c.lna = false;
    FreqRange r(c.grid.startHz, c.grid.stopHz);
    r.setLimits(m.lowLimit(c.input), m.highLimit(c.input));
    c.grid.startHz = r.start();
    c.grid.stopHz = r.stop();
    c.grid.points = std::clamp(c.grid.points, 2, 30000);
    if (c.rbwKHz > 0 && !m.rbwKHz.contains(c.rbwKHz)) {
        auto best = std::min_element(m.rbwKHz.begin(), m.rbwKHz.end(), [&](double a, double b) {
            return std::abs(std::log(a / c.rbwKHz)) < std::abs(std::log(b / c.rbwKHz));
        });
        c.rbwKHz = best == m.rbwKHz.end() ? 0 : *best;
    }
    c.atten = std::clamp(c.atten, -1, m.maxAtten);
    c.extGainDb = std::clamp(std::round(c.extGainDb * 10) / 10, -100.0, 100.0);  // `ext_gain` -100.0..+100.0
    return c;
}
