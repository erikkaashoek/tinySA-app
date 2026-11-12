#include "tracking.h"
#include <algorithm>
#include <cmath>

static GenConfig trackerBase()
{
    GenConfig g;
    g.path = GenPath::Low;
    g.mixer = true;
    g.extGainDb = 0;
    g.type = GenType::CW;
    g.levelKnown = true;
    return g;
}

GenLimits trackerLimits(const DeviceInfo &tg)
{
    return genLimits(trackerBase(), tg);
}

TrackConfig clampTrack(TrackConfig t, const DeviceInfo &tg)
{
    const GenLimits l = trackerLimits(tg);
    t.levelDbm = std::clamp(std::round(t.levelDbm * 10) / 10, l.minDbm, l.maxDbm);
    t.settleMs = std::clamp(t.settleMs, -1, 5000);
    return t;
}

SweepConfig clampToTracker(SweepConfig c, const ModelDesc &analyzer, const DeviceInfo &tg)
{
    c = clampConfig(c, analyzer);
    if (!tg.model)
        return c;
    const GenLimits l = trackerLimits(tg);
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

int trackerPath(const DeviceInfo &tg, double hz)
{
    if (!tg.model || !tg.model->gen.hasMixer)
        return 0;
    return hz < 823e6 ? 0 : hz <= 1130e6 ? 1 : 2;  // as outputPathText
}

int trackSettleMs(const TrackConfig &t, bool screenOff, TrackChange change)
{
    if (t.settleMs >= 0)
        return t.settleMs;
    if (!screenOff)
        return change == TrackChange::RfOn ? 300 : 150;
    return change == TrackChange::None ? 10 : 60;
}

GenConfig trackerGen(GenConfig g, const DeviceInfo &tg, double hz, double levelDbm, bool rf)
{
    const GenConfig base = trackerBase();
    g.path = base.path;
    g.mixer = base.mixer;
    g.extGainDb = base.extGainDb;
    g.type = base.type;
    g.levelKnown = true;
    g.freqHz = hz;
    g.levelDbm = levelDbm;
    g.on = rf;
    return clampGen(g, tg);
}
