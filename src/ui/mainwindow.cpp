#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QLabel>
#include <QSerialPortInfo>
#include <QSignalBlocker>
#include <cmath>

static QString mhz(double hz)  // 433.92, 100, 0.0001 - no trailing zeros
{
    QString s = QString::number(hz / 1e6, 'f', 6);
    while (s.endsWith('0'))
        s.chop(1);
    if (s.endsWith('.'))
        s.chop(1);
    return s;
}

MainWindow::MainWindow(bool simulators, QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_simulators(simulators)
{
    ui->setupUi(this);
    ui->splitter->setStretchFactor(1, 1);
    ui->pointsCombo->setCurrentText("450");

    m_sweepsLabel = new QLabel(this);
    m_rateLabel = new QLabel(this);
    m_batteryLabel = new QLabel(this);
    for (QLabel *l : {m_sweepsLabel, m_rateLabel, m_batteryLabel})
        statusBar()->addPermanentWidget(l);

    connect(ui->connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(ui->scanButton, &QPushButton::clicked, this, [this](bool on) {
        m_session.setScan(on ? DeviceLink::Continuous : DeviceLink::Off);
    });
    connect(ui->onceButton, &QPushButton::clicked, this, [this] { m_session.setScan(DeviceLink::Once); });
    for (QLineEdit *e : {ui->startEdit, ui->stopEdit, ui->centerEdit, ui->spanEdit})
        connect(e, &QLineEdit::editingFinished, this, [this, e] { editRange(e); });
    connect(ui->pointsCombo, &QComboBox::activated, this, [this] {
        SweepConfig c = m_session.config();
        c.grid.points = ui->pointsCombo->currentText().toInt();
        m_session.setConfig(c);
    });

    connect(&m_session, &Session::connected, this, &MainWindow::onConnected);
    connect(&m_session, &Session::disconnected, this, &MainWindow::onDisconnected);
    connect(&m_session, &Session::configChanged, this, &MainWindow::showConfig);
    connect(&m_session, &Session::frame, this, &MainWindow::onFrame);
    connect(&m_session, &Session::scanChanged, this, &MainWindow::onScanChanged);
    connect(&m_session, &Session::message, this, [this](const QString &m) {
        statusBar()->showMessage(m, 5000);
    });
    connect(&m_session, &Session::battery, this, [this](int mV) {
        m_batteryLabel->setText(QString("Battery %1 V").arg(mV / 1000.0, 0, 'f', 2));
    });

    SweepConfig c;
    c.grid = {0, 350e6, ui->pointsCombo->currentText().toInt()};
    m_session.setConfig(c);
    showConfig(m_session.config());
    onScanChanged(DeviceLink::Off);

    refreshPorts();
    connect(&m_portPoll, &QTimer::timeout, this, &MainWindow::refreshPorts);
    m_portPoll.start(1000);  // hot-plug: cheap enough to poll
}

MainWindow::~MainWindow() = default;

void MainWindow::refreshPorts()
{
    const auto ports = QSerialPortInfo::availablePorts();
    QStringList names;
    for (const auto &p : ports)
        names << p.portName();
    if (names == m_portNames && ui->portCombo->count())
        return;
    m_portNames = names;

    const QString current = ui->portCombo->currentData().toString();
    QStringList tinySAs;
    QSignalBlocker block(ui->portCombo);
    ui->portCombo->clear();
    for (const auto &p : ports) {
        const bool isTinySA = p.vendorIdentifier() == 0x0483 && p.productIdentifier() == 0x5740;
        ui->portCombo->addItem(isTinySA ? p.portName() + " - tinySA" : p.portName(), p.portName());
        if (isTinySA)
            tinySAs << p.portName();
    }
    if (m_simulators) {
        ui->portCombo->addItem("Simulated tinySA", "sim:basic");
        ui->portCombo->addItem("Simulated tinySA Ultra", "sim:ultra");
    }
    // keep a tinySA selection; otherwise pick a (newly plugged) tinySA
    int at = ui->portCombo->findData(current);
    const bool keep = at >= 0 && (tinySAs.contains(current) || current.startsWith("sim:"));
    if (!keep && !tinySAs.isEmpty() && !m_session.isConnected())
        at = ui->portCombo->findData(tinySAs.first());
    ui->portCombo->setCurrentIndex(std::max(at, 0));
}

void MainWindow::connectTo(const QString &port)
{
    int at = ui->portCombo->findData(port);
    if (at < 0) {
        ui->portCombo->addItem(port, port);
        at = ui->portCombo->count() - 1;
    }
    ui->portCombo->setCurrentIndex(at);
    if (!ui->connectButton->isChecked())
        ui->connectButton->click();
}

void MainWindow::onConnectClicked(bool checked)
{
    if (checked) {
        ui->connectButton->setEnabled(false);
        ui->connectButton->setText("Connecting...");
        ui->deviceLabel->setText("Connecting to " + ui->portCombo->currentText());
        m_session.connectDevice(ui->portCombo->currentData().toString());
    } else {
        m_session.disconnectDevice();
    }
}

void MainWindow::onConnected(const DeviceInfo &info)
{
    ui->connectButton->setEnabled(true);
    ui->connectButton->setChecked(true);
    ui->connectButton->setText("Disconnect");
    ui->portCombo->setEnabled(false);
    ui->deviceLabel->setText(info.title());
    setWindowTitle("tinySA-App - " + info.title());
    for (QWidget *w : {static_cast<QWidget *>(ui->scanButton), static_cast<QWidget *>(ui->onceButton)})
        w->setEnabled(true);
    showConfig(m_session.config());
}

void MainWindow::onDisconnected(const QString &reason)
{
    ui->connectButton->setEnabled(true);
    ui->connectButton->setChecked(false);
    ui->connectButton->setText("Connect");
    ui->portCombo->setEnabled(true);
    ui->deviceLabel->setText("Not connected");
    setWindowTitle("tinySA-App");
    ui->scanButton->setEnabled(false);
    ui->onceButton->setEnabled(false);
    m_batteryLabel->clear();
    statusBar()->showMessage(reason, 5000);
}

void MainWindow::onScanChanged(int mode)
{
    ui->scanButton->setChecked(mode == DeviceLink::Continuous);
    ui->scanButton->setText(mode == DeviceLink::Continuous ? "Stop" : "Scan");
    ui->scanButton->setEnabled(m_session.isConnected());
    ui->onceButton->setEnabled(m_session.isConnected() && mode == DeviceLink::Off);
}

void MainWindow::onFrame(const SweepFrame &f)
{
    ui->spectrum->setFrame(f);
    if (!f.complete)
        return;
    ++m_sweeps;
    m_sweepsLabel->setText(QString("Sweeps %1").arg(m_sweeps));
    if (f.seconds > 0)
        m_rateLabel->setText(QString("%1 points/s").arg(std::lround(f.grid.points / f.seconds)));
}

void MainWindow::showConfig(const SweepConfig &c)
{
    ui->startEdit->setText(mhz(c.grid.startHz));
    ui->stopEdit->setText(mhz(c.grid.stopHz));
    ui->centerEdit->setText(mhz((c.grid.startHz + c.grid.stopHz) / 2));
    ui->spanEdit->setText(mhz(c.grid.stopHz - c.grid.startHz));
    ui->pointsCombo->setCurrentText(QString::number(c.grid.points));
    ui->spectrum->setSpan(c.grid.startHz, c.grid.stopHz);
}

void MainWindow::editRange(QLineEdit *edit)
{
    SweepConfig c = m_session.config();
    const auto hz = parseHz(edit->text(), 1e6);
    if (hz) {
        const ModelDesc *m = m_session.device().model;
        FreqRange r(c.grid.startHz, c.grid.stopHz);
        r.setLimits(m ? m->lowLimit(c.input) : 0, m ? m->highLimit(c.input) : 100e9);
        if (edit == ui->startEdit) r.setStart(*hz);
        else if (edit == ui->stopEdit) r.setStop(*hz);
        else if (edit == ui->centerEdit) r.setCenter(*hz);
        else r.setSpan(*hz);
        c.grid.startHz = r.start();
        c.grid.stopHz = r.stop();
        m_session.setConfig(c);
    }
    showConfig(m_session.config());  // also reverts invalid input
}

QString MainWindow::testQuery(const QString &name)
{
    const SweepFrame &f = ui->spectrum->frame();
    auto peak = [&] {
        int best = -1;
        for (int i = 0; i < f.dBm.size(); ++i)
            if (!std::isnan(f.dBm[i]) && (best < 0 || f.dBm[i] > f.dBm[best]))
                best = i;
        return best;
    };
    if (name == "connected") return m_session.isConnected() ? "1" : "0";
    if (name == "model") return m_session.device().model ? m_session.device().model->name : "";
    if (name == "sweeps") return QString::number(m_sweeps);
    if (name == "scan") return QString::number(m_session.scanMode());
    if (name == "start") return QString::number(m_session.config().grid.startHz, 'f', 0);
    if (name == "stop") return QString::number(m_session.config().grid.stopHz, 'f', 0);
    if (name == "points") return QString::number(f.grid.points);
    if (name == "peak.hz") return peak() < 0 ? "" : QString::number(f.grid.freq(peak()), 'f', 0);
    if (name == "peak.dbm") return peak() < 0 ? "" : QString::number(f.dBm[peak()], 'f', 2);
    if (name == "trace") {
        QString csv;
        for (int i = 0; i < f.dBm.size(); ++i)
            csv += QString("%1,%2\n").arg(f.grid.freq(i), 0, 'f', 0).arg(f.dBm[i], 0, 'f', 2);
        return csv;
    }
    return {};
}

bool MainWindow::testAction(const QString &name, const QStringList &args)
{
    if (name == "connect" && args.size() == 1) {
        connectTo(args[0]);
        return true;
    }
    if (name == "touch") {
        m_session.simTouch();
        return true;
    }
    return false;
}
