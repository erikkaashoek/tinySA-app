// DeviceLink, tracking generator: the analyzer's link sweeps point by point and drives the
// generator unit's link (same thread). Per point: the generator gets `sweep cw f` (and `level`
// at the first point: the Ultra lowers the level for good where it cannot reach it), the
// settle time passes, then the analyzer measures that one frequency with `scanraw f f 1`
// (firmware from v1.4-316 measures it as one sweep point; older firmware as zero span, slower).
#include "link.h"
#include <algorithm>
#include <cmath>

void DeviceLink::setTracker(DeviceLink *tg)
{
    m_tg = tg;
    if (!tg)
        return;
    tg->m_isTracker = true;
    connect(tg, &DeviceLink::busyChanged, this, [this](bool busy) {
        if (!busy && m_tg && m_track.on)  // waiting for it to be ready for the next point
            pump();
    });
    // queued: the generator unit finishes its handshake (output mode) first
    connect(tg, &DeviceLink::opened, this, [this] {
        if (m_tg && m_tg->m_info.model)
            m_track = clampTrack(m_track, m_tg->m_info);
        m_trackAligned = false;  // another unit, perhaps: its frequency error is measured again
        pump();
    }, Qt::QueuedConnection);
    connect(tg, &DeviceLink::closed, this, [this] {
        m_trackAligned = false;
        if (m_mode != Off && m_track.on) {
            emit message("Tracking generator lost: scan stopped");
            setMode(Off);
        }
    });
    connect(tg, &DeviceLink::trackerStopped, this, [this] {
        if (m_mode != Off) {
            emit message("Scan stopped on the tracking generator");
            setMode(Off);
        }
    });
}

void DeviceLink::setTracking(const TrackConfig &t)
{
    const bool was = m_track.on;
    m_track = m_tg && m_tg->m_info.model ? clampTrack(t, m_tg->m_info) : t;
    if (was && !m_track.on)
        stopTracking(true);  // the next sweep is an ordinary one
    pump();
}

bool DeviceLink::tracking() const
{
    return m_tg && m_track.on && m_tg->m_ready;
}

void DeviceLink::stopTracking(bool rfOff)
{
    m_trackNext = -1;
    m_trackWait = false;
    m_trackRestart = TrackChange::None;
    m_trackSettle.stop();
    if (m_aligning) {  // given up halfway: measured again at the next tracking sweep
        m_aligning = 0;
        m_alignHz.clear();
        m_queue.removeIf([](const Cmd &c) { return c.align; });
        m_have.rbwKHz.reset();  // ours again
    }
    if (rfOff && m_tg && m_tg->m_trackRf)
        m_tg->setTrackOutput(false, m_tg->m_gen.levelDbm, m_tg->m_gen.freqHz);
}

void DeviceLink::pumpTracking()
{
    if (!m_trackAligned)
        return pumpAlign();
    const ScanGrid &g = m_want.grid;
    if (m_trackNext < 0 || !(m_frame.grid == g)) {  // a new sweep (or the grid changed)
        m_frame = SweepFrame{m_frame.seq + 1, g, QVector<double>(g.points, qQNaN())};
        m_trackNext = 0;
        m_sweepClock.start();
        m_emitClock.start();
    }
    const double hz = trackerHz(g.freq(m_trackNext));
    if (!m_tg->m_trackRf || m_tg->m_gen.levelDbm != m_track.levelDbm) {
        // RF on (it may have been anywhere, with its screen on just redrawn) or a new level:
        // the first point waits as long as after a path change
        if (m_trackRestart != TrackChange::RfOn)
            m_trackRestart = !m_tg->m_trackRf ? TrackChange::RfOn : TrackChange::Path;
        m_tg->setTrackOutput(true, m_track.levelDbm, hz);
    }
    if (!m_tg->readyToTune())
        return;  // its busyChanged(false) pumps again
    m_trackWait = true;
    const TrackChange change = m_trackRestart != TrackChange::None ? m_trackRestart
                               : trackerPath(m_tg->m_info, m_tg->m_gen.freqHz) != trackerPath(m_tg->m_info, hz)
                                   ? TrackChange::Path : TrackChange::None;
    m_trackRestart = TrackChange::None;
    m_tg->tune(hz, m_trackNext == 0, [this, seq = m_frame.seq, change](bool ok) {
        if (!m_trackWait || seq != m_frame.seq)
            return;  // stopped or started again meanwhile
        if (!ok) {
            m_trackWait = false;
            emit message("No reply from the tracking generator: scan stopped");
            return setMode(Off);
        }
        const int settle = trackSettleMs(m_track, m_tg->m_uiOff, change);
        if (settle > 0)
            m_trackSettle.start(settle);
        else
            measureTrackPoint();
    });
}

