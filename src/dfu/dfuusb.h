#pragma once
// Finds tinySA (STM32) bootloaders in DFU mode (USB 0483:DF11) and opens them
// through whichever Windows driver is bound: WinUSB (STM32CubeProgrammer,
// Zadig), libusb-win32 / libusbK (Zadig) or ST's legacy STTub30 (DfuSe).
#include "dfuse.h"
#include <memory>

struct DfuDevice {
    QString instanceId;   // USB\VID_0483&PID_DF11\<serial>
    QString service;      // bound driver: WinUSB, libusb0, libusbK, STTub30, ...
    QString description;  // e.g. "STM32 BOOTLOADER"
    QString label() const { return QString("%1 (%2 driver)").arg(description, service); }
};

QList<DfuDevice> findDfuDevices();
std::unique_ptr<DfuTransport> openDfu(const DfuDevice &d, QString *error);
