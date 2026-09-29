#pragma once
// tinySA shell byte stream -> events. Pure state machine, no I/O, so it can be fed
// in arbitrary chunks (tst_core checks chunking invariance).
//
// Shell facts (firmware main.c / sa_cmd.c):
//   command "cmd\r" is echoed as "cmd\r\n", followed by output lines and the
//   prompt "ch> " (no newline). Binary replies follow the echo directly:
//   scanraw: '{' + N x ('x', lo, hi) + '}' then prompt; aborted by touch/jog -> early '}',
//            NO prompt. capture: W*H*2 bytes RGB565 then prompt.
//   "capture rle" (DiSlord NanoVNA-D RLE8 format; older firmware ignores the argument
//   and sends raw pixels, told apart by the header): "BM" u16 width u16 height u8 bpp=8
//   u8 compression=1, u16 palette bytes + palette (RGB565 as read from the LCD), then per
//   row u16 length + PackBits-packed palette indices. All integers little-endian.
//   With `refresh on` the unit pushes "bulk\r\n"|"fill\r\n" + x,y,w,h (int16 LE)
//   + payload + "ch> " at any time between replies.
#include <QByteArray>
#include <QRect>
#include <QVector>

struct RxEvent {
    enum Type {
        Line,      // text line without CR/LF
        Echo,      // the echo of the expected command was seen
        Prompt,    // "ch> " at line start
        ScanData,  // batch of raw scanraw values
        ScanEnd,   // '}' after `count` points
        Pixels,    // complete capture payload (raw RGB565, or RLE8 when rle8 is set)
        Region,    // refresh push: rect + payload (fill: 2 bytes)
    } type;
    QByteArray data;         // Line text, Pixels, Region payload
    QVector<qint16> values;  // ScanData
    int count = 0;           // ScanEnd
    QRect rect;              // Region
    bool fill = false;       // Region
    bool rle8 = false;       // Pixels: DiSlord RLE8 payload for decodeRle8()
};

class RxParser {
public:
    enum class Body { Text, ScanRaw, Capture };

    // Called when a command is written: after its echo the parser switches to the
    // given body type. bytes = capture size.
    void expect(const QByteArray &commandLine, Body body, int bytes = 0);
    void reset();  // forget everything (resync)
    QVector<RxEvent> feed(const QByteArray &bytes);
    bool idle() const { return m_state == State::Text && m_buf.isEmpty(); }

private:
    enum class State { Text, ScanStart, ScanPoints, CaptureHead, Capture, Rle8Palette, Rle8Rows,
                       RegionHeader, RegionBody };
    bool step(QVector<RxEvent> &out);  // consumes one unit from m_buf; false = need more bytes
    bool textStep(QVector<RxEvent> &out);
    bool scanStep(QVector<RxEvent> &out);
    void flushScan(QVector<RxEvent> &out);
    bool atPrompt() const;        // "ch> " at the read position
    bool atPromptPrefix() const;  // an incomplete "ch> " at the end of the buffer

    State m_state = State::Text;
    QByteArray m_buf;
    qsizetype m_pos = 0;        // read position in m_buf (compacted after each feed)
    QByteArray m_echo;          // expected echo line ("" = none pending)
    QByteArray m_fragment;      // start of a line interrupted by a screen push
    Body m_body = Body::Text;
    int m_captureBytes = 0;
    QByteArray m_rle;           // RLE8 capture being collected
    int m_rleRows = 0;
    QVector<qint16> m_scan;     // points parsed but not yet emitted
    int m_scanCount = 0;
    bool m_swallowPrompt = false;  // prompt that terminates a region push
    QRect m_region;
    bool m_regionFill = false;
};

// Pixel decoders (RGB565). capture sends big-endian pixels; region pushes too.
class QImage;
void blitRgb565BE(QImage &img, const QRect &r, const QByteArray &pixels);
void fillRgb565BE(QImage &img, const QRect &r, const QByteArray &colour);
bool decodeRle8(const QByteArray &payload, QImage &img);   // RLE8 capture -> RGB16 image
QByteArray unpackBits(const char *src, int srcLen, int outLen);  // PackBits, as DiSlord's packbits()
