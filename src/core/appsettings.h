#pragma once
// Persistent application settings. visit() lists every setting once and drives
// both load() and save(), so adding a setting is one line.
#include "csvio.h"
#include "genconfig.h"
#include "sweepconfig.h"
#include "tracking.h"
#include <QColor>
#include <QString>
#include <QStringList>

class QSettings;

// Graph view state (SpectrumView applies and reports it).
struct ViewSettings {
    bool linear = false;       // linear power (W) instead of dBm
    double logMin = -110, logMax = -10, linMin = 0, linMax = 1e-6;
    bool autoMin = false, autoMax = false;
    bool spline = false, points = false;
    bool show[4] = {true, true, true, true};  // live, M1, M2, M3
    bool peakMax = true, peakMin = false;     // peak markers on the live trace
    bool markerText = false;                  // marker readout block on the graph
    bool showMarkers = true;                  // user markers on the graph
};

struct Theme {
    QColor background{0x10, 0x10, 0x14}, grid{0x50, 0x50, 0x58}, text{0xc8, 0xc8, 0xc8};
    QColor trace[4] = {{0xff, 0xe0, 0x40}, {0x40, 0xd0, 0xff}, {0xff, 0x60, 0xd0}, {0x60, 0xff, 0x80}};
    QColor marker{0xff, 0xff, 0xff}, selected{0xff, 0x80, 0x20};
    int lineWidth = 1;
    int fontPt = 9;  // graph text size

    static Theme dark() { return {}; }
    static Theme light()
    {
        Theme t;
        t.background = QColor(0xff, 0xff, 0xff);
        t.grid = QColor(0xc8, 0xc8, 0xd0);
        t.text = QColor(0x20, 0x20, 0x20);
        t.trace[0] = QColor(0xc0, 0x70, 0x00);
        t.trace[1] = QColor(0x00, 0x70, 0xc0);
        t.trace[2] = QColor(0xc0, 0x00, 0xa0);
        t.trace[3] = QColor(0x00, 0x90, 0x40);
        t.marker = QColor(0x00, 0x00, 0x00);
        t.selected = QColor(0xff, 0x50, 0x00);
        return t;
    }
};

struct AppSettings {
    SweepConfig config;
    GenConfig gen;                // RF on/off is never stored: the generator starts with RF off
    QString port;                 // last connected port
    int hold = 0;                 // Hold enum
    int average = 1;              // sweeps
    ViewSettings view;
    Theme theme;
    QString markers;              // MarkerSet::serialize()
    QString sweepName;            // header of saved images and CSV files
    QString recordFolder;         // default: Documents/tinySA-App
    CsvFormat csv;
    QStringList macroNames, macroCommands, history;  // console
    int colorScheme = 0;          // application: 0 system, 1 light, 2 dark
    bool screenInvert = false;    // screen window: inverted colours (printer friendly)
    bool screenOffScanning = true;  // the tinySA's screen off while scanning (firmware with `ui off`)
    TrackConfig track;            // tracking generator
    QString trackerPort;          // its last connected port

    template <class V> void visit(V &&v)
    {
        v("sweep/start_hz", config.grid.startHz);
        v("sweep/stop_hz", config.grid.stopHz);
        v("sweep/points", config.grid.points);
        v("sweep/log", config.grid.log);
        v("device/rbw_khz", config.rbwKHz);
        v("device/atten_db", config.atten);
        v("device/lna", config.lna);
        v("device/ext_gain_db", config.extGainDb);
        v("device/high_input", m_highInput);
        v("device/port", port);
        v("device/screen_off_scanning", screenOffScanning);
        v("tracker/port", trackerPort);
        v("tracker/on", track.on);
        v("tracker/level_dbm", track.levelDbm);
        v("tracker/settle_ms", track.settleMs);
        v("tracker/mixer", track.mixer);
        v("tracker/harmonic", track.harmonic);
        v("generator/high_output", m_genHigh);
        v("generator/mixer", gen.mixer);
        v("generator/harmonic", gen.harmonic);
        v("generator/freq_hz", gen.freqHz);
        v("generator/level_dbm", gen.levelDbm);
        v("generator/type", m_genType);
        v("generator/span_hz", gen.spanHz);
        v("generator/sweep_s", gen.sweepS);
        v("generator/level_change_db", gen.levelChangeDb);
        v("generator/mod_hz", gen.modHz);
        v("generator/am_depth", gen.amDepth);
        v("generator/fm_dev_hz", gen.fmDevHz);
        v("generator/burst_period_s", gen.burstPeriodS);
        v("generator/burst_on_s", gen.burstOnS);
        v("generator/burst_count", gen.burstCount);
        v("trace/hold", hold);
        v("trace/average", average);
        v("view/linear", view.linear);
        v("view/log_min", view.logMin);
        v("view/log_max", view.logMax);
        v("view/lin_min", view.linMin);
        v("view/lin_max", view.linMax);
        v("view/auto_min", view.autoMin);
        v("view/auto_max", view.autoMax);
        v("view/spline", view.spline);
        v("view/points", view.points);
        v("view/show_live", view.show[0]);
        v("view/show_m1", view.show[1]);
        v("view/show_m2", view.show[2]);
        v("view/show_m3", view.show[3]);
        v("view/peak_max", view.peakMax);
        v("view/peak_min", view.peakMin);
        v("view/marker_text", view.markerText);
        v("view/show_markers", view.showMarkers);
        v("markers/list", markers);
        v("theme/background", theme.background);
        v("theme/grid", theme.grid);
        v("theme/text", theme.text);
        v("theme/live", theme.trace[0]);
        v("theme/m1", theme.trace[1]);
        v("theme/m2", theme.trace[2]);
        v("theme/m3", theme.trace[3]);
        v("theme/marker", theme.marker);
        v("theme/selected", theme.selected);
        v("theme/line_width", theme.lineWidth);
        v("theme/font_pt", theme.fontPt);
        v("view/color_scheme", colorScheme);
        v("screen/invert", screenInvert);
        v("file/sweep_name", sweepName);
        v("file/record_folder", recordFolder);
        v("file/csv_separator", m_csvSeparator);
        v("file/csv_decimal_comma", csv.decimalComma);
        v("file/csv_mhz", csv.mhz);
        v("console/macro_names", macroNames);
        v("console/macro_commands", macroCommands);
        v("console/history", history);
    }

    void load(QSettings &s);
    void save(QSettings &s);

    // Portable by default: <exe dir>/tinySA-App.settings.ini if writable, else the
    // user's AppData folder. (The legacy app's tinySA-App.ini has another format.)
    static QString defaultPath();

private:
    void storeGenerator();     // gen -> the stored forms below
    bool m_highInput = false;  // stored forms of enum / char fields
    QString m_csvSeparator = ";";
    bool m_genHigh = false;
    QString m_genType = "CW";
};
