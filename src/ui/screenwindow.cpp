#include "screenwindow.h"
#include "ui_screenwindow.h"
#include <QClipboard>
#include <QDateTime>
#include <QFileDialog>
#include <QGuiApplication>
#include <QSignalBlocker>
#include <QTimer>

ScreenWindow::ScreenWindow(QWidget *parent) : QWidget(parent, Qt::Window), ui(new Ui::ScreenWindow)
{
    ui->setupUi(this);
    connect(ui->captureButton, &QPushButton::clicked, this, &ScreenWindow::captureRequested);
    connect(ui->remoteCheck, &QCheckBox::toggled, this, [this](bool on) {
        ui->captureButton->setEnabled(!on);
        emit remoteRequested(on);
    });
    connect(ui->fitCheck, &QCheckBox::toggled, ui->screenView, &ScreenView::setFit);
    connect(ui->screenView, &ScreenView::touched, this, [this](QPoint lcd, bool down) {
        emit touchRequested(lcd, down);
        if (!down && !ui->remoteCheck->isChecked())  // no live updates: show the result
            QTimer::singleShot(300, this, &ScreenWindow::captureRequested);
    });
    connect(ui->saveScreenButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, "Save screen", "tinySA screen " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm-ss") + ".png",
            "PNG (*.png);;Bitmap (*.bmp)");
        if (!path.isEmpty())
            ui->screenView->image().save(path);
    });
    connect(ui->copyScreenButton, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setImage(ui->screenView->image());
    });
}

ScreenWindow::~ScreenWindow() = default;

ScreenView *ScreenWindow::view() const
{
    return ui->screenView;
}

void ScreenWindow::setScreen(const QImage &img)
{
    ++m_updates;
    ui->screenView->setImage(img);
    setWindowTitle(QString("tinySA screen %1 x %2").arg(img.width()).arg(img.height()));
}

void ScreenWindow::remoteEnded()
{
    QSignalBlocker block(ui->remoteCheck);
    ui->remoteCheck->setChecked(false);
    ui->captureButton->setEnabled(true);
}

void ScreenWindow::hideEvent(QHideEvent *e)
{
    ui->remoteCheck->setChecked(false);
    QWidget::hideEvent(e);
}
