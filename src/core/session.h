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
    int scanMode() const { return m_scan; }
    bool isBusy() const { return m_busy; }  // commands in flight or queued
    void setScan(int mode);                // DeviceLink::Off / Once / Continuous
    void send(const QString &command);
    void capture();
    void simTouch();

    TraceSet &traces() { return m_traces; }
    MarkerSet &markers() { return m_markers; }

signals:
    void tracesChanged(bool sweepComplete);  // live trace updated (and memories if edited)
    void connected(const DeviceInfo &info);
    void disconnected(const QString &reason);
    void configChanged(const SweepConfig &c);
    void frame(const SweepFrame &f);
    void scanChanged(int mode);
    void busyChanged(bool busy);
    void reply(const QString &command, const QStringList &lines);
    void battery(int mV);
    void screen(const QImage &img);
    void traffic(bool sent, const QString &text);
    void message(const QString &text);

private:
    QThread m_thread;
    DeviceLink *m_link;
    DeviceInfo m_info;
    bool m_connected = false;
    SweepConfig m_config;
    int m_scan = DeviceLink::Off;
    bool m_busy = false;
    TraceSet m_traces;
    MarkerSet m_markers;
};
