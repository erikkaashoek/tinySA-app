#include "simdevice.h"
#include <QHash>
#include <cmath>
#include <cstring>

static const char *kHelpUltra =
    "commands: version reset freq time dac sweep_voltage nf saveconfig clearconfig zero sweep "
    "pause restart resume wait waitscan repeat status caloutput save recall trace trigger marker "
    "line usart_cfg vbat_offset color\r\n"
    "Other commands: data dump frequencies gamma scan hop scanraw abort test touchcal touchtest "
    "channel usart capture refresh touch release vbat help info threads rbw attenuate lna mode "
    "deviceid threshold\r\n";
static const char *kHelpBasic =
    "commands: version reset freq time dac saveconfig clearconfig data frequencies scan scanraw "
    "zero sweep pause resume caloutput save recall trace trigger marker capture refresh touch "
    "release vbat vbat_offset help info color rbw attenuate mode deviceid threshold\r\n";

SimTransport::SimTransport(Kind kind, QObject *parent)
    : QIODevice(parent), rleCapture(kind == Kind::Ultra), m_kind(kind)
{
    m_timer.setInterval(2);
    connect(&m_timer, &QTimer::timeout, this, &SimTransport::scanTick);
    if (kind == Kind::Ultra)
        m_sweepStop = 800000000;
    m_lcd = kind == Kind::Ultra ? QSize(480, 320) : QSize(320, 240);
    for (int y = 0; y < m_lcd.height(); ++y)  // dark screen, grid, a yellow sine "trace"
        for (int x = 0; x < m_lcd.width(); ++x) {
            quint16 c = x % 40 == 0 || y % 40 == 0 ? 0x4208 : 0x0000;
            if (std::abs(y - m_lcd.height() / 2 - int(m_lcd.height() / 4 * std::sin(x * 0.05))) < 2)
                c = 0xFFE0;
            m_screen += char(c >> 8);
            m_screen += char(c & 0xff);
        }
    m_pushTimer.setInterval(100);
    connect(&m_pushTimer, &QTimer::timeout, this, &SimTransport::pushTick);
}

// DiSlord's packbits() (NanoVNA-D common.c), byte for byte.
static int packbits(const char *source, char *dest, int size)
{
    int i = 0, rle, l, pk = 0, sz = -1;
    while (i < size) {
        l = size - i;
        if (l > 128)
            l = 128;
        const char c = source[i++];
        for (rle = 0; --l && c == source[i + rle];)
            rle++;
        if (sz >= 0 && rle < 2)
            rle = 0;
        else if (sz < 0 || rle > 0)
            sz = pk++;
        dest[pk++] = c;
        if (rle > 0) {
            i += rle;
            dest[sz] = char(-rle);
        } else if ((dest[sz] = char(pk - sz - 2)) < 127) {
            continue;
        }
        sz = -1;
    }
    return pk;
}

QByteArray SimTransport::packBits(const QByteArray &data)
{
    QByteArray out(data.size() + data.size() / 128 + 2, 0);
    out.resize(packbits(data.constData(), out.data(), int(data.size())));
    return out;
}

// NanoVNA-D capture_rle8(): header, 32-entry palette, PackBits rows of palette indices.
QByteArray SimTransport::rle8() const
{
    auto le16 = [](int v) { return QByteArray(1, char(v & 0xff)) + char((v >> 8) & 0xff); };
    const int w = m_lcd.width(), h = m_lcd.height();
    QByteArray palette(64, 0);  // MAX_PALETTE 32, as read from the LCD (big-endian RGB565)
    QHash<quint16, int> index;
    QByteArray out = QByteArray("BM") + le16(w) + le16(h) + char(8) + char(1) + le16(64);
    QList<QByteArray> rows;
    for (int y = 0; y < h; ++y) {
        QByteArray idx(w, 0);
        for (int x = 0; x < w; ++x) {
            const int at = 2 * (y * w + x);
            const quint16 c = quint16(quint8(m_screen[at]) << 8 | quint8(m_screen[at + 1]));
            if (!index.contains(c) && index.size() < 32) {
                const int i = int(index.size());
                index.insert(c, i);
                palette[2 * i] = m_screen[at];
                palette[2 * i + 1] = m_screen[at + 1];
            }
            idx[x] = char(index.value(c, 0));  // colours outside the palette -> 0, as the firmware
        }
        rows << packBits(idx);
    }
    out += palette;
    for (const QByteArray &r : rows)
        out += le16(int(r.size())) + r;
    return out;
}

