#include "protocol.h"
#include <QImage>
#include <cstring>

void RxParser::expect(const QByteArray &commandLine, Body body, int bytes)
{
    m_echo = commandLine;
    m_body = body;
    m_captureBytes = bytes;
}

void RxParser::reset()
{
    *this = RxParser();
}

QVector<RxEvent> RxParser::feed(const QByteArray &bytes)
{
    QVector<RxEvent> out;
    m_buf += bytes;
    while (m_pos < m_buf.size() && step(out))
        ;
    flushScan(out);
    m_buf.remove(0, m_pos);
    m_pos = 0;
    if (m_state == State::Text && m_buf.size() > 65536) {  // binary junk without newlines
        out.append({RxEvent::Line, m_buf.left(80) + "..."});
        m_buf.clear();
    }
    return out;
}

bool RxParser::atPrompt() const
{
    return m_buf.size() - m_pos >= 4 && std::memcmp(m_buf.constData() + m_pos, "ch> ", 4) == 0;
}

bool RxParser::atPromptPrefix() const
{
    const int n = int(m_buf.size() - m_pos);
    return n < 4 && std::memcmp(m_buf.constData() + m_pos, "ch> ", n) == 0;
}

bool RxParser::step(QVector<RxEvent> &out)
{
    const int avail = int(m_buf.size() - m_pos);
    const char *p = m_buf.constData() + m_pos;
    switch (m_state) {
    case State::Text:
        return textStep(out);
    case State::ScanStart:
        if (*p == '\r' || *p == '\n') {
            ++m_pos;
            return true;
        }
        if (*p == '{') {
            ++m_pos;
            m_scanCount = 0;
            m_state = State::ScanPoints;
            return true;
        }
        m_state = State::Text;  // text reply instead of data, e.g. "frequency range is invalid"
        return true;
    case State::ScanPoints:
        return scanStep(out);
    case State::Capture:
        if (avail < m_captureBytes)
            return false;
        out.append({RxEvent::Pixels, m_buf.mid(m_pos, m_captureBytes)});
        m_pos += m_captureBytes;
        m_state = State::Text;
        return true;
    case State::RegionHeader: {
        if (avail < 8)
            return false;
        auto i16 = [&](int k) { return int(qint16(quint8(p[2 * k]) | quint8(p[2 * k + 1]) << 8)); };
        m_region = QRect(i16(0), i16(1), i16(2), i16(3));
        m_pos += 8;
        const bool sane = m_region.x() >= 0 && m_region.y() >= 0 && m_region.width() > 0
                          && m_region.height() > 0 && m_region.right() < 1024
                          && m_region.bottom() < 1024;
        m_state = sane ? State::RegionBody : State::Text;
        return true;
    }
    case State::RegionBody: {
        const int need = m_regionFill ? 2 : m_region.width() * m_region.height() * 2;
        if (avail < need)
            return false;
        RxEvent e{RxEvent::Region, m_buf.mid(m_pos, need)};
        e.rect = m_region;
        e.fill = m_regionFill;
        out.append(e);
        m_pos += need;
        m_swallowPrompt = true;  // the push ends with its own "ch> "
        m_state = State::Text;
        return true;
    }
    }
    return false;
}

bool RxParser::textStep(QVector<RxEvent> &out)
{
    if (atPrompt()) {
        m_pos += 4;
        if (m_swallowPrompt)
            m_swallowPrompt = false;
        else
            out.append({RxEvent::Prompt});
        return true;
    }
    if (atPromptPrefix())
        return false;
    const int nl = int(m_buf.indexOf('\n', m_pos));
    if (nl < 0)
        return false;
    QByteArray line = m_buf.mid(m_pos, nl - m_pos);
    m_pos = nl + 1;
    if (line.endsWith('\r'))
        line.chop(1);

    if (!m_echo.isEmpty() && line.endsWith(m_echo)) {
        m_echo.clear();
        out.append({RxEvent::Echo, line});
        if (m_body == Body::ScanRaw)
            m_state = State::ScanStart;
        else if (m_body == Body::Capture)
            m_state = State::Capture;
        return true;
    }
    if (line == "bulk" || line == "fill") {
        m_regionFill = line == "fill";
        m_state = State::RegionHeader;
        return true;
    }
    out.append({RxEvent::Line, line});
    return true;
}

bool RxParser::scanStep(QVector<RxEvent> &out)
{
    const int avail = int(m_buf.size() - m_pos);
    const char *p = m_buf.constData() + m_pos;
    if (*p == 'x') {
        if (avail < 3)
            return false;
        m_scan.append(qint16(quint8(p[1]) | quint8(p[2]) << 8));
        ++m_scanCount;
        m_pos += 3;
        return true;
    }
    if (*p == '}') {
        ++m_pos;
        flushScan(out);
        RxEvent e{RxEvent::ScanEnd};
        e.count = m_scanCount;
        out.append(e);
        m_state = State::Text;
        return true;
    }
    // Out-of-band text between 20-point chunks, e.g. "Command timeout" + prompt from
    // the shell thread while a long scan runs. Logged, prompt swallowed.
    if (atPrompt()) {
        m_pos += 4;
        return true;
    }
    if (atPromptPrefix())
        return false;
    const int nl = int(m_buf.indexOf('\n', m_pos));
    if (nl < 0)
        return false;
    QByteArray line = m_buf.mid(m_pos, nl - m_pos);
    m_pos = nl + 1;
    flushScan(out);  // keep event order: points before the text
    out.append({RxEvent::Line, line.trimmed()});
    return true;
}

void RxParser::flushScan(QVector<RxEvent> &out)
{
    if (m_scan.isEmpty())
        return;
    RxEvent e{RxEvent::ScanData};
    e.values.swap(m_scan);
    out.append(e);
}

static inline quint16 be16(const char *p)
{
    return quint16(quint8(p[0]) << 8 | quint8(p[1]));
}

void blitRgb565BE(QImage &img, const QRect &r, const QByteArray &pixels)
{
    const QRect clip = r & img.rect();
    const char *src = pixels.constData();
    for (int y = clip.top(); y <= clip.bottom(); ++y) {
        auto *dst = reinterpret_cast<quint16 *>(img.scanLine(y));
        const char *row = src + 2 * ((y - r.top()) * r.width());
        for (int x = clip.left(); x <= clip.right(); ++x)
            dst[x] = be16(row + 2 * (x - r.left()));
    }
}

void fillRgb565BE(QImage &img, const QRect &r, const QByteArray &colour)
{
    if (colour.size() < 2)
        return;
    const QRect clip = r & img.rect();
    const quint16 c = be16(colour.constData());
    for (int y = clip.top(); y <= clip.bottom(); ++y) {
        auto *dst = reinterpret_cast<quint16 *>(img.scanLine(y));
        std::fill(dst + clip.left(), dst + clip.right() + 1, c);
    }
}
