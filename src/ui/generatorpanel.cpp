#include "generatorpanel.h"
#include "ui_generatorpanel.h"
#include <QButtonGroup>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <cmath>


static QString dbm(double v) { return QString::number(v, 'f', 1).replace('-', QChar(0x2212)) + " dBm"; }

GeneratorPanel::GeneratorPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::GeneratorPanel), m_types(new QButtonGroup(this))
{
    ui->setupUi(this);
    ui->freqDisplay->setFormat(1e6, 6, "MHz", true, true);
    ui->freqDisplay->setParser([](const QString &t) { return parseHz(t, 1e6); });
    ui->levelDisplay->setFormat(1, 1, "dBm", false, false);
    ui->rfButton->setStyleSheet(
        "QPushButton#rfButton { font-size: 20pt; font-weight: bold; border: 1px solid palette(mid);"
        "  border-radius: 8px; background: palette(button); padding: 6px; }"
        "QPushButton#rfButton:hover { border-color: palette(highlight); }"
        "QPushButton#rfButton:checked { background: #c62828; color: white; border-color: #8e0000; }"
        "QPushButton#rfButton[state=\"unknown\"] { background: #b07800; color: white; }"
        "QPushButton#rfButton:disabled { color: palette(mid); background: palette(window); }");
    const QList<QToolButton *> types{ui->cwButton, ui->sweepButton, ui->amButton, ui->fmButton,
                                     ui->nfmButton, ui->wfmButton, ui->extButton, ui->burstButton};
    for (int i = 0; i < types.size(); ++i)  // in GenType order
        m_types->addButton(types[i], i);

    connect(ui->freqDisplay, &DigitEdit::valueEdited, this, [this](double hz) {
        edit([hz](GenConfig &c) { c.freqHz = hz; });
    });
    connect(ui->levelDisplay, &DigitEdit::valueEdited, this, [this](double db) {
        edit([db](GenConfig &c) { c.levelDbm = db, c.levelKnown = true; });
    });
    const QList<std::pair<QToolButton *, double>> steps{{ui->levelDown10Button, -10}, {ui->levelDown1Button, -1},
                                                        {ui->levelUp1Button, 1}, {ui->levelUp10Button, 10}};
    for (auto [b, db] : steps)
        connect(b, &QToolButton::clicked, this, [this, db] {
            edit([db](GenConfig &c) { c.levelDbm += db, c.levelKnown = true; });
        });
    connect(ui->levelSlider, &QSlider::valueChanged, this, [this](int tenths) {
        edit([tenths](GenConfig &c) { c.levelDbm = tenths / 10.0, c.levelKnown = true; });
    });
    connect(ui->rfButton, &QPushButton::clicked, this, [this](bool on) {
        edit([on](GenConfig &c) { c.on = c.on && on; });  // from "RF ?" the first click is the safe one: off
    });
    connect(ui->pathCombo, &QComboBox::activated, this, [this] {
        const QString key = ui->pathCombo->currentData().toString();
        edit([key](GenConfig &c) {
            if (key == "mixer" || key == "normal") {
                c.mixer = key == "mixer";
            } else if ((key == "high") != (c.path == GenPath::High)) {
                c.path = key == "high" ? GenPath::High : GenPath::Low;
                c.on = false;  // the other connector: RF off, as the unit does on a mode switch
            }
        });
    });
    connect(ui->extGainSpin, &QDoubleSpinBox::valueChanged, this, [this](double gain) {
        edit([gain](GenConfig &c) {  // as on the unit: the port level stays, the shown level moves
            c.levelDbm += gain - c.extGainDb;
            c.extGainDb = gain;
        });
    });
    connect(m_types, &QButtonGroup::idClicked, this, [this](int id) { edit([id](GenConfig &c) { c.type = GenType(id); }); });
    for (QLineEdit *e : {ui->sweepStartEdit, ui->sweepStopEdit, ui->sweepSpanEdit})
        connect(e, &QLineEdit::editingFinished, this, [this, e] { editSweep(e); });
    connect(ui->sweepTimeSpin, &QDoubleSpinBox::valueChanged, this, [this](double s) {
        edit([s](GenConfig &c) { c.sweepS = s; });
    });
    connect(ui->levelChangeSpin, &QDoubleSpinBox::valueChanged, this, [this](double db) {
        edit([db](GenConfig &c) { c.levelChangeDb = db; });
    });
    connect(ui->modRateSpin, &QSpinBox::valueChanged, this, [this](int hz) { edit([hz](GenConfig &c) { c.modHz = hz; }); });
    connect(ui->amDepthSpin, &QSpinBox::valueChanged, this, [this](int p) { edit([p](GenConfig &c) { c.amDepth = p; }); });
    connect(ui->fmDevSpin, &QDoubleSpinBox::valueChanged, this, [this](double khz) {
        edit([khz](GenConfig &c) { c.fmDevHz = int(std::lround(khz * 1000)); });
    });
    connect(ui->ctcssCheck, &QCheckBox::clicked, this, [this](bool on) { edit([on](GenConfig &c) { c.ctcss = on; }); });
    for (QLineEdit *e : {ui->burstPeriodEdit, ui->burstOnEdit})
        connect(e, &QLineEdit::editingFinished, this, [this, e] { editBurstTime(e); });
    connect(ui->burstCountSpin, &QSpinBox::valueChanged, this, [this](int n) {
        edit([n](GenConfig &c) { c.burstCount = n; });
    });
    connect(ui->burstTriggerButton, &QPushButton::clicked, this, &GeneratorPanel::burstTriggered);
    setDevice(nullptr);
}

