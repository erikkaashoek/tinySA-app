#include "dfuusb.h"
#include <QUuid>
#include <windows.h>
#include <initguid.h>
#include <setupapi.h>
#include <usbiodef.h>
#include <winioctl.h>
#include <winusb.h>

static QString winError(DWORD e = GetLastError())
{
    wchar_t *text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, 0, reinterpret_cast<wchar_t *>(&text), 0, nullptr);
    const QString s = text ? QString::fromWCharArray(text).trimmed() : QString("error %1").arg(e);
    LocalFree(text);
    return s;
}

static const wchar_t *wide(const QString &s)
{
    return reinterpret_cast<const wchar_t *>(s.utf16());
}

static constexpr DWORD code(DWORD function)  // buffered IOCTL of a USB driver
{
    return CTL_CODE(FILE_DEVICE_UNKNOWN, function, METHOD_BUFFERED, FILE_ANY_ACCESS);
}

// ---------------------------------------------------------------- enumeration

static QString property(HDEVINFO set, SP_DEVINFO_DATA &d, DWORD prop)
{
    wchar_t buf[1024] = {};
    if (!SetupDiGetDeviceRegistryPropertyW(set, &d, prop, nullptr, reinterpret_cast<PBYTE>(buf), sizeof(buf) - 4, nullptr))
        return {};
    return QString::fromWCharArray(buf);
}

QList<DfuDevice> findDfuDevices()
{
    QList<DfuDevice> out;
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE)
        return out;
    SP_DEVINFO_DATA d{sizeof(d)};
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &d); ++i) {
        wchar_t ids[2048] = {};  // REG_MULTI_SZ
        if (!SetupDiGetDeviceRegistryPropertyW(set, &d, SPDRP_HARDWAREID, nullptr, reinterpret_cast<PBYTE>(ids),
                                               sizeof(ids) - 4, nullptr))
            continue;
        bool match = false;
        for (const wchar_t *p = ids; *p; p += wcslen(p) + 1)
            match |= QString::fromWCharArray(p).contains("VID_0483&PID_DF11", Qt::CaseInsensitive);
        if (!match)
            continue;
        wchar_t instance[512] = {};
        SetupDiGetDeviceInstanceIdW(set, &d, instance, 512, nullptr);
        DfuDevice dev{QString::fromWCharArray(instance), property(set, d, SPDRP_SERVICE),
                      property(set, d, SPDRP_FRIENDLYNAME)};
        if (dev.description.isEmpty())
            dev.description = property(set, d, SPDRP_DEVICEDESC);
        if (dev.service.isEmpty())
            dev.service = "no";
        out << dev;
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

// Device path of an interface of the given class on this device instance.
static QString interfacePath(const QString &instance, const GUID &guid)
{
    HDEVINFO set = SetupDiGetClassDevsW(&guid, wide(instance), nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return {};
    QString path;
    SP_DEVICE_INTERFACE_DATA ifd{sizeof(ifd)};
    if (SetupDiEnumDeviceInterfaces(set, nullptr, &guid, 0, &ifd)) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
        QByteArray buf(need + 8, 0);
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, need, nullptr, nullptr))
            path = QString::fromWCharArray(detail->DevicePath);
    }
    SetupDiDestroyDeviceInfoList(set);
    return path;
}