void DeviceLink::measureTrackPoint()
{
    m_trackWait = false;
    if (m_mode == Off || m_trackNext < 0 || !m_io)
        return pump();
    const QByteArray f = QByteArray::number(std::llround(m_frame.grid.freq(m_trackNext)));
    m_frame.received = m_trackNext;  // the value lands at this point
    Cmd c{"scanraw " + f + " " + f + " 1", RxParser::Body::ScanRaw, 0, 5000};
    c.points = 1;
    c.done = [this, seq = m_frame.seq](bool ok, const QStringList &lines) {
        if (seq != m_frame.seq || m_trackNext < 0)
            return;  // stopped, or touched on the unit (handle() stopped the scan)
        if (m_frame.received <= m_trackNext) {  // no value
            if (ok && !lines.isEmpty()) {
                emit message("Scan failed: " + lines.join(' '));
                setMode(Off);
            } else {
                m_trackNext = -1;  // no reply: the sweep starts again after resynchronising
            }
            return;
        }
        if (++m_trackNext < m_frame.grid.points)
            return;  // progress: handle() emits partial frames
        m_trackNext = -1;
        m_frame.complete = true;
        m_frame.seconds = m_sweepClock.elapsed() / 1000.0;
        emit frame(m_frame);
        if (m_mode == Once)
            setMode(Off);
    };
    m_queue.prepend(c);  // before anything queued while the generator was tuned
    pump();
}

// --- the generator's frequency error ---
// The two units' reference crystals differ by some ppb (about -430 ppb between a ZS407 and a
// ZS405): at 3 GHz over a kHz, which a narrow RBW does not see. Measured once, at a high
// frequency: the generator at hz, the analyzer scans around it with a 3 kHz RBW, then with
// the smallest RBW from 1 kHz in steps of a tenth of it; the peak is interpolated.

double DeviceLink::trackerHz(double hz) const
{
    return hz / (1 + m_trackPpb * 1e-9);
}

void DeviceLink::pumpAlign()
{
    if (m_alignHz.isEmpty()) {  // high, then the sweep's top (a device in between may block high)
        const GenLimits l = trackerLimits(m_tg->m_info);
        const double top = std::min(l.maxHz, m_info.model->highLimit(m_want.input));
        const double bottom = std::max(l.minHz, m_info.model->lowLimit(m_want.input));
        const double high = std::round(std::clamp(std::min(3e9, 0.9 * top), bottom, top));
        m_alignHz = {high};
        if (std::abs(m_want.grid.stopHz - high) > 0.1 * high)
            m_alignHz << std::round(m_want.grid.stopHz);
    }
    const double hz = m_alignHz.first();
    if (!m_tg->m_trackRf || m_tg->m_gen.freqHz != hz || m_tg->m_gen.levelDbm != m_track.levelDbm)
        m_tg->setTrackOutput(true, m_track.levelDbm, hz);  // as set, uncorrected
    if (!m_tg->readyToTune())
        return;  // its busyChanged(false) pumps again
    const QVector<double> &rbws = m_info.model->rbwKHz;
    auto rbwFrom = [&rbws](double kHz) {
        const auto it = std::find_if(rbws.begin(), rbws.end(), [kHz](double r) { return r >= kHz; });
        return it == rbws.end() ? rbws.last() : *it;
    };
    const double coarse = rbwFrom(3), fine = rbwFrom(1), coarseStep = coarse * 1e3 / 3;
    const int id = m_aligning = ++m_alignIds;
    // coarse: +-20 ppm (at least 50 kHz), the carrier 15 dB above the median (noise); fine: two
    // coarse steps each side, mostly the carrier itself
    auto scans = [this, id, hz, coarse, fine, coarseStep] {
        if (id != m_aligning)
            return;
        alignScan(hz, std::max(50e3, 20e-6 * hz), coarse, coarseStep, 15,
                  [this, id, hz, fine, coarseStep](std::optional<double> at) {
            if (id != m_aligning)
                return;
            if (!at)
                return alignFailed();
            alignScan(*at, 2 * coarseStep, fine, fine * 1e3 / 10, 0, [this, id, hz](std::optional<double> at) {
                if (id != m_aligning)
                    return;
                if (!at)
                    return alignFailed();
                endAlign((*at - hz) / hz * 1e9);
            });
        });
    };
    // its output there first (it may have been anywhere, RF off)
    QTimer::singleShot(trackSettleMs(m_track, m_tg->m_uiOff, TrackChange::RfOn), this, scans);
}