void GeneratorPanel::editBurstTime(QLineEdit *e)
{
    const auto s = parseSeconds(e->text());
    if (!s || *s <= 0)
        return showConfig(m_config);  // revert invalid input
    // as on the unit: an ON time that does not fit in the period is shortened (clampGen)
    edit([&](GenConfig &c) { (e == ui->burstPeriodEdit ? c.burstPeriodS : c.burstOnS) = *s; });
}

GeneratorPanel::~GeneratorPanel() = default;

void GeneratorPanel::edit(const std::function<void(GenConfig &)> &change)
{
    GenConfig c = m_config;
    change(c);
    emit edited(c);
}

void GeneratorPanel::keyPressEvent(QKeyEvent *e)
{
    // the main window's Esc shortcut takes the key while the window is active; otherwise an Esc
    // the fields did not use ends up here
    if (e->key() == Qt::Key_Escape && m_connected && m_config.on != false)
        return edit([](GenConfig &c) { c.on = false; });
    QWidget::keyPressEvent(e);
}

void GeneratorPanel::editSweep(QLineEdit *e)
{
    const auto hz = parseHz(e->text(), 1e6);
    if (!hz)
        return showConfig(m_config);  // revert invalid input
    edit([&](GenConfig &c) {
        const GenLimits l = genLimits(c, m_device);
        FreqRange r(c.freqHz - c.spanHz / 2, c.freqHz + c.spanHz / 2);
        r.setLimits(l.minHz, l.maxHz, 0);
        if (e == ui->sweepStartEdit) r.setStart(*hz);
        else if (e == ui->sweepStopEdit) r.setStop(*hz);
        else r.setSpan(*hz);
        c.freqHz = r.center();
        c.spanHz = r.span();
    });
}

void GeneratorPanel::setDevice(const DeviceInfo *d)
{
    m_connected = d && d->model;
    if (m_connected)
        m_device = *d;
    const GenDesc *g = m_device.model ? &m_device.model->gen : nullptr;
    {
        QSignalBlocker block(ui->pathCombo);
        ui->pathCombo->clear();
        auto upTo = [this](bool mixer) {  // the unit's own words: "Highest accuracy, max 5.4GHz"
            GenConfig c = m_config;
            c.path = GenPath::Low;
            c.mixer = mixer;
            return formatHz(genLimits(c, m_device).maxHz, 1e7);
        };
        if (g && g->hasMixer) {
            ui->pathCombo->addItem("Accurate, max " + upTo(true), "mixer");
            ui->pathCombo->addItem("Cleanest, max " + upTo(false), "normal");
        } else if (g) {
            ui->pathCombo->addItem("LOW port, max " + formatHz(g->maxHz, 1e6), "low");
            if (g->highMaxHz > 0)
                ui->pathCombo->addItem(QString("HIGH port, %1 - %2").arg(formatHz(g->highMinHz, 1e6), formatHz(g->highMaxHz, 1e6)),
                                       "high");
        }
    }
    const QVector<GenType> offered = genTypes(m_device);  // Burst with firmware that has it
    for (QAbstractButton *b : m_types->buttons()) {
        const GenType t = GenType(m_types->id(b));
        b->setVisible(g ? offered.contains(t) : t <= GenType::FM);
    }
    QSignalBlocker block(ui->levelChangeSpin);
    const double change = g ? g->maxLevelChange : 90;
    ui->levelChangeSpin->setRange(-change, change);
    ui->modRateSpin->setMaximum(g ? g->maxModHz : 10000);
    for (QWidget *w : {static_cast<QWidget *>(ui->freqGroup), static_cast<QWidget *>(ui->levelGroup),
                       static_cast<QWidget *>(ui->outputGroup), static_cast<QWidget *>(ui->signalGroup),
                       static_cast<QWidget *>(ui->rfButton)})
        w->setEnabled(m_connected);
    showConfig(m_config);
}

