#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include <QColorDialog>
#include <QDateTime>
#include <QFileDialog>
#include <QMessageBox>

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent), ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);
    ui->buttonBox->button(QDialogButtonBox::Ok)->setObjectName("settingsOk");
    m_colours = {
        {ui->backgroundColour, [](Theme &t) -> QColor & { return t.background; }},
        {ui->gridColour, [](Theme &t) -> QColor & { return t.grid; }},
        {ui->textColour, [](Theme &t) -> QColor & { return t.text; }},
        {ui->liveColour, [](Theme &t) -> QColor & { return t.trace[0]; }},
        {ui->m1Colour, [](Theme &t) -> QColor & { return t.trace[1]; }},
        {ui->m2Colour, [](Theme &t) -> QColor & { return t.trace[2]; }},
        {ui->m3Colour, [](Theme &t) -> QColor & { return t.trace[3]; }},
        {ui->markerColour, [](Theme &t) -> QColor & { return t.marker; }},
        {ui->selectedColour, [](Theme &t) -> QColor & { return t.selected; }},
    };
    for (auto &[button, field] : m_colours)
        connect(button, &QPushButton::clicked, this, [this, button = button, field = field] {
            const QColor c = QColorDialog::getColor(field(m_theme), this, button->text(),
                                                    QColorDialog::ShowAlphaChannel);
            if (c.isValid()) {
                field(m_theme) = c;
                showTheme();
            }
        });
    connect(ui->darkPresetButton, &QPushButton::clicked, this, [this] {
        m_theme = Theme::dark();  // colours only; width and text size stay
        showTheme();
    });
    connect(ui->lightPresetButton, &QPushButton::clicked, this, [this] {
        m_theme = Theme::light();  // colours only; width and text size stay
        showTheme();
    });
    connect(ui->browseFolderButton, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "Record / save folder", ui->recordFolderEdit->text());
        if (!dir.isEmpty())
            ui->recordFolderEdit->setText(dir);
    });

    connect(ui->vbatOffsetButton, &QPushButton::clicked, this, [this] {
        emit commandRequested(QString("vbat_offset %1").arg(ui->vbatOffsetSpin->value()));
    });
    connect(ui->setTimeButton, &QPushButton::clicked, this, [this] {
        const QDateTime now = QDateTime::currentDateTime();
        emit commandRequested(QString("time b 0x%1 0x%2").arg(now.toString("yyMMdd"), now.toString("hhmmss")));
    });
    connect(ui->saveConfigButton, &QPushButton::clicked, this, [this] { emit commandRequested("saveconfig"); });
    connect(ui->clearConfigButton, &QPushButton::clicked, this, [this] {
        const auto ask = [this](const QString &text) {
            return QMessageBox::warning(this, "Clear config", text, QMessageBox::Yes | QMessageBox::No,
                                        QMessageBox::No) == QMessageBox::Yes;
        };
        if (ask("Reset the tinySA configuration to factory defaults?")
            && ask("This erases the calibration and all settings stored in the tinySA. Really clear?"))
            emit commandRequested("clearconfig 1234");
    });
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::load(const AppSettings &s, const DeviceInfo &info, bool connected)
{
    ui->infoText->setPlainText(connected ? info.title() + "\n\n" + info.info : QString());
    ui->vbatOffsetSpin->setValue(info.vbatOffset);
    for (QWidget *w : {static_cast<QWidget *>(ui->vbatOffsetSpin), static_cast<QWidget *>(ui->vbatOffsetButton),
                       static_cast<QWidget *>(ui->setTimeButton), static_cast<QWidget *>(ui->saveConfigButton),
                       static_cast<QWidget *>(ui->clearConfigButton)})
        w->setEnabled(connected);
    ui->schemeCombo->setCurrentIndex(std::clamp(s.colorScheme, 0, 2));
    m_theme = s.theme;
    ui->lineWidthSpin->setValue(s.theme.lineWidth);
    ui->fontSizeSpin->setValue(s.theme.fontPt);
    showTheme();
    ui->recordFolderEdit->setText(s.recordFolder);
    ui->csvSeparatorCombo->setCurrentIndex(s.csv.separator == ',' ? 1 : s.csv.separator == '\t' ? 2 : 0);
    ui->decimalCommaCheck->setChecked(s.csv.decimalComma);
}

void SettingsDialog::store(AppSettings &s) const
{
    s.colorScheme = ui->schemeCombo->currentIndex();
    s.theme = m_theme;
    s.theme.lineWidth = ui->lineWidthSpin->value();
    s.theme.fontPt = ui->fontSizeSpin->value();
    s.recordFolder = ui->recordFolderEdit->text();
    s.csv.separator = QList<QChar>{';', ',', '\t'}.value(ui->csvSeparatorCombo->currentIndex(), ';');
    s.csv.decimalComma = ui->decimalCommaCheck->isChecked();
}

void SettingsDialog::showTheme()
{
    for (auto &[button, field] : m_colours) {
        QPixmap swatch(16, 16);
        swatch.fill(field(m_theme));
        button->setIcon(swatch);
    }
}
