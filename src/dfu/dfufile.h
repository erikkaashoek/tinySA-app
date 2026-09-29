#pragma once
// Firmware files for DFU upload: .bin (raw, at the flash base), Intel .hex and
// DfuSe .dfu, all turned into address segments. Plus the DfuSe memory layout
// string the bootloader reports ("@Internal Flash  /0x08000000/128*0002Kg").
#include <QByteArray>
#include <QList>
#include <QString>

struct FirmwareSegment {
    quint32 address = 0;
    QByteArray data;
};

struct FirmwareImage {
    QList<FirmwareSegment> segments;
    int size() const;
};

// Returns false and sets *error on failure. baseAddress is used for .bin files.
bool readFirmware(const QString &path, FirmwareImage *image, QString *error, quint32 baseAddress = 0x08000000);
bool parseIntelHex(const QByteArray &text, FirmwareImage *image, QString *error);
bool parseDfuSe(const QByteArray &file, FirmwareImage *image, QString *error);
quint32 dfuCrc(const QByteArray &data);  // CRC over a .dfu file minus its last 4 bytes

struct FlashLayout {
    quint32 base = 0x08000000;
    QList<std::pair<int, int>> sectors;  // (count, size in bytes), in address order
    int size() const;
    quint32 pageStart(quint32 address) const;  // start of the sector holding address
    int pageSize(quint32 address) const;       // 0 outside the flash
};
FlashLayout parseFlashLayout(const QString &s, bool *ok = nullptr);
