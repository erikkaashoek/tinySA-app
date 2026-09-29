#include "firmwaredialog.h"
#include "ui_firmwaredialog.h"
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

FirmwareDialog::FirmwareDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::FirmwareDialog), m_worker(new FirmwareUpdater)
{
    ui->setupUi(this);
    qRegisterMetaType<DfuDevice>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();

    connect(m_worker, &FirmwareUpdater::progress, this, [this](int p, const QString &step) {
        ui->dfuProgress->setValue(p);
        ui->dfuProgress->setFormat(step + " %p%");
    });
    connect(m_worker, &FirmwareUpdater::log, this, &FirmwareDialog::log);
    connect(m_worker, &FirmwareUpdater::finished, this, [this](bool ok, const QString &message) {
        log((ok ? "" : "ERROR: ") + message);
        setBusy(false);
        refresh();
    });

    connect(ui->enterDfuButton, &QPushButton::clicked, this, [this] {
        log("Sending 'reset dfu' to the connected tinySA");
        emit enterDfuRequested();
    });
    connect(ui->browseFirmwareButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Firmware file", ui->firmwareEdit->text(),
                                                          "Firmware (*.bin *.hex *.dfu);;All files (*)");
        if (!path.isEmpty())
            ui->firmwareEdit->setText(path);
    });
    connect(ui->uploadButton, &QPushButton::clicked, this, [this] {
        const DfuDevice *d = device();
        const QString file = ui->firmwareEdit->text();
        if (!d || !QFileInfo::exists(file)) {
            log("Choose a DFU device and an existing firmware file first");
            return;
        }
        if (QMessageBox::question(this, "Firmware update",
                                  "Write " + QFileInfo(file).fileName() + " to the tinySA?")
            == QMessageBox::Yes)
            startWrite(file, ui->verifyCheck->isChecked());
    });
    connect(ui->saveFlashButton, &QPushButton::clicked, this, [this] {
        const DfuDevice *d = device();
        if (!d)
            return;
        const QString file = QFileDialog::getSaveFileName(
            this, "Save flash", "tinySA flash " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm") + ".bin",
            "Binary (*.bin)");
        if (!file.isEmpty())
            startRead(file);
    });
    connect(ui->leaveDfuButton, &QPushButton::clicked, this, &FirmwareDialog::startLeave);
    connect(&m_poll, &QTimer::timeout, this, &FirmwareDialog::refresh);
    setBusy(false);
}

FirmwareDialog::~FirmwareDialog()
{
    m_thread.quit();
    m_thread.wait();
}

bool FirmwareDialog::startWrite(const QString &file, bool verify)
{
    const DfuDevice *d = device();
    if (!d || m_busy)
        return false;
    setBusy(true);
    QMetaObject::invokeMethod(m_worker, [w = m_worker, dev = *d, file, verify] { w->write(dev, file, verify); });
    return true;
}

bool FirmwareDialog::startRead(const QString &file)
{
    const DfuDevice *d = device();
    if (!d || m_busy)
        return false;
    setBusy(true);
    QMetaObject::invokeMethod(m_worker, [w = m_worker, dev = *d, file] { w->read(dev, file); });
    return true;
}

bool FirmwareDialog::startLeave()
{
    const DfuDevice *d = device();
    if (!d || m_busy)
        return false;
    setBusy(true);
    QMetaObject::invokeMethod(m_worker, [w = m_worker, dev = *d] { w->leave(dev); });
    return true;
}

void FirmwareDialog::showEvent(QShowEvent *e)
{
    refresh();
    m_poll.start(1000);  // DFU devices come and go when the tinySA restarts
    QDialog::showEvent(e);
}

void FirmwareDialog::hideEvent(QHideEvent *e)
{
    m_poll.stop();
    QDialog::hideEvent(e);
}

void FirmwareDialog::setConnected(bool connected, bool dfuCommand)
{
    m_canEnterDfu = connected && dfuCommand;
    ui->enterDfuButton->setToolTip(connected && !dfuCommand
                                       ? "The tinySA Ultra has no DFU command: switch it off, hold the jog "
                                         "switch pressed and switch it on"
                                       : QString());
    setBusy(m_busy);
}

QString FirmwareDialog::logText() const
{
    return ui->dfuLog->toPlainText();
}

void FirmwareDialog::log(const QString &line)
{
    ui->dfuLog->appendPlainText(QTime::currentTime().toString("hh:mm:ss ") + line);
}

const DfuDevice *FirmwareDialog::device() const
{
    const int i = ui->dfuDeviceCombo->currentIndex();
    return i >= 0 && i < m_devices.size() ? &m_devices[i] : nullptr;
}

void FirmwareDialog::refresh()
{
    if (m_busy)
        return;
    const QList<DfuDevice> found = findDfuDevices();
    QStringList was, now;
    for (const DfuDevice &d : m_devices)
        was << d.instanceId + d.service;
    for (const DfuDevice &d : found)
        now << d.instanceId + d.service;
    if (was == now)
        return;
    m_devices = found;
    ui->dfuDeviceCombo->clear();
    for (const DfuDevice &d : m_devices) {
        ui->dfuDeviceCombo->addItem(d.label());
        log("Found " + d.label());
    }
    if (!m_devices.isEmpty())  // with a placeholder text Qt does not select the first item
        ui->dfuDeviceCombo->setCurrentIndex(0);
    setBusy(false);
}

void FirmwareDialog::setBusy(bool busy)
{
    m_busy = busy;
    const bool dev = !busy && !m_devices.isEmpty();
    ui->uploadButton->setEnabled(dev);
    ui->saveFlashButton->setEnabled(dev);
    ui->leaveDfuButton->setEnabled(dev);
    ui->dfuDeviceCombo->setEnabled(!busy);
    ui->enterDfuButton->setEnabled(!busy && m_canEnterDfu);
    if (!busy)
        ui->dfuProgress->setFormat("%p%");
}
