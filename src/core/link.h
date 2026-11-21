#pragma once
// Connection to one tinySA. Lives in its own thread (Session moves it there), so
// the next command goes out as soon as a reply ends, whatever the GUI is doing.
//
// One command in flight; replies are recognised by their echo and end at the
// prompt (protocol.h). A sweep is one `scanraw S E N`; queued work (user commands,
// config changes, battery polls) runs between sweeps.
//
// The unit is an analyzer or a signal generator, never both: in generator mode the
// link only sends GenConfig differences and never scans (linkgen.cpp).
//
// Tracking generator (linktrack.cpp): a second DeviceLink in the same thread drives another
// unit as generator. The analyzer's link then sweeps point by point: it tunes the generator
// unit, waits for its reply (and the settle time), then measures that one frequency.
#include "genconfig.h"
#include "model.h"
#include "protocol.h"
#include "sweepconfig.h"
#include "tracking.h"
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <optional>

class QIODevice;

struct SweepFrame {
    int seq = 0;            // increments per sweep
    ScanGrid grid;
    QVector<double> dBm;    // grid.points values; NaN where not received yet
    int received = 0;
    bool complete = false;
    double seconds = 0;     // duration of the completed sweep
};
Q_DECLARE_METATYPE(SweepFrame)
Q_DECLARE_METATYPE(DeviceInfo)

class DeviceLink : public QObject {
    Q_OBJECT
public:
    enum Scan { Off, Once, Continuous };
    Q_ENUM(Scan)
    enum Activity { Analyzer, Generator };
    Q_ENUM(Activity)

    explicit DeviceLink(QObject *parent = nullptr);
    ~DeviceLink() override;

    static QString findPort();  // first USB port with a tinySA VID/PID, "" if none
    static bool isTinySAUsbId(quint16 vid, quint16 pid);  // 0483:5740 or the Ultra's 1781:0A44
    QIODevice *transport() const { return m_io; }  // tests: reach the SimTransport

public slots:
    void open(const QString &port);  // "COM5", or "sim:basic" / "sim:ultra"
    void close();
    void setConfig(const SweepConfig &c);
    void setScan(int mode);
    void send(const QString &command);  // console/dialog command; reply() delivers the answer
    void capture();                     // screen() delivers the image
    // Remote control: the unit drives its own display (our scanning pauses) and
    // pushes screen changes; off restores the previous scan mode.
    void setRemote(bool on);
    void touch(QPoint lcd, bool down);  // down: press/drag at lcd pixel, else release
    void simTouch();                    // test hook: touch the simulated screen
    // Generator: stops scanning and switches the unit to output mode (RF as in the
    // GenConfig); Analyzer: back to input mode and the scan mode from before.
    void setActivity(int activity);
    void setGenerator(const GenConfig &g);
    void triggerBurst();  // Burst: start the counted bursts, or restart the continuous ones
    // Firmware with `ui off` (tinySA Ultra from v1.4-314): the unit's screen is off while we
    // scan, so its commands do not wait for screen redraws. A touch on the unit gives it back
    // ("ui on" on the console) and stops our scanning.
    void setScreenOffWhileScanning(bool on);
    // Analyzer: sweep with the tracking generator (when it is connected and track.on).
    void setTracking(const TrackConfig &t);

public:
    // Called from the same thread only (both links live in the Session's link thread).
    // Analyzer: the link of the generator unit (before the thread starts; nullptr = none).
    void setTracker(DeviceLink *tg);
    // Generator unit: a carrier at hz and levelDbm as tracking generator, RF on or off. Its
    // screen is off as long as it is the tracking generator (firmware with `ui`).
    void setTrackOutput(bool rf, double levelDbm, double hz);
    bool readyToTune() const;  // the output as asked for, nothing queued or in flight
    // `sweep cw hz` (and `level` again, withLevel), then done(ok) once the unit has replied.
    // The reply comes before the unit's sweep thread has set the output; sync: then a bare
    // `sweep` too, which waits for that thread (CMD_WAIT_MUTEX), so done() means the output is
    // set (into another output path: up to 190 ms, the Si4468 reset into the leakage path).
    void tune(double hz, bool withLevel, std::function<void(bool ok)> done, bool sync = false);
    bool isReady() const { return m_ready; }
    const DeviceInfo &info() const { return m_info; }

signals:
    void opened(const DeviceInfo &info);
    void activityChanged(int activity);           // also after connecting (the unit's own mode)
    void generatorAdopted(const GenConfig &g);    // connected to a generating unit: its settings
    void closed(const QString &reason);
    void frame(const SweepFrame &f);
    void scanChanged(int mode);
    void busyChanged(bool busy);  // false = nothing in flight or queued (e.g. a stop has completed)
    void reply(const QString &command, const QStringList &lines);
    void battery(int mV);
    void screen(const QImage &img);
    void traffic(bool sent, const QString &text, bool user);  // console log; user = from send()
    void message(const QString &text);
    void trackerStopped();  // generator unit: touched while it tracked (the analyzer stops)
    // analyzer: the tracking generator's frequency error, corrected from now on (measured =
    // false: no signal to measure, not corrected)
    void trackerAligned(double ppb, bool measured);

private:
    struct Cmd {
        QByteArray line;  // empty = local step, runs `done` without I/O
        RxParser::Body body = RxParser::Body::Text;
        int bytes = 0;    // capture size
        int timeoutMs = 3000;
        std::function<void(bool ok, const QStringList &lines)> done;
        bool user = false;       // typed by the user (console, dialogs), not the app's own traffic
        bool generator = false;  // from genCommands(): obsolete once back in analyzer mode
        int points = 0;          // scanraw: values it returns unless aborted
        bool align = false;      // measuring the tracking generator's frequency (not a sweep)
        bool sweep = false;      // a sweep's scanraw (a log sweep: one of its pieces)
    };
    void enqueue(Cmd c);
    // Reports refusals ("cmd?", "usage: ..."). Generator commands without a reply are
    // forgotten in m_genHave, so the next diff sends them again.
    void queueChecked(const QByteArrayList &lines, bool generator = false);
    void pump();      // send the next command, or start the next sweep
    void pumpStep();
    void pumpGenerator();
    Cmd extGainRead();                                   // handshake: the unit's ext_gain
    void probeGenerator(const QStringList &levelReply);  // handshake: is the unit generating?
    void adoptGenerator();                               // handshake end: follow a generating unit
    void updateBusy();
    void transmit(Cmd c);
    void onReadyRead();
    void handle(const RxEvent &e);
    void finish(bool ok);
    void onTimeout();
    void resync();  // discard input until quiet, then continue with the queue
    void writeDirect(const QByteArray &line);  // remote mode: no echo/prompt wait
    void flushTouch();
    void fail(const QString &reason);
    void shutdown(const QString &reason);
    void handshake();
    void startSweep();
    void setMode(int mode);
    bool tracking() const;     // analyzer: a tracking generator is connected and wanted
    bool trackerInMode() const;  // analyzer: the generator unit's output in m_track's mode
    void pumpTracking();       // analyzer: the next point of the tracking sweep
    void measureTrackPoint();  // analyzer: after tuning and settle
    void stopTracking(bool rfOff);  // analyzer: no point pending; rfOff: the generator's RF off
    void setFineTimer(bool on);     // Windows: a 1 ms clock for the settle times (timeBeginPeriod)
    bool trackScreenOff() const;  // generator unit: its screen off while it tracks
    QByteArray uiOffText() const;  // shown with the screen off: "Tracking generator", ...
    bool uiTextDue() const;        // off, but with another text
    Cmd uiCommand(bool off);       // ui on / ui off "text" (plain ui off on older firmware)
    // analyzer: the generator's frequency error against this unit, measured once at a high
    // frequency (coarse, then fine scan around it) and corrected as ppb on every frequency
    void pumpAlign();
    void alignScan(double centerHz, double halfSpanHz, double rbwKHz, double stepHz, double minRiseDb,
                   std::function<void(std::optional<double> peakHz)> done);
    void alignFailed();
    void endAlign(std::optional<double> ppb);
    double trackerHz(double hz) const;  // what the generator gets for its output at hz

