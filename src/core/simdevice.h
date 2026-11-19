#pragma once
// Simulated tinySA as a QIODevice that speaks the firmware shell byte-for-byte:
// char echo, "ch> " prompt, scanraw streamed in 20-point chunks, capture pixels,
// input queued (not echoed) while a scan runs. Deterministic spectrum with known
// carriers, so tests and screenshots are reproducible without hardware.
#include <QByteArrayList>
#include <QElapsedTimer>
#include <QIODevice>
#include <QSize>
#include <QTimer>
#include <optional>

class SimTransport : public QIODevice {
    Q_OBJECT
public:
    enum class Kind { Basic, Ultra };
    struct Carrier { double hz, dBm; };

    explicit SimTransport(Kind kind, QObject *parent = nullptr);
    ~SimTransport() override;
    bool isSequential() const override { return true; }

    // Bench: the input cabled to the outputs of the other simulated units (a tracking
    // generator), directly or through a 5th-order Butterworth low-pass at 150 MHz; no antenna
    // carriers then. None: the antenna (the carriers below).
    enum class Dut { None, Thru, LowPass };
    Dut dut = Dut::None;
    static double dutGainDb(Dut d, double hz);
    qint64 bytesAvailable() const override { return m_out.size() + QIODevice::bytesAvailable(); }

    static QVector<Carrier> carriers();
    double levelAt(double hz, double rbwHz) const;  // noiseless model value

    // test hooks
    void touchScreen();                       // abort a running scanraw like a touch; screen off: back on
    bool uiOff() const { return m_uiOff; }    // `ui off` (Ultra v1.4-314)
    QByteArray uiText() const { return m_uiText; }  // `ui off "text"` (v1.4-317)
    bool uiWithText = true;                   // false: v1.4-314..316, ui off|on only
    QByteArrayList received() const { return m_received; }
    QByteArray pendingInput() const { return m_in; }  // written but not yet read by the shell
    double pointsPerSecond = 20000;
    int shellWaitMs = 5000;  // the shell reads input again this long into a scan (firmware)
    bool rleCapture;         // "capture rle" answered in RLE8 (Ultra) or raw like old firmware (basic)
    QByteArray screenBytes() const { return m_screen; }  // raw RGB565 big-endian
    void stopPushes() { m_pushTimer.stop(); }  // the screen stands still (refresh stays on)
    static QByteArray packBits(const QByteArray &data);
    // generator ("output mode"): what the app may read back, plus what tests check
    void startGenerating();  // as a unit left generating: LOW output, 100 MHz, -30 dBm, RF on, FM
    bool generating() const { return m_outMode; }
    bool rfOn() const { return m_rfOn; }
    double outputLevel() const { return m_level; }
    double outputHz() const { return m_sweepStart * (1 + ppb * 1e-9); }  // CW, with its crystal's error
    double ppb = 0;  // reference error: what it generates is off by this (a real unit: some 100 ppb)
    QByteArray modulation() const { return m_modulation; }
    double burstPeriod() const { return m_burstPeriod; }
    double burstOnTime() const { return m_burstOn; }
    int burstCount() const { return m_burstCount; }
    int burstTriggers() const { return m_burstTriggers; }
    int modulationHz() const { return m_modHz; }
    double extGain() const { return m_extGain; }  // `ext_gain`: shifts the reported levels, both modes
    void setExtGain(double db) { m_extGain = db; }  // as left by an earlier session (backup memory)
    int fmDeviation() const { return m_fmDev; }
    // "CTCSS + 1 kHz" (config.wfm_1khz_harmonic): kept over mode switches; `modulation ctcss`
    bool ctcssTone = false;
    bool ctcssCommand = true;      // false: Ultra firmware before v1.4-341 (the tone only in the menu)
    int scansWhileGenerating = 0;  // scanraw in output mode: junk, and a hang with modulation
    bool oldFirmware = false;      // v1.4-220 ZS405 (Ultra): the shell hangs on a command sent right
    bool hung() const { return m_hung; }  // after output/modulation/levelchange/ext_gain

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

private:
    static QList<SimTransport *> &bench();
    void out(const QByteArray &bytes);
    void processInput();
    void execute(const QByteArray &line);
    void scanTick();
    void pushTick();  // refresh on/rle: stream a changing screen region (rle: and a waterfall)
    QByteArray rle8() const;

    Kind m_kind;
    QByteArray m_in, m_out, m_line;
    QByteArrayList m_received;
    bool m_readyPending = false;

    qint64 m_sweepStart = 0, m_sweepStop = 350000000;
    int m_sweepPoints = 450;
    double m_rbwKHz = 0;
    int m_atten = -1;
    bool m_lna = false, m_paused = false;
    void setMode(bool output, bool high);  // a mode switch resets the settings, as reset_settings()
    bool generatorCommand(const QByteArrayList &tok);  // output, level, modulation, ...
    bool m_outMode = false, m_high = false, m_rfOn = false, m_mixer = true, m_genSweep = false;
    double m_level = -18.5, m_extGain = 0, m_levelChange = 0, m_sweepTime = 2;
    QByteArray m_modulation = "off";
    int m_modHz = 1000, m_amDepth = 80, m_fmDev = 3000;
    double m_burstPeriod = 1e-3, m_burstOn = 100e-6;
    int m_burstCount = 0, m_burstTriggers = 0;
    QElapsedTimer m_directClock;  // old firmware: since the last direct command in output mode
    bool m_hung = false;
    bool m_uiOff = false;
    QByteArray m_uiText;

    struct Scan { qint64 start, stop; int points, next; };
    std::optional<Scan> m_scan;
    QTimer m_timer;
    QElapsedTimer m_clock;
    unsigned m_sweeps = 0;
    QSize m_lcd;
    QByteArray m_screen;  // RGB565 big-endian, as the LCD is read
    QTimer m_pushTimer;
    int m_pushes = 0;
    bool m_pushRle = false;  // refresh rle (Ultra): "rle8" pushes and a scrolling waterfall
    void region(bool fill, int x, int y, int w, int h, quint16 colour);  // draw + push if refreshing
    void scroll(int x, int y, int w, int h);  // the rows move down by one, pushed as "scrl"
};
