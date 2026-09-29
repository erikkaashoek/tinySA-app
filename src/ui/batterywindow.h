#pragma once
// Battery voltage against time. Collects the unit's vbat reports from app start,
// shown or not.
#include "appsettings.h"
#include <QWidget>
#include <memory>

namespace Ui { class BatteryWindow; }

class BatteryWindow : public QWidget {
    Q_OBJECT
public:
    explicit BatteryWindow(QWidget *parent = nullptr);
    ~BatteryWindow() override;

    void setTheme(const Theme &t);
    void addSample(int mV);
    int samples() const;

private:
    void clear();

    std::unique_ptr<Ui::BatteryWindow> ui;
};
