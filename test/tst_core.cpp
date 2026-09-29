// Unit tests for src/core. One executable, one test class per module.
#include "link.h"
#include "model.h"
#include "protocol.h"
#include "simdevice.h"
#include "sweepconfig.h"
#include "units.h"
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
            s << QString("R:%1,%2,%3,%4,%5,%6").arg(e.rect.x()).arg(e.rect.y())
                     .arg(e.rect.width()).arg(e.rect.height()).arg(e.fill).arg(e.data.size());
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

    void captureDecodesScreen()
    {
        DeviceLink link;
        QSignalSpy opened(&link, &DeviceLink::opened), screen(&link, &DeviceLink::screen);
        link.open("sim:ultra");
        QVERIFY(opened.wait(3000));
        link.capture();
        QVERIFY(screen.wait(3000));
        QCOMPARE(screen.first().first().value<QImage>().size(), QSize(480, 320));
    }

    void notATinySA()
    {
        QCOMPARE(detectModel("NanoVNA-H", "1.0"), nullptr);
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
    return fails;
}

#include "tst_core.moc"
