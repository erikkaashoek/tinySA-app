#pragma once
// Firmware update dialog (Designer form + thin controller). DFU work runs in a
// FirmwareUpdater on its own thread.
#include "firmwareupdater.h"
#include <QDialog>
#include <QThread>
#include <QTimer>
#include <memory>

namespace Ui { class FirmwareDialog; }

class FirmwareDialog : public QDialog {
    Q_OBJECT
public:
    explicit FirmwareDialog(QWidget *parent = nullptr);
    ~FirmwareDialog() override;
    void setConnected(bool connected);  // a connected unit can be sent to DFU mode
    QString logText() const;
    int deviceCount() const { return int(m_devices.size()); }
    bool busy() const { return m_busy; }
    // the buttons use these after their file dialog / confirmation (tests call them directly)
    bool startWrite(const QString &file, bool verify);
    bool startRead(const QString &file);
    bool startLeave();

signals:
    void enterDfuRequested();

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void refresh();
    void setBusy(bool busy);
    void log(const QString &line);
    const DfuDevice *device() const;

    std::unique_ptr<Ui::FirmwareDialog> ui;
    QThread m_thread;
    FirmwareUpdater *m_worker;
    QList<DfuDevice> m_devices;
    QTimer m_poll;
    bool m_busy = false, m_canEnterDfu = false;
};
