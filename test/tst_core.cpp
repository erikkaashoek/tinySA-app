// Unit tests for src/core. One executable, one test class per module.
#include "units.h"
#include <QTest>

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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int fails = 0;
    TstUnits units;
    fails += QTest::qExec(&units, argc, argv);
    return fails;
}

#include "tst_core.moc"
