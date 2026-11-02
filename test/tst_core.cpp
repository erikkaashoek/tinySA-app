// Unit tests for src/core. One executable, one test class per module.
#include "csvio.h"
#include "dfufile.h"
#include "dfuse.h"
#include "link.h"
#include "markers.h"
#include "model.h"
#include "protocol.h"
#include "simdevice.h"
#include "sweepconfig.h"
#include "traces.h"
#include "units.h"
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QPointer>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTest>
#include <cmath>

class TstUnits : public QObject {
    Q_OBJECT
private slots:
    void rangeSettersKeepTheirAnchor()
    {
        FreqRange r(100e6, 200e6);
        r.setLimits(0, 1e9);
        r.setStart(120e6);
        QCOMPARE(r.stop(), 200e6);
        r.setCenter(500e6);
        QCOMPARE(r.span(), 80e6);
        QCOMPARE(r.center(), 500e6);
        r.setSpan(20e6);
        QCOMPARE(r.center(), 500e6);
        QCOMPARE(r.span(), 20e6);
    }
    void rangeClampsToLimitsAndMinSpan()
    {
        FreqRange r(100e6, 200e6);
        r.setLimits(0, 350e6, 10e3);
        r.setStop(900e6);
        QCOMPARE(r.stop(), 350e6);
        r.setStart(350e6);  // would collapse the span
        QCOMPARE(r.span(), 10e3);
        QCOMPARE(r.stop(), 350e6);
        r.set(-5, 5);
        QCOMPARE(r.start(), 0.0);
        QCOMPARE(r.span(), 10e3);
        r.setCenter(349.999e6);  // shifted back inside, span kept
        QCOMPARE(r.stop(), 350e6);
        QCOMPARE(r.span(), 10e3);
    }
    void zoomKeepsAnchorAndPanKeepsSpan()
    {
        FreqRange r(100e6, 200e6);
        r.setLimits(0, 1e9);
        r.zoom(0.5, 125e6);
        QCOMPARE(r.start(), 112.5e6);
        QCOMPARE(r.stop(), 162.5e6);
        r.pan(-500e6);
        QCOMPARE(r.start(), 0.0);
        QCOMPARE(r.span(), 50e6);
        r.zoom(1e6, 0);
        QCOMPARE(r.span(), 1e9);
    }
    void scanGridMatchesFirmwareStep()
    {
        ScanGrid g{100e6, 200e6, 101};
        QCOMPARE(g.step(), 1e6);
        // firmware step = (stop'-start)/points must equal our step
        QCOMPARE((g.scanrawStop() - 100e6) / 101, 1e6);
        QCOMPARE(g.nearest(150.4e6), 50);
        QCOMPARE(g.nearest(1e12), 100);
    }
    void niceSteps()
    {
        QCOMPARE(niceStep(0.9), 1.0);
        QCOMPARE(niceStep(1.1), 2.0);
        QCOMPARE(niceStep(2.2), 2.5);
        QCOMPARE(niceStep(3), 5.0);
        QCOMPARE(niceStep(6e6), 10e6);
        QCOMPARE(niceStep(2e6), 2e6);
    }
    void formatAndParse()
    {
        QCOMPARE(formatHz(433.92e6, 1e3), QString("433.920 MHz"));
        QCOMPARE(formatHz(2.4e9, 1e6), QString("2.400 GHz"));
        QCOMPARE(formatHz(500, 1), QString("500 Hz"));
        QCOMPARE(parseHz("433.92").value(), 433.92e6);
        QCOMPARE(parseHz("433,92").value(), 433.92e6);
        QCOMPARE(parseHz("100k").value(), 100e3);
        QCOMPARE(parseHz("1.2 GHz").value(), 1.2e9);
        QCOMPARE(parseHz("50 Hz").value(), 50.0);
        QVERIFY(!parseHz("abc"));
        QVERIFY(!parseHz(""));
    }
};

// ---------------------------------------------------------------------------

static QByteArray point(int raw)
{
    return QByteArray("x") + char(raw & 0xff) + char((raw >> 8) & 0xff);
}

// Plays a conversation: for each command, expect() is armed (as DeviceLink does
// when it transmits) and the bytes the unit sends back are fed in random chunks.
struct Step {
    QByteArray cmd;
    RxParser::Body body;
    int arg;
    QByteArray reply;
};
static QVector<RxEvent> play(const QVector<Step> &steps, int maxChunk)
{
    RxParser p;
    QVector<RxEvent> all;
    for (const Step &st : steps) {
        p.expect(st.cmd, st.body, st.arg);
        for (int at = 0; at < st.reply.size();) {
            const int n = maxChunk <= 0 ? int(st.reply.size())
                                        : 1 + QRandomGenerator::global()->bounded(maxChunk);
            for (const RxEvent &e : p.feed(st.reply.mid(at, n))) {
                if (e.type == RxEvent::ScanData && !all.isEmpty()
                    && all.last().type == RxEvent::ScanData)
                    all.last().values += e.values;  // batching depends on chunking
                else
                    all.append(e);
            }
            at += n;
        }
    }
    return all;
}

static QString describe(const QVector<RxEvent> &ev)
{
    QStringList s;
    for (const RxEvent &e : ev) {
        switch (e.type) {
        case RxEvent::Line: s << "L:" + QString::fromLatin1(e.data); break;
        case RxEvent::Echo: s << "E:" + QString::fromLatin1(e.data); break;
        case RxEvent::Prompt: s << "P"; break;
        case RxEvent::ScanData: {
            QStringList v;
            for (qint16 x : e.values) v << QString::number(x);
            s << "D:" + v.join(',');
            break;
        }
        case RxEvent::ScanEnd: s << QString("}%1").arg(e.count); break;
        case RxEvent::Pixels: s << QString("PX:%1").arg(e.data.size()); break;
        case RxEvent::Region:
            s << QString("R:%1,%2,%3,%4,%5,%6%7").arg(e.rect.x()).arg(e.rect.y())
                     .arg(e.rect.width()).arg(e.rect.height()).arg(e.fill).arg(e.data.size())
                     .arg(e.rle8 ? ",rle8" : e.scroll ? ",scrl" : "");
            break;
        }
    }
    return s.join(' ');
}

