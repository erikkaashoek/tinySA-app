#include "genconfig.h"
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

// The Ultra's output paths (sa_core.c update_min_max_freq, the unit's OUTPUT menu): the mixer
// path ("highest accuracy") reaches the LO maximum plus the first IF, the direct PLL path
// ("cleanest signal") the LO maximum. Above that the unit uses LO harmonics, not offered here.
static double ultraMaxHz(const QString &hw, bool mixer)
{
    static const QRegularExpression zs(R"(ZS(\d{3}))");
    const auto m = zs.match(hw);
    const int id = m.hasMatch() ? m.captured(1).toInt() : 0;
    if (id >= 407 || hw.contains("max2871", Qt::CaseInsensitive))  // MAX2871 synthesizer
        return mixer ? 7.37e9 : 6.3e9;
    if (!mixer)
        return 4.35e9;  // ADF4351
    return id == 406 || hw.contains("V0.4.6") ? 5.42e9 : 5.327e9;
}

GenLimits genLimits(const GenConfig &c, const DeviceInfo &d)
{
    GenLimits l{0, 100e9, -200, 50};  // not connected: anything, clamped on connect
    if (const ModelDesc *m = d.model) {
        const GenDesc &g = m->gen;
        if (c.path == GenPath::High && g.highMaxHz > 0)
            l = {g.highMinHz, g.highMaxHz, g.highMinDbm, g.highMaxDbm};
        else
            l = {g.minHz, g.hasMixer ? ultraMaxHz(d.hardware, c.mixer) : g.maxHz, g.minDbm, g.maxDbm};
    }
    l.minDbm += c.extGainDb;
    l.maxDbm += c.extGainDb;
    return l;
}

static bool isFm(GenType t) { return t == GenType::FM || t == GenType::NFM || t == GenType::WFM; }

GenConfig clampGen(GenConfig c, const DeviceInfo &d)
{
    c.extGainDb = std::clamp(c.extGainDb, -100.0, 100.0);
    c.sweepS = std::clamp(c.sweepS, 0.0, 600.0);
    c.modHz = std::clamp(c.modHz, 1, d.model ? d.model->gen.maxModHz : 10000);
    c.amDepth = std::clamp(c.amDepth, 0, 100);
    c.fmDevHz = std::clamp(c.fmDevHz, 1000, 300000);
    c.levelChangeDb = std::clamp(c.levelChangeDb, -90.0, 90.0);
    if (const ModelDesc *m = d.model) {
        const GenDesc &g = m->gen;
        if (g.highMaxHz <= 0)
            c.path = GenPath::Low;
        if (c.type && !g.types.contains(*c.type)) {  // a type of the other model: nearest one
            const auto fm = std::find_if(g.types.begin(), g.types.end(), isFm);
            c.type = isFm(*c.type) && fm != g.types.end() ? *fm : GenType::CW;
        }
        if (c.path == GenPath::High) {  // the HIGH output has no AM, external modulation or level sweep
            if (c.type == GenType::AM || c.type == GenType::External)
                c.type = GenType::CW;
            c.levelChangeDb = 0;
        }
        c.levelChangeDb = std::clamp(c.levelChangeDb, -g.maxLevelChange, g.maxLevelChange);
    }
    const GenLimits l = genLimits(c, d);
    c.levelDbm = std::clamp(std::round(c.levelDbm * 10) / 10, l.minDbm, l.maxDbm);
    c.spanHz = std::clamp(std::round(c.spanHz), 0.0, l.maxHz - l.minHz);
    c.freqHz = std::clamp(std::round(c.freqHz), l.minHz, l.maxHz);
    if (c.type == GenType::Sweep)  // the whole sweep inside the limits
        c.freqHz = std::clamp(c.freqHz, l.minHz + c.spanHz / 2, l.maxHz - c.spanHz / 2);
    return c;
}

static QByteArray num(double v) { return QByteArray::number(v, 'g', 10); }
static QByteArray hz(qint64 v) { return QByteArray::number(v); }

static QByteArray modulationName(std::optional<GenType> t)
{
    switch (t.value_or(GenType::CW)) {
    case GenType::AM: return "am";
    case GenType::FM: return "fm";
    case GenType::NFM: return "nfm";
    case GenType::WFM: return "wfm";
    case GenType::External: return "extern";
    default: return "off";
    }
}

