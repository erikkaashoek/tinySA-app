#include "autotest.h"
#include <QAbstractButton>
#include <QAbstractSlider>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLineEdit>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <QWidget>
#include <cmath>

AutoTest::AutoTest(TestHost *host, const QString &scriptPath, const QString &outDir, QObject *parent)
    : QObject(parent), m_host(host), m_outDir(outDir)
{
    QDir().mkpath(outDir);
    QFile::remove(outDir + "/log.txt");
    QFile f(scriptPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        fail("cannot open script " + scriptPath);
        m_lines << "quit";
        return;
    }
    for (const QString &l : QString(f.readAll()).split('\n')) {
        const QString t = l.trimmed();
        if (!t.isEmpty() && !t.startsWith('#'))
            m_lines << t;
    }
}

void AutoTest::start()
{
    QTimer::singleShot(300, this, &AutoTest::step);  // let the window settle
}

void AutoTest::log(const QString &line)
{
    QFile f(m_outDir + "/log.txt");
    if (f.open(QIODevice::Append | QIODevice::Text))
        QTextStream(&f) << line << "\n";
}

void AutoTest::fail(const QString &why)
{
    ++m_failures;
    log("FAIL: " + why);
}

QWidget *AutoTest::find(const QString &name)
{
    if (auto *w = m_host->testWindow()->findChild<QWidget *>(name))
        return w;
    for (QWidget *top : QApplication::topLevelWidgets())  // dialogs and tool windows
        if (top->objectName() == name)
            return top;
        else if (auto *w = top->findChild<QWidget *>(name))
            return w;
    fail("no widget named " + name);
    return nullptr;
}

void AutoTest::step()
{
    if (m_pc >= m_lines.size())
        m_lines << "quit";
    const QStringList tok = m_lines[m_pc].split(' ', Qt::SkipEmptyParts);
    if (tok[0] == "quit") {
        log(m_failures ? "RESULT: FAIL" : "RESULT: PASS");
        m_host->testWindow()->close();
        QApplication::exit(m_failures);
        return;
    }
    if (tok[0] == "wait") {
        log(m_lines[m_pc]);
        ++m_pc;
        QTimer::singleShot(tok.value(1).toInt(), this, &AutoTest::step);
        return;
    }
    if (m_waitStart < 0)
        log(m_lines[m_pc]);
    if (exec(tok)) {
        m_waitStart = -1;
        ++m_pc;
        QTimer::singleShot(30, this, &AutoTest::step);
    } else {
        QTimer::singleShot(50, this, &AutoTest::step);  // poll the same waitfor line
    }
}

bool AutoTest::compare(const QString &query, const QString &op, const QString &want, double tol,
                       QString *got)
{
    *got = m_host->testQuery(query);
    if (got->isNull()) {
        *got = "<unknown query>";
        return false;
    }
    bool okA = false, okB = false;
    const double a = got->toDouble(&okA), b = want.toDouble(&okB);
    if (op == "==") return okA && okB ? a == b : *got == want;
    if (op == "!=") return okA && okB ? a != b : *got != want;
    if (op == "contains") return got->contains(want);
    if (!okA || !okB) return false;
    if (op == "<") return a < b;
    if (op == "<=") return a <= b;
    if (op == ">") return a > b;
    if (op == ">=") return a >= b;
    if (op == "~") return std::abs(a - b) <= tol;
    return false;
}

bool AutoTest::exec(const QStringList &tok)
{
    const QString cmd = tok[0], arg = tok.value(1);
    if (cmd == "set") {
        QWidget *w = find(arg);
        const QString val = tok.mid(2).join(' ');
        if (auto *e = qobject_cast<QLineEdit *>(w)) {
            e->setFocus();
            e->selectAll();
            QTest::keyClicks(e, val);
            QTest::keyClick(e, Qt::Key_Return);
        } else if (auto *c = qobject_cast<QComboBox *>(w)) {
            const int i = c->findText(val);
            if (i < 0)
                return fail(QString("set %1: no item '%2'").arg(arg, val)), true;
            c->setCurrentIndex(i);
            emit c->activated(i);  // what a user selection emits
        } else if (auto *b = qobject_cast<QAbstractButton *>(w)) {
            if (b->isChecked() != (val == "1" || val == "true"))
                b->click();
        } else if (auto *d = qobject_cast<QDoubleSpinBox *>(w)) {
            d->setValue(val.toDouble());
        } else if (auto *s = qobject_cast<QSpinBox *>(w)) {
            s->setValue(val.toInt());
        } else if (auto *s = qobject_cast<QAbstractSlider *>(w)) {
            s->setValue(val.toInt());
        } else if (w) {
            fail("set: unsupported widget " + arg);
        }
    } else if (cmd == "click") {
        if (auto *b = qobject_cast<QAbstractButton *>(find(arg)))
            b->click();
        else
            fail("click: not a button: " + arg);
    } else if (cmd == "key") {
        if (QWidget *w = find(arg)) {
            const QKeyCombination k = QKeySequence::fromString(tok.value(2))[0];
            QTest::keyClick(w, k.key(), k.keyboardModifiers());
        }
    } else if (cmd == "waitfor") {
        const int timeout = tok.value(4, "5000").toInt();
        if (m_waitStart < 0)
            m_waitStart = QDateTime::currentMSecsSinceEpoch();
        QString got;
        if (compare(arg, tok.value(2), tok.value(3), 0, &got))
            return true;
        if (QDateTime::currentMSecsSinceEpoch() - m_waitStart < timeout)
            return false;
        fail(QString("waitfor %1 %2 %3: timed out, got '%4'").arg(arg, tok.value(2), tok.value(3), got));
    } else if (cmd == "check") {
        const QString op = tok.value(2);
        const bool text = op == "==" || op == "!=" || op == "contains";  // value = rest of line
        const QString want = text ? tok.mid(3).join(' ') : tok.value(3);
        QString got;
        if (!compare(arg, op, want, tok.value(4, "0").toDouble(), &got))
            fail(QString("%1: got '%2'").arg(tok.join(' '), got));
    } else if (cmd == "do") {
        if (!m_host->testAction(arg, tok.mid(2)))
            fail("action failed: " + tok.mid(1).join(' '));
    } else if (cmd == "shot") {
        m_host->testWindow()->grab().save(m_outDir + "/" + arg);
    } else if (cmd == "shotw") {
        if (QWidget *w = find(arg))
            w->grab().save(m_outDir + "/" + tok.value(2));
    } else if (cmd == "dump") {
        QFile f(m_outDir + "/" + tok.value(2));
        const QString text = m_host->testQuery(arg);
        if (text.isNull() || !f.open(QIODevice::WriteOnly | QIODevice::Text))
            fail("dump " + arg + " failed");
        else
            f.write(text.toUtf8());
    } else {
        fail("unknown command: " + cmd);
    }
    return true;
}