class TstProtocol : public QObject {
    Q_OBJECT
private slots:
    void textReplyIgnoresStaleOutput()
    {
        const auto ev = play({{"version", RxParser::Body::Text, 0,
                               "abort\r\nch> version\r\ntinySA4_v1.4\r\nHW Version:V0.5.4\r\nch> "}},
                             0);
        QCOMPARE(describe(ev), QString("L:abort P E:version L:tinySA4_v1.4 L:HW Version:V0.5.4 P"));
    }
    void scanrawCaptureAndPushesAreChunkingInvariant()
    {
        const QVector<Step> steps{
            {"scanraw 0 1000 4", RxParser::Body::ScanRaw, 4,
             "scanraw 0 1000 4\r\n{" + point(100) + point(-32) + point(0x1234) + point(7) + "}ch> "},
            {"capture", RxParser::Body::Capture, 24, "capture\r\n" + QByteArray(24, 'U') + "ch> "},
            {"vbat", RxParser::Body::Text, 0,
             "bulk\r\n" + QByteArray("\x02\x00\x03\x00\x02\x00\x01\x00", 8) + "ABCD" + "ch> "
                 + "fill\r\n" + QByteArray("\x00\x00\x00\x00\x04\x00\x04\x00", 8)
                 + QByteArray("\xf8\x00", 2) + "ch> vbat\r\n4101 mV\r\nch> "}};
        const QString want = "E:scanraw 0 1000 4 D:100,-32,4660,7 }4 P E:capture PX:24 P "
                             "R:2,3,2,1,0,4 R:0,0,4,4,1,2 E:vbat L:4101 mV P";
        QCOMPARE(describe(play(steps, 0)), want);
        for (int i = 0; i < 200; ++i)
            QCOMPARE(describe(play(steps, 1 + i % 64)), want);
    }
    void abortedScanHasNoPrompt()
    {
        const auto ev = play({{"scanraw 0 1000 10", RxParser::Body::ScanRaw, 10,
                               "scanraw 0 1000 10\r\n{" + point(1) + point(2) + "}"},
                              {"resume", RxParser::Body::Text, 0, "resume\r\nch> "}},
                             3);
        QCOMPARE(describe(ev), QString("E:scanraw 0 1000 10 D:1,2 }2 E:resume P"));
    }
    void outOfBandTextInsideScan()
    {
        const auto ev = play({{"scanraw 0 1 2", RxParser::Body::ScanRaw, 2,
                               "scanraw 0 1 2\r\n{" + point(5) + "Command timeout\r\nch> "
                                   + point(6) + "}ch> "}},
                             0);
        QCOMPARE(describe(ev), QString("E:scanraw 0 1 2 D:5 L:Command timeout D:6 }2 P"));
    }
    void refusedScanFallsBackToText()
    {
        const auto ev = play({{"scanraw 5 1 2", RxParser::Body::ScanRaw, 2,
                               "scanraw 5 1 2\r\nfrequency range is invalid\r\nch> "}},
                             0);
        QCOMPARE(describe(ev), QString("E:scanraw 5 1 2 L:frequency range is invalid P"));
    }
    void packBitsRoundTrip()
    {
        QList<QByteArray> cases{QByteArray(480, 0), QByteArray(1, 7), QByteArray("ab"),
                                QByteArray(128, 5), QByteArray(129, 5), QByteArray(257, 9)};
        QByteArray mixed;
        for (int i = 0; i < 480; ++i)  // runs of 1..9 of changing values
            mixed += char((i / (1 + i % 9)) & 0x1f);
        cases << mixed;
        QByteArray noise;
        for (int i = 0; i < 480; ++i)
            noise += char(QRandomGenerator::global()->bounded(256));
        cases << noise;
        for (const QByteArray &c : cases) {
            const QByteArray packed = SimTransport::packBits(c);
            QCOMPARE(unpackBits(packed.constData(), int(packed.size()), int(c.size())), c);
        }
        QVERIFY(SimTransport::packBits(QByteArray(480, 0)).size() <= 8);  // 4 runs of 128/96
    }
    void captureRle8OrRawIsDetected()
    {
        // 2x2 image, palette {black, red}: rows "0 1" / "1 1"
        auto le16 = [](int v) { return QByteArray(1, char(v & 0xff)) + char((v >> 8) & 0xff); };
        QByteArray rle = QByteArray("BM") + le16(2) + le16(2) + char(8) + char(1) + le16(4)
                         + QByteArray("\x00\x00\xf8\x00", 4);
        rle += le16(3) + QByteArray("\x01\x00\x01", 3);  // literal 2: 0,1
        rle += le16(2) + QByteArray("\xff\x01", 2);      // repeat 2: 1
        for (int chunk : {0, 1, 3, 7}) {
            const auto ev = play({{"capture rle", RxParser::Body::Capture, 8,
                                   "capture rle\r\n" + rle + "ch> "}}, chunk);
            QCOMPARE(describe(ev), QString("E:capture rle PX:%1 P").arg(rle.size()));
            QVERIFY(ev[1].rle8);
            QImage img;
            QVERIFY(decodeRle8(ev[1].data, img));
            QCOMPARE(img.size(), QSize(2, 2));
            QCOMPARE(img.pixelColor(0, 0), QColor(Qt::black));
            QCOMPARE(img.pixelColor(1, 0).red(), 255);
            QCOMPARE(img.pixelColor(0, 1).red(), 255);
        }
        // old firmware ignores "rle" and sends 2x2 raw pixels
        const auto raw = play({{"capture rle", RxParser::Body::Capture, 8,
                                "capture rle\r\n" + QByteArray(8, '\x11') + "ch> "}}, 3);
        QCOMPARE(describe(raw), QString("E:capture rle PX:8 P"));
        QVERIFY(!raw[1].rle8);
    }
    void pushInsideAnEcho()
    {
        const QByteArray push = "bulk\r\n" + QByteArray("\x00\x00\x00\x00\x01\x00\x01\x00", 8) + "AB" + "ch> ";
        for (int chunk : {0, 1, 5}) {
            const auto ev = play({{"refresh off", RxParser::Body::Text, 0,
                                   "ref" + push + "resh off\r\nch> "}}, chunk);
            QCOMPARE(describe(ev), QString("R:0,0,1,1,0,2 E:refresh off P"));
        }
    }
    void refreshRlePushes()
    {
        // "rle8": 2x2 at 3,4, palette {black, red}, indices 0 1 1 1 as one PackBits stream
        // (literal 1: 0, then 3 x 1); "scrl": rows 0..9 of x 0..1 down by 1
        auto le16 = [](int v) { return QByteArray(1, char(v & 0xff)) + char((v >> 8) & 0xff); };
        const QByteArray rle8 = "rle8\r\n" + le16(3) + le16(4) + le16(2) + le16(2) + le16(4)
                                + QByteArray("\x00\x00\xf8\x00", 4) + QByteArray("\x00\x00\xfe\x01", 4) + "ch> ";
        const QByteArray scrl = "scrl\r\n" + le16(0) + le16(0) + le16(2) + le16(10) + le16(1) + "ch> ";
        QCOMPARE(SimTransport::packBits(QByteArray("\x00\x01\x01\x01", 4)), QByteArray("\x00\x00\xfe\x01", 4));
        for (int chunk : {0, 1, 3, 7}) {
            const auto ev = play({{"refresh rle", RxParser::Body::Text, 0,
                                   "refr" + rle8 + "esh rle\r\n" + scrl + "ch> "}}, chunk);
            QCOMPARE(describe(ev), QString("R:3,4,2,2,0,10,rle8 E:refresh rle R:0,0,2,10,0,2,scrl P"));
            QImage img(6, 12, QImage::Format_RGB16);
            img.fill(Qt::white);
            QVERIFY(blitRle8(img, ev[0].rect, ev[0].data));
            QCOMPARE(img.pixelColor(3, 4), QColor(Qt::black));
            QCOMPARE(img.pixelColor(4, 4).red(), 255);
            QCOMPARE(img.pixelColor(3, 5).red(), 255);
            QCOMPARE(img.pixelColor(2, 4), QColor(Qt::white));  // outside the region
        }
    }
    void scrollMovesRowsDown()
    {
        QImage img(3, 4, QImage::Format_RGB16);
        auto px = [&](int x, int y) -> quint16 & { return reinterpret_cast<quint16 *>(img.scanLine(y))[x]; };
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 3; ++x)
                px(x, y) = quint16(10 * y + x);
        scrollRgb16(img, QRect(0, 1, 2, 3), 1);  // rows 1..3 of x 0..1 down by 1: row 3 drops off
        QCOMPARE(px(0, 0), quint16(0));
        QCOMPARE(px(0, 1), quint16(10));  // left as it was: the caller draws the new top row
        QCOMPARE(px(0, 2), quint16(10));
        QCOMPARE(px(1, 3), quint16(21));
        QCOMPARE(px(2, 3), quint16(32));  // outside the rectangle
    }
    void rgb565IsBigEndian()
    {
        QImage img(2, 1, QImage::Format_RGB16);
        blitRgb565BE(img, img.rect(), QByteArray("\xf8\x00\x07\xe0", 4));
        QCOMPARE(img.pixelColor(0, 0).red(), 255);
        QCOMPARE(img.pixelColor(1, 0).green(), 255);
    }
};