// Interface class GUIDs the driver INF registered (WinUSB, libusbK).
static QList<GUID> interfaceGuids(const QString &instance)
{
    QList<GUID> guids;
    HDEVINFO set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    SP_DEVINFO_DATA d{sizeof(d)};
    if (SetupDiOpenDeviceInfoW(set, wide(instance), nullptr, 0, &d)) {
        HKEY key = SetupDiOpenDevRegKey(set, &d, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key != INVALID_HANDLE_VALUE) {
            for (const wchar_t *name : {L"DeviceInterfaceGUIDs", L"DeviceInterfaceGUID"}) {
                wchar_t buf[1024] = {};
                DWORD size = sizeof(buf) - 4;
                if (RegQueryValueExW(key, name, nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS)
                    for (const wchar_t *p = buf; *p; p += wcslen(p) + 1) {
                        const QUuid u(QString::fromWCharArray(p));
                        if (!u.isNull())
                            guids << GUID(u);
                    }
            }
            RegCloseKey(key);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return guids;
}

// iInterface of interface 0, alternate setting 0 in a configuration descriptor
static int dfuInterfaceString(const QByteArray &config)
{
    for (int at = 0; at + 9 <= config.size() && quint8(config[at]) >= 2; at += quint8(config[at]))
        if (config[at + 1] == 4 && config[at + 2] == 0 && config[at + 3] == 0)
            return quint8(config[at + 8]);
    return 0;
}

static QString stringDescriptor(const QByteArray &d)
{
    if (d.size() < 2 || d[1] != 3)
        return {};
    const int n = std::min<int>(quint8(d[0]), int(d.size()));
    return QString::fromUtf16(reinterpret_cast<const char16_t *>(d.constData() + 2), (n - 2) / 2);
}

// ---------------------------------------------------------------- WinUSB

class WinUsbTransport : public DfuTransport {
public:
    ~WinUsbTransport() override
    {
        if (m_usb)
            WinUsb_Free(m_usb);
        if (m_file != INVALID_HANDLE_VALUE)
            CloseHandle(m_file);
    }
    bool open(const QString &path)
    {
        m_file = CreateFileW(wide(path), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
        if (m_file == INVALID_HANDLE_VALUE || !WinUsb_Initialize(m_file, &m_usb)) {
            error = winError();
            return false;
        }
        ULONG timeout = 5000;
        WinUsb_SetPipePolicy(m_usb, 0, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout);
        return true;
    }
    bool out(quint8 request, quint16 value, const QByteArray &data) override
    {
        WINUSB_SETUP_PACKET p{0x21, request, value, 0, USHORT(data.size())};
        ULONG done = 0;
        if (WinUsb_ControlTransfer(m_usb, p, reinterpret_cast<PUCHAR>(const_cast<char *>(data.constData())),
                                   ULONG(data.size()), &done, nullptr))
            return true;
        error = winError();
        return false;
    }
    bool in(quint8 request, quint16 value, int length, QByteArray *data) override
    {
        WINUSB_SETUP_PACKET p{0xA1, request, value, 0, USHORT(length)};
        data->resize(length);
        ULONG done = 0;
        if (!WinUsb_ControlTransfer(m_usb, p, reinterpret_cast<PUCHAR>(data->data()), ULONG(length), &done, nullptr)) {
            error = winError();
            return false;
        }
        data->resize(done);
        return true;
    }
    QString layout() override
    {
        USB_INTERFACE_DESCRIPTOR d{};
        UCHAR buf[256] = {};
        ULONG got = 0;
        if (!WinUsb_QueryInterfaceSettings(m_usb, 0, &d) || !d.iInterface
            || !WinUsb_GetDescriptor(m_usb, 3, d.iInterface, 0x0409, buf, sizeof(buf), &got))
            return {};
        return stringDescriptor(QByteArray(reinterpret_cast<char *>(buf), int(got)));
    }

private:
    HANDLE m_file = INVALID_HANDLE_VALUE;
    WINUSB_INTERFACE_HANDLE m_usb = nullptr;
};

// ---------------------------------------------------------------- libusb-win32 / libusbK

// Request block of the libusb0.sys IOCTL interface (libusb-win32 driver_api.h);
// libusbK.sys accepts the same IOCTLs.
#pragma pack(push, 1)
struct Lib0Request {
    unsigned int timeout;
    union {
        struct { unsigned int type, recipient, request, value, index; } vendor;
        struct { unsigned int type, index, language_id, recipient; } descriptor;
        struct { unsigned int configuration; } configuration;
        struct { unsigned int interface_number, altsetting_number; } intf;
        unsigned int raw[5];
    };
};
#pragma pack(pop)
static_assert(sizeof(Lib0Request) == 24);

class Lib0Transport : public DfuTransport {
public:
    ~Lib0Transport() override
    {
        if (m_h != INVALID_HANDLE_VALUE)
            CloseHandle(m_h);
    }
    bool open(const QString &path)
    {
        m_h = CreateFileW(wide(path), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                          OPEN_EXISTING, 0, nullptr);
        if (m_h == INVALID_HANDLE_VALUE) {
            error = winError();
            return false;
        }
        return true;
    }
    void claim()  // only once we know it is our device
    {
        Lib0Request r{5000};
        r.configuration.configuration = 1;  // as usb_set_configuration(1) + usb_claim_interface(0)
        ioctl(kSetConfiguration, raw(r), 0, nullptr);
        r = Lib0Request{5000};
        ioctl(kClaimInterface, raw(r), 0, nullptr);
        error.clear();
    }
    // vendor/product ID from the device descriptor (to find our device among \\.\libusb0-NNNN)
    quint32 ids()
    {
        const QByteArray d = descriptor(1, 0, 0, 18);
        return d.size() < 12 ? 0 : quint32(quint8(d[8]) | quint8(d[9]) << 8) << 16 | quint8(d[10]) | quint8(d[11]) << 8;
    }
    bool out(quint8 request, quint16 value, const QByteArray &data) override
    {
        return ioctl(kVendorWrite, raw(classRequest(request, value)) + data, 0, nullptr);
    }
    bool in(quint8 request, quint16 value, int length, QByteArray *data) override
    {
        return ioctl(kVendorRead, raw(classRequest(request, value)), length, data);
    }
    QString layout() override
    {
        const int index = dfuInterfaceString(descriptor(2, 0, 0, 512));
        return index ? stringDescriptor(descriptor(3, index, 0x0409, 255)) : QString();
    }

private:
    static constexpr DWORD kSetConfiguration = code(0x801), kGetDescriptor = code(0x809),
                           kVendorWrite = code(0x80C), kVendorRead = code(0x80D), kClaimInterface = code(0x815);
    static QByteArray raw(const Lib0Request &r) { return QByteArray(reinterpret_cast<const char *>(&r), sizeof(r)); }
    static Lib0Request classRequest(quint8 request, quint16 value)
    {
        Lib0Request r{5000};
        r.vendor.type = 1;       // class
        r.vendor.recipient = 1;  // interface
        r.vendor.request = request;
        r.vendor.value = value;
        r.vendor.index = 0;
        return r;
    }
    QByteArray descriptor(unsigned type, unsigned index, unsigned lang, int length)
    {
        Lib0Request r{5000};
        r.descriptor.type = type;
        r.descriptor.index = index;
        r.descriptor.language_id = lang;
        r.descriptor.recipient = 0;
        QByteArray out;
        ioctl(kGetDescriptor, raw(r), length, &out);
        return out;
    }
    bool ioctl(DWORD code, const QByteArray &in, int outLen, QByteArray *out)
    {
        QByteArray buf(std::max(outLen, 1), 0);
        DWORD got = 0;
        if (!DeviceIoControl(m_h, code, const_cast<char *>(in.constData()), DWORD(in.size()),
                             outLen ? buf.data() : nullptr, DWORD(outLen), &got, nullptr)) {
            error = winError();
            return false;
        }
        if (out)
            *out = buf.left(int(got));
        return true;
    }

    HANDLE m_h = INVALID_HANDLE_VALUE;
};

// ---------------------------------------------------------------- STTub30 (ST DfuSe)

class StTubTransport : public DfuTransport {
public:
    ~StTubTransport() override
    {
        if (m_h != INVALID_HANDLE_VALUE)
            CloseHandle(m_h);
    }
    bool open(const QString &path)
    {
        m_h = CreateFileW(wide(path), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (m_h == INVALID_HANDLE_VALUE) {
            error = winError();
            return false;
        }
        const char config[3] = {0, 0, 0};  // configuration index, interface, alternate setting
        return ioctl(kConfig, QByteArray(config, 3), 0, nullptr);
    }
    bool out(quint8 request, quint16 value, const QByteArray &data) override
    {
        return ioctl(kVendorRequest, raw(request, value, 0, data.size()) + data, 0, nullptr);
    }
    bool in(quint8 request, quint16 value, int length, QByteArray *data) override
    {
        return ioctl(kVendorRequest, raw(request, value, 1, length), length, data);
    }
    QString layout() override
    {
        QByteArray config;
        const int index = 0;
        if (!ioctl(kGetConfigDescriptor, QByteArray(reinterpret_cast<const char *>(&index), 4), 512, &config))
            return {};
        const int str = dfuInterfaceString(config);
        const char id[3] = {char(str), 0x04, 0x09};  // string index, language 0x0409
        QByteArray d;
        return str && ioctl(kGetStringDescriptor, QByteArray(id, 3), 255, &d) ? stringDescriptor(d) : QString();
    }

private:
    // CNTRPIPE_RQ of the STTub30 driver (natural alignment, 20 bytes)
    struct Request {
        USHORT function;
        ULONG direction;
        char request;
        USHORT value;
        USHORT index;
        ULONG length;
    };
    static_assert(sizeof(Request) == 20);
    static constexpr DWORD kGetConfigDescriptor = code(0x801), kGetStringDescriptor = code(0x803),
                           kConfig = code(0x804), kVendorRequest = code(0x805);
    static QByteArray raw(quint8 request, quint16 value, ULONG direction, qsizetype length)
    {
        const Request r{0x1B /* URB_FUNCTION_CLASS_INTERFACE */, direction, char(request), value, 0, ULONG(length)};
        return QByteArray(reinterpret_cast<const char *>(&r), sizeof(r));
    }
    bool ioctl(DWORD code, const QByteArray &in, int outLen, QByteArray *out)
    {
        QByteArray buf(std::max(outLen, 1), 0);
        DWORD got = 0;
        if (!DeviceIoControl(m_h, code, const_cast<char *>(in.constData()), DWORD(in.size()),
                             outLen ? buf.data() : nullptr, DWORD(outLen), &got, nullptr)) {
            error = winError();
            return false;
        }
        if (out)
            *out = buf.left(int(got));
        return true;
    }

    HANDLE m_h = INVALID_HANDLE_VALUE;
};

// ---------------------------------------------------------------- opening

std::unique_ptr<DfuTransport> openDfu(const DfuDevice &d, QString *error)
{
    const QString driver = d.service.toLower();
    if (driver == "winusb" || driver == "libusbk") {
        for (const GUID &g : interfaceGuids(d.instanceId)) {
            const QString path = interfacePath(d.instanceId, g);
            if (path.isEmpty())
                continue;
            if (driver == "winusb") {
                auto t = std::make_unique<WinUsbTransport>();
                if (t->open(path))
                    return t;
                *error = t->error;
            } else {
                auto t = std::make_unique<Lib0Transport>();
                if (t->open(path)) {
                    t->claim();
                    return t;
                }
                *error = t->error;
            }
        }
        if (error->isEmpty())
            *error = "no device interface registered by the " + d.service + " driver";
        return nullptr;
    }
    if (driver == "libusb0") {  // libusb-win32 names its devices \\.\libusb0-0001 ...
        for (int i = 1; i <= 256; ++i) {
            auto t = std::make_unique<Lib0Transport>();
            if (t->open(QString("\\\\.\\libusb0-%1").arg(i, 4, 10, QChar('0'))) && t->ids() == 0x0483DF11u) {
                t->claim();
                return t;
            }
        }
        *error = "the libusb-win32 device was not found";
        return nullptr;
    }
    if (driver == "sttub30") {
        static const GUID kDfuSe = {0x3FE809AB, 0xFB91, 0x4CB5, {0xA6, 0x43, 0x69, 0x67, 0x0D, 0x52, 0x36, 0x6E}};
        for (const GUID &g : {kDfuSe, GUID_DEVINTERFACE_USB_DEVICE}) {
            const QString path = interfacePath(d.instanceId, g);
            auto t = std::make_unique<StTubTransport>();
            if (!path.isEmpty() && t->open(path))
                return t;
            if (!path.isEmpty())
                *error = t->error;
        }
        if (error->isEmpty())
            *error = "the STTub30 device was not found";
        return nullptr;
    }
    *error = QString("The DFU device uses the '%1' driver, which the app cannot talk to. "
                     "Install the WinUSB driver for it, e.g. with STM32CubeProgrammer or Zadig.").arg(d.service);
    return nullptr;
}
