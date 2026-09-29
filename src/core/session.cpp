#include "session.h"

Session::Session(QObject *parent) : QObject(parent), m_link(new DeviceLink)
{
    qRegisterMetaType<SweepFrame>();
    qRegisterMetaType<SweepConfig>();
    qRegisterMetaType<DeviceInfo>();
    m_link->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_link, &QObject::deleteLater);

    connect(m_link, &DeviceLink::opened, this, [this](const DeviceInfo &info) {
        m_info = info;
        m_connected = true;
        setConfig(m_config);  // clamp to the model's limits
        emit connected(info);
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