// ---------------------------------------------------------------------------

class TstModel : public QObject {
    Q_OBJECT
private slots:
    void detectFromInfo()
    {
        QCOMPARE(detectModel("tinySA v0.3ZS304\n2019-2024 Copyright", "")->name, QString("tinySA"));
        QCOMPARE(detectModel("tinySA ULTRA ZS405\n", "")->name, QString("tinySA Ultra"));
        QCOMPARE(detectModel("tinySA ULTRA+ ZS407\n", "")->name, QString("tinySA Ultra+"));
        QCOMPARE(detectModel("", "tinySA4_v1.4-100")->name, QString("tinySA Ultra"));
        QCOMPARE(detectModel("", "tinySA_v1.4-100")->name, QString("tinySA"));
        QVERIFY(!detectModel("NanoVNA-H 4\n", "1.2.00"));
    }
    void helpAndZero()
    {
        const QStringList cmds = parseHelp({"commands: version zero", "Other commands: scanraw vbat"});
        QCOMPARE(cmds, QStringList({"version", "zero", "scanraw", "vbat"}));
        QCOMPARE(parseZero({"usage: zero {level}", "174dBm"}, 0), 174);
        QCOMPARE(parseZero({"zero?"}, 128), 128);
    }
    void configDiffSendsOnlyChanges()
    {
        const ModelDesc &basic = *detectModel("tinySA v0.3ZS304", "");
        AppliedConfig have;
        SweepConfig c;
        c.grid = {100e6, 200e6, 450};
        QCOMPARE(configCommands(c, have, basic),
                 QByteArrayList({"mode low input", "attenuate auto", "rbw auto",
                                 "sweep start 100000000", "sweep stop 200000000"}));
        QVERIFY(configCommands(c, have, basic).isEmpty());
        c.rbwKHz = 30;
        QCOMPARE(configCommands(c, have, basic), QByteArrayList({"rbw 30"}));
        c.input = InputMode::High;  // mode switch reloads rbw/atten/range on the unit
        QCOMPARE(configCommands(c, have, basic),
                 QByteArrayList({"mode high input", "attenuate auto", "rbw 30",
                                 "sweep start 100000000", "sweep stop 200000000"}));
        const ModelDesc &ultra = *detectModel("tinySA ULTRA ZS405", "");
        AppliedConfig u;
        c.lna = true;
        QVERIFY(configCommands(c, u, ultra).contains("lna on"));
        QVERIFY(!configCommands(c, u, ultra).contains("mode high input"));
    }
    void clampToModel()
    {
        const ModelDesc &basic = *detectModel("tinySA v0.3ZS304", "");
        SweepConfig c;
        c.grid = {100e6, 2e9, 100000};
        c.rbwKHz = 850;
        c.lna = true;
        c = clampConfig(c, basic);
        QCOMPARE(c.grid.stopHz, 350e6);
        QCOMPARE(c.grid.points, 30000);
        QCOMPARE(c.rbwKHz, 600.0);
        QVERIFY(!c.lna);
    }
};

