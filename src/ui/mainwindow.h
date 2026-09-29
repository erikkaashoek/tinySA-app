#pragma once
// Main window controller: wires the Designer form to the Session. Widget signals
// go to Session setters; Session signals go to the small render functions below.
#include "autotest.h"
#include "session.h"
#include <QMainWindow>
#include <QTimer>
#include <memory>

namespace Ui { class MainWindow; }
class QLabel;
class QLineEdit;

class MainWindow : public QMainWindow, public TestHost {
    Q_OBJECT
public:
    explicit MainWindow(bool simulators, QWidget *parent = nullptr);
    ~MainWindow() override;

    void connectTo(const QString &port);

    // TestHost
    QWidget *testWindow() override { return this; }
    QString testQuery(const QString &name) override;
    bool testAction(const QString &name, const QStringList &args) override;

private:
    void refreshPorts();
    void onConnectClicked(bool checked);
    void onConnected(const DeviceInfo &info);
    void onDisconnected(const QString &reason);
    void onFrame(const SweepFrame &f);
    void onScanChanged(int mode);
    void showConfig(const SweepConfig &c);
    void editRange(QLineEdit *edit);

    std::unique_ptr<Ui::MainWindow> ui;
    Session m_session;
    QTimer m_portPoll;
    QStringList m_portNames;
    bool m_simulators;
    QLabel *m_sweepsLabel, *m_rateLabel, *m_batteryLabel;
    int m_sweeps = 0;
};
