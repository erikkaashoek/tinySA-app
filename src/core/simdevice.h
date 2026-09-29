#pragma once
// Simulated tinySA as a QIODevice that speaks the firmware shell byte-for-byte:
// char echo, "ch> " prompt, scanraw streamed in 20-point chunks, capture pixels,
// input queued (not echoed) while a scan runs. Deterministic spectrum with known
// carriers, so tests and screenshots are reproducible without hardware.
#include <QByteArrayList>
#include <QElapsedTimer>
#include <QIODevice>
#include <QTimer>
#include <optional>

class SimTransport : public QIODevice {
    Q_OBJECT
public:
    enum class Kind { Basic, Ultra };
    struct Carrier { double hz, dBm; };

    explicit SimTransport(Kind kind, QObject *parent = nullptr);
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return m_out.size() + QIODevice::bytesAvailable(); }

    static QVector<Carrier> carriers();
    double levelAt(double hz, double rbwHz) const;  // noiseless model value

    // test hooks
    void touchScreen();                       // abort a running scanraw like a touch
    QByteArrayList received() const { return m_received; }
    double pointsPerSecond = 20000;

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;

private:
    void out(const QByteArray &bytes);
    void processInput();
    void execute(const QByteArray &line);
    void scanTick();
    QByteArray capture() const;

    Kind m_kind;
    QByteArray m_in, m_out, m_line;
    QByteArrayList m_received;
    bool m_readyPending = false;

    qint64 m_sweepStart = 0, m_sweepStop = 350000000;
    int m_sweepPoints = 450;
    double m_rbwKHz = 0;
    int m_atten = -1;
    bool m_lna = false, m_paused = false;

    struct Scan { qint64 start, stop; int points, next; };
    std::optional<Scan> m_scan;
    QTimer m_timer;
    QElapsedTimer m_clock;
    unsigned m_sweeps = 0;
};