// ---------------------------------------------------------------------------

class TstLink : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qRegisterMetaType<SweepFrame>(); qRegisterMetaType<DeviceInfo>(); }

    void usbIdsOfBothFirmwares()
    {
        QVERIFY(DeviceLink::isTinySAUsbId(0x0483, 0x5740));
        QVERIFY(DeviceLink::isTinySAUsbId(0x1781, 0x0A44));
        QVERIFY(!DeviceLink::isTinySAUsbId(0x0483, 0x5741));  // tinyGTC
        QVERIFY(!DeviceLink::isTinySAUsbId(0x0483, 0xDF11));  // DFU bootloader
        QVERIFY(!DeviceLink::isTinySAUsbId(0x1781, 0x5740));
        QVERIFY(!DeviceLink::isTinySAUsbId(0, 0));            // port without USB IDs
    }

    void handshakeDetectsSimUltra()
    {
        DeviceLink link;
        QSignalSpy opened(&link, &DeviceLink::opened);
        link.open("sim:ultra");
        QVERIFY(opened.wait(3000));
        const auto info = opened.first().first().value<DeviceInfo>();
        QCOMPARE(info.model->name, QString("tinySA Ultra+"));
        QCOMPARE(info.hardware, QString("ZS407"));
        QCOMPARE(info.zeroDbm, 174);
        QVERIFY(info.has("scanraw"));
        QCOMPARE(info.version, QString("tinySA4_v1.4-sim"));
    }

    void singleSweepLandsOnTheGrid()
    {
        DeviceLink link;
        QSignalSpy opened(&link, &DeviceLink::opened), frames(&link, &DeviceLink::frame),
            scan(&link, &DeviceLink::scanChanged);
        link.open("sim:basic");
        QVERIFY(opened.wait(3000));
        SweepConfig c;
        c.grid = {50e6, 150e6, 101};
        link.setConfig(c);
        link.setScan(DeviceLink::Once);
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty() && frames.last().first().value<SweepFrame>().complete, 5000);
        const auto f = frames.last().first().value<SweepFrame>();
        QCOMPARE(f.received, 101);
        QCOMPARE(f.grid.freq(50), 100e6);
        QVERIFY(std::abs(f.dBm[50] - -30) < 2);  // 100 MHz carrier exactly on point 50
        QVERIFY(f.dBm[10] < -90);
        QTRY_COMPARE_WITH_TIMEOUT(scan.last().first().toInt(), int(DeviceLink::Off), 2000);

        auto *sim = qobject_cast<SimTransport *>(link.transport());
        QTRY_VERIFY_WITH_TIMEOUT(sim->received().contains("resume"), 2000);
        const QByteArrayList rx = sim->received();
        const int scanAt = rx.indexOf("scanraw 50000000 151000000 101");
        QVERIFY(scanAt > 0);
        QVERIFY(rx.indexOf("pause") < scanAt);
        QVERIFY(rx.indexOf("rbw auto") < scanAt);
        QVERIFY(rx.indexOf("resume") > scanAt);
    }

    void touchOnTheUnitStopsScanning()
    {
        DeviceLink link;
        QSignalSpy opened(&link, &DeviceLink::opened), frames(&link, &DeviceLink::frame),
            scan(&link, &DeviceLink::scanChanged), msg(&link, &DeviceLink::message);
        link.open("sim:ultra");
        QVERIFY(opened.wait(3000));
        auto *sim = qobject_cast<SimTransport *>(link.transport());
        sim->pointsPerSecond = 2000;
        SweepConfig c;
        c.grid = {1e6, 900e6, 3000};
        link.setConfig(c);
        link.setScan(DeviceLink::Continuous);
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 3000);  // mid-sweep progress frame
        link.simTouch();
        QTRY_COMPARE_WITH_TIMEOUT(scan.last().first().toInt(), int(DeviceLink::Off), 2000);
        QVERIFY(!msg.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(sim->received().contains("resume"), 2000);
        // the link is still usable afterwards
        QSignalSpy reply(&link, &DeviceLink::reply);
        link.send("vbat");
        QVERIFY(reply.wait(2000));
        QCOMPARE(reply.first().at(1).toStringList(), QStringList({"4101 mV"}));
    }

    void captureDecodesScreen_data()
    {
        QTest::addColumn<QString>("port");
        QTest::addColumn<QSize>("size");
        QTest::addColumn<bool>("rle");
        QTest::newRow("ultra, RLE8") << "sim:ultra" << QSize(480, 320) << true;
        QTest::newRow("basic, raw (old firmware)") << "sim:basic" << QSize(320, 240) << false;
    }
    void captureDecodesScreen()
    {
        QFETCH(QString, port);
        QFETCH(QSize, size);
        QFETCH(bool, rle);
        DeviceLink link;
        QSignalSpy opened(&link, &DeviceLink::opened), screen(&link, &DeviceLink::screen),
            traffic(&link, &DeviceLink::traffic);
        link.open(port);
        QVERIFY(opened.wait(3000));
        auto *sim = qobject_cast<SimTransport *>(link.transport());
        link.capture();
        QVERIFY(screen.wait(3000));
        const QImage img = screen.first().first().value<QImage>();
        QCOMPARE(img.size(), size);
        QImage want(size, QImage::Format_RGB16);
        blitRgb565BE(want, want.rect(), sim->screenBytes());
        QCOMPARE(img, want);  // lossless: every sim colour is in the palette
        bool sawRle = false;
        for (const auto &t : traffic)
            sawRle |= t.at(1).toString().contains("RLE8");
        QCOMPARE(sawRle, rle);
        QVERIFY(sim->received().contains("capture rle"));
    }

    void remoteMirrorsScreen_data()
    {
        QTest::addColumn<QString>("port");
        QTest::addColumn<bool>("rle");
        QTest::newRow("ultra, refresh rle") << "sim:ultra" << true;
        QTest::newRow("basic, falls back to refresh on") << "sim:basic" << false;
    }
    void remoteMirrorsScreen()
    {
        QFETCH(QString, port);
        QFETCH(bool, rle);
        DeviceLink link;
        QSignalSpy opened(&link, &DeviceLink::opened), screen(&link, &DeviceLink::screen);
        link.open(port);
        QVERIFY(opened.wait(3000));
        auto *sim = qobject_cast<SimTransport *>(link.transport());
        link.setRemote(true);
        QTRY_VERIFY_WITH_TIMEOUT(screen.size() >= 6, 5000);  // the capture, then pushes (rle: scrolls too)
        sim->stopPushes();
        QImage want(sim->screenBytes().size() == 480 * 320 * 2 ? QSize(480, 320) : QSize(320, 240),
                    QImage::Format_RGB16);
        blitRgb565BE(want, want.rect(), sim->screenBytes());
        QTRY_COMPARE_WITH_TIMEOUT(screen.last().first().value<QImage>(), want, 2000);  // pixel exact
        const QByteArrayList rx = sim->received();
        QVERIFY(rx.contains("refresh rle"));
        QCOMPARE(rx.contains("refresh on"), !rle);
        link.setRemote(false);
        QTRY_VERIFY_WITH_TIMEOUT(sim->received().contains("refresh off"), 3000);
    }

    void stopAbortsALongScan()
    {
        DeviceLink link;
        QSignalSpy opened(&link, &DeviceLink::opened), frames(&link, &DeviceLink::frame),
            busy(&link, &DeviceLink::busyChanged), msg(&link, &DeviceLink::message);
        link.open("sim:ultra");
        QVERIFY(opened.wait(3000));
        auto *sim = qobject_cast<SimTransport *>(link.transport());
        sim->pointsPerSecond = 500;  // 3000 points take 6 s
        sim->shellWaitMs = 300;
        SweepConfig c;
        c.grid = {1e6, 900e6, 3000};
        link.setConfig(c);
        link.setScan(DeviceLink::Continuous);
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 3000);
        QElapsedTimer t;
        t.start();
        link.setScan(DeviceLink::Off);
        QTRY_VERIFY_WITH_TIMEOUT(!busy.isEmpty() && !busy.last().first().toBool(), 3000);
        QVERIFY(t.elapsed() < 2000);  // not the rest of the 6 s sweep
        QVERIFY(msg.isEmpty());       // our own abort is not reported as a touch
        QVERIFY(sim->received().contains("abort"));
        QCOMPARE(sim->received().last(), QByteArray("resume"));
    }

    void quitRightAfterStopStillResumesTheUnit()
    {
        DeviceLink link;
        QSignalSpy opened(&link, &DeviceLink::opened), frames(&link, &DeviceLink::frame);
        link.open("sim:basic");
        QVERIFY(opened.wait(3000));
        QPointer<SimTransport> sim = qobject_cast<SimTransport *>(link.transport());
        sim->pointsPerSecond = 500;
        SweepConfig c;
        c.grid = {1e6, 300e6, 3000};
        link.setConfig(c);
        link.setScan(DeviceLink::Continuous);
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 3000);
        link.setScan(DeviceLink::Off);  // resume queued behind the running scan...
        link.close();                   // ...and the app quits at once
        QVERIFY(sim);                   // deleted later
        QCOMPARE(sim->pendingInput(), QByteArray("abort\rresume\r"));
    }

    void notATinySA()
    {
        QCOMPARE(detectModel("NanoVNA-H", "1.0"), nullptr);
    }
};

