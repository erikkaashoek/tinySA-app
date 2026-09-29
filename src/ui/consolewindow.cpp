#include "consolewindow.h"
#include "ui_consolewindow.h"
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextStream>

static QList<QPushButton *> macroButtons(Ui::ConsoleWindow *ui)
{
    return {ui->macro1Button, ui->macro2Button, ui->macro3Button, ui->macro4Button};
}

ConsoleWindow::ConsoleWindow(QWidget *parent) : QWidget(parent, Qt::Window), ui(new Ui::ConsoleWindow)
{
    ui->setupUi(this);
    ui->logView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    ui->commandEdit->installEventFilter(this);
    connect(ui->commandEdit, &QLineEdit::returnPressed, this, &ConsoleWindow::send);
    connect(ui->sendButton, &QPushButton::clicked, this, &ConsoleWindow::send);
    connect(ui->clearLogButton, &QPushButton::clicked, ui->logView, &QPlainTextEdit::clear);
    connect(ui->saveLogButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, "Save log", "tinySA console.txt", "Text (*.txt)");
        QFile f(path);
        if (!path.isEmpty() && f.open(QIODevice::WriteOnly | QIODevice::Text))
            QTextStream(&f) << ui->logView->toPlainText();
    });
    const auto buttons = macroButtons(ui.get());
    for (int i = 0; i < buttons.size(); ++i) {
        connect(buttons[i], &QPushButton::clicked, this, [this, i] { runMacro(i); });
        connect(buttons[i], &QWidget::customContextMenuRequested, this, [this, i] { editMacro(i); });
    }
    setMacros({}, {});
}

ConsoleWindow::~ConsoleWindow() = default;

void ConsoleWindow::appendTraffic(bool sent, const QString &text, bool user)
{
    if (!user && !ui->pollCheck->isChecked())
        return;
    QScrollBar *bar = ui->logView->verticalScrollBar();
    const int keep = bar->value();
    ui->logView->appendPlainText(QString("%1 %2 %3").arg(QTime::currentTime().toString("hh:mm:ss.zzz"),
                                                         sent ? ">" : " ", text));
    if (ui->holdCheck->isChecked())
        bar->setValue(keep);
}

QString ConsoleWindow::logText() const
{
    return ui->logView->toPlainText();
}

void ConsoleWindow::send()
{
    const QString cmd = ui->commandEdit->text().trimmed();
    if (cmd.isEmpty())
        return;
    m_history.removeAll(cmd);
    m_history.append(cmd);
    while (m_history.size() > 50)
        m_history.removeFirst();
    m_historyPos = int(m_history.size());
    ui->commandEdit->clear();
    emit sendRequested(cmd);
}

bool ConsoleWindow::eventFilter(QObject *o, QEvent *e)
{
    if (o == ui->commandEdit && e->type() == QEvent::KeyPress && !m_history.isEmpty()) {
        const int key = static_cast<QKeyEvent *>(e)->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down) {
            m_historyPos = std::clamp(m_historyPos + (key == Qt::Key_Up ? -1 : 1), 0, int(m_history.size()));
            ui->commandEdit->setText(m_history.value(m_historyPos));
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

void ConsoleWindow::setMacros(const QStringList &names, const QStringList &commands)
{
    const auto buttons = macroButtons(ui.get());
    m_names = names;
    m_commands = commands;
    for (int i = 0; i < buttons.size(); ++i) {
        if (m_names.size() <= i)
            m_names << QString("Macro %1").arg(i + 1);
        if (m_commands.size() <= i)
            m_commands << QString();
        buttons[i]->setText(m_names[i]);
    }
}

// One command per line; lines starting with # or ; are comments.
void ConsoleWindow::runMacro(int i)
{
    for (const QString &line : m_commands.value(i).split('\n')) {
        const QString cmd = line.trimmed();
        if (!cmd.isEmpty() && !cmd.startsWith('#') && !cmd.startsWith(';'))
            emit sendRequested(cmd);
    }
}

void ConsoleWindow::editMacro(int i)
{
    QDialog d(this);
    d.setWindowTitle(QString("Macro %1").arg(i + 1));
    auto *form = new QFormLayout(&d);
    auto *name = new QLineEdit(m_names.value(i), &d);
    auto *cmds = new QPlainTextEdit(m_commands.value(i), &d);
    cmds->setPlaceholderText("One command per line, e.g.\ncaloutput 10\nrbw 30");
    form->addRow("Name", name);
    form->addRow("Commands", cmds);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return;
    m_names[i] = name->text().trimmed().isEmpty() ? QString("Macro %1").arg(i + 1) : name->text().trimmed();
    m_commands[i] = cmds->toPlainText();
    setMacros(m_names, m_commands);
}