QByteArrayList genCommands(const GenConfig &w, AppliedGen &h, const ModelDesc &m)
{
    const GenDesc &g = m.gen;
    QByteArrayList cmds;
    const GenPath path = g.highMaxHz > 0 ? w.path : GenPath::Low;
    if (h.mode != path) {
        cmds << (path == GenPath::High ? "mode high output" : "mode low output");
        // a mode switch resets the unit (sa_core.c reset_settings): output off, no modulation
        // (1 kHz, 80 %, 3 kHz), no level change, not sweeping, mixer output; level at maximum
        h = AppliedGen{};
        h.mode = path;
        h.on = false;
        h.modulation = "off";
        h.modHz = 1000;
        h.amDepth = 80;
        h.fmDevHz = 3000;
        h.levelChangeDb = 0;
        h.sweeping = false;
        if (g.hasMixer)
            h.mixer = true;
    }
    const bool known = w.type.has_value(), sweep = w.type == GenType::Sweep;
    if (g.needsSweepGo && known && !sweep && h.sweeping != false) {  // stop before the span goes
        cmds << "sweep abort";
        h.sweeping = false;
    }
    const QByteArray mod = modulationName(w.type);
    if (known && mod == "off" && h.modulation != mod) {  // before a span: the basic sweeps at once
        cmds << "modulation off";
        h.modulation = mod;
    }
    // the path only matters above 1.13 GHz (below, both make the same signal): unknown after
    // taking a unit over, it is set once the frequencies get there
    const bool pathMatters = w.freqHz + (sweep ? w.spanHz / 2 : 0) > 1130e6;
    if (g.hasMixer && h.mixer != w.mixer && (h.mixer || pathMatters)) {
        cmds << (w.mixer ? "output mixer" : "output normal");
        h.mixer = w.mixer;
    }
    if (h.extGainDb != w.extGainDb) {
        cmds << "ext_gain " + num(w.extGainDb);
        h.extGainDb = w.extGainDb;
    }
    // centre then span: `sweep center` keeps the centre (shrinking the old span to fit),
    // `sweep span` keeps it too (main.c set_sweep_frequency)
    const std::pair<qint64, qint64> freq{std::llround(w.freqHz), sweep ? std::llround(w.spanHz) : 0};
    if (h.freq != freq) {
        if (freq.second == 0)
            cmds << "sweep cw " + hz(freq.first);
        else
            cmds << "sweep center " + hz(freq.first) << "sweep span " + hz(freq.second);
        h.freq = freq;
        h.levelDbm.reset();  // the Ultra lowers the level for good where it cannot reach it
        h.sweepS.reset();    // and a span change to zero resets the sweep time
    }
    const double port = w.levelDbm - w.extGainDb;  // `level` is the absolute level at the port
    if (w.levelKnown && h.levelDbm != port) {
        cmds << "level " + num(port);
        h.levelDbm = port;
    }
    if (known) {
        const double change = sweep && path == GenPath::Low ? w.levelChangeDb : 0;
        if (h.levelChangeDb != change) {
            cmds << "levelchange " + num(change);
            h.levelChangeDb = change;
        }
        if (sweep && h.sweepS != w.sweepS) {
            cmds << "sweeptime " + num(w.sweepS);
            h.sweepS = w.sweepS;
        }
        if (mod != "off" && mod != "extern") {  // parameters first: the modulation starts right
            if (h.modHz != w.modHz) {
                cmds << "modulation freq " + QByteArray::number(w.modHz);
                h.modHz = w.modHz;
            }
            if (g.adjustableModulation && mod == "am" && h.amDepth != w.amDepth) {
                cmds << "modulation depth " + QByteArray::number(w.amDepth);
                h.amDepth = w.amDepth;
            }
            if (g.adjustableModulation && mod == "fm" && h.fmDevHz != w.fmDevHz) {
                cmds << "modulation deviation " + QByteArray::number(w.fmDevHz);
                h.fmDevHz = w.fmDevHz;
            }
        }
        if (h.modulation != mod) {
            cmds << "modulation " + mod;
            h.modulation = mod;
        }
        if (g.needsSweepGo && sweep && h.sweeping != true) {
            cmds << "sweep go";
            h.sweeping = true;
        }
    }
    if (w.on && h.on != w.on) {  // last: everything else is in place when the signal appears
        cmds << (*w.on ? "output on" : "output off");
        h.on = w.on;
    }
    return cmds;
}