void GeneratorPanel::showConfig(const GenConfig &c)
{
    m_config = c;
    const GenLimits l = genLimits(c, m_device);
    const GenDesc *g = m_device.model ? &m_device.model->gen : nullptr;
    const QSignalBlocker b1(ui->levelSlider), b2(ui->extGainSpin), b3(ui->sweepTimeSpin), b4(ui->levelChangeSpin),
        b5(ui->modRateSpin), b6(ui->amDepthSpin), b7(ui->fmDevSpin), b8(ui->pathCombo), b9(ui->burstCountSpin);

    ui->freqDisplay->setRange(l.minHz, l.maxHz);
    ui->freqDisplay->setValue(c.freqHz);
    ui->levelDisplay->setRange(l.minDbm, l.maxDbm);
    ui->levelDisplay->setValue(c.levelDbm);
    ui->levelDisplay->setUnknown(!c.levelKnown);
    ui->levelSlider->setRange(int(std::lround(l.minDbm * 10)), int(std::lround(l.maxDbm * 10)));
    ui->levelSlider->setSingleStep(5);
    ui->levelSlider->setPageStep(100);
    ui->levelSlider->setValue(int(std::lround(c.levelDbm * 10)));
    ui->levelMinLabel->setText(dbm(l.minDbm));
    ui->levelMaxLabel->setText(dbm(l.maxDbm));

    ui->rfButton->setChecked(c.on.value_or(false));
    ui->rfButton->setText(!c.on ? "RF ?" : *c.on ? "RF ON" : "RF OFF");
    ui->rfButton->setProperty("state", !c.on ? "unknown" : *c.on ? "on" : "off");
    ui->rfButton->style()->unpolish(ui->rfButton);  // the style sheet follows the property
    ui->rfButton->style()->polish(ui->rfButton);

    const QString path = g && g->hasMixer ? (c.mixer ? "mixer" : "normal") : c.path == GenPath::High ? "high" : "low";
    ui->pathCombo->setCurrentIndex(std::max(0, ui->pathCombo->findData(path)));
    const QString signal = outputPathText(c, m_device);
    ui->pathInfoLabel->setText(signal.isEmpty() ? QString(QChar(0x2014)) : signal);
    ui->extGainSpin->setValue(c.extGainDb);
    showSignal(c, g);
}

