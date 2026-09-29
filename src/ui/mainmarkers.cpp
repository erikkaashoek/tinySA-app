// MainWindow: markers (graph intents, marker dock, table model).
#include "mainwindow.h"
#include "markermodel.h"
#include "ui_mainwindow.h"
#include <QItemSelectionModel>
#include <QSignalBlocker>
#include <cmath>

void MainWindow::setupMarkers()
{
    m_markerModel = new MarkerModel(this);
    ui->markerTable->setModel(m_markerModel);
    ui->menuView->addAction(ui->mainToolBar->toggleViewAction());
    connect(ui->actionShowMarkers, &QAction::toggled, ui->markerPanel, &QWidget::setVisible);
    ui->graphSplitter->setStretchFactor(0, 1);
    ui->graphSplitter->setSizes({560, 110});

    MarkerSet *ms = &m_session.markers();
    SpectrumView *view = ui->spectrum;
    auto done = [this] { renderMarkers(); };
    connect(view, &SpectrumView::markerAddRequested, this, [=](double hz) { ms->add(snap(hz)); done(); });
    connect(view, &SpectrumView::markerRemoveRequested, this, [=](int i) { ms->remove(i); done(); });
    connect(view, &SpectrumView::markerMoveRequested, this, [=](int i, double hz) { ms->move(i, snap(hz)); done(); });
    connect(view, &SpectrumView::markerSelectRequested, this, [=](int i) { ms->select(i); done(); });
    connect(view, &SpectrumView::markerDeltaRequested, this, [=](int i) {
        ms->setDelta(i, !ms->all().value(i).delta);
        done();
    });
    connect(view, &SpectrumView::markerStepRequested, this, [=](int n) {
        const Trace &t = view->trace(0);
        const int i = ms->selected();
        if (i >= 0 && !t.isEmpty())
            ms->move(i, t.grid.freq(std::clamp(t.grid.nearest(ms->all()[i].hz) + n, 0, t.grid.points - 1)));
        done();
    });
    connect(view, &SpectrumView::markerPeakRequested, this, [=] { markerToPeak(false); done(); });
    connect(ui->addMarkerButton, &QPushButton::clicked, this, [=] { markerToPeak(true); done(); });
    connect(ui->peakMarkerButton, &QPushButton::clicked, this, [=] { markerToPeak(false); done(); });
    connect(ui->deltaMarkerButton, &QPushButton::clicked, this, [=] {
        ms->setDelta(ms->selected(), !ms->all().value(ms->selected()).delta);
        done();
    });
    connect(ui->deleteMarkerButton, &QPushButton::clicked, this, [=] { ms->remove(ms->selected()); done(); });
    connect(ui->clearMarkersButton, &QPushButton::clicked, this, [=] { ms->clear(); done(); });
    connect(ui->markerTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [=](const QModelIndex &cur) { ms->select(cur.row()); done(); });
    connect(m_markerModel, &MarkerModel::frequencyEdited, this, [=](int row, double hz) { ms->move(row, hz); done(); });
}

// Snap to the nearest point of the displayed live trace (markers read point values).
double MainWindow::snap(double hz) const
{
    const Trace &t = ui->spectrum->trace(0);
    return t.isEmpty() ? hz : t.grid.freq(t.grid.nearest(hz));
}

void MainWindow::markerToPeak(bool addNew)
{
    MarkerSet &ms = m_session.markers();
    const QPointF pk = tracePeak(ui->spectrum->trace(0), true);
    const SweepConfig c = m_session.config();
    const double hz = std::isnan(pk.x()) ? (c.grid.startHz + c.grid.stopHz) / 2 : snap(pk.x());
    if (addNew || ms.selected() < 0)
        ms.add(hz);
    else
        ms.move(ms.selected(), hz);
}

void MainWindow::renderMarkers()
{
    const MarkerSet &ms = m_session.markers();
    const Trace &live = ui->spectrum->trace(0);
    const QString unit = m_session.traces().normalized() ? "dB" : "dBm";
    const double res = live.isEmpty() ? 1 : live.grid.step();
    QList<MarkerMark> marks;
    QList<MarkerReading> rows;
    for (int i = 0; i < ms.count(); ++i) {
        const MarkerReading r = ms.read(i, live);
        rows << r;
        const bool delta = !std::isnan(r.dDb);
        const QString readout = delta ? QString("%1: %2%3  %4%5 dB").arg(r.name, r.dHz >= 0 ? "+" : "",
                                            formatHz(r.dHz, res), r.dDb >= 0 ? "+" : "")
                                            .arg(r.dDb, 0, 'f', 1)
                                      : QString("%1: %2  %3 %4").arg(r.name, formatHz(r.hz, res))
                                            .arg(r.dBm, 0, 'f', 1).arg(unit);
        marks << MarkerMark{r.hz, r.dBm, r.name, readout, i == ms.selected()};
    }
    ui->spectrum->setMarkers(marks);
    m_markerModel->setReadings(rows, res, unit);
    QSignalBlocker block(ui->markerTable->selectionModel());
    if (ms.selected() >= 0)
        ui->markerTable->selectRow(ms.selected());
    else
        ui->markerTable->clearSelection();
    for (QPushButton *b : {ui->peakMarkerButton, ui->deltaMarkerButton, ui->deleteMarkerButton})
        b->setEnabled(ms.selected() >= 0);
    ui->clearMarkersButton->setEnabled(ms.count() > 0);
}