// ---------------------------------------------------------------------------

static SweepFrame frame(const ScanGrid &g, QVector<double> dBm, bool complete = true, int received = -1)
{
    SweepFrame f{1, g, dBm};
    f.received = received < 0 ? int(dBm.size()) : received;
    f.complete = complete;
    return f;
}

class TstTraces : public QObject {
    Q_OBJECT
private slots:
    void averagesPower()
    {
        const ScanGrid g{0, 100, 2};
        TraceSet t;
        t.setAverage(2);
        t.add(frame(g, {-40, -40}));
        t.add(frame(g, {-30, -30}));
        QVERIFY(std::abs(t.live().dBm[0] - 10 * std::log10((1e-4 + 1e-3) / 2)) < 1e-9);
        t.add(frame(g, {-30, -30}));  // window of 2: the -40 sweep has left
        QVERIFY(std::abs(t.live().dBm[0] - -30) < 1e-9);
    }
    void countsAndRestartsTheAverage()
    {
        const ScanGrid g{0, 100, 2};
        TraceSet t;
        t.setAverage(4);
        QCOMPARE(t.averaged(), 0);
        for (int i = 1; i <= 6; ++i) {
            t.add(frame(g, {-40, -40}));
            QCOMPARE(t.averaged(), std::min(i, 4));  // 1/4, 2/4, 3/4, 4/4, then stays 4/4
        }
        t.add(frame(g, {-10, qQNaN()}, false, 1));  // a partial sweep does not count
        QCOMPARE(t.averaged(), 4);
        t.setHold(Hold::Max);
        t.add(frame(g, {-20, -20}));
        t.restartAverage();                         // average and max hold forget the -40 / -20 sweeps
        QCOMPARE(t.averaged(), 0);
        t.add(frame(g, {-60, -60}));
        QCOMPARE(t.averaged(), 1);
        QVERIFY(std::abs(t.live().dBm[0] - -60) < 1e-9);
        t.setAverage(8);                            // a new length starts over too
        QCOMPARE(t.averaged(), 0);
    }
    void holdsAndRestarts()
    {
        const ScanGrid g{0, 100, 3};
        TraceSet t;
        t.setHold(Hold::Max);
        t.add(frame(g, {-50, -20, -60}));
        t.add(frame(g, {-40, -30, -70}));
        QCOMPARE(t.live().dBm, QVector<double>({-40, -20, -60}));
        t.setHold(Hold::Min);
        t.add(frame(g, {-40, -30, -70}));
        t.add(frame(g, {-45, -10, -65}));
        QCOMPARE(t.live().dBm, QVector<double>({-45, -30, -70}));
        t.add(frame({0, 200, 3}, {-1, -2, -3}));  // new range: history restarts
        QCOMPARE(t.live().dBm, QVector<double>({-1, -2, -3}));
    }
    void partialFramesKeepThePreviousSweep()
    {
        const ScanGrid g{0, 100, 3};
        TraceSet t;
        t.add(frame(g, {-50, -50, -50}));
        t.add(frame(g, {-10, qQNaN(), qQNaN()}, false, 1));  // sweep cursor at point 1
        QCOMPARE(t.live().dBm, QVector<double>({-10, -50, -50}));
    }
    void normalizesAgainstTheReference()
    {
        TraceSet t;
        t.add(frame({0, 100, 3}, {-50, -40, -30}));
        t.setNormalize(true);
        QCOMPARE(t.normalizedView(t.live()).dBm, QVector<double>({0, 0, 0}));
        const Trace other{{0, 50, 3}, {-50, -45, -40}};  // reference interpolated: -50 -45 -40
        QCOMPARE(t.normalizedView(other).dBm, QVector<double>({0, 0, 0}));
        t.setNormalize(false);
        QCOMPARE(t.normalizedView(other).dBm, other.dBm);
        QVERIFY(std::isnan(other.at(60)));
        QCOMPARE(other.at(37.5), -42.5);
    }
    void memoriesAreCopies()
    {
        TraceSet t;
        t.add(frame({0, 100, 2}, {-50, -40}));
        t.store(1);
        t.add(frame({0, 100, 2}, {-20, -20}));
        QCOMPARE(t.memory(1).dBm, QVector<double>({-50, -40}));
        QVERIFY(t.memory(0).isEmpty());
    }
};

