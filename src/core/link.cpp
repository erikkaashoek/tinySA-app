#include "link.h"
#include "simdevice.h"
#include <QPointer>
#include <QRegularExpression>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <cmath>
#include <utility>

DeviceLink::DeviceLink(QObject *parent)
    : QObject(parent), m_timeout(this), m_battery(this), m_drain(this), m_screenTimer(this), m_touchTimer(this)
{
    m_touchTimer.setInterval(30);
    connect(&m_touchTimer, &QTimer::timeout, this, [this] {
        if (m_pendingTouch.isEmpty())
            m_touchTimer.stop();
        flushTouch();
    });
    m_screenTimer.setSingleShot(true);
    connect(&m_screenTimer, &QTimer::timeout, this, [this] { emit screen(m_screen); });
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

bool DeviceLink::isTinySAUsbId(quint16 vid, quint16 pid)
{
    return (vid == 0x0483 && pid == 0x5740)     // ST virtual COM port: tinySA, older Ultra firmware
        || (vid == 0x1781 && pid == 0x0A44);    // newer tinySA Ultra firmware
}

QString DeviceLink::findPort()
{
    for (const auto &pi : QSerialPortInfo::availablePorts())
        if (isTinySAUsbId(pi.vendorIdentifier(), pi.productIdentifier()))
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
        // Queued: a failing write reports its error from inside write() or waitForBytesWritten(),
        // where closing the port would pull it from under the caller.
        connect(sp, &QSerialPort::errorOccurred, this, [this, port = QPointer<QSerialPort>(sp)](QSerialPort::SerialPortError e) {
            if ((e == QSerialPort::ResourceError || e == QSerialPort::PermissionError) && port && m_io == port)
                fail("Connection lost");
        }, Qt::QueuedConnection);
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
    enqueue({"vbat_offset", {}, 0, 3000, [this](bool ok, const QStringList &lines) {
        bool num = false;
        const int v = lines.value(0).trimmed().toInt(&num);
        if (ok && num)
            m_info.vbatOffset = v;
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
    const bool wasOpen = m_io != nullptr;
    if (QIODevice *io = std::exchange(m_io, nullptr)) {
        io->disconnect(this);  // nothing from the port gets back in here while it closes
        auto *sp = qobject_cast<QSerialPort *>(io);
        const bool gone = sp && (sp->error() == QSerialPort::ResourceError
                                 || sp->error() == QSerialPort::PermissionError);
        // Leave the unit sweeping on its own: stop our scan (the shell reads the abort
        // within 5 s) and undo our pause. Both wait in the unit's input buffer if needed.
        // Not when the unit has left USB (unplugged, restarting in DFU mode).
        if (!gone) {
            if (m_cur && m_cur->body == RxParser::Body::ScanRaw && !m_scanDone && !m_aborting)
                io->write("abort\r");
            if (m_remote)
                io->write("refresh off\r");
            if (m_paused && m_ready)
                io->write("resume\r");
            if (sp)
                sp->waitForBytesWritten(200);
        }
        io->close();
        io->deleteLater();
    }
    m_queue.clear();
    m_cur.reset();
    m_rx.reset();
    m_ready = m_paused = m_draining = m_remote = false;
    m_touchTimer.stop();
    m_pendingTouch.clear();
    if (m_mode != Off) {
        m_mode = Off;
        emit scanChanged(Off);
    }
    updateBusy();
    if (wasOpen && !reason.isEmpty())  // once: the port poll may ask again after a lost connection
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
    // RLE8 (DiSlord) when the firmware has it; older firmware ignores the argument and
    // sends raw RGB565, which the parser recognises by the missing RLE8 header.
    const QSize lcd = m_info.model->lcd;
    enqueue({"capture rle", RxParser::Body::Capture, lcd.width() * lcd.height() * 2, 5000,
             [this, lcd](bool ok, const QStringList &) {
        if (!ok)
            return;
        QImage img(lcd, QImage::Format_RGB16);
        if (m_pixelsRle8) {
            if (!decodeRle8(m_pixels, img) || img.size() != lcd)
                return emit message("Screen capture: bad RLE8 data");
        } else if (m_pixels.size() == lcd.width() * lcd.height() * 2) {
            blitRgb565BE(img, img.rect(), m_pixels);
        } else {
            return;
        }
        emit traffic(false, QString("<screen %1x%2, %3 %4 bytes, %5 ms>").arg(lcd.width()).arg(lcd.height())
                                .arg(m_pixelsRle8 ? "RLE8" : "raw").arg(m_pixels.size()).arg(m_sentClock.elapsed()),
                     false);
        m_screen = img;
        emit screen(m_screen);
    }});
}

// While the unit streams screen pushes it still echoes typed commands, but from another
// thread: a push can land inside the echo. So in remote mode touches and `refresh off`
// are sent without waiting for echo and prompt, and input is drained afterwards.
void DeviceLink::setRemote(bool on)
{
    if (!m_ready || on == m_remote)
        return;
    if (on) {
        m_remote = true;
        m_modeBeforeRemote = m_mode;
        setMode(Off);  // resumes the unit's own sweeping and display
        capture();     // full picture first, then only the changes
        // compressed pushes and a scrolling waterfall; firmware without them answers with usage
        enqueue({"refresh rle", {}, 0, 3000, [this](bool, const QStringList &lines) {
            if (m_remote && lines.join(' ').contains("usage"))
                enqueue({"refresh on"});
        }});
    } else {
        enqueue({"", {}, 0, 0, [this](bool, const QStringList &) {  // after anything in flight
            flushTouch();
            m_touchTimer.stop();
            writeDirect("refresh off");
            m_remote = false;
            resync();  // pushes still in the pipe; their echo too
        }});
        setMode(m_modeBeforeRemote);
    }
}

void DeviceLink::writeDirect(const QByteArray &line)
{
    m_io->write(line + "\r");
    emit traffic(true, QString::fromLatin1(line), false);
}

void DeviceLink::flushTouch()
{
    if (!m_pendingTouch.isEmpty() && m_io)
        writeDirect(std::exchange(m_pendingTouch, {}));
}

void DeviceLink::touch(QPoint lcd, bool down)
{
    if (!m_ready)
        return;
    const QByteArray line = down ? "touch " + QByteArray::number(lcd.x()) + " " + QByteArray::number(lcd.y())
                                 : QByteArray("release");
    if (m_remote) {  // live screen: no echo wait; drags at most every 30 ms
        if (!down) {
            flushTouch();
            writeDirect(line);
        } else if (!m_touchTimer.isActive()) {
            writeDirect(line);
            m_touchTimer.start();
        } else {
            m_pendingTouch = line;
        }
        return;
    }
    if (down)  // dragging: update a touch that is still waiting instead of queueing more
        for (Cmd &c : m_queue)
            if (c.line.startsWith("touch ")) {
                c.line = line;
                return;
            }
    enqueue({line});
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
    if (m_batteryDue && !m_remote) {  // no polls while the screen streams
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
    m_sentClock.start();
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
        m_pixelsRle8 = e.rle8;
        break;
    case RxEvent::Region:  // pushed screen change (refresh on / rle)
        if (m_info.model && m_screen.size() != m_info.model->lcd) {
            m_screen = QImage(m_info.model->lcd, QImage::Format_RGB16);
            m_screen.fill(Qt::black);
        }
        if (e.scroll)
            scrollRgb16(m_screen, e.rect, qint16(quint8(e.data[0]) | quint8(e.data[1]) << 8));
        else if (e.fill)
            fillRgb565BE(m_screen, e.rect, e.data);
        else if (e.rle8)
            blitRle8(m_screen, e.rect, e.data);
        else
            blitRgb565BE(m_screen, e.rect, e.data);
        if (!m_screenTimer.isActive())
            m_screenTimer.start(40);
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
