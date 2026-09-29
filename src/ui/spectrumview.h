#pragma once
// The spectrum graph. One implementation of all graph drawing and (later) mouse
// interaction; it shows data and emits intents, it never talks to the device.
// Promoted from a plain QWidget in mainwindow.ui.
#include "link.h"
#include "qcustomplot.h"

class SpectrumView : public QCustomPlot {
    Q_OBJECT
public:
    explicit SpectrumView(QWidget *parent = nullptr);

    void setFrame(const SweepFrame &f);      // live trace
    void setSpan(double startHz, double stopHz);
    void setLevels(double minDbm, double maxDbm);
    const SweepFrame &frame() const { return m_frame; }

private:
    SweepFrame m_frame;
    QCPGraph *m_live;
};

// Frequency axis: {1, 2, 2.5, 5} x 10^n steps, labels with k/M/G units
class FreqTicker : public QCPAxisTicker {
protected:
    double getTickStep(const QCPRange &range) override;
    QString getTickLabel(double tick, const QLocale &locale, QChar formatChar, int precision) override;

private:
    double m_step = 1, m_unit = 1;
    const char *m_suffix = "";
};