class TstCsv : public QObject {
    Q_OBJECT
private slots:
    void roundTripBothFormats()
    {
        const Trace t{{100e6, 200e6, 3}, {-45.25, qQNaN(), -30}};
        for (const CsvFormat fmt : {CsvFormat{';', true}, CsvFormat{',', false}, CsvFormat{'\t', false}}) {
            const QString path = QDir::tempPath() + "/tst_trace.csv";
            QString err;
            QVERIFY(writeTraceCsv(path, t, fmt, &err, "title"));
            const auto back = readTraceCsv(path, &err);
            QVERIFY2(back.has_value(), qPrintable(err));
            QCOMPARE(back->grid, t.grid);
            QCOMPARE(back->dBm[0], -45.25);
            QVERIFY(std::isnan(back->dBm[1]));
            QFile::remove(path);
        }
    }
    void rejectsUnevenFrequencies()
    {
        const QString path = QDir::tempPath() + "/tst_uneven.csv";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("1000;-10\n2000;-11\n3500;-12\n");
        f.close();
        QString err;
        QVERIFY(!readTraceCsv(path, &err));
        QVERIFY(err.contains("evenly"));
        QFile::remove(path);
    }
};

class TstMarkers : public QObject {
    Q_OBJECT
private slots:
    void parabolicPeakFindsTheVertex()
    {
        // samples of -20 - (f - 1003.3)^2 / 50 at 1000..1010: vertex between points
        Trace t{{1000, 1010, 11}, {}};
        for (int i = 0; i < 11; ++i)
            t.dBm << -20 - std::pow(1000 + i - 1003.3, 2) / 50;
        const QPointF max = tracePeak(t, true);
        QVERIFY(std::abs(max.x() - 1003.3) < 1e-6);
        QVERIFY(std::abs(max.y() - -20) < 1e-9);
        QCOMPARE(tracePeak(t, false).x(), 1010.0);  // the minimum sits at the edge
        QVERIFY(std::isnan(tracePeak(Trace{}, true).x()));
    }
    void peakRobustToQuantisedLevels()
    {
        // Gaussian RBW response (-3 dB at +-15 kHz) sampled every 10 kHz and rounded to the
        // unit's 0.5 dB steps, for carriers at several offsets between two points
        for (double offset : {0.0, 1.5, 3.0, 4.2, 5.0, 6.8}) {
            const double f0 = 110e6 + offset * 1e3;
            Trace t{{109.9e6, 110.1e6, 21}, {}};
            for (int i = 0; i < 21; ++i) {
                const double x = (t.grid.freq(i) - f0) / 15e3;
                t.dBm << std::round((-56 - 3.01 * x * x) * 2) / 2;
            }
            const double err = tracePeak(t, true).x() - f0;
            QVERIFY2(std::abs(err) < 1000, qPrintable(QString("offset %1 kHz: error %2 Hz").arg(offset).arg(err)));
        }
    }
    void deltaMarkersReferToTheFirstNormalMarker()
    {
        const Trace t{{0, 100, 11}, {-10, -20, -30, -40, -50, -60, -70, -80, -90, -100, -110}};
        MarkerSet ms;
        ms.add(20);
        ms.add(52);  // reads the nearest point, 50
        ms.setDelta(1, true);
        const MarkerReading r = ms.read(1, t);
        QCOMPARE(r.name, QString("D2"));
        QCOMPARE(r.hz, 50.0);
        QCOMPARE(r.dHz, 30.0);
        QCOMPARE(r.dDb, -30.0);
        QVERIFY(std::isnan(ms.read(0, t).dDb));  // normal markers have no delta
        ms.remove(0);  // no reference left: the delta marker reads absolute only
        QVERIFY(std::isnan(ms.read(0, t).dDb));
        QCOMPARE(ms.selected(), 0);
    }
    void selectionFollowsRemovals()
    {
        MarkerSet ms;
        ms.add(1);
        ms.add(2);
        ms.add(3);
        ms.select(2);
        ms.remove(0);
        QCOMPARE(ms.selected(), 1);
        ms.remove(1);
        QCOMPARE(ms.selected(), -1);
        QCOMPARE(ms.near(1.5, 1), 0);
        QCOMPARE(ms.near(10, 1), -1);
    }
    void serializeRoundTrip()
    {
        MarkerSet ms;
        ms.add(100e6);
        ms.add(433.92e6);
        ms.setDelta(1, true);
        MarkerSet back;
        back.deserialize(ms.serialize());
        QCOMPARE(back.count(), 2);
        QCOMPARE(back.all()[1].hz, 433.92e6);
        QVERIFY(back.all()[1].delta);
    }
};

