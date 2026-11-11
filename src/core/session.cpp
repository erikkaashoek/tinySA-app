#include "session.h"

Session::Session(QObject *parent) : QObject(parent), m_link(new DeviceLink)
{
    qRegisterMetaType<SweepFrame>();
    qRegisterMetaType<SweepConfig>();
    qRegisterMetaType<DeviceInfo>();
    qRegisterMetaType<GenConfig>();
    m_link->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_link, &QObject::deleteLater);

    connect(m_link, &DeviceLink::opened, this, [this](const DeviceInfo &info) {
        m_info = info;
        m_connected = true;
        setConfig(m_config);  // clamp to the model's limits
        // the link clamps its own copy (and may take over a generating unit's settings
        // next): sending ours would race with that
        const GenConfig g = clampGen(m_gen, m_info);
        if (!(g == m_gen)) {
            m_gen = g;
            emit generatorChanged(m_gen);
        }
        emit connected(info);
    });
    connect(m_link, &DeviceLink::generatorAdopted, this, [this](const GenConfig &g) {
        m_gen = g;
        emit generatorChanged(m_gen);
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
    // close synchronously so the unit gets its `resume` before the app exits
    QMetaObject::invokeMethod(m_link, &DeviceLink::close, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void Session::connectDevice(const QString &port)
{
    QMetaObject::invokeMethod(m_link, [l = m_link, port] { l->open(port); });
}

void Session::disconnectDevice()
{
    QMetaObject::invokeMethod(m_link, &DeviceLink::close);
}

void Session::setConfig(const SweepConfig &c)
{
    const SweepConfig clamped = m_info.model ? clampConfig(c, *m_info.model) : c;
    const bool changed = !(clamped == m_config);
    m_config = clamped;
    QMetaObject::invokeMethod(m_link, [l = m_link, clamped] { l->setConfig(clamped); });
    if (changed || !(clamped == c))
        emit configChanged(m_config);
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
