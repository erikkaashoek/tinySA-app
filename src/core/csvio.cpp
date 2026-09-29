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
        hz << num(m.captured(1));
        db << (m.captured(2).compare("nan", Qt::CaseInsensitive) ? num(m.captured(2)) : qQNaN());
    }
    if (hz.size() < 2) {
        *error = "no frequency/level rows found";
        return std::nullopt;
    }
    Trace t{{hz.first(), hz.last(), int(hz.size())}, db};
    const double tol = std::max(1.0, t.grid.step() * 1e-3);
    for (int i = 0; i < hz.size(); ++i)
        if (std::abs(hz[i] - t.grid.freq(i)) > tol) {
            *error = QString("frequencies are not evenly spaced (row %1)").arg(i + 1);
            return std::nullopt;
        }
    return t;
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
