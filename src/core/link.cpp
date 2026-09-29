#include "link.h"
#include "simdevice.h"
#include <QRegularExpression>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <cmath>

DeviceLink::DeviceLink(QObject *parent)
    : QObject(parent), m_timeout(this), m_battery(this), m_drain(this)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &DeviceLink::onTimeout);
    m_battery.setInterval(6000);
    connect(&m_battery, &QTimer::timeout, this, [this] {
        m_batteryDue = true;
        pump();
    });
    m_drain.setSingleShot(true);
    connect(&m_drain, &QTimer::timeout, this, [this] {
        m_draining = false;
        m_rx.reset();
        pump();
    });
}

DeviceLink::~DeviceLink()
{
    shutdown({});
}

QString DeviceLink::findPort()
{
    for (const auto &pi : QSerialPortInfo::availablePorts())
        if (pi.vendorIdentifier() == 0x0483 && pi.productIdentifier() == 0x5740)
            return pi.portName();
    return {};
}

void DeviceLink::open(const QString &port)
{
    shutdown({});
    m_info = DeviceInfo{};
    m_info.port = port;
    m_have = AppliedConfig{};
    if (port.startsWith("sim:")) {
        auto *sim = new SimTransport(port.endsWith("basic") ? SimTransport::Kind::Basic
                                                            : SimTransport::Kind::Ultra, this);
        sim->open(QIODevice::ReadWrite | QIODevice::Unbuffered);
        m_io = sim;
    } else {
        auto *sp = new QSerialPort(port, this);
        sp->setBaudRate(576000);
        if (!sp->open(QIODevice::ReadWrite)) {
            const QString err = sp->errorString();
            delete sp;
            emit closed(QString("Cannot open %1: %2").arg(port, err));
            return;
        }
        connect(sp, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
            if (e == QSerialPort::ResourceError || e == QSerialPort::PermissionError)
                fail("Connection lost");
        });
        sp->clear(QSerialPort::Input);
        m_io = sp;
    }
    connect(m_io, &QIODevice::readyRead, this, &DeviceLink::onReadyRead);
    // A scan left running by an earlier session: stop it and drop its output.
    m_io->write("abort\r");
    resync();
    m_battery.start();
    handshake();
}

void DeviceLink::handshake()
{
    enqueue({"version", {}, 0, 3000, [this](bool ok, const QStringList &lines) {
        for (const QString &l : lines) {
            const int at = l.indexOf("tinySA");  // skip junk left in the line
            if (at >= 0 && m_info.version.isEmpty())
                m_info.version = l.mid(at).trimmed();
            if (l.startsWith("HW Version:"))
                m_info.hardware = l.mid(11).trimmed();
        }
        if (!ok)
            fail("No reply from " + m_info.port);
    }});
    enqueue({"info", {}, 0, 3000, [this](bool ok, const QStringList &lines) {
        m_info.info = lines.join('\n');
        m_info.model = ok ? detectModel(m_info.info, m_info.version) : nullptr;
        if (!m_info.model) {
            fail(QString("No tinySA on %1 %2").arg(m_info.port, m_info.version));
            return;
        }
        static const QRegularExpression zs(R"(ZS\d{3})");
        const auto m = zs.match(lines.value(0));
        if (m.hasMatch())
            m_info.hardware = m.captured(0);
    }});
    enqueue({"help", {}, 0, 3000, [this](bool, const QStringList &lines) {
        m_info.commands = parseHelp(lines);
    }});
    enqueue({"zero", {}, 0, 3000, [this](bool, const QStringList &lines) {
        m_info.zeroDbm = parseZero(lines, m_info.model->defaultZero);
    }});
    enqueue({"sweep", {}, 0, 3000, [this](bool ok, const QStringList &lines) {
        const QStringList v = lines.value(0).split(' ', Qt::SkipEmptyParts);  // "start stop points"
        if (ok && v.size() >= 3)
            m_info.deviceGrid = {v[0].toDouble(), v[1].toDouble(), v[2].toInt()};
    }});
    enqueue({"", {}, 0, 0, [this](bool, const QStringList &) {
        m_want = clampConfig(m_want, *m_info.model);
        m_ready = true;
        m_batteryDue = true;
        emit opened(m_info);
    }});
}

void DeviceLink::close()
{
    shutdown("Disconnected");
}

void DeviceLink::fail(const QString &reason)
{
    shutdown(reason);
}

