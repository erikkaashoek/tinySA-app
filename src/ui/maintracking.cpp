// MainWindow, tracking generator: the second tinySA's port and connection, Track and level.
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QSignalBlocker>
#include <cmath>

void MainWindow::setupTracking()
{
    connect(ui->trackerConnectButton, &QPushButton::clicked, this, [this](bool checked) {
        if (!checked)
            return m_session.disconnectTracker();
        ui->trackerConnectButton->setEnabled(false);
        ui->trackerConnectButton->setText("Connecting...");
        ui->trackerLabel->setText("Connecting to " + ui->trackerPortCombo->currentText());
        m_session.connectTracker(ui->trackerPortCombo->currentData().toString());
    });
    connect(ui->trackCheck, &QCheckBox::clicked, this, [this](bool on) {
        TrackConfig t = m_session.track();
        t.on = on;
        m_session.setTrack(t);
    });
    connect(ui->trackLevelSpin, &QDoubleSpinBox::valueChanged, this, [this](double dbm) {
        TrackConfig t = m_session.track();
        t.levelDbm = dbm;
        m_session.setTrack(t);
    });
    connect(&m_session, &Session::trackChanged, this, &MainWindow::showTrack);
    connect(&m_session, &Session::trackerConnected, this, [this](const DeviceInfo &info) {
        m_settings.trackerPort = info.port;
        ui->trackerConnectButton->setEnabled(true);
        ui->trackerConnectButton->setChecked(true);
        ui->trackerConnectButton->setText("Disconnect");
        ui->trackerPortCombo->setEnabled(false);
        const GenLimits l = trackerLimits(info);
        QSignalBlocker block(ui->trackLevelSpin);
        ui->trackLevelSpin->setRange(l.minDbm, l.maxDbm);
        showTrackerLabel();
        showTrack(m_session.track());
    });
    connect(&m_session, &Session::trackerAligned, this, &MainWindow::showTrackerLabel);
    connect(&m_session, &Session::trackerDisconnected, this, [this](const QString &reason) {
        ui->trackerConnectButton->setEnabled(true);
        ui->trackerConnectButton->setChecked(false);
        ui->trackerConnectButton->setText("Connect");
        ui->trackerPortCombo->setEnabled(true);
        ui->trackerLabel->setText("Not connected");
        ui->trackerLabel->setToolTip({});
        showTrack(m_session.track());
        error(reason);
    });
    connect(&m_session, &Session::trackerTraffic, this, [this](bool sent, const QString &text) {
        if (sent && m_trackerSent.size() < 10000)
            m_trackerSent << text;
    });
}

// "tinySA Ultra ZS405, to 5.327 GHz, -430 ppb": the unit, its top frequency and its frequency
// error against the analyzer once measured (at the first tracking sweep)
void MainWindow::showTrackerLabel()
{
    const DeviceInfo &info = m_session.tracker();
    if (!m_session.isTrackerConnected() || !info.model)
        return;
    const GenLimits l = trackerLimits(info);
    const std::optional<double> ppb = m_session.trackerPpb();
    ui->trackerLabel->setText(QString("%1 %2, to %3%4").arg(info.model->name, info.hardware, formatHz(l.maxHz, 1e6),
                                                             ppb ? QString(", %1 ppb").arg(std::lround(*ppb)) : QString()));
    ui->trackerLabel->setToolTip(QString("%1\nOutput %2 to %3, %4 to %5 dBm\n%6")
                                     .arg(info.title(), formatHz(l.minHz, 1e3), formatHz(l.maxHz, 1e6))
                                     .arg(l.minDbm).arg(l.maxDbm)
                                     .arg(ppb ? QString("Its frequency is %1 ppb off this tinySA's: corrected").arg(*ppb, 0, 'f', 0)
                                              : QString("Its frequency error is measured at the first tracking sweep")));
}

void MainWindow::showTrack(const TrackConfig &t)
{
    QSignalBlocker b1(ui->trackCheck), b2(ui->trackLevelSpin);
    ui->trackCheck->setChecked(t.on);
    ui->trackCheck->setEnabled(m_session.isTrackerConnected());
    ui->trackLevelSpin->setValue(t.levelDbm);
}

void MainWindow::connectTrackerTo(const QString &port)
{
    int at = ui->trackerPortCombo->findData(port);
    if (at < 0) {
        ui->trackerPortCombo->addItem(port, port);
        at = ui->trackerPortCombo->count() - 1;
    }
    ui->trackerPortCombo->setCurrentIndex(at);
    if (!ui->trackerConnectButton->isChecked())
        ui->trackerConnectButton->click();
}
