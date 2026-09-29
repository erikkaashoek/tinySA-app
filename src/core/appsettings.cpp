#include "appsettings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

void AppSettings::load(QSettings &s)
{
    m_highInput = config.input == InputMode::High;
    m_csvSeparator = csv.separator;
    visit([&](const char *key, auto &field) {
        using T = std::decay_t<decltype(field)>;
        if constexpr (std::is_same_v<T, QColor>)  // readable "#aarrggbb" in the ini
            field = QColor::fromString(s.value(key, field.name(QColor::HexArgb)).toString());
        else
            field = s.value(key, QVariant::fromValue(field)).template value<T>();
    });
    config.input = m_highInput ? InputMode::High : InputMode::Low;
    csv.separator = m_csvSeparator == "\\t" ? QChar('\t')
                    : m_csvSeparator.isEmpty() ? QChar(';') : m_csvSeparator.at(0);
    if (recordFolder.isEmpty())
        recordFolder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/tinySA-App";
}

void AppSettings::save(QSettings &s)
{
    m_highInput = config.input == InputMode::High;
    m_csvSeparator = csv.separator == '\t' ? QString("\\t") : QString(csv.separator);
    visit([&](const char *key, auto &field) {
        if constexpr (std::is_same_v<std::decay_t<decltype(field)>, QColor>)
            s.setValue(key, field.name(QColor::HexArgb));
        else
            s.setValue(key, QVariant::fromValue(field));
    });
}

QString AppSettings::defaultPath()
{
    const QString name = "/tinySA-App.settings.ini";
    QFile probe(QCoreApplication::applicationDirPath() + name);  // ACLs: only trying tells
    if (probe.open(QIODevice::Append))
        return probe.fileName();
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    return appData + name;
}