void DeviceLink::shutdown(const QString &reason)
{
    m_timeout.stop();
    m_battery.stop();
    m_drain.stop();
    if (m_io) {
        // Leave the unit sweeping on its own: stop our scan (the shell reads the abort
        // within 5 s) and undo our pause. Both wait in the unit's input buffer if needed.
        if (m_cur && m_cur->body == RxParser::Body::ScanRaw && !m_scanDone && !m_aborting)
            m_io->write("abort\r");
        if (m_paused && m_ready)
            m_io->write("resume\r");
        if (auto *sp = qobject_cast<QSerialPort *>(m_io))
            sp->waitForBytesWritten(200);
        m_io->disconnect(this);
        m_io->close();
        m_io->deleteLater();
        m_io = nullptr;
    }
    m_queue.clear();
    m_cur.reset();
    m_rx.reset();
    m_ready = m_paused = m_draining = false;
    if (m_mode != Off) {
        m_mode = Off;
        emit scanChanged(Off);
    }
    updateBusy();
    if (!reason.isEmpty())
        emit closed(reason);
}

void DeviceLink::setConfig(const SweepConfig &c)
{
    m_want = m_info.model ? clampConfig(c, *m_info.model) : c;
    pump();
}

void DeviceLink::setScan(int mode)
{
    setMode(mode);
}

void DeviceLink::setMode(int mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    emit scanChanged(mode);
    auto dropQueued = [this](const char *line) {
        return m_queue.removeIf([line](const Cmd &c) { return c.line == line; }) > 0;
    };
    if (mode == Off) {
        // Stop now rather than after a possibly minutes-long sweep. The shell reads the
        // abort when the scan ends or within 5 s; its echo inside the data is tolerated.
        if (m_cur && m_cur->body == RxParser::Body::ScanRaw && !m_scanDone && !m_aborting) {
            m_aborting = true;
            m_io->write("abort\r");
        }
        dropQueued("pause");
        if (m_paused)
            enqueue({"resume"});
    } else {
        dropQueued("resume");  // scanning again before the resume went out
    }
    pump();
}

void DeviceLink::send(const QString &command)
{
    Cmd c{command.toLatin1()};
    if (command.trimmed() == "capture" && m_info.model)
        return capture();
    c.done = [this, command](bool, const QStringList &lines) { emit reply(command, lines); };
    c.user = true;
    enqueue(c);
}

void DeviceLink::capture()
{
    if (!m_info.model)
        return;
    const QSize lcd = m_info.model->lcd;
    enqueue({"capture", RxParser::Body::Capture, lcd.width() * lcd.height() * 2, 5000,
             [this, lcd](bool ok, const QStringList &) {
        if (!ok || m_pixels.size() != lcd.width() * lcd.height() * 2)
            return;
        QImage img(lcd, QImage::Format_RGB16);
        blitRgb565BE(img, img.rect(), m_pixels);
        emit screen(img);
    }});
}

void DeviceLink::simTouch()
{
    if (auto *sim = qobject_cast<SimTransport *>(m_io))
        sim->touchScreen();
}

void DeviceLink::enqueue(Cmd c)
{
    m_queue.append(std::move(c));
    pump();
}

void DeviceLink::pump()
{
    pumpStep();
    updateBusy();
}

void DeviceLink::updateBusy()
{
    const bool busy = m_cur || !m_queue.isEmpty() || m_draining;
    if (busy != m_busy) {
        m_busy = busy;
        emit busyChanged(busy);
    }
}

void DeviceLink::pumpStep()
{
    if (!m_io || m_cur || m_draining)
        return;
    if (!m_queue.isEmpty())
        return transmit(m_queue.takeFirst());
    if (!m_ready)
        return;
    if (m_batteryDue) {
        m_batteryDue = false;
        return enqueue({"vbat", {}, 0, 3000, [this](bool ok, const QStringList &lines) {
            static const QRegularExpression re(R"((\d+)\s*mV)");
            const auto m = re.match(lines.value(0));
            if (ok && m.hasMatch())
                emit battery(m.captured(1).toInt());
        }});
    }
    if (m_mode == Off)
        return;
    if (!m_paused)  // the unit's own sweeping would compete with scanraw
        return enqueue({"pause"});
    const QByteArrayList cmds = configCommands(m_want, m_have, *m_info.model);
    if (!cmds.isEmpty()) {
        for (const QByteArray &c : cmds)
            m_queue.append({c, {}, 0, 3000, [this, c](bool, const QStringList &lines) {
                const QString l = lines.value(0);
                if (l.endsWith('?') || l.startsWith("usage"))  // unknown command / bad value
                    emit message(QString("tinySA refused '%1': %2").arg(QString::fromLatin1(c), l));
            }});
        return pump();
    }
    startSweep();
}

