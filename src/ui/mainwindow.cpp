#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QCloseEvent>
#include <QLabel>
#include <QRegularExpression>
#include <QSerialPortInfo>
#include <QSettings>
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

MainWindow::MainWindow(bool simulators, const QString &settingsPath, QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_settingsPath(settingsPath),
      m_simulators(simulators)
{
    ui->setupUi(this);
    ui->splitter->setStretchFactor(1, 1);

    m_sweepsLabel = new QLabel(this);
    m_rateLabel = new QLabel(this);
    m_batteryLabel = new QLabel(this);
    for (QLabel *l : {m_sweepsLabel, m_rateLabel, m_batteryLabel})
        statusBar()->addPermanentWidget(l);

    // --- widgets -> session ---
    connect(ui->connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(ui->scanButton, &QPushButton::clicked, this, [this](bool on) {
        m_session.setScan(on ? DeviceLink::Continuous : DeviceLink::Off);
    });
    connect(ui->onceButton, &QPushButton::clicked, this, [this] { m_session.setScan(DeviceLink::Once); });
    for (QLineEdit *e : {ui->startEdit, ui->stopEdit, ui->centerEdit, ui->spanEdit})
        connect(e, &QLineEdit::editingFinished, this, [this, e] { editRange(e); });
    connect(ui->pointsCombo, &QComboBox::activated, this, [this] {
        editConfig([this](SweepConfig &c) { c.grid.points = ui->pointsCombo->currentText().toInt(); });
    });
    connect(ui->rbwCombo, &QComboBox::activated, this, [this] {
        editConfig([this](SweepConfig &c) { c.rbwKHz = ui->rbwCombo->currentData().toDouble(); });
    });
    connect(ui->attenCombo, &QComboBox::activated, this, [this] {
        editConfig([this](SweepConfig &c) { c.atten = ui->attenCombo->currentData().toInt(); });
    });
    connect(ui->inputCombo, &QComboBox::activated, this, [this](int i) {
        editConfig([this, i](SweepConfig &c) {
            c.input = i ? InputMode::High : InputMode::Low;
            if (const ModelDesc *m = m_session.device().model) {  // full range of that input
                c.grid.startHz = m->lowLimit(c.input);
                c.grid.stopHz = m->highLimit(c.input);
            }
        });
    });
    connect(ui->lnaCheck, &QCheckBox::clicked, this, [this](bool on) {
        editConfig([on](SweepConfig &c) { c.lna = on; });
    });
    connect(ui->deviceRangeButton, &QPushButton::clicked, this, [this] {
        const ScanGrid g = m_session.device().deviceGrid;
        if (g.points > 1)
            editConfig([g](SweepConfig &c) { c.grid = g; });
    });

    // --- session -> render ---
    connect(&m_session, &Session::connected, this, &MainWindow::onConnected);
    connect(&m_session, &Session::disconnected, this, &MainWindow::onDisconnected);
    connect(&m_session, &Session::configChanged, this, &MainWindow::showConfig);
    connect(&m_session, &Session::frame, this, &MainWindow::onFrame);
    connect(&m_session, &Session::scanChanged, this, &MainWindow::onScanChanged);
    connect(&m_session, &Session::message, this, [this](const QString &m) {
        statusBar()->showMessage(m, 5000);
        m_messages << m;
    });
    connect(&m_session, &Session::battery, this, [this](int mV) {
        m_batteryLabel->setText(QString("Battery %1 V").arg(mV / 1000.0, 0, 'f', 2));
    });
    connect(&m_session, &Session::traffic, this, [this](bool sent, const QString &text) {
        if (sent && m_sent.size() < 10000)
            m_sent << text;
    });

    // --- restore ---
    QSettings qs(m_settingsPath, QSettings::IniFormat);
    m_settings.config.grid = {0, 350e6, 450};
    m_settings.load(qs);
    restoreGeometry(qs.value("window/geometry").toByteArray());
    ui->splitter->restoreState(qs.value("window/splitter").toByteArray());
    applyModel(nullptr);
    m_session.setConfig(m_settings.config);
    showConfig(m_session.config());
    onScanChanged(DeviceLink::Off);

    refreshPorts();
    connect(&m_portPoll, &QTimer::timeout, this, &MainWindow::refreshPorts);
    m_portPoll.start(1000);  // hot-plug: cheap enough to poll
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *e)
{
    QSettings qs(m_settingsPath, QSettings::IniFormat);
    m_settings.config = m_session.config();
    m_settings.save(qs);
    qs.setValue("window/geometry", saveGeometry());
    qs.setValue("window/splitter", ui->splitter->saveState());
    e->accept();
}

void MainWindow::refreshPorts()
{
    const auto ports = QSerialPortInfo::availablePorts();
    QStringList names;
    for (const auto &p : ports)
        names << p.portName();
    const QString connectedPort = m_session.device().port;
    if (m_session.isConnected() && !connectedPort.startsWith("sim:") && !names.contains(connectedPort)) {
        m_session.disconnectDevice();  // unplugged; the serial error may come later or never
        statusBar()->showMessage("tinySA unplugged", 5000);
    }
    if (names == m_portNames && ui->portCombo->count())
        return;
    m_portNames = names;

    const QString current = ui->portCombo->count() ? ui->portCombo->currentData().toString()
                                                   : m_settings.port;
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
    m_settings.port = info.port;
    ui->connectButton->setEnabled(true);
    ui->connectButton->setChecked(true);
    ui->connectButton->setText("Disconnect");
    ui->portCombo->setEnabled(false);
    ui->deviceLabel->setText(info.title());
    setWindowTitle("tinySA-App - " + info.title());
    ui->deviceRangeButton->setEnabled(info.deviceGrid.points > 1);
    applyModel(info.model);
    onScanChanged(m_session.scanMode());
}

void MainWindow::onDisconnected(const QString &reason)
{
    ui->connectButton->setEnabled(true);
    ui->connectButton->setChecked(false);
    ui->connectButton->setText("Connect");
    ui->portCombo->setEnabled(true);
    ui->deviceLabel->setText("Not connected");
    setWindowTitle("tinySA-App");
    ui->deviceRangeButton->setEnabled(false);
    m_batteryLabel->clear();
    onScanChanged(DeviceLink::Off);
    statusBar()->showMessage(reason, 5000);
}

void MainWindow::applyModel(const ModelDesc *m)
{
    // before connecting, offer the widest lists; the session clamps on connect
    const QVector<double> rbw = m ? m->rbwKHz : QVector<double>{0.2, 1, 3, 10, 30, 100, 300, 600, 850};
    ui->rbwCombo->clear();
    ui->rbwCombo->addItem("Auto", 0.0);
    for (double k : rbw)
        ui->rbwCombo->addItem(k < 1 ? QString("%1 Hz").arg(k * 1000) : QString("%1 kHz").arg(k), k);
    ui->attenCombo->clear();
    ui->attenCombo->addItem("Auto", -1);
    for (int db = 0; db <= (m ? m->maxAtten : 30); ++db)
        ui->attenCombo->addItem(QString("%1 dB").arg(db), db);
    ui->inputLabel->setVisible(m && m->hasInputMode);
    ui->inputCombo->setVisible(m && m->hasInputMode);
    ui->lnaCheck->setVisible(m && m->hasLna);
    showConfig(m_session.config());
}

void MainWindow::onScanChanged(int mode)
{
    const bool on = m_session.isConnected();
    ui->scanButton->setChecked(mode == DeviceLink::Continuous);
    ui->scanButton->setText(mode == DeviceLink::Continuous ? "Stop" : "Scan");
    ui->scanButton->setEnabled(on);
    ui->onceButton->setEnabled(on && mode == DeviceLink::Off);
}

void MainWindow::onFrame(const SweepFrame &f)
{
    ui->spectrum->setFrame(f);
    if (!f.complete)
        return;
    ++m_sweeps;
    m_sweepMs = f.seconds * 1000;
    if (m_cycle.isValid())
        m_cycleMs = m_cycle.restart();  // sweep-to-sweep time, incl. commands in between
    else
        m_cycle.start();
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
    const QString points = QString::number(c.grid.points);
    if (ui->pointsCombo->findText(points) < 0)  // e.g. the unit's own 450 points
        ui->pointsCombo->addItem(points);
    ui->pointsCombo->setCurrentText(points);
    ui->rbwCombo->setCurrentIndex(std::max(0, ui->rbwCombo->findData(c.rbwKHz)));
    ui->attenCombo->setCurrentIndex(std::max(0, ui->attenCombo->findData(c.atten)));
    ui->inputCombo->setCurrentIndex(c.input == InputMode::High ? 1 : 0);
    ui->lnaCheck->setChecked(c.lna);
    ui->spectrum->setSpan(c.grid.startHz, c.grid.stopHz);
}

void MainWindow::editConfig(const std::function<void(SweepConfig &)> &change)
{
    SweepConfig c = m_session.config();
    change(c);
    m_session.setConfig(c);
    showConfig(m_session.config());
}

void MainWindow::editRange(QLineEdit *edit)
{
    const auto hz = parseHz(edit->text(), 1e6);
    if (!hz)
        return showConfig(m_session.config());  // revert invalid input
    editConfig([&](SweepConfig &c) {
        const ModelDesc *m = m_session.device().model;
        FreqRange r(c.grid.startHz, c.grid.stopHz);
        r.setLimits(m ? m->lowLimit(c.input) : 0, m ? m->highLimit(c.input) : 100e9);
        if (edit == ui->startEdit) r.setStart(*hz);
        else if (edit == ui->stopEdit) r.setStop(*hz);
        else if (edit == ui->centerEdit) r.setCenter(*hz);
        else r.setSpan(*hz);
        c.grid.startHz = r.start();
        c.grid.stopHz = r.stop();
    });
}

std::optional<QString> MainWindow::testQuery(const QString &name)
{
    const SweepFrame &f = ui->spectrum->frame();
    const SweepConfig c = m_session.config();
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
    if (name == "sweeps.new") return QString::number(m_sweeps - m_sweepsMark);  // since "do mark"
    if (name == "scan") return QString::number(m_session.scanMode());
    if (name == "idle") return m_session.scanMode() == DeviceLink::Off && !m_session.isBusy() ? "1" : "0";
    if (name == "start") return QString::number(c.grid.startHz, 'f', 0);
    if (name == "stop") return QString::number(c.grid.stopHz, 'f', 0);
    if (name == "rbw") return QString::number(c.rbwKHz);
    if (name == "atten") return QString::number(c.atten);
    if (name == "lna") return c.lna ? "1" : "0";
    if (name == "input") return c.input == InputMode::High ? "high" : "low";
    if (name == "points") return QString::number(f.grid.points);
    if (name == "sweepms") return QString::number(m_sweepMs, 'f', 1);
    if (name == "cyclems") return QString::number(m_cycleMs, 'f', 1);
    if (name == "messages") return m_messages.join('|');
    if (name == "sent") return QStringList(m_sent).filter(QRegularExpression("^(?!vbat$)")).join('|');
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
    if (name == "clearsent") {
        m_sent.clear();
        return true;
    }
    if (name == "mark") {
        m_sweepsMark = m_sweeps;
        return true;
    }
    if (name == "send" && !args.isEmpty()) {  // raw command, e.g. "caloutput 10"
        m_session.send(args.join(' '));
        return true;
    }
    return false;
}
