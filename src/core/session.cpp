#include "session.h"

Session::Session(QObject *parent) : QObject(parent), m_link(new DeviceLink), m_trackerLink(new DeviceLink)
{
    qRegisterMetaType<SweepFrame>();
    qRegisterMetaType<SweepConfig>();
    qRegisterMetaType<DeviceInfo>();
    qRegisterMetaType<GenConfig>();
    qRegisterMetaType<TrackConfig>();
    m_link->moveToThread(&m_thread);
    m_trackerLink->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_link, &QObject::deleteLater);
    connect(&m_thread, &QThread::finished, m_trackerLink, &QObject::deleteLater);
    m_link->setTracker(m_trackerLink);  // the thread does not run yet
    connect(m_trackerLink, &DeviceLink::opened, this, [this](const DeviceInfo &info) {
        m_trackerInfo = info;
        m_trackerConnected = true;
        m_trackerPpb.reset();
        emit trackerConnected(info);
        setTrack(m_track);    // its level limits
        setConfig(m_config);  // its frequency range
    });
    connect(m_trackerLink, &DeviceLink::closed, this, [this](const QString &reason) {
        m_trackerConnected = false;
        m_trackerPpb.reset();
        emit trackerDisconnected(reason);
    });
    connect(m_link, &DeviceLink::trackerAligned, this, [this](double ppb, bool measured) {
        m_trackerPpb = measured ? std::optional<double>(ppb) : std::nullopt;
        emit trackerAligned();
    });
    connect(m_trackerLink, &DeviceLink::message, this, [this](const QString &text) {
        emit message("Tracking generator: " + text);
    });
    connect(m_trackerLink, &DeviceLink::traffic, this, [this](bool sent, const QString &text) {
        emit trackerTraffic(sent, text);
    });

    connect(m_link, &DeviceLink::opened, this, [this](const DeviceInfo &info) {
        m_info = info;
        m_connected = true;
        setConfig(m_config);  // clamp to the model's limits
        // the link clamps its own copy (and may take over a generating unit's settings
        // next): sending ours would race with that
        GenConfig g = clampGen(m_gen, m_info);
        g.ctcss = m_info.ctcssTone;  // the unit's CTCSS tone, as the link takes it
        if (!(g == m_gen)) {
            m_gen = g;
            emit generatorChanged(m_gen);
        }
        emit connected(info);
    });
    connect(m_link, &DeviceLink::generatorAdopted, this, [this](const GenConfig &g) {
        m_gen = g;
        emit generatorChanged(m_gen);
        syncExtGain();  // the generating unit's external gain is the app's now
    });
    connect(m_link, &DeviceLink::activityChanged, this, [this](int a) {
        if (a != m_activity) {
            m_activity = a;
            emit activityChanged(a);
        }
    });
    connect(m_link, &DeviceLink::closed, this, [this](const QString &reason) {
        m_connected = false;
        emit disconnected(reason);
    });
    connect(m_link, &DeviceLink::scanChanged, this, [this](int mode) {
        m_scan = mode;
        emit scanChanged(mode);
    });
    connect(m_link, &DeviceLink::busyChanged, this, [this](bool busy) {
        m_busy = busy;
        emit busyChanged(busy);
    });
    connect(m_link, &DeviceLink::frame, this, [this](const SweepFrame &f) {
        if (m_restartAfterSeq && f.seq != *m_restartAfterSeq) {  // a sweep after an ext gain change
            m_traces.restartAverage();
            m_restartAfterSeq.reset();
        }
        m_lastSeq = f.seq;
        m_traces.add(f);
        emit frame(f);
        emit tracesChanged(f.complete);
    });
    connect(m_link, &DeviceLink::reply, this, &Session::reply);
    connect(m_link, &DeviceLink::battery, this, &Session::battery);
    connect(m_link, &DeviceLink::screen, this, &Session::screen);
    connect(m_link, &DeviceLink::traffic, this, &Session::traffic);
    connect(m_link, &DeviceLink::message, this, &Session::message);
    m_thread.setObjectName("DeviceLink");
    m_thread.start();
}

