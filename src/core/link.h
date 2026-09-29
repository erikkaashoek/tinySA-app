#pragma once
// Connection to one tinySA. Lives in its own thread (Session moves it there), so
// the next command goes out as soon as a reply ends, whatever the GUI is doing.
//
// One command in flight; replies are recognised by their echo and end at the
// prompt (protocol.h). A sweep is one `scanraw S E N`; queued work (user commands,
// config changes, battery polls) runs between sweeps.
#include "model.h"
#include "protocol.h"
#include "sweepconfig.h"
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
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

    explicit DeviceLink(QObject *parent = nullptr);
    ~DeviceLink() override;

    static QString findPort();  // first USB port with the tinySA VID/PID, "" if none
    QIODevice *transport() const { return m_io; }  // tests: reach the SimTransport

public slots:
    void open(const QString &port);  // "COM5", or "sim:basic" / "sim:ultra"
    void close();
    void setConfig(const SweepConfig &c);
    void setScan(int mode);
    void send(const QString &command);  // console/dialog command; reply() delivers the answer
    void capture();                     // screen() delivers the image
    void simTouch();                    // test hook: touch the simulated screen

signals:
    void opened(const DeviceInfo &info);
    void closed(const QString &reason);
    void frame(const SweepFrame &f);
    void scanChanged(int mode);
    void reply(const QString &command, const QStringList &lines);
    void battery(int mV);
    void screen(const QImage &img);
    void traffic(bool sent, const QString &text);  // command lines and reply lines, for the console
    void message(const QString &text);

private:
    struct Cmd {
        QByteArray line;  // empty = local step, runs `done` without I/O
        RxParser::Body body = RxParser::Body::Text;
        int bytes = 0;    // capture size
        int timeoutMs = 3000;
        std::function<void(bool ok, const QStringList &lines)> done;
    };
    void enqueue(Cmd c);
    void pump();
    void transmit(Cmd c);
    void onReadyRead();
    void handle(const RxEvent &e);
    void finish(bool ok);
    void onTimeout();
    void resync();  // discard input until quiet, then continue with the queue
    void fail(const QString &reason);
    void shutdown(const QString &reason);
    void handshake();
    void startSweep();
    void setMode(int mode);

    QIODevice *m_io = nullptr;
    RxParser m_rx;
    QList<Cmd> m_queue;
    std::optional<Cmd> m_cur;
    bool m_echoSeen = false, m_scanDone = false;
    QStringList m_lines;
    QByteArray m_pixels;
    QTimer m_timeout, m_battery, m_drain;
    bool m_draining = false;

    DeviceInfo m_info;
    bool m_ready = false;
    SweepConfig m_want;
    AppliedConfig m_have;
    int m_mode = Off;
    bool m_paused = false, m_batteryDue = false;
    SweepFrame m_frame;
    QElapsedTimer m_sweepClock, m_emitClock, m_drainClock;
};
