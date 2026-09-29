#pragma once
// Console: timestamped command/reply log, command line with history, four
// user-defined macro buttons (right click to edit). Sends through sendRequested().
#include <QWidget>
#include <memory>

namespace Ui { class ConsoleWindow; }

class ConsoleWindow : public QWidget {
    Q_OBJECT
public:
    explicit ConsoleWindow(QWidget *parent = nullptr);
    ~ConsoleWindow() override;

    void appendTraffic(bool sent, const QString &text, bool user);
    void setMacros(const QStringList &names, const QStringList &commands);
    QStringList macroNames() const { return m_names; }
    QStringList macroCommands() const { return m_commands; }
    void setHistory(const QStringList &h) { m_history = h; }
    QStringList history() const { return m_history; }
    QString logText() const;

signals:
    void sendRequested(const QString &command);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;  // Up/Down history in the command line

private:
    void send();
    void runMacro(int i);
    void editMacro(int i);

    std::unique_ptr<Ui::ConsoleWindow> ui;
    QStringList m_names, m_commands, m_history;
    int m_historyPos = 0;
};