// A fake STM32 ROM bootloader: DfuSe commands on a flash array. Programming
// only works on erased (0xFF) bytes, like the real chip.
class FakeBootloader : public DfuTransport {
public:
    FakeBootloader(int pages) : flash(pages * 2048, char(0x5A)) {}
    QByteArray flash;
    quint32 base = 0x08000000, pointer = 0;
    int state = 2, status = 0, busy = 0, erased = 0;
    QString layoutText = "@Internal Flash  /0x08000000/064*0002Kg";

    bool out(quint8 request, quint16 value, const QByteArray &data) override
    {
        if (request == 4 || request == 6) {  // CLRSTATUS, ABORT
            state = 2, status = 0;
            return true;
        }
        if (request != 1)
            return false;
        if (data.isEmpty()) {  // leave
            state = 7;
            return true;
        }
        if (value == 0) {  // DfuSe command
            const quint32 a = data.size() >= 5 ? quint32(quint8(data[1])) | quint32(quint8(data[2])) << 8
                                                   | quint32(quint8(data[3])) << 16 | quint32(quint8(data[4])) << 24
                                               : 0;
            if (quint8(data[0]) == 0x21)
                pointer = a;
            else if (quint8(data[0]) == 0x41) {
                if (a < base || a >= base + quint32(flash.size())) {
                    status = 8, state = 10;  // errADDRESS
                    return true;
                }
                flash.replace(int((a - base) / 2048 * 2048), 2048, QByteArray(2048, char(0xFF)));
                ++erased;
            }
        } else {  // program at pointer + (block - 2) * length
            const quint32 a = pointer + quint32(value - 2) * quint32(data.size());
            for (int i = 0; i < data.size(); ++i) {
                const qsizetype at = qsizetype(a - base) + i;
                if (at < 0 || at >= flash.size() || flash[at] != char(0xFF)) {
                    status = 6, state = 10;  // errPROG
                    return true;
                }
                flash[at] = data[i];
            }
        }
        state = 3, busy = 1;
        return true;
    }
    bool in(quint8 request, quint16 value, int length, QByteArray *data) override
    {
        if (request == 3) {  // GETSTATUS: DNLOAD-SYNC -> DNBUSY -> DNLOAD-IDLE
            if (state == 3)
                state = busy-- > 0 ? 4 : 5;
            else if (state == 4)
                state = 5;
            *data = QByteArray(1, char(status)) + QByteArray("\x01\x00\x00", 3) + char(state) + char(0);
            return true;
        }
        if (request == 2 && value >= 2) {  // UPLOAD
            const qsizetype at = qsizetype(pointer - base) + qsizetype(value - 2) * length;
            *data = flash.mid(at, length);
            state = 9;
            return true;
        }
        return false;
    }
    QString layout() override { return layoutText; }
};

