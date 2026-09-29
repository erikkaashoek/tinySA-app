#pragma once
// Script-driven GUI test runner (tinySA-App --script file.txt --out dir).
// One command per line, '#' comments. Widgets are addressed by objectName;
// app state through the host's named queries/actions.
//
//   set <widget> <value>          line edit (typed + Enter), combo (text), check box, spin box
//   click <widget>                button
//   key <widget> <key>            QKeySequence name: Return, Escape, PgUp, Left, Del ...
//   wait <ms>
//   waitfor <query> <op> <value> [timeoutMs=5000]
//   check <query> <op> <value> [tolerance]   ops: == != contains < <= > >= ~ (~ uses
//                                 tolerance); for == != contains the value is the rest of the line
//   do <action> [args...]         host action, e.g. "do connect sim:ultra"
//   shot <file.png>               grab the main window into the out dir
//   shotw <widget> <file.png>     grab one widget
//   dump <query> <file>           write a query result (e.g. trace CSV) into the out dir
//   quit                          exit; process exit code = number of failures
#include <QObject>
#include <QStringList>

class QWidget;

class TestHost {
public:
    virtual ~TestHost() = default;
    virtual QWidget *testWindow() = 0;
    virtual QString testQuery(const QString &name) = 0;  // null QString = unknown query
    virtual bool testAction(const QString &name, const QStringList &args) = 0;
};

class AutoTest : public QObject {
    Q_OBJECT
public:
    AutoTest(TestHost *host, const QString &scriptPath, const QString &outDir, QObject *parent = nullptr);
    void start();

private:
    void step();
    bool exec(const QStringList &tok);  // false = keep polling this line (waitfor)
    bool compare(const QString &query, const QString &op, const QString &want, double tol,
                 QString *got);
    void fail(const QString &why);
    void log(const QString &line);
    QWidget *find(const QString &name);

    TestHost *m_host;
    QStringList m_lines;
    int m_pc = 0, m_failures = 0;
    QString m_outDir;
    qint64 m_waitStart = -1;
};
