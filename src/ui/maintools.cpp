// MainWindow: tool windows (console, battery).
#include "batterywindow.h"
#include "consolewindow.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QSettings>

void MainWindow::setupTools()
{
    m_console = new ConsoleWindow(this);
    m_console->setObjectName("consoleWindow");
    m_battery = new BatteryWindow(this);
    m_battery->setObjectName("batteryWindow");
    connect(ui->actionConsole, &QAction::triggered, this, [this] { showTool(m_console); });
    connect(ui->actionBattery, &QAction::triggered, this, [this] { showTool(m_battery); });
    connect(m_console, &ConsoleWindow::sendRequested, &m_session, &Session::send);
    connect(&m_session, &Session::traffic, m_console, &ConsoleWindow::appendTraffic);
    connect(&m_session, &Session::battery, m_battery, &BatteryWindow::addSample);

    QSettings qs(m_settingsPath, QSettings::IniFormat);
    m_console->restoreGeometry(qs.value("tools/console_geometry").toByteArray());
    m_battery->restoreGeometry(qs.value("tools/battery_geometry").toByteArray());
}

void MainWindow::showTool(QWidget *w)
{
    w->show();
    w->raise();
    w->activateWindow();
}

void MainWindow::saveTools(QSettings &qs)
{
    qs.setValue("tools/console_geometry", m_console->saveGeometry());
    qs.setValue("tools/battery_geometry", m_battery->saveGeometry());
}