void SimTransport::region(bool fill, int x, int y, int w, int h, quint16 colour)
{
    x = std::clamp(x, 0, m_lcd.width() - 1);
    y = std::clamp(y, 0, m_lcd.height() - 1);
    w = std::min(w, m_lcd.width() - x);
    h = std::min(h, m_lcd.height() - y);
    QByteArray px;
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            const quint16 v = fill ? colour : quint16(colour + 0x0841 * ((r + c) % 8));
            const int at = 2 * ((y + r) * m_lcd.width() + x + c);
            m_screen[at] = char(v >> 8);
            m_screen[at + 1] = char(v & 0xff);
            px += char(v >> 8);
            px += char(v & 0xff);
        }
    if (!m_pushTimer.isActive())
        return;
    auto le16 = [](int v) { return QByteArray(1, char(v & 0xff)) + char((v >> 8) & 0xff); };
    out((fill ? "fill\r\n" : "bulk\r\n") + le16(x) + le16(y) + le16(w) + le16(h)
        + (fill ? px.left(2) : px) + "ch> ");
}

void SimTransport::pushTick()
{
    if (m_scan)
        return;  // never inside scan data
    ++m_pushes;
    region(false, (m_pushes * 16) % (m_lcd.width() - 32), m_lcd.height() - 24, 32, 16, 0x001F);
}

QVector<SimTransport::Carrier> SimTransport::carriers()
{
    return {{100e6, -30}, {250e6, -50}, {433.92e6, -40}, {868e6, -45}, {2.4e9, -35}, {5.8e9, -55}};
}

double SimTransport::levelAt(double hz, double rbwHz) const
{
    const double floorDbm = (m_kind == Kind::Ultra ? -105 : -100) + std::max(m_atten, 0);
    double mw = std::pow(10.0, floorDbm / 10);
    const double half = std::max(rbwHz, 100.0) / 2;
    for (const Carrier &c : carriers()) {
        if (m_kind == Kind::Basic && c.hz > 960e6)
            continue;
        const double x = (hz - c.hz) / half;
        if (std::abs(x) < 40)  // -3 dB at +-rbw/2, Gaussian filter shape
            mw += std::pow(10.0, (c.dBm - 3.01 * x * x) / 10);
    }
    return 10 * std::log10(mw);
}

void SimTransport::touchScreen()
{
    if (!m_scan)
        return;
    m_scan.reset();
    m_timer.stop();
    out("}");  // aborted scanraw: no prompt
    processInput();
}

qint64 SimTransport::readData(char *data, qint64 maxlen)
{
    const qint64 n = std::min<qint64>(maxlen, m_out.size());
    std::memcpy(data, m_out.constData(), size_t(n));
    m_out.remove(0, n);
    return n;
}

qint64 SimTransport::writeData(const char *data, qint64 len)
{
    m_in.append(data, len);
    processInput();
    return len;
}

void SimTransport::out(const QByteArray &bytes)
{
    m_out += bytes;
    if (!m_readyPending) {
        m_readyPending = true;
        QMetaObject::invokeMethod(this, [this] {
            m_readyPending = false;
            emit readyRead();
        }, Qt::QueuedConnection);
    }
}

void SimTransport::processInput()
{
    // the shell thread reads (and echoes) nothing while a scan runs
    while (!m_scan && !m_in.isEmpty()) {
        const char c = m_in.at(0);
        m_in.remove(0, 1);
        if (c == '\r') {
            out("\r\n");
            const QByteArray line = m_line;
            m_line.clear();
            execute(line);
        } else if (c >= ' ') {
            m_line += c;
            out(QByteArray(1, c));
            if (m_pushTimer.isActive() && m_line.size() == 2)
                pushTick();  // a screen push inside the echo, as the firmware's threads allow
        }
    }
}

