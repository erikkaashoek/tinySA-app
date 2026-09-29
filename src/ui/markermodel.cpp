#include "markermodel.h"
#include "units.h"
#include <cmath>

void MarkerModel::setReadings(const QList<MarkerReading> &rows, double resolutionHz, const QString &unit)
{
    const bool reshape = rows.size() != m_rows.size();
    if (reshape)
        beginResetModel();
    m_rows = rows;
    m_resolution = std::max(1.0, resolutionHz);
    m_unit = unit;
    if (reshape)
        endResetModel();
    else if (!rows.isEmpty())
        emit dataChanged(index(0, 0), index(int(rows.size()) - 1, Columns - 1), {Qt::DisplayRole});
}

QVariant MarkerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const MarkerReading &r = m_rows[index.row()];
    const bool delta = !std::isnan(r.dDb);
    auto level = [&](double v, bool sign) {
        return std::isnan(v) ? QString("-")
                             : QString("%1%2 %3").arg(sign && v >= 0 ? "+" : "").arg(v, 0, 'f', 1)
                                   .arg(sign ? "dB" : m_unit);
    };
    if (role == Qt::TextAlignmentRole)
        return index.column() == Name ? int(Qt::AlignCenter) : int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::EditRole && index.column() == Frequency)
        return QString::number(r.hz / 1e6, 'f', 6);  // MHz, like the sweep edits
    if (role != Qt::DisplayRole)
        return {};
    switch (index.column()) {
    case Name: return r.name;
    case Frequency: return formatHz(r.hz, m_resolution);
    case Level: return level(r.dBm, false);
    case DeltaFrequency: return delta ? (r.dHz >= 0 ? "+" : "") + formatHz(r.dHz, m_resolution) : QString();
    case DeltaLevel: return delta ? level(r.dDb, true) : QString();
    }
    return {};
}

QVariant MarkerModel::headerData(int section, Qt::Orientation o, int role) const
{
    static const char *kNames[Columns] = {"Marker", "Frequency", "Level", "Δ Frequency", "Δ Level"};
    if (o == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 && section < Columns)
        return QString::fromUtf8(kNames[section]);
    return {};
}

Qt::ItemFlags MarkerModel::flags(const QModelIndex &index) const
{
    const Qt::ItemFlags f = QAbstractTableModel::flags(index);
    return index.column() == Frequency ? f | Qt::ItemIsEditable : f;
}

bool MarkerModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || index.column() != Frequency)
        return false;
    const auto hz = parseHz(value.toString(), 1e6);
    if (!hz)
        return false;
    emit frequencyEdited(index.row(), *hz);
    return true;
}
