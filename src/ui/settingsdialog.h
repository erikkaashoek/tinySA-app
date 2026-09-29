#pragma once
// Settings dialog (Designer form + thin controller). Device buttons act at once
// through commandRequested(); appearance and file options are stored on OK.
#include "appsettings.h"
#include "model.h"
#include <QDialog>
#include <functional>
#include <memory>

namespace Ui { class SettingsDialog; }
class QPushButton;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

    void load(const AppSettings &s, const DeviceInfo &info, bool connected);
    void store(AppSettings &s) const;  // appearance + files

signals:
    void commandRequested(const QString &command);

private:
    void showTheme();

    std::unique_ptr<Ui::SettingsDialog> ui;
    Theme m_theme;
    QList<std::pair<QPushButton *, std::function<QColor &(Theme &)>>> m_colours;
};