void DeviceLink::startSweep()
{
    m_aborting = false;
    const ScanGrid g = m_want.grid;
    m_frame = SweepFrame{m_frame.seq + 1, g, QVector<double>(g.points, qQNaN())};
    m_sweepClock.start();
    m_emitClock.start();
    Cmd c;
    c.line = QString("scanraw %1 %2 %3").arg(std::llround(g.startHz)).arg(g.scanrawStop())
                 .arg(g.points).toLatin1();
    c.body = RxParser::Body::ScanRaw;
    c.timeoutMs = 15000;  // inactivity watchdog, restarted by every data chunk
    c.done = [this](bool, const QStringList &lines) {
        if (m_frame.received == 0 && !lines.isEmpty()) {  // refused, e.g. range invalid
            emit message("Scan failed: " + lines.join(' '));
            setMode(Off);
        } else if (m_mode == Once) {
            setMode(Off);
        }
    };
    transmit(c);
}

void DeviceLink::transmit(Cmd c)
{
    if (c.line.isEmpty()) {  // local step in a command sequence
        if (c.done)
            c.done(true, {});
        return pump();
    }
    m_echoSeen = m_scanDone = false;
    m_lines.clear();
    m_pixels.clear();
    m_rx.expect(c.line, c.body, c.bytes);
    m_io->write(c.line + "\r");
    if (c.line == "pause" || c.line == "resume")  // the unit's state from now on
        m_paused = c.line == "pause";
    if (c.body != RxParser::Body::ScanRaw)
        emit traffic(true, QString::fromLatin1(c.line), c.user);
    m_timeout.start(c.timeoutMs);
    m_cur = std::move(c);
}

void DeviceLink::onReadyRead()
{
    if (!m_io)
        return;
    const QByteArray bytes = m_io->readAll();
    if (m_draining) {
        if (m_drainClock.elapsed() < 3000)
            m_drain.start(150);  // drain until the line has been quiet for a while
        return;
    }
    for (const RxEvent &e : m_rx.feed(bytes)) {
        handle(e);
        if (!m_io)
            return;
    }
}

void DeviceLink::handle(const RxEvent &e)
{
    const bool scanning = m_cur && m_cur->body == RxParser::Body::ScanRaw;
    switch (e.type) {
    case RxEvent::Echo:
        m_echoSeen = true;
        break;
    case RxEvent::Line:
        emit traffic(false, QString::fromLatin1(e.data), m_cur && m_cur->user && m_echoSeen);
        if (m_cur && m_echoSeen)
            m_lines << QString::fromLatin1(e.data);
        break;
    case RxEvent::Prompt:
        if (m_cur && m_echoSeen)  // prompts before our echo are stale
            finish(true);
        break;
    case RxEvent::ScanData:
        if (!scanning)
            break;
        m_timeout.start(m_cur->timeoutMs);
        for (qint16 v : e.values)
            if (m_frame.received < m_frame.dBm.size())
                m_frame.dBm[m_frame.received++] = v / 32.0 - m_info.zeroDbm;
        if (m_emitClock.elapsed() > 50) {  // live progress for slow sweeps
            m_emitClock.restart();
            emit frame(m_frame);
        }
        break;
    case RxEvent::ScanEnd:
        if (!scanning)
            break;
        if (e.count < m_frame.grid.points) {  // our abort, or touch/jog: no prompt follows
            m_scanDone = true;
            if (!m_aborting) {
                emit message("Scan stopped on the tinySA");
                setMode(Off);
            }
            finish(true);
        } else {
            m_frame.complete = true;
            m_frame.seconds = m_sweepClock.elapsed() / 1000.0;
            emit frame(m_frame);
            m_scanDone = true;
            m_timeout.start(500);  // the prompt follows at once unless aborted late
        }
        break;
    case RxEvent::Pixels:
        m_pixels = e.data;
        break;
    case RxEvent::Region:
        break;
    }
}

void DeviceLink::finish(bool ok)
{
    m_timeout.stop();
    Cmd c = std::move(*m_cur);
    m_cur.reset();
    if (c.done)
        c.done(ok, m_lines);
    pump();
}

void DeviceLink::onTimeout()
{
    if (!m_cur)
        return;
    if (m_scanDone)  // complete sweep whose prompt never came
        return finish(true);
    if (!m_ready)
        return finish(false);  // handshake step: its callback decides
    // No reply: drop the command, drain whatever is still arriving, then carry on.
    emit message(QString("No reply to '%1' - resynchronising").arg(QString::fromLatin1(m_cur->line)));
    Cmd c = std::move(*m_cur);
    m_cur.reset();
    resync();
    if (c.done)
        c.done(false, m_lines);
}

void DeviceLink::resync()
{
    m_rx.reset();
    m_draining = true;
    m_drainClock.start();
    m_drain.start(150);
}
