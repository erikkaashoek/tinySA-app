#pragma once
// DfuSe (ST's DFU extension, used by the STM32 ROM bootloader of every tinySA)
// over an abstract USB transport, so every Windows driver the bootloader may be
// bound to (WinUSB, libusb-win32, libusbK, STTub30) shares the protocol code.
#include "dfufile.h"
#include <functional>

// Class requests to the DFU interface (interface 0).
class DfuTransport {
public:
    virtual ~DfuTransport() = default;
    virtual bool out(quint8 request, quint16 value, const QByteArray &data) = 0;
    virtual bool in(quint8 request, quint16 value, int length, QByteArray *data) = 0;
    virtual QString layout() = 0;  // interface string = DfuSe memory layout, "" if unknown
    QString error;
};

class DfuSe {
public:
    using Progress = std::function<void(int percent, const QString &step)>;
    DfuSe(DfuTransport &t, Progress progress = {});

    FlashLayout flash() const { return m_flash; }
    bool readFlash(QByteArray *data);  // the whole flash
    // Erases only the pages the image covers (the unit keeps its stored config).
    bool write(const FirmwareImage &img, bool verify);
    bool leave(quint32 address = 0x08000000);  // start the firmware
    QString error() const { return m_error; }

    static constexpr int kTransfer = 2048;  // wTransferSize of the STM32 ROM bootloader

private:
    struct Status { int status = 0, state = 0, pollMs = 0; };
    bool status(Status *s);
    bool idle();                              // back to dfuIDLE from any state
    bool command(const QByteArray &payload);  // DfuSe command in download block 0
    bool setAddress(quint32 a);
    bool download(quint16 block, const QByteArray &data);
    bool upload(quint32 address, int length, QByteArray *data);
    bool fail(const QString &what);

    DfuTransport &m_t;
    Progress m_progress;
    FlashLayout m_flash;
    QString m_error;
};
