#pragma once
// Runs DFU jobs (write, read, leave) in its own thread so the GUI stays live.
#include "dfuusb.h"
#include <QObject>

class FirmwareUpdater : public QObject {
    Q_OBJECT
public slots:
    void write(const DfuDevice &dev, const QString &file, bool verify);
    void read(const DfuDevice &dev, const QString &file);
    void leave(const DfuDevice &dev);

signals:
    void progress(int percent, const QString &step);
    void log(const QString &line);
    void finished(bool ok, const QString &message);

private:
    std::unique_ptr<DfuTransport> open(const DfuDevice &dev);
};
Q_DECLARE_METATYPE(DfuDevice)
