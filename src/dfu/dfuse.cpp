#include "dfuse.h"
#include <QThread>
#include <algorithm>

enum Request { DETACH, DNLOAD, UPLOAD, GETSTATUS, CLRSTATUS, GETSTATE, ABORT };
enum State { dfuIDLE = 2, dfuDNLOAD_SYNC = 3, dfuDNBUSY = 4, dfuDNLOAD_IDLE = 5, dfuERROR = 10 };

static QString statusText(int s)
{
    static const char *kText[] = {"OK", "target not supported", "file wrong", "cannot write", "cannot erase",
                                  "erase check failed", "cannot program", "verify failed",
                                  "address out of range", "not done", "firmware corrupt", "vendor error",
                                  "unexpected USB reset", "unexpected power-on reset", "unknown error",
                                  "unexpected request"};
    return s >= 0 && s < 16 ? kText[s] : QString("error %1").arg(s);
}

static QByteArray le32(quint32 v)
{
    return QByteArray(1, char(v)) + char(v >> 8) + char(v >> 16) + char(v >> 24);
}

DfuSe::DfuSe(DfuTransport &t, Progress progress) : m_t(t), m_progress(std::move(progress))
{
    bool ok = false;
    m_flash = parseFlashLayout(t.layout(), &ok);
    if (!ok)
        m_flash.sectors = {{64, 2048}};  // unknown: the smallest tinySA flash (128 KB)
}

bool DfuSe::fail(const QString &what)
{
    m_error = m_t.error.isEmpty() ? what : what + " (" + m_t.error + ")";
    return false;
}

bool DfuSe::status(Status *s)
{
    QByteArray r;
    if (!m_t.in(GETSTATUS, 0, 6, &r) || r.size() < 6)
        return fail("no status from the bootloader");
    s->status = quint8(r[0]);
    s->pollMs = quint8(r[1]) | quint8(r[2]) << 8 | quint8(r[3]) << 16;
    s->state = quint8(r[4]);
    return true;
}

bool DfuSe::idle()
{
    for (int tries = 0; tries < 5; ++tries) {
        Status s;
        if (!status(&s))
            return false;
        if (s.state == dfuIDLE)
            return true;
        m_t.out(s.state == dfuERROR ? CLRSTATUS : ABORT, 0, {});
    }
    return fail("the bootloader does not return to idle");
}

// Download request, then poll GETSTATUS until the bootloader has done the work.
bool DfuSe::download(quint16 block, const QByteArray &data)
{
    if (!m_t.out(DNLOAD, block, data))
        return fail("download request failed");
    for (int tries = 0; tries < 200; ++tries) {
        Status s;
        if (!status(&s))
            return false;
        if (s.status != 0)
            return fail("bootloader: " + statusText(s.status));
        if (s.state == dfuDNLOAD_IDLE || s.state == dfuIDLE)
            return true;
        if (s.state != dfuDNBUSY && s.state != dfuDNLOAD_SYNC)
            return fail(QString("unexpected bootloader state %1").arg(s.state));
        QThread::msleep(std::clamp(s.pollMs, 1, 1000));
    }
    return fail("the bootloader stays busy");
}

bool DfuSe::command(const QByteArray &payload)
{
    return download(0, payload);
}

bool DfuSe::setAddress(quint32 a)
{
    return command(char(0x21) + le32(a));
}

// Every chunk gets its own address pointer and block 2, so the bootloader's
// "pointer + (block - 2) * length" rule never depends on the chunk length.
bool DfuSe::upload(quint32 address, int length, QByteArray *data)
{
    data->clear();
    while (data->size() < length) {
        const int n = std::min(kTransfer, length - int(data->size()));
        QByteArray chunk;
        if (!idle() || !setAddress(address + quint32(data->size())) || !idle())
            return false;
        if (!m_t.in(UPLOAD, 2, n, &chunk) || chunk.size() != n)
            return fail(QString("read failed at 0x%1").arg(address + data->size(), 8, 16, QChar('0')));
        *data += chunk;
        if (m_progress)
            m_progress(int(100 * data->size() / length), "Reading");
    }
    return idle();
}

bool DfuSe::readFlash(QByteArray *data)
{
    return upload(m_flash.base, m_flash.size(), data);
}

bool DfuSe::write(const FirmwareImage &img, bool verify)
{
    QList<quint32> pages;
    for (const FirmwareSegment &seg : img.segments) {
        const quint32 end = seg.address + quint32(seg.data.size());
        for (quint32 a = m_flash.pageStart(seg.address); a < end; a += quint32(m_flash.pageSize(a))) {
            if (m_flash.pageSize(a) == 0)
                return fail(QString("the image does not fit the flash (0x%1)").arg(a, 8, 16, QChar('0')));
            if (!pages.contains(a))
                pages << a;
        }
    }
    int chunks = 0;
    for (const FirmwareSegment &seg : img.segments)
        chunks += int((seg.data.size() + kTransfer - 1) / kTransfer);
    const int work = int(pages.size()) + chunks * (verify ? 2 : 1);
    int done = 0;
    auto step = [&](const QString &what, int units = 1) {
        done += units;
        if (m_progress)
            m_progress(std::min(100, 100 * done / std::max(1, work)), what);
    };
    if (!idle())
        return false;
    for (quint32 page : pages) {  // only what the image covers: stored config survives
        if (!command(char(0x41) + le32(page)))
            return false;
        step("Erasing");
    }
    for (const FirmwareSegment &seg : img.segments)
        for (int off = 0; off < seg.data.size(); off += kTransfer) {
            if (!setAddress(seg.address + quint32(off)) || !download(2, seg.data.mid(off, kTransfer)))
                return false;
            step("Writing");
        }
    if (verify)
        for (const FirmwareSegment &seg : img.segments) {
            QByteArray back;
            const Progress keep = std::exchange(m_progress, {});
            const bool ok = upload(seg.address, int(seg.data.size()), &back);
            m_progress = keep;
            if (!ok)
                return false;
            for (int i = 0; i < seg.data.size(); ++i)
                if (back[i] != seg.data[i])
                    return fail(QString("verify failed at 0x%1").arg(seg.address + i, 8, 16, QChar('0')));
            step("Verifying", int((seg.data.size() + kTransfer - 1) / kTransfer));
        }
    return idle();
}

bool DfuSe::leave(quint32 address)
{
    if (!idle() || !setAddress(address))
        return false;
    m_t.out(DNLOAD, 2, {});  // zero-length download: manifest and start the firmware
    QByteArray ignore;
    m_t.in(GETSTATUS, 0, 6, &ignore);  // the device resets here; errors are expected
    return true;
}
