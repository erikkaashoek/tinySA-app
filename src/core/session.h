#pragma once
// GUI-thread facade: owns the DeviceLink and its thread, keeps the current
// SweepConfig, device info and processed traces. Controllers talk only to this class.
#include "link.h"
#include "markers.h"
#include "traces.h"
#include <QThread>

class Session : public QObject {
    Q_OBJECT
public:
    explicit Session(QObject *parent = nullptr);
    ~Session() override;

    void connectDevice(const QString &port);  // "COMx" / "sim:basic" / "sim:ultra"
    void disconnectDevice();
    bool isConnected() const { return m_connected; }
    const DeviceInfo &device() const { return m_info; }

    SweepConfig config() const { return m_config; }
    void setConfig(const SweepConfig &c);  // clamped to the connected model
    // Analyzer or signal generator (DeviceLink::Activity). Entering the generator switches
    // RF off first, as the unit does; the unit's own mode is followed on connect.
    int activity() const { return m_activity; }
    void setActivity(int activity);
    GenConfig generator() const { return m_gen; }
    void setGenerator(const GenConfig &g);  // clamped to the connected unit
    void triggerBurst();                    // Burst: start the counted bursts / restart continuous
    void setScreenOffWhileScanning(bool on);  // firmware with `ui off`: faster scans
    // Tracking generator: a second tinySA (its own port) whose output follows the sweep.
    void connectTracker(const QString &port);
    void disconnectTracker();
    bool isTrackerConnected() const { return m_trackerConnected; }
    const DeviceInfo &tracker() const { return m_trackerInfo; }
    TrackConfig track() const { return m_track; }
    void setTrack(const TrackConfig &t);  // clamped to the generator unit; the sweep range too
    bool isTracking() const { return m_track.on && m_trackerConnected; }
    // its frequency error against the analyzer, corrected on every point; nullopt = not
    // measured yet (at the first tracking sweep) or no signal to measure
    std::optional<double> trackerPpb() const { return m_trackerPpb; }
    int scanMode() const { return m_scan; }
    bool isBusy() const { return m_busy; }  // commands in flight or queued
    void setScan(int mode);                // DeviceLink::Off / Once / Continuous
    void send(const QString &command);
    void capture();
    void setRemote(bool on);
    void touch(QPoint lcd, bool down);
    void simTouch();

    TraceSet &traces() { return m_traces; }
    MarkerSet &markers() { return m_markers; }

signals:
    void tracesChanged(bool sweepComplete);  // live trace updated (and memories if edited)
    void connected(const DeviceInfo &info);
    void disconnected(const QString &reason);
    void configChanged(const SweepConfig &c);
    void activityChanged(int activity);
    void generatorChanged(const GenConfig &g);
    void trackerConnected(const DeviceInfo &info);
    void trackerDisconnected(const QString &reason);
    void trackChanged(const TrackConfig &t);
    void trackerTraffic(bool sent, const QString &text);
    void trackerAligned();  // trackerPpb() measured
    void frame(const SweepFrame &f);
    void scanChanged(int mode);
    void busyChanged(bool busy);
    void reply(const QString &command, const QStringList &lines);
    void battery(int mV);
    void screen(const QImage &img);
    void traffic(bool sent, const QString &text, bool user);
    void message(const QString &text);

private:
    QThread m_thread;
    DeviceLink *m_link;
    DeviceLink *m_trackerLink;  // the tracking generator's unit, same thread
    DeviceInfo m_info, m_trackerInfo;
    bool m_connected = false, m_trackerConnected = false;
    TrackConfig m_track;
    std::optional<double> m_trackerPpb;
    SweepConfig m_config;
    int m_activity = DeviceLink::Analyzer;
    GenConfig m_gen;
    int m_scan = DeviceLink::Off;
    bool m_busy = false;
    TraceSet m_traces;
    MarkerSet m_markers;
};
