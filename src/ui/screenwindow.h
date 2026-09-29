#pragma once
// Screen capture and remote control window (Designer form + thin controller).
#include <QImage>
#include <QWidget>
#include <memory>

namespace Ui { class ScreenWindow; }
class ScreenView;

class ScreenWindow : public QWidget {
    Q_OBJECT
public:
    explicit ScreenWindow(QWidget *parent = nullptr);
    ~ScreenWindow() override;

    void setScreen(const QImage &img);
    void remoteEnded();  // the link dropped remote mode (disconnect)
    ScreenView *view() const;
    int updates() const { return m_updates; }

signals:
    void captureRequested();
    void remoteRequested(bool on);
    void touchRequested(QPoint lcd, bool down);

protected:
    void hideEvent(QHideEvent *e) override;  // closing the window ends remote control

private:
    std::unique_ptr<Ui::ScreenWindow> ui;
    int m_updates = 0;
};
