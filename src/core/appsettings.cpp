#include "appsettings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

void AppSettings::load(QSettings &s)
{
    highInput = config.input == InputMode::High;
    visit([&](const char *key, auto &field) {
        using T = std::decay_t<decltype(field)>;
        field = s.value(key, QVariant::fromValue(field)).template value<T>();
    });
    config.input = highInput ? InputMode::High : InputMode::Low;
}

void AppSettings::save(QSettings &s)
{
    highInput = config.input == InputMode::High;
    visit([&](const char *key, auto &field) { s.setValue(key, QVariant::fromValue(field)); });
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
