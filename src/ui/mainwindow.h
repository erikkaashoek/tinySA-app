#pragma once
// Main window controller: wires the Designer form to the Session. Widget signals
// go to Session setters; Session signals go to the small render functions below.
#include "appsettings.h"
#include "autotest.h"
#include "csvio.h"
#include "session.h"
#include "spectrumview.h"
#include <QElapsedTimer>
#include <QMainWindow>
#include <QTimer>
#include <memory>

namespace Ui { class MainWindow; }
class QLabel;
class QLineEdit;
class QShortcut;
class QToolButton;
class QSettings;
class MarkerModel;
class ConsoleWindow;
class BatteryWindow;
class ScreenWindow;
class SettingsDialog;
class FirmwareDialog;

class MainWindow : public QMainWindow, public TestHost {
    Q_OBJECT
public:
    MainWindow(bool simulators, const QString &settingsPath, QWidget *parent = nullptr);
    ~MainWindow() override;

    void connectTo(const QString &port);

    // TestHost
    QWidget *testWindow() override { return this; }
    std::optional<QString> testQuery(const QString &name) override;
    bool testAction(const QString &name, const QStringList &args) override;

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void refreshPorts();
    void onConnectClicked(bool checked);
    void onConnected(const DeviceInfo &info);
    void onDisconnected(const QString &reason);
    void onFrame(const SweepFrame &f);
    void onScanChanged(int mode);
    void applyModel(const ModelDesc *m);  // per-model control lists and visibility
    void showConfig(const SweepConfig &c);
    void editRange(QLineEdit *edit);
    void editConfig(const std::function<void(SweepConfig &)> &change);
    void applyRangeOp(SpectrumView::RangeOp op, double a, double b);
    FreqRange rangeOf(const SweepConfig &c) const;

    void renderTraces(bool memories);  // TraceSet -> SpectrumView
    void showAverage();                // "Avg 3/16" in the status bar while averaging
    void setupMemoryButton(int m, QToolButton *b);
    bool saveCsv(const QString &path);
    bool saveImage(const QString &path);
    bool loadMemory(int m, const QString &path);
    QString imageHeader() const;
    QString memoryFile(int m) const;  // persisted memories next to the settings
    void setRecording(bool on);
    void setupMarkers();
    void renderMarkers();  // MarkerSet + shown live trace -> graph marks and table
    void markerToPeak(bool addNew);
    double snap(double hz) const;  // nearest point of the shown live trace
    void setupTools();             // console, battery (maintools.cpp)
    void showTool(QWidget *w);
    void saveTools(QSettings &qs);
    void setupGenerator();              // generator page and mode switch (maingenerator.cpp)
    void showActivity(int activity);    // analyzer or generator page, toolbar, status bar
    void showGeneratorStatus();
    void escape();                      // Esc: stop scanning, or RF off when generating
    void setupTracking();               // tracking generator group (maintracking.cpp)
    void showTrack(const TrackConfig &t);
    void showTrackerLabel();
    void connectTrackerTo(const QString &port);
    void applyAppearance();  // theme to the graphs, colour scheme to the application
    void error(const QString &what);

    std::unique_ptr<Ui::MainWindow> ui;
    Session m_session;
    QString m_settingsPath;
    AppSettings m_settings;
    SweepRecorder m_recorder;
    MarkerModel *m_markerModel = nullptr;
    ConsoleWindow *m_console = nullptr;
    BatteryWindow *m_battery = nullptr;
    ScreenWindow *m_screen = nullptr;
    SettingsDialog *m_settingsDialog = nullptr;
    FirmwareDialog *m_firmwareDialog = nullptr;
    QTimer m_portPoll;
    QStringList m_portNames;
    bool m_simulators;
    QLabel *m_stepLabel, *m_sweepsLabel, *m_avgLabel, *m_rateLabel, *m_batteryLabel;
    QLabel *m_genLabel = nullptr;
    QList<QShortcut *> m_analyzerShortcuts;  // off on the generator page (its fields take the keys)
    int m_sweeps = 0, m_sweepsMark = 0;
    QElapsedTimer m_cycle;
    double m_cycleMs = 0, m_sweepMs = 0;  // last sweep-to-sweep time, last sweep duration
    QStringList m_sent;      // commands sent to the unit (console + tests)
    QStringList m_trackerSent;  // commands sent to the tracking generator (tests)
    QStringList m_messages;  // status messages from the session (tests)
};