void DeviceLink::alignScan(double centerHz, double halfSpanHz, double rbwKHz, double stepHz, double minRiseDb,
                           std::function<void(std::optional<double> peakHz)> done)
{
    // scanraw S E N measures at S + (E - S) / N * i
    const int n = int(std::ceil(2 * halfSpanHz / stepHz)) + 1;
    const qint64 s = std::llround(centerHz - stepHz * (n - 1) / 2), e = s + std::llround(stepHz * n);
    Cmd rbw{"rbw " + QByteArray::number(rbwKHz)};
    rbw.align = true;
    Cmd scan{"scanraw " + QByteArray::number(s) + " " + QByteArray::number(e) + " " + QByteArray::number(n),
             RxParser::Body::ScanRaw, 0, 10000};
    scan.points = n;
    scan.align = true;
    scan.done = [this, s, e, n, minRiseDb, done](bool ok, const QStringList &) {
        const QVector<double> v = std::exchange(m_alignValues, {});
        if (!ok || v.size() < n)
            return done(std::nullopt);
        const int i = int(std::max_element(v.begin(), v.end()) - v.begin());
        QVector<double> sorted = v;
        std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
        if (v[i] - sorted[n / 2] < minRiseDb)  // no carrier above the noise
            return done(std::nullopt);
        double x = i;
        if (i > 0 && i < n - 1) {  // parabola through the top three (a Gaussian filter in dB)
            const double den = v[i - 1] - 2 * v[i] + v[i + 1];
            if (den < 0)
                x += 0.5 * (v[i - 1] - v[i + 1]) / den;
        }
        done(s + double(e - s) / n * x);
    };
    m_queue.append(rbw);
    enqueue(scan);
}

void DeviceLink::alignFailed()
{
    m_aligning = 0;
    m_alignHz.removeFirst();
    if (m_alignHz.isEmpty())
        return endAlign(std::nullopt);
    pump();  // the next frequency
}

void DeviceLink::endAlign(std::optional<double> ppb)
{
    m_aligning = 0;
    m_alignHz.clear();
    m_trackAligned = true;
    m_trackPpb = ppb.value_or(0);
    m_have.rbwKHz.reset();  // ours again
    if (!ppb)
        emit message("No signal from the tracking generator to measure its frequency: not corrected");
    emit trackerAligned(m_trackPpb, ppb.has_value());
    pump();
}

// --- the generator unit's side ---

void DeviceLink::setTrackOutput(bool rf, double levelDbm, double hz)
{
    if (rf)
        m_trackTouched = false;  // the analyzer scans again: the screen off again
    m_trackRf = rf;
    m_gen = trackerGen(m_gen, m_info, hz, levelDbm, rf);
    if (m_ready && m_activity != Generator)
        setActivity(Generator);  // stops its own scanning first
    pump();
}

// The whole time it is the tracking generator: no command waits for a redraw of its screen
// (its mode switch included). Back on: disconnected, or touched (until the next scan).
bool DeviceLink::trackScreenOff() const
{
    return m_isTracker && m_activity == Generator && !m_trackTouched && m_screenOffWanted && m_info.has("ui");
}

bool DeviceLink::readyToTune() const
{
    if (!m_ready || !m_info.model || m_activity != Generator || !m_trackRf || m_cur || !m_queue.isEmpty()
        || m_draining || m_settle.isActive() || m_paused || trackScreenOff() != m_uiOff)
        return false;
    AppliedGen have = m_genHave;
    return genCommands(m_gen, have, *m_info.model).isEmpty();
}

void DeviceLink::tune(double hz, bool withLevel, std::function<void(bool ok)> done)
{
    m_gen.freqHz = hz;
    m_gen = clampGen(m_gen, m_info);
    const qint64 f = std::llround(m_gen.freqHz);
    if (m_genHave.freq == std::pair<qint64, qint64>{f, 0})
        return done(true);  // there already: its RF was just switched on at this frequency
    m_genHave.freq = std::pair<qint64, qint64>{f, 0};
    Cmd tuneCmd{"sweep cw " + QByteArray::number(f)};
    tuneCmd.generator = true;
    auto reply = [done](bool ok, const QStringList &) { done(ok); };
    if (!withLevel) {
        tuneCmd.done = reply;
        return enqueue(tuneCmd);
    }
    const double port = m_gen.levelDbm - m_gen.extGainDb;  // as genCommands
    m_genHave.levelDbm = port;
    Cmd levelCmd{"level " + QByteArray::number(port, 'g', 10)};
    levelCmd.generator = true;
    levelCmd.done = reply;
    m_queue.append(tuneCmd);
    enqueue(levelCmd);
}