class TstDfu : public QObject {
    Q_OBJECT
private slots:
    void intelHex()
    {
        FirmwareImage img;
        QString err;
        const QByteArray hex = ":020000040800F2\n"
                               ":10000000000102030405060708090A0B0C0D0E0F78\n"
                               ":10001000101112131415161718191A1B1C1D1E1F68\n"
                               ":00000001FF\n";
        QVERIFY2(parseIntelHex(hex, &img, &err), qPrintable(err));
        QCOMPARE(img.segments.size(), 1);
        QCOMPARE(img.segments[0].address, 0x08000000u);
        QCOMPARE(img.segments[0].data.size(), 32);
        QCOMPARE(quint8(img.segments[0].data[31]), quint8(0x1F));
        FirmwareImage bad;
        QVERIFY(!parseIntelHex(":10000000000102030405060708090A0B0C0D0E0F79\n", &bad, &err));
        QVERIFY(err.contains("checksum"));
    }
    void dfuSeFile()
    {
        auto le32 = [](quint32 v) { return QByteArray(1, char(v)) + char(v >> 8) + char(v >> 16) + char(v >> 24); };
        const QByteArray payload(1000, char(0x42));
        QByteArray target = QByteArray("Target") + char(0) + le32(0) + QByteArray(255, 0)
                            + le32(8 + payload.size()) + le32(1) + le32(0x08004000) + le32(payload.size()) + payload;
        QByteArray file = QByteArray("DfuSe") + char(1) + le32(0) + char(1) + target;
        file += QByteArray("\xff\xff\x11\xdf\x83\x04\x1a\x01", 8) + "UFD" + char(16);
        file += le32(0);
        const quint32 crc = dfuCrc(file);
        file.replace(file.size() - 4, 4, le32(crc));
        FirmwareImage img;
        QString err;
        QVERIFY2(parseDfuSe(file, &img, &err), qPrintable(err));
        QCOMPARE(img.segments[0].address, 0x08004000u);
        QCOMPARE(img.segments[0].data, payload);
        file[20] = 'x';  // corrupt: CRC no longer matches
        QVERIFY(!parseDfuSe(file, &img, &err));
    }
    void flashLayout()
    {
        bool ok = false;
        FlashLayout f = parseFlashLayout("@Internal Flash  /0x08000000/128*0002Kg", &ok);
        QVERIFY(ok);
        QCOMPARE(f.size(), 256 * 1024);
        QCOMPARE(f.pageStart(0x08000FFF), 0x08000800u);
        f = parseFlashLayout("@Internal Flash  /0x08000000/04*016Kg,01*064Kg,07*128Kg", &ok);
        QCOMPARE(f.pageSize(0x08010000), 64 * 1024);
        QCOMPARE(f.pageSize(0x08100000), 0);
        parseFlashLayout("garbage", &ok);
        QVERIFY(!ok);
    }
    void writeVerifyKeepsTheConfigPage()
    {
        FakeBootloader boot(64);
        DfuSe dfu(boot);
        QCOMPARE(dfu.flash().size(), 128 * 1024);
        FirmwareImage img;
        QByteArray fw(5000, 0);
        for (int i = 0; i < fw.size(); ++i)
            fw[i] = char(i * 7);
        img.segments.append({0x08000000, fw});
        img.segments.append({0x08010100, QByteArray(300, char(0x33))});
        int lastPercent = 0;
        DfuSe writer(boot, [&](int p, const QString &) { lastPercent = p; });
        QVERIFY2(writer.write(img, true), qPrintable(writer.error()));
        QCOMPARE(lastPercent, 100);
        QCOMPARE(boot.erased, 4);  // 3 pages for 5000 bytes + 1 for the second segment
        QCOMPARE(boot.flash.left(5000), fw);
        QCOMPARE(boot.flash.mid(0x10100, 300), QByteArray(300, char(0x33)));
        QCOMPARE(boot.flash.right(2048), QByteArray(2048, char(0x5A)));  // "config" page untouched
        QByteArray all;
        QVERIFY(dfu.readFlash(&all));
        QCOMPARE(all, boot.flash);
        QVERIFY(dfu.leave());
        QCOMPARE(boot.state, 7);
    }
    void errorsAreReported()
    {
        FakeBootloader boot(64);
        DfuSe dfu(boot);
        FirmwareImage tooBig;
        tooBig.segments.append({0x08000000, QByteArray(200 * 1024, 1)});
        QVERIFY(!dfu.write(tooBig, false));
        QVERIFY(dfu.error().contains("does not fit"));
        // a bootloader error (erase outside its flash) is reported, not ignored
        boot.base = 0x08010000;
        FirmwareImage img;
        img.segments.append({0x08000000, QByteArray(10, 1)});
        QVERIFY(!dfu.write(img, false));
        QVERIFY2(dfu.error().contains("address out of range"), qPrintable(dfu.error()));
        // unknown layout: the smallest tinySA flash (64 x 2 KB) is assumed
        boot.layoutText.clear();
        QCOMPARE(DfuSe(boot).flash().size(), 128 * 1024);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int fails = 0;
    // each class logs to stdout and to tst_<class>.log in the working directory
    auto run = [&](QObject *t) {
        QStringList args = app.arguments();
        args << "-o" << QString("tst_%1.log,txt").arg(t->metaObject()->className()) << "-o" << "-,txt";
        fails += QTest::qExec(t, args);
    };
    TstUnits units;
    run(&units);
    TstProtocol protocol;
    run(&protocol);
    TstModel model;
    run(&model);
    TstLink link;
    run(&link);
    TstTraces traces;
    run(&traces);
    TstCsv csv;
    run(&csv);
    TstMarkers markers;
    run(&markers);
    TstDfu dfu;
    run(&dfu);
    return fails;
}

#include "tst_core.moc"
