#pragma once
// Generator page: a bench-generator front panel for the unit's output mode (it replaces
// the analyzer's controls and graph, as the unit's own screen does). Shows one GenConfig
// and reports each user edit as a new GenConfig; the session clamps it and hands it back
// through showConfig().
#include "genconfig.h"
#include <QWidget>
#include <functional>
#include <memory>

namespace Ui { class GeneratorPanel; }
class QButtonGroup;
class QLineEdit;

class GeneratorPanel : public QWidget {
    Q_OBJECT
public:
    explicit GeneratorPanel(QWidget *parent = nullptr);
    ~GeneratorPanel() override;

    void setDevice(const DeviceInfo *d);  // nullptr: not connected, controls disabled
    void showConfig(const GenConfig &c);
    GenConfig config() const { return m_config; }
    QString statusText() const;  // "RF ON · 433.92 MHz · -40.0 dBm · FM" for the status bar

signals:
    void edited(const GenConfig &c);

private:
    void edit(const std::function<void(GenConfig &)> &change);
    void editSweep(QLineEdit *e);  // start, stop or span typed
    void showSignal(const GenConfig &c, const GenDesc *g);

    std::unique_ptr<Ui::GeneratorPanel> ui;
    QButtonGroup *m_types;  // id = GenType
    GenConfig m_config;
    DeviceInfo m_device;    // the last unit (its lists stay while disconnected)
    bool m_connected = false;
};
