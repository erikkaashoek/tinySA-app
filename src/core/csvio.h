#pragma once
// Trace CSV files: "Hz;dBm" rows (separator and decimal mark configurable; reading
// detects both), or the firmware's MHz CSV for wireless managers ("433.920000,-85.25",
// no header; reading takes a frequency with a decimal mark as MHz).
// Recording writes one such file per sweep into a session folder.
#include "traces.h"
#include <QString>
#include <optional>

struct CsvFormat {
    QChar separator = ';';
    bool decimalComma = false;
    bool mhz = false;  // MHz CSV for wireless managers; separator and decimalComma do not apply
};

bool writeTraceCsv(const QString &path, const Trace &t, const CsvFormat &fmt, QString *error,
                   const QString &title = {});
std::optional<Trace> readTraceCsv(const QString &path, QString *error);

class SweepRecorder {
public:
    bool start(const QString &baseFolder, const CsvFormat &fmt, QString *error);  // new session folder
    void stop() { m_folder.clear(); }
    bool active() const { return !m_folder.isEmpty(); }
    QString folder() const { return m_folder; }
    int count() const { return m_count; }
    bool add(const Trace &t, QString *error);

private:
    QString m_folder;
    CsvFormat m_fmt;
    int m_count = 0;
};
