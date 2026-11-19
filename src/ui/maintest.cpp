// MainWindow: TestHost queries and actions for the script runner (autotest.h).
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "batterywindow.h"
#include "consolewindow.h"
#include "digitedit.h"
#include "generatorpanel.h"
#include "firmwaredialog.h"
#include "screenview.h"
#include "screenwindow.h"
#include <QApplication>
#include <QMenu>
#include <QRegularExpression>
#include <QSerialPortInfo>
#include <QTimer>
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
    if (name == "activity") return m_session.activity() == DeviceLink::Generator ? "generator" : "analyzer";
    if (name == "firmware.warning") return ui->firmwareWarningLabel->isHidden() ? "0" : "1";
    if (name.startsWith("gen.")) {  // the generator as the session has it; "?" = unknown
        const GenConfig g = m_session.generator();
        const QString f = name.mid(4);
        if (f == "on") return !g.on ? "?" : *g.on ? "1" : "0";
        if (f == "freq") return num(g.freqHz);
        if (f == "level") return g.levelKnown ? num(g.levelDbm, 1) : "?";
        if (f == "type") return g.type ? genTypeName(*g.type) : "?";
        if (f == "span") return num(g.spanHz);
        if (f == "path") return g.path == GenPath::High ? "high" : "low";
        if (f == "mixer") return g.mixer ? "1" : "0";
        if (f == "extgain") return num(g.extGainDb, 1);
        if (f == "period") return QString::number(g.burstPeriodS, 'g', 9);  // seconds
        if (f == "ontime") return QString::number(g.burstOnS, 'g', 9);
        if (f == "count") return num(g.burstCount);
        if (f == "burstbutton")  // offered: firmware with burst
            return ui->generatorPanel->findChild<QWidget *>("burstButton")->isHidden() ? "0" : "1";
        if (f == "ctcss") return !g.ctcss ? "?" : *g.ctcss ? "1" : "0";  // the unit's CTCSS tone
        if (f == "ctcssbox") {  // the + 1 kHz tone check box: hidden, disabled (rate >= 255 Hz) or enabled
            const auto *b = ui->generatorPanel->findChild<QWidget *>("ctcssCheck");
            return b->isHidden() ? "hidden" : b->isEnabled() ? "enabled" : "disabled";
        }
        if (f == "status") return m_genLabel->text();
        if (f == "editing") {  // an inline entry of the big displays is open
            const auto edits = ui->generatorPanel->findChildren<DigitEdit *>();
            return std::any_of(edits.begin(), edits.end(), [](DigitEdit *d) { return d->isEditing(); }) ? "1" : "0";
        }
    }
    if (name == "tracker.connected") return m_session.isTrackerConnected() ? "1" : "0";
    if (name == "tracker.model") return m_session.tracker().model ? m_session.tracker().model->name : "";
    if (name == "tracking") return m_session.isTracking() ? "1" : "0";
    if (name == "track.level") return num(m_session.track().levelDbm, 1);
    if (name == "tracker.ppb") return m_session.trackerPpb() ? num(*m_session.trackerPpb()) : "?";
    if (name == "tracker.sent") return QStringList(m_trackerSent).filter(QRegularExpression("^(?!vbat$)")).join('|');
    if (name == "model") return m_session.device().model ? m_session.device().model->name : "";
    if (name == "version") return m_session.device().version;
    if (name == "sweeps") return num(m_sweeps);
    if (name == "sweeps.new") return num(m_sweeps - m_sweepsMark);  // since "do mark"
    if (name == "scan") return num(m_session.scanMode());
    if (name == "idle") return m_session.scanMode() == DeviceLink::Off && !m_session.isBusy() ? "1" : "0";
    if (name == "start") return num(c.grid.startHz);
    if (name == "stop") return num(c.grid.stopHz);
    if (name == "rbw") return QString::number(c.rbwKHz);
    if (name == "atten") return num(c.atten);
    if (name == "lna") return c.lna ? "1" : "0";
    if (name == "extgain") return num(c.extGainDb, 1);  // the analyzer's
    if (name == "ylabel") return ui->spectrum->yAxis->label();
    if (name == "input") return c.input == InputMode::High ? "high" : "low";
    if (name == "points") return num(live.grid.points);
    if (name == "sweepms") return num(m_sweepMs, 1);
    if (name == "cyclems") return num(m_cycleMs, 1);
    if (name == "messages") return m_messages.join('|');
    if (name == "sent") return QStringList(m_sent).filter(QRegularExpression("^(?!vbat$)")).join('|');
    if (name == "peak.hz") return peak() < 0 ? "" : num(live.grid.freq(peak()));
    if (name == "peak.dbm") return peak() < 0 ? "" : num(live.dBm[peak()], 2);
    if (name == "hover") return ui->spectrum->hoverText();
    if (name == "hover.covers") return ui->spectrum->hoverCoversMark() ? "1" : "0";
    if (name == "hover.place") {  // top left / bottom left / top centre
        const QPointF at = ui->spectrum->hoverPlace();
        return at.y() > 0.5 ? "bottom" : at.x() > 0.25 ? "centre" : "top";
    }
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
    if (name == "markers") return num(m_session.markers().count());
    if (name == "console.log") return m_console->logText();
    if (name == "battery.samples") return num(m_battery->samples());
    if (name == "dfu.devices") return num(m_firmwareDialog->deviceCount());
    if (name == "dfu.log") return m_firmwareDialog->logText();
    if (name == "dfu.busy") return m_firmwareDialog->busy() ? "1" : "0";
    if (name == "theme.linewidth") return num(m_settings.theme.lineWidth);
    if (name == "theme.background") return m_settings.theme.background.name();
    if (name == "csv.separator") return QString(m_settings.csv.separator);
    if (name == "csv.decimalcomma") return m_settings.csv.decimalComma ? "1" : "0";
    if (name == "csv.mhz") return m_settings.csv.mhz ? "1" : "0";
    if (name == "scheme") return num(m_settings.colorScheme);
    if (name == "screen.width") return num(m_screen->view()->image().width());
    if (name == "screen.updates") return num(m_screen->updates());
    if (name.startsWith("screen.pixel@")) {  // screen.pixel@x,y -> #rrggbb as shown (inverted or not)
        const QStringList xy = name.mid(13).split(',');
        const QImage &img = m_screen->view()->shownImage();
        const QPoint p(xy.value(0).toInt(), xy.value(1).toInt());
        return img.valid(p) ? img.pixelColor(p).name() : QString("none");
    }
    if (name == "selected") return num(m_session.markers().selected());
    if (name.startsWith("peak.max.") || name.startsWith("peak.min.")) {  // interpolated peak markers
        const QPointF pk = ui->spectrum->peak(name.startsWith("peak.max"));
        return name.endsWith(".hz") ? num(pk.x()) : num(pk.y(), 2);
    }
    if (name.startsWith("marker") && name.contains('.')) {  // marker<N>.hz|dbm|dhz|ddb
        const int i = name.mid(6, name.indexOf('.') - 6).toInt() - 1;
        if (i < 0 || i >= m_session.markers().count())
            return QString("none");
        const MarkerReading r = m_session.markers().read(i, live);
        const QString f = name.section('.', 1);
        return f == "hz" ? num(r.hz) : f == "dbm" ? num(r.dBm, 2) : f == "dhz" ? num(r.dHz) : num(r.dDb, 2);
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
    } else if (name == "trackerconnect" && args.size() == 1) {  // tracking generator; auto =
        QString port = a0;                                         // the other tinySA's port
        for (const auto &p : QSerialPortInfo::availablePorts())
            if (a0 == "auto" && port == "auto" && p.portName() != m_session.device().port
                && DeviceLink::isTinySAUsbId(p.vendorIdentifier(), p.productIdentifier()))
                port = p.portName();
        connectTrackerTo(port);
    } else if (name == "touch") {
        m_session.simTouch();
    } else if (name == "clearsent") {
        m_sent.clear();
        m_trackerSent.clear();
    } else if (name == "mark") {
        m_sweepsMark = m_sweeps;
    } else if (name == "send" && !args.isEmpty()) {  // raw command, e.g. "caloutput 10"
        // "a|b": queued together, nothing of ours goes out between them (touch|release)
        for (const QString &command : args.join(' ').split('|'))
            m_session.send(command.trimmed());
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
    } else if (name == "macro") {  // macro <1..4> <name> <command|command...>
        QStringList names = m_console->macroNames(), cmds = m_console->macroCommands();
        const int i = a0.toInt() - 1;
        if (i < 0 || i >= names.size())
            return false;
        names[i] = a1;
        cmds[i] = args.mid(2).join(' ').split('|').join('\n');
        m_console->setMacros(names, cmds);
    } else if (name == "screentouch") {  // screentouch x y [hold ms]: click on the screen view at LCD x,y
        ScreenView *v = m_screen->view();
        const QPointF p = v->widgetPoint({a0.toInt(), a1.toInt()});
        auto send = [v, p](QEvent::Type t) {
            QMouseEvent e(t, p, v->mapToGlobal(p), Qt::LeftButton,
                          t == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(v, &e);
        };
        send(QEvent::MouseButtonPress);
        if (args.size() > 2)  // held like a real click, released while the script goes on
            QTimer::singleShot(args.value(2).toInt(), v, [send] { send(QEvent::MouseButtonRelease); });
        else
            send(QEvent::MouseButtonRelease);
    } else if (name == "dfuread") {  // dfuread <file>: Save flash without the file dialog
        return m_firmwareDialog->startRead(a0);
    } else if (name == "dfuwrite") {  // dfuwrite <file>: Upload + verify without confirmation
        return m_firmwareDialog->startWrite(a0, true);
    } else if (name == "dfuleave") {
        return m_firmwareDialog->startLeave();
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
        // mouse <hover|press|move|release|click|dblclick|wheelup|wheeldown> <plot|xaxis|yaxis> <hz> [level]
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
        else if (a0 == "click") {
            send(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        } else if (a0 == "dblclick") {
            send(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
            send(QEvent::MouseButtonDblClick, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        }
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
