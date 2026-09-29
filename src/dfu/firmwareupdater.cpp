#include "firmwareupdater.h"
#include <QFile>

std::unique_ptr<DfuTransport> FirmwareUpdater::open(const DfuDevice &dev)
{
    QString error;
    auto t = openDfu(dev, &error);
    if (!t)
        emit finished(false, "Cannot open " + dev.label() + ": " + error);
    else
        emit log("Opened " + dev.label());
    return t;
}

void FirmwareUpdater::write(const DfuDevice &dev, const QString &file, bool verify)
{
    FirmwareImage img;
    QString error;
    if (!readFirmware(file, &img, &error))
        return emit finished(false, "Cannot read " + file + ": " + error);
    for (const FirmwareSegment &s : img.segments)
        emit log(QString("Image: %1 bytes at 0x%2").arg(s.data.size()).arg(s.address, 8, 16, QChar('0')));
    auto t = open(dev);
    if (!t)
        return;
    DfuSe dfu(*t, [this](int p, const QString &step) { emit progress(p, step); });
    emit log(QString("Flash: %1 KB at 0x%2").arg(dfu.flash().size() / 1024).arg(dfu.flash().base, 8, 16, QChar('0')));
    if (!dfu.write(img, verify))
        return emit finished(false, "Upload failed: " + dfu.error());
    emit finished(true, verify ? "Firmware written and verified" : "Firmware written");
}

void FirmwareUpdater::read(const DfuDevice &dev, const QString &file)
{
    auto t = open(dev);
    if (!t)
        return;
    DfuSe dfu(*t, [this](int p, const QString &step) { emit progress(p, step); });
    QByteArray data;
    if (!dfu.readFlash(&data))
        return emit finished(false, "Reading the flash failed: " + dfu.error());
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
        return emit finished(false, "Cannot write " + file + ": " + f.errorString());
    emit finished(true, QString("Saved %1 KB of flash to %2").arg(data.size() / 1024).arg(file));
}

void FirmwareUpdater::leave(const DfuDevice &dev)
{
    auto t = open(dev);
    if (!t)
        return;
    DfuSe dfu(*t);
    if (!dfu.leave())
        return emit finished(false, "Leaving DFU failed: " + dfu.error());
    emit finished(true, "The tinySA starts its firmware");
}
