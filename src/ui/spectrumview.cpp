#include "spectrumview.h"
#include "units.h"
#include <cmath>

double FreqTicker::getTickStep(const QCPRange &range)
{
    m_step = niceStep(range.size() / std::max(2, mTickCount));
    const double a = std::max(std::abs(range.lower), std::abs(range.upper));
    if (a >= 1e9) m_unit = 1e9, m_suffix = "G";
    else if (a >= 1e6) m_unit = 1e6, m_suffix = "M";
    else if (a >= 1e3) m_unit = 1e3, m_suffix = "k";
    else m_unit = 1, m_suffix = "";
    return m_step;
}

QString FreqTicker::getTickLabel(double tick, const QLocale &, QChar, int)
{
    int decimals = 0;  // enough to show the step exactly (2.5 MHz steps need one)
    const double s = m_step / m_unit;
    while (decimals < 9 && std::abs(s * std::pow(10, decimals) - std::round(s * std::pow(10, decimals))) > 1e-6)
        ++decimals;
    return QString::number(tick / m_unit, 'f', decimals) + m_suffix;
}

SpectrumView::SpectrumView(QWidget *parent) : QCustomPlot(parent)
{
    const QColor bg(0x10, 0x10, 0x14), fg(0xc8, 0xc8, 0xc8), grid(0x50, 0x50, 0x58);
    setBackground(bg);
    for (QCPAxis *ax : {xAxis, yAxis}) {
        ax->setBasePen(QPen(fg));
        ax->setTickPen(QPen(fg));
        ax->setSubTickPen(Qt::NoPen);
        ax->setTickLabelColor(fg);
        ax->setLabelColor(fg);
        ax->grid()->setPen(QPen(grid, 1, Qt::DashLine));
    }
    xAxis->setTicker(QSharedPointer<FreqTicker>::create());
    xAxis->setLabel("Hz");
    auto yt = QSharedPointer<QCPAxisTicker>::create();
    yt->setTickCount(10);
    yAxis->setTicker(yt);
    yAxis->setLabel("dBm");

    m_live = addGraph();
    m_live->setObjectName("live");
    m_live->setPen(QPen(QColor(0xff, 0xe0, 0x40), 1));
    setAntialiasedElements(QCP::aePlottables);
    setNotAntialiasedElements(QCP::aeGrid | QCP::aeAxes);
    setLevels(-120, 0);
    setSpan(0, 350e6);
}

void SpectrumView::setSpan(double startHz, double stopHz)
{
    xAxis->setRange(startHz, stopHz);
    replot(rpQueuedReplot);
}

void SpectrumView::setLevels(double minDbm, double maxDbm)
{
    yAxis->setRange(minDbm, maxDbm);
    replot(rpQueuedReplot);
}

void SpectrumView::setFrame(const SweepFrame &f)
{
    m_frame = f;
    const int n = f.dBm.size();
    QVector<double> x(n);
    for (int i = 0; i < n; ++i)
        x[i] = f.grid.freq(i);
    m_live->setData(x, f.dBm, true);
    replot(rpQueuedReplot);
}