void GeneratorPanel::showSignal(const GenConfig &c, const GenDesc *g)
{
    if (c.type) {
        m_types->button(int(*c.type))->setChecked(true);
    } else {  // unknown: no type checked
        m_types->setExclusive(false);
        for (QAbstractButton *b : m_types->buttons())
            b->setChecked(false);
        m_types->setExclusive(true);
    }
    const bool high = c.path == GenPath::High && g && g->highMaxHz > 0;
    ui->amButton->setEnabled(!high);  // the HIGH output has no AM, external modulation or level sweep
    ui->extButton->setEnabled(!high);
    const GenType t = c.type.value_or(GenType::CW);
    ui->signalStack->setCurrentWidget(!c.type ? ui->unknownPage
                                      : t == GenType::CW ? ui->cwPage
                                      : t == GenType::Sweep ? ui->sweepPage
                                      : t == GenType::Burst ? ui->burstPage
                                                            : ui->modPage);
    // burst: ON time of every period, continuously or COUNT bursts per Start burst
    ui->burstPeriodEdit->setText(formatSeconds(c.burstPeriodS));
    ui->burstOnEdit->setText(formatSeconds(c.burstOnS));
    ui->burstCountSpin->setValue(c.burstCount);
    const QString duty = QString::number(100 * c.burstOnS / c.burstPeriodS, 'g', 3);
    ui->burstInfoLabel->setText(
        !burstPossible(c) ? QString("<span style=\"color:#d35400\">No burst above 1.13 GHz with Path Cleanest: "
                                    "choose Accurate</span>")
        : c.burstCount == 0 ? QString("On %1 of every %2 (%3 %), continuously")
                                  .arg(formatSeconds(c.burstOnS), formatSeconds(c.burstPeriodS), duty)
                            : QString("%1 bursts of %2 every %3, after each Start burst")
                                  .arg(c.burstCount).arg(formatSeconds(c.burstOnS), formatSeconds(c.burstPeriodS)));
    ui->burstTriggerButton->setText(c.burstCount == 0 ? "Restart burst" : "Start burst");
    // sweep: around the frequency above
    ui->sweepStartEdit->setText(formatMHz(c.freqHz - c.spanHz / 2));
    ui->sweepStopEdit->setText(formatMHz(c.freqHz + c.spanHz / 2));
    ui->sweepSpanEdit->setText(formatMHz(c.spanHz));
    ui->sweepTimeSpin->setValue(c.sweepS);
    ui->levelChangeSpin->setValue(c.levelChangeDb);
    ui->levelChangeSpin->setEnabled(!high);
    ui->sweepInfoLabel->setText(c.levelChangeDb != 0 && !high
                                    ? QString("Level %1 to %2").arg(dbm(c.levelDbm), dbm(c.levelDbm + c.levelChangeDb))
                                    : QString());
    // modulation: what this model can adjust
    const bool adjustable = g && g->adjustableModulation;
    ui->modRateSpin->setValue(c.modHz);
    ui->modRateLabel->setVisible(t != GenType::External);
    ui->modRateSpin->setVisible(t != GenType::External);
    ui->amDepthLabel->setVisible(adjustable && t == GenType::AM);
    ui->amDepthSpin->setVisible(adjustable && t == GenType::AM);
    ui->amDepthSpin->setValue(c.amDepth);
    ui->fmDevLabel->setVisible(adjustable && t == GenType::FM);
    ui->fmDevSpin->setVisible(adjustable && t == GenType::FM);
    ui->fmDevSpin->setValue(c.fmDevHz / 1000.0);
    // the unit's CTCSS + 1 kHz: shown and set where the firmware can (v1.4-341), only acts below 255 Hz
    const bool fm = adjustable && t == GenType::FM;
    const bool ctcssRate = c.modHz < kCtcssMaxHz;
    ui->ctcssCheck->setVisible(fm && c.ctcss.has_value());
    ui->ctcssCheck->setChecked(c.ctcss.value_or(false));
    ui->ctcssCheck->setEnabled(ctcssRate);
    auto khz = [](int hz) { return QString::number(hz / 1000.0, 'g', 3) + " kHz"; };
    QString fmInfo;
    if (const auto tone = ctcssTone(c))
        fmInfo = QString("Adds %1 Hz at %2 deviation, %3 Hz at %4")
                     .arg(tone->toneHz).arg(khz(tone->toneDevHz)).arg(c.modHz).arg(khz(tone->rateDevHz));
    else if (fm && c.ctcss == true)
        fmInfo = "1 kHz tone only below 255 Hz";
    else if (fm && !c.ctcss && ctcssRate && m_device.firmware() >= 1004220)  // the tone, but no command
        fmInfo = "Below 255 Hz the tinySA adds a 1 kHz tone if CTCSS + 1 kHz is on in its MODULATION menu";
    ui->modInfoLabel->setText(t == GenType::AM && !adjustable ? "Depth about 30 %"
                              : t == GenType::NFM            ? "Deviation 3 kHz"
                              : t == GenType::WFM            ? "Deviation 75 kHz"
                              : t == GenType::External       ? "Modulation from the external input"
                                                             : fmInfo);
}

QString GeneratorPanel::statusText() const
{
    const GenConfig &c = m_config;
    QString s = !c.on ? "RF ?" : *c.on ? "RF ON" : "RF OFF";
    s += " · " + formatMHz(c.freqHz) + " MHz";
    if (c.levelKnown)
        s += " · " + dbm(c.levelDbm);
    if (c.type && *c.type != GenType::CW)
        s += " · " + genTypeName(*c.type);
    if (const auto tone = ctcssTone(c))
        s += QString(" + %1 Hz tone").arg(tone->toneHz);
    return s;
}
