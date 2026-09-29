// MainWindow: TestHost queries and actions for the script runner (autotest.h).
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QApplication>
#include <QMenu>
#include <QRegularExpression>
#include <cmath>

std::optional<QString> MainWindow::testQuery(const QString &name)
{
    const Trace &live = ui->spectrum->trace(0);  // as shown (normalised when normalising)
    const SweepConfig c = m_session.config();
    const ViewSettings &v = ui->spectrum->view();
    auto peak = [&] {
        int best = -1;
        for (int i = 0; i < live.dBm.size(); ++i)
            if (!std::isnan(live.dBm[i]) && (best < 0 || live.dBm[i] > live.dBm[best]))
                best = i;
        return best;
    };
    auto num = [](double x, int dec = 0) { return QString::number(x, 'f', dec); };
    if (name == "connected") return m_session.isConnected() ? "1" : "0";
    if (name == "model") return m_session.device().model ? m_session.device().model->name : "";
    if (name == "sweeps") return num(m_sweeps);
    if (name == "sweeps.new") return num(m_sweeps - m_sweepsMark);  // since "do mark"
    if (name == "scan") return num(m_session.scanMode());
    if (name == "idle") return m_session.scanMode() == DeviceLink::Off && !m_session.isBusy() ? "1" : "0";
    if (name == "start") return num(c.grid.startHz);
    if (name == "stop") return num(c.grid.stopHz);
    if (name == "rbw") return QString::number(c.rbwKHz);
    if (name == "atten") return num(c.atten);
    if (name == "lna") return c.lna ? "1" : "0";
    if (name == "input") return c.input == InputMode::High ? "high" : "low";
    if (name == "points") return num(live.grid.points);
    if (name == "sweepms") return num(m_sweepMs, 1);
    if (name == "cyclems") return num(m_cycleMs, 1);
    if (name == "messages") return m_messages.join('|');
    if (name == "sent") return QStringList(m_sent).filter(QRegularExpression("^(?!vbat$)")).join('|');
    if (name == "peak.hz") return peak() < 0 ? "" : num(live.grid.freq(peak()));
    if (name == "peak.dbm") return peak() < 0 ? "" : num(live.dBm[peak()], 2);
    if (name == "hover") return ui->spectrum->hoverText();
    if (name == "ymin") return QString::number(ui->spectrum->yAxis->range().lower, 'g', 6);
    if (name == "ymax") return QString::number(ui->spectrum->yAxis->range().upper, 'g', 6);
    if (name == "xmin") return num(ui->spectrum->xAxis->range().lower);
    if (name == "xmax") return num(ui->spectrum->xAxis->range().upper);
    if (name == "linear") return v.linear ? "1" : "0";
    if (name == "recording") return num(m_recorder.count());
    if (name.startsWith("mem") && name.size() == 4) {  // mem1..mem3: points stored
        const int m = name.back().digitValue() - 1;
        return m >= 0 && m < TraceSet::kMemories ? num(m_session.traces().memory(m).dBm.size()) : "";
    }
    if (name.startsWith("level@")) {  // level of the shown live trace at a frequency
        const double hz = name.mid(6).toDouble();
        return num(live.at(hz), 2);
    }
    if (name == "trace") {
        QString csv;
        for (int i = 0; i < live.dBm.size(); ++i)
            csv += QString("%1,%2\n").arg(live.grid.freq(i), 0, 'f', 0).arg(live.dBm[i], 0, 'f', 2);
        return csv;
    }
    return std::nullopt;
}

bool MainWindow::testAction(const QString &name, const QStringList &args)
{
    const QString a0 = args.value(0), a1 = args.value(1);
    if (name == "connect" && args.size() == 1) {
        connectTo(a0);
    } else if (name == "touch") {
        m_session.simTouch();
    } else if (name == "clearsent") {
        m_sent.clear();
    } else if (name == "mark") {
        m_sweepsMark = m_sweeps;
    } else if (name == "send" && !args.isEmpty()) {  // raw command, e.g. "caloutput 10"
        m_session.send(args.join(' '));
    } else if (name == "savecsv") {
        return saveCsv(a0);
    } else if (name == "saveimage") {
        return saveImage(a0);
    } else if (name == "loadmem") {  // loadmem <1..3> <file>
        return loadMemory(a0.toInt() - 1, a1);
    } else if (name == "memory") {  // memory <1..3> <menu item prefix...>
        QToolButton *b = QList<QToolButton *>{ui->m1Button, ui->m2Button, ui->m3Button}.value(a0.toInt() - 1);
        const QString label = args.mid(1).join(' ');
        for (QAction *a : b ? b->menu()->actions() : QList<QAction *>{})
            if (a->text().startsWith(label)) {
                a->trigger();
                return true;
            }
        return false;
    } else if (name == "recordfolder") {  // keep tests out of the user's documents
        m_settings.recordFolder = a0;
    } else if (name == "levels") {  // levels <bottom> <top>: fixed graph levels
        ViewSettings v = ui->spectrum->view();
        v.autoMin = v.autoMax = false;
        (v.linear ? v.linMin : v.logMin) = a0.toDouble();
        (v.linear ? v.linMax : v.logMax) = a1.toDouble();
        ui->spectrum->setView(v);
        ui->spectrum->replot(QCustomPlot::rpImmediateRefresh);
    } else if (name == "mouse") {
        // mouse <press|move|release|click|wheelup|wheeldown> <plot|xaxis|yaxis> <hz> [level]
        SpectrumView *s = ui->spectrum;
        s->replot(QCustomPlot::rpImmediateRefresh);  // pixel positions from the current layout
        const QPointF p = s->pointFor(a1, args.value(2).toDouble(), args.value(3).toDouble());
        auto send = [&](QEvent::Type t, Qt::MouseButton b, Qt::MouseButtons held) {
            QMouseEvent e(t, p, s->mapToGlobal(p), b, held, Qt::NoModifier);
            QApplication::sendEvent(s, &e);
        };
        if (a0 == "press") send(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        else if (a0 == "move") send(QEvent::MouseMove, Qt::NoButton, Qt::LeftButton);
        else if (a0 == "hover") send(QEvent::MouseMove, Qt::NoButton, Qt::NoButton);
        else if (a0 == "release") send(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        else if (a0 == "wheelup" || a0 == "wheeldown") {
            QWheelEvent e(p, s->mapToGlobal(p), {}, {0, a0 == "wheelup" ? 120 : -120}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(s, &e);
        } else return false;
    } else if (name == "menu") {  // menu <hz> <level> <label prefix...>: context menu item
        SpectrumView *s = ui->spectrum;
        QObject owner;
        const QString label = args.mid(2).join(' ');
        for (QAction *a : s->menuActions(s->pointFor("plot", a0.toDouble(), a1.toDouble()), &owner))
            if (!a->isSeparator() && a->text().startsWith(label)) {
                a->trigger();
                return true;
            }
        return false;
    } else {
        return false;
    }
    return true;
}