Session::~Session()
{
    // close synchronously so the unit gets its `resume` before the app exits (the tracking
    // generator its `output off`)
    QMetaObject::invokeMethod(m_link, &DeviceLink::close, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_trackerLink, &DeviceLink::close, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void Session::connectDevice(const QString &port)
{
    if (m_trackerConnected && port == m_trackerInfo.port) {
        emit disconnected(port + " is the tracking generator");
        return;
    }
    QMetaObject::invokeMethod(m_link, [l = m_link, port] { l->open(port); });
}

void Session::disconnectDevice()
{
    QMetaObject::invokeMethod(m_link, &DeviceLink::close);
}

void Session::connectTracker(const QString &port)
{
    if (m_connected && port == m_info.port) {
        emit trackerDisconnected(port + " is the analyzer");
        return;
    }
    QMetaObject::invokeMethod(m_trackerLink, [l = m_trackerLink, port] { l->open(port); });
}

void Session::disconnectTracker()
{
    QMetaObject::invokeMethod(m_trackerLink, &DeviceLink::close);
}

void Session::setTrack(const TrackConfig &t)
{
    const TrackConfig clamped = m_trackerInfo.model ? clampTrack(t, m_trackerInfo) : t;
    const bool changed = !(clamped == m_track);
    const bool switched = clamped.on != m_track.on || clamped.mixer != m_track.mixer || clamped.harmonic != m_track.harmonic;
    m_track = clamped;
    QMetaObject::invokeMethod(m_link, [l = m_link, clamped] { l->setTracking(clamped); });
    if (changed || !(clamped == t))
        emit trackChanged(m_track);
    if (switched)
        setConfig(m_config);  // tracking: the range inside the generator's (in its output mode)
}

void Session::setConfig(const SweepConfig &c)
{
    const SweepConfig clamped = !m_info.model ? c
                                : isTracking() ? clampToTracker(c, *m_info.model, m_trackerInfo, m_track)
                                               : clampConfig(c, *m_info.model);
    const bool changed = !(clamped == m_config);
    if (clamped.extGainDb != m_config.extGainDb)  // the levels jump: average and hold start again
        m_restartAfterSeq = m_lastSeq;           // with the first sweep that has the new gain
    m_config = clamped;
    QMetaObject::invokeMethod(m_link, [l = m_link, clamped] { l->setConfig(clamped); });
    if (changed || !(clamped == c))
        emit configChanged(m_config);
    if (m_gen.extGainDb != m_config.extGainDb) {  // one external gain: the generator's too
        GenConfig g = m_gen;
        g.levelDbm += m_config.extGainDb - g.extGainDb;  // as on its page: the port level stays
        g.extGainDb = m_config.extGainDb;
        setGenerator(g);
    }
}

void Session::setGenerator(const GenConfig &want)
{
    GenConfig g = want;
    if (g.path != m_gen.path)  // a mode switch sets the unit's level to its maximum: send ours
        g.levelKnown = true;
    const GenConfig clamped = m_info.model ? clampGen(g, m_info) : g;
    const bool changed = !(clamped == m_gen);
    m_gen = clamped;
    QMetaObject::invokeMethod(m_link, [l = m_link, clamped] { l->setGenerator(clamped); });
    if (changed || !(clamped == want))
        emit generatorChanged(m_gen);
    syncExtGain();
}

// One external gain for analyzer and generator, as on the unit: the generator's (edited on its
// page or taken over from a generating unit) becomes the analyzer's
void Session::syncExtGain()
{
    if (m_config.extGainDb == m_gen.extGainDb)
        return;
    SweepConfig c = m_config;
    c.extGainDb = m_gen.extGainDb;
    setConfig(c);  // equal now: no way back
}

void Session::setScreenOffWhileScanning(bool on)
{
    QMetaObject::invokeMethod(m_link, [l = m_link, on] { l->setScreenOffWhileScanning(on); });
    QMetaObject::invokeMethod(m_trackerLink, [l = m_trackerLink, on] { l->setScreenOffWhileScanning(on); });
}

void Session::triggerBurst()
{
    QMetaObject::invokeMethod(m_link, &DeviceLink::triggerBurst);
}

void Session::setActivity(int activity)
{
    if (!m_connected || activity == m_activity)
        return;
    if (activity == DeviceLink::Generator) {  // the unit starts with RF off; so do we
        GenConfig g = m_gen;
        g.on = false;
        g.levelKnown = true;
        if (!g.type)
            g.type = GenType::CW;
        setGenerator(g);
    }
    QMetaObject::invokeMethod(m_link, [l = m_link, activity] { l->setActivity(activity); });
}

void Session::setScan(int mode)
{
    QMetaObject::invokeMethod(m_link, [l = m_link, mode] { l->setScan(mode); });
}

void Session::send(const QString &command)
{
    QMetaObject::invokeMethod(m_link, [l = m_link, command] { l->send(command); });
}

void Session::capture()
{
    QMetaObject::invokeMethod(m_link, &DeviceLink::capture);
}

void Session::setRemote(bool on)
{
    QMetaObject::invokeMethod(m_link, [l = m_link, on] { l->setRemote(on); });
}

void Session::touch(QPoint lcd, bool down)
{
    QMetaObject::invokeMethod(m_link, [l = m_link, lcd, down] { l->touch(lcd, down); });
}

void Session::simTouch()
{
    QMetaObject::invokeMethod(m_link, &DeviceLink::simTouch);
}
