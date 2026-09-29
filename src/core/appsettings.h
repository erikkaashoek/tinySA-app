#pragma once
// Persistent application settings. visit() lists every setting once and drives
// both load() and save(), so adding a setting is one line.
#include "sweepconfig.h"
#include <QString>

class QSettings;

struct AppSettings {
    SweepConfig config;
    QString port;       // last connected port

    template <class V> void visit(V &&v)
    {
        v("sweep/start_hz", config.grid.startHz);
        v("sweep/stop_hz", config.grid.stopHz);
        v("sweep/points", config.grid.points);
        v("device/rbw_khz", config.rbwKHz);
        v("device/atten_db", config.atten);
        v("device/lna", config.lna);
        v("device/high_input", highInput);
        v("device/port", port);
    }

    void load(QSettings &s);
    void save(QSettings &s);

    // Portable by default: <exe dir>/tinySA-App.settings.ini if writable, else the
    // user's AppData folder. (The legacy app's tinySA-App.ini has another format.)
    static QString defaultPath();

private:
    bool highInput = false;  // InputMode as stored value
};
