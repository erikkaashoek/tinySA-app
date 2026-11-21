#include "tracking.h"
#include <algorithm>
#include <cmath>

static GenConfig trackerBase(const TrackConfig &t)
{
    GenConfig g;
    g.path = GenPath::Low;
    g.mixer = t.mixer;
    g.harmonic = t.harmonic;
    g.extGainDb = 0;
    g.type = GenType::CW;
    g.levelKnown = true;
    return g;
}

GenLimits trackerLimits(const DeviceInfo &tg, const TrackConfig &t)
{
    return genLimits(trackerBase(t), tg);
}

TrackConfig clampTrack(TrackConfig t, const DeviceInfo &tg)
{
    if (tg.model && !tg.model->gen.hasMixer) {  // the basic: one LOW output
        t.mixer = true;
        t.harmonic = false;
    }
    const GenLimits l = trackerLimits(tg, t);
    t.levelDbm = std::clamp(std::round(t.levelDbm * 10) / 10, l.minDbm, l.maxDbm);
    t.settleMs = std::clamp(t.settleMs, -1, 5000);
    return t;
}

SweepConfig clampToTracker(SweepConfig c, const ModelDesc &analyzer, const DeviceInfo &tg, const TrackConfig &t)
{
    c = clampConfig(c, analyzer);
    if (!tg.model)
        return c;
    const GenLimits l = trackerLimits(tg, t);
    const double lo = std::max(analyzer.lowLimit(c.input), l.minHz);
    const double hi = std::min(analyzer.highLimit(c.input), l.maxHz);
    if (hi - lo < 10e3)  // no common range (e.g. the basic's HIGH input): the analyzer's
        return c;
    FreqRange r(c.grid.startHz, c.grid.stopHz);
    r.setLimits(lo, hi);
    c.grid.startHz = r.start();
    c.grid.stopHz = r.stop();
    return c;
}

int trackerPath(const DeviceInfo &tg, const TrackConfig &t, double hz)
{
    if (!tg.model || !tg.model->gen.hasMixer)
        return 0;
    if (hz > ultraFundamentalMaxHz(tg, t.mixer))
        return 3;
    return hz < 823e6 ? 0 : hz <= 1130e6 ? 1 : 2;  // as outputPathText
}

int trackSettleMs(const TrackConfig &t, bool screenOff, TrackChange change)
{
    if (t.settleMs >= 0)
        return t.settleMs;
    if (!screenOff)
        return change == TrackChange::RfOn ? 300 : 150;
    return change == TrackChange::None ? 10 : 32;
}

GenConfig trackerGen(GenConfig g, const DeviceInfo &tg, const TrackConfig &t, double hz, double levelDbm, bool rf)
{
    const GenConfig base = trackerBase(t);
    g.path = base.path;
    g.mixer = base.mixer;
    g.harmonic = base.harmonic;
    g.extGainDb = base.extGainDb;
    g.type = base.type;
    g.levelKnown = true;
    g.freqHz = hz;
    g.levelDbm = levelDbm;
    g.on = rf;
    return clampGen(g, tg);
}
