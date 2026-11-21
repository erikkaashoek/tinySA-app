#pragma once
// Table model for the marker list. Holds only the readings the controller hands it;
// edits to the frequency column come back as frequencyEdited().
#include "levelunit.h"
#include "markers.h"
#include <QAbstractTableModel>

class MarkerModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Name, Frequency, Level, DeltaFrequency, DeltaLevel, Columns };
    using QAbstractTableModel::QAbstractTableModel;

    void setReadings(const QList<MarkerReading> &rows, double resolutionHz, LevelUnit unit, bool normalized);

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : Columns; }
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

signals:
    void frequencyEdited(int row, double hz);

private:
    QList<MarkerReading> m_rows;
    double m_resolution = 1;
    LevelUnit m_unit = LevelUnit::dBm;
    bool m_normalized = false;
};