void SimTransport::execute(const QByteArray &line)
{
    m_received << line;
    const QByteArrayList tok = line.split(' ');
    const QByteArray cmd = tok.value(0), a1 = tok.value(1), a2 = tok.value(2);
    const bool ultra = m_kind == Kind::Ultra;

    if (cmd == "version") {
        out(ultra ? "tinySA4_v1.4-sim\r\nHW Version:V0.5.4 max2871\r\n" : "tinySA_v1.4-sim\r\n");
    } else if (cmd == "info") {
        out(ultra ? "tinySA ULTRA+ ZS407\r\n" : "tinySA v0.3ZS304\r\n");
        out("2019-2024 Copyright @Erik Kaashoek\r\n2016-2020 Copyright @edy555\r\n"
            "SW licensed under GPL. See: https://github.com/erikkaashoek/tinySA\r\n");
        out(ultra ? "Version: tinySA4_v1.4-sim\r\n" : "Version: tinySA_v1.4-sim\r\n");
    } else if (cmd == "help") {
        out(ultra ? kHelpUltra : kHelpBasic);
    } else if (cmd == "zero") {
        out(ultra ? "usage: zero {level}\r\n174dBm\r\n" : "usage: zero {level}\r\n128dBm\r\n");
    } else if (cmd == "sweep") {
        if (a1 == "start")
            m_sweepStart = a2.toLongLong();
        else if (a1 == "stop")
            m_sweepStop = a2.toLongLong();
        else if (a1.isEmpty())
            out(QByteArray::number(m_sweepStart) + " " + QByteArray::number(m_sweepStop) + " "
                + QByteArray::number(m_sweepPoints) + "\r\n");
    } else if (cmd == "vbat") {
        out("4101 mV\r\n");
    } else if (cmd == "vbat_offset") {
        out("300\r\n");
    } else if (cmd == "deviceid") {
        out("deviceid 0\r\n");
    } else if (cmd == "rbw") {
        m_rbwKHz = a1 == "auto" ? 0 : a1.toDouble();
    } else if (cmd == "attenuate") {
        m_atten = a1 == "auto" ? -1 : a1.toInt();
    } else if (cmd == "lna") {
        m_lna = a1 == "on";
    } else if (cmd == "pause" || cmd == "resume") {
        m_paused = cmd == "pause";
    } else if (cmd == "capture") {
        out(!a1.isEmpty() && rleCapture ? rle8() : m_screen);
    } else if (cmd == "refresh") {
        if (a1 == "on")
            m_pushTimer.start();
        else
            m_pushTimer.stop();
    } else if (cmd == "touch") {
        region(true, a1.toInt() - 4, a2.toInt() - 4, 8, 8, 0xFFFF);  // a white dot where touched
    } else if (cmd == "scanraw") {
        const qint64 s = a1.toLongLong(), e = a2.toLongLong();
        const int n = tok.size() > 3 ? tok[3].toInt() : m_sweepPoints;
        if (tok.size() < 3 || e < s || n <= 0) {
            out("frequency range is invalid\r\n");
        } else {
            m_scan = Scan{s, e, n, 0};
            m_clock.start();
            m_timer.start();
            return;  // '}' + prompt when the scan completes
        }
    } else if (!(cmd.isEmpty() || cmd == "mode" || cmd == "abort" || cmd == "release" || cmd == "reset"
                 || cmd == "saveconfig"
                 || cmd == "marker" || cmd == "threshold")) {
        out(cmd + "?\r\n");
    }
    out("ch> ");
}

void SimTransport::scanTick()
{
    if (m_clock.elapsed() >= shellWaitMs && m_in.startsWith("abort\r")) {
        // shell thread: echo + prompt of the direct command, between data chunks;
        // sweep thread: sees the abort, ends the data with '}' and no prompt
        m_in.remove(0, 6);
        m_received << "abort";
        out("abort\r\nch> }");
        m_scan.reset();
        m_timer.stop();
        processInput();
        return;
    }
    Scan &s = *m_scan;
    const int due = std::min<int>(s.points, int(m_clock.elapsed() * pointsPerSecond / 1000) + 1);
    const double step = double(s.stop - s.start) / s.points;  // as firmware: /points
    const double rbw = m_rbwKHz > 0 ? m_rbwKHz * 1e3 : std::max(step * 1.5, 1e3);
    const int zero = m_kind == Kind::Ultra ? 174 : 128;
    while (s.next + 20 <= due || (s.next < due && due == s.points)) {
        QByteArray chunk = s.next == 0 ? "{" : "";
        for (int k = 0; k < 20 && s.next < due; ++k, ++s.next) {
            const double hz = s.start + step * s.next;
            // deterministic +-1.5 dB noise, different every sweep
            const unsigned h = (unsigned(s.next) * 2654435761u) ^ (m_sweeps * 40503u);
            const double noise = ((h >> 8) % 3000) / 1000.0 - 1.5;
            const int v = int(std::lround((levelAt(hz, rbw) + noise + zero) * 32));
            chunk += 'x';
            chunk += char(v & 0xff);
            chunk += char((v >> 8) & 0xff);
        }
        out(chunk);
    }
    if (s.next >= s.points) {
        m_scan.reset();
        m_timer.stop();
        ++m_sweeps;
        out("}ch> ");
        processInput();
    }
}