void forgetGenCommand(const QByteArray &cmd, AppliedGen &h)
{
    const QByteArrayList t = cmd.split(' ');
    const QByteArray a = t.value(0), b = t.value(1);
    if (a == "mode") h.mode.reset();  // sent again: no reset if it did arrive (same mode)
    else if (a == "output") (b == "on" || b == "off" ? h.on : h.mixer).reset();
    else if (a == "ext_gain") h.extGainDb.reset();
    else if (a == "level") h.levelDbm.reset();
    else if (a == "levelchange") h.levelChangeDb.reset();
    else if (a == "sweeptime") h.sweepS.reset();
    else if (a == "sweep" && (b == "go" || b == "abort")) h.sweeping.reset();
    else if (a == "sweep") h.freq.reset();
    else if (a == "modulation" && b == "freq") h.modHz.reset();
    else if (a == "modulation" && b == "depth") h.amDepth.reset();
    else if (a == "modulation" && b == "deviation") h.fmDevHz.reset();
    else if (a == "modulation") h.modulation.reset();
}

GenConfig adoptUnitGenerator(GenConfig c, const DeviceInfo &d, AppliedGen *have)
{
    const UnitGenerator &u = d.gen;
    c.path = u.high ? GenPath::High : GenPath::Low;
    c.mixer = true;  // cannot be read: the unit's default after a mode switch
    const ScanGrid &grid = d.deviceGrid;
    const double span = grid.points > 0 ? std::max(0.0, grid.stopHz - grid.startHz) : 0;
    const double unitHz = grid.points > 0 ? (grid.startHz + grid.stopHz) / 2 : c.freqHz;
    c.freqHz = unitHz;
    if (u.extGainDb)
        c.extGainDb = *u.extGainDb;
    c.levelKnown = false;  // `attenuate` reports the attenuator, several dB off the level
    c.on.reset();
    if (span > 0) {  // the unit's menus never combine a span with modulation
        c.type = GenType::Sweep;
        c.spanHz = span;
        c.levelChangeDb = 0;  // cannot be read: assumed
        if (u.sweepS)
            c.sweepS = *u.sweepS;
    } else {
        c.type.reset();
    }
    c = clampGen(c, d);
    AppliedGen h;
    if (d.model)
        genCommands(c, h, *d.model);  // the unit as far as it reported (or is assumed)
    // what the unit really has where the app had to assume or clamp: sent when it matters
    if (grid.points > 0)  // outside the app's limits (e.g. an LO harmonic): moved inside at once
        h.freq = std::pair<qint64, qint64>{std::llround(unitHz), c.type == GenType::Sweep ? std::llround(span) : 0};
    h.on.reset();
    h.mixer.reset();  // sent once the frequency is in the range where the path matters
    h.modHz.reset();  // sent with the first modulation chosen
    h.amDepth.reset();
    h.fmDevHz.reset();
    if (!u.extGainDb)  // not read: ours goes out
        h.extGainDb.reset();
    if (!u.sweepS)
        h.sweepS.reset();
    if (!c.type) {
        h.modulation.reset();
        h.levelChangeDb.reset();
        h.sweeping.reset();
    }
    if (have)
        *have = h;
    return c;
}

QString outputPathText(const GenConfig &c, const DeviceInfo &d)
{
    if (!d.model)
        return {};
    if (!d.model->gen.hasMixer)
        return c.path == GenPath::High ? "Level in coarse steps" : "";
    // as the unit's OUTPUT line (ui.c menu_lowoutput_settings_acb) over the frequencies used
    const double span = c.type == GenType::Sweep ? c.spanHz : 0;
    const double start = c.freqHz - span / 2, stop = c.freqHz + span / 2;
    QStringList parts;
    if (start < 823e6)
        parts << "Sine";
    if (stop >= 823e6)
        parts << "Square wave";
    if (stop > 1130e6 && c.mixer)
        parts << "Mixer";
    return parts.join(", ");
}

static const char *const kTypeNames[] = {"CW", "Sweep", "AM", "FM", "NFM", "WFM", "Ext"};

QString genTypeName(GenType t)
{
    return kTypeNames[int(t)];
}

std::optional<GenType> genTypeFromName(const QString &name)
{
    for (int i = 0; i < int(std::size(kTypeNames)); ++i)
        if (name.compare(kTypeNames[i], Qt::CaseInsensitive) == 0)
            return GenType(i);
    return std::nullopt;
}