    QIODevice *m_io = nullptr;
    RxParser m_rx;
    QList<Cmd> m_queue;
    std::optional<Cmd> m_cur;
    bool m_echoSeen = false, m_scanDone = false;
    QStringList m_lines;
    QByteArray m_pixels;
    bool m_pixelsRle8 = false;
    QTimer m_timeout, m_battery, m_drain;
    bool m_draining = false;
    QTimer m_settle;  // old firmware: nothing goes out for a while after output/modulation/...

    DeviceInfo m_info;
    bool m_ready = false;
    SweepConfig m_want;
    AppliedConfig m_have;
    int m_activity = Analyzer;
    GenConfig m_gen;
    AppliedGen m_genHave;
    int m_modeBeforeGen = Off;
    int m_mode = Off;
    bool m_paused = false;    // unit paused by us: set when pause/resume is transmitted
    bool m_uiOff = false;     // unit's screen off by us: set when ui off/on is transmitted
    QByteArray m_uiText;      // the text sent with ui off
    bool m_uiTextOk = true;   // ui off "text" (v1.4-317); false after a usage reply
    bool m_screenOffWanted = true;
    bool m_aborting = false;  // we sent abort for the scan in flight
    int m_scanExpect = 0;     // values the scanraw in flight returns when not aborted
    int m_sweepPiece = -1;    // the piece of the sweep in progress (log: one scanraw each); -1 = none
    QPointer<DeviceLink> m_tg;   // analyzer: the generator unit's link
    TrackConfig m_track;
    int m_trackNext = -1;        // next point of the tracking sweep in progress; -1 = none
    bool m_trackWait = false;    // waiting for the generator unit (reply, settle)
    TrackChange m_trackRestart = TrackChange::None;  // its RF just on or a new level: settle long
    QTimer m_trackSettle;
    bool m_fineTimer = false;    // timeBeginPeriod(1) in effect
    bool m_isTracker = false;    // generator unit: driven as tracking generator
    bool m_trackRf = false;      //   with RF on
    bool m_trackTouched = false;  //   touched: its screen stays on until the next scan
    bool m_trackAligned = false;  // analyzer: the generator's frequency error measured (or given up)
    double m_trackPpb = 0;        //   output = set x (1 + ppb 1e-9)
    int m_aligning = 0, m_alignIds = 0;  // alignment in progress (its id), 0 = none
    QVector<double> m_alignHz;    // frequencies still to try
    bool m_alignScan = false;     // the scanraw in flight measures for the alignment
    QVector<double> m_alignValues;
    bool m_busy = false, m_batteryDue = false;
    SweepFrame m_frame;
    QElapsedTimer m_sweepClock, m_emitClock, m_drainClock;
    QElapsedTimer m_sentClock;  // since the command in flight was sent
    QImage m_screen;          // last known unit screen (capture + pushed regions)
    QTimer m_screenTimer;     // throttles screen() while regions stream in
    bool m_remote = false;
    int m_modeBeforeRemote = Off;
    QByteArray m_pendingTouch;  // latest drag position, sent at most every 30 ms
    QTimer m_touchTimer;
};
