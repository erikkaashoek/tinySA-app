#include "csvio.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <cmath>

bool writeTraceCsv(const QString &path, const Trace &t, const CsvFormat &fmt, QString *error,
                   const QString &title)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *error = f.errorString();
        return false;
    }
    QTextStream ts(&f);
    if (fmt.mhz) {  // as the firmware's MHz CSV, which Wireless Workbench and others import
        for (int i = 0; i < t.dBm.size(); ++i)
            ts << QString::number(t.grid.freq(i) / 1e6, 'f', 6) << ',' << QString::number(t.dBm[i], 'f', 2) << "\n";
        return true;
    }
    if (!title.isEmpty())
        ts << "# " << title << "\n";
    ts << "Frequency (Hz)" << fmt.separator << "Level (dBm)\n";
    for (int i = 0; i < t.dBm.size(); ++i) {
        QString level = QString::number(t.dBm[i], 'f', 2);
        if (fmt.decimalComma)
            level.replace('.', ',');
        ts << QString::number(t.grid.freq(i), 'f', 0) << fmt.separator << level << "\n";
    }
    return true;
}

std::optional<Trace> readTraceCsv(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = f.errorString();
        return std::nullopt;
    }
    // two numbers per row; separators ; , tab or spaces; decimal ',' allowed with ; or tab
    static const QRegularExpression row(
        R"(^\s*([-+]?\d+(?:[.,]\d*)?(?:[eE][-+]?\d+)?)\s*[;,\t ]\s*([-+]?(?:\d+(?:[.,]\d*)?(?:[eE][-+]?\d+)?|nan))\s*$)",
        QRegularExpression::CaseInsensitiveOption);
    QVector<double> hz, db;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        const auto m = row.match(line);
        if (!m.hasMatch())
            continue;  // header, comment
        auto num = [](QString s) { return s.replace(',', '.').toDouble(); };
        const QString freq = m.captured(1);  // Hz are written without decimals, MHz with
        const bool mhz = (freq.contains('.') || freq.contains(',')) && !freq.contains('e', Qt::CaseInsensitive);
        hz << (mhz ? std::round(num(freq) * 1e6) : num(freq));
        db << (m.captured(2).compare("nan", Qt::CaseInsensitive) ? num(m.captured(2)) : qQNaN());
    }
    if (hz.size() < 2) {
        *error = "no frequency/level rows found";
        return std::nullopt;
    }
    // the app's grids: linear, a log sweep in linear pieces, a tracking sweep's exact log
    const int n = int(hz.size());
    auto firstOff = [&](const ScanGrid &g) {  // the first row off the grid, -1 = none
        const double tol = std::max(1.0, g.step() * 1e-3);
        for (int i = 0; i < n; ++i)
            if (std::abs(hz[i] - g.freq(i)) > tol)
                return i;
        return -1;
    };
    const ScanGrid linear{hz.first(), hz.last(), n};
    const int off = firstOff(linear);
    if (off < 0)
        return Trace{linear, db};
    for (const int pieces : {logPieces(hz.first(), hz.last(), n), 0}) {
        const ScanGrid g{hz.first(), hz.last(), n, true, pieces};
        if (hz.first() > 0 && firstOff(g) < 0)
            return Trace{g, db};
    }
    *error = QString("frequencies are neither evenly nor logarithmically spaced (row %1)").arg(off + 1);
    return std::nullopt;
}

bool SweepRecorder::start(const QString &baseFolder, const CsvFormat &fmt, QString *error)
{
    const QString folder = baseFolder + "/" + QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm-ss");
    if (!QDir().mkpath(folder)) {
        *error = "cannot create " + folder;
        return false;
    }
    m_folder = folder;
    m_fmt = fmt;
    m_count = 0;
    return true;
}

bool SweepRecorder::add(const Trace &t, QString *error)
{
    ++m_count;
    const QString path = QString("%1/%2.csv").arg(m_folder).arg(m_count, 5, 10, QChar('0'));
    return writeTraceCsv(path, t, m_fmt, error, QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
}
