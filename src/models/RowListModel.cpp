#include "RowListModel.h"

#include <QSet>

namespace omakeyboard {

namespace {

QString idOf(const QVariant &row)
{
    return row.toMap().value(QStringLiteral("id")).toString();
}

} // namespace

int RowListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariantMap RowListModel::rowAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return m_rows.at(row).toMap();
}

void RowListModel::replaceRows(const QVariantList &rows, bool reset)
{
    const int before = static_cast<int>(m_rows.size());

    // Ids must be unique for a diff to mean anything; if they are not, fall
    // back to the honest option.
    QSet<QString> incoming;
    for (const QVariant &row : rows)
        incoming.insert(idOf(row));
    if (reset || incoming.size() != rows.size()) {
        beginResetModel();
        m_rows = rows;
        endResetModel();
        if (before != m_rows.size())
            emit countChanged();
        return;
    }

    // 1. Rows that are gone, from the end so indices stay valid.
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
        if (incoming.contains(idOf(m_rows.at(i))))
            continue;
        beginRemoveRows({}, i, i);
        m_rows.removeAt(i);
        endRemoveRows();
    }

    // 2. Walk the new order: each position is either already right, a row
    // that moved up from further down, or a new row.
    for (int i = 0; i < rows.size(); ++i) {
        const QString id = idOf(rows.at(i));

        if (i < m_rows.size() && idOf(m_rows.at(i)) == id) {
            if (m_rows.at(i) != rows.at(i)) {
                m_rows[i] = rows.at(i);
                emit dataChanged(index(i), index(i));
            }
            continue;
        }

        int found = -1;
        for (int j = i + 1; j < m_rows.size(); ++j) {
            if (idOf(m_rows.at(j)) == id) {
                found = j;
                break;
            }
        }

        if (found >= 0) {
            beginMoveRows({}, found, found, {}, i);
            m_rows.move(found, i);
            endMoveRows();
            if (m_rows.at(i) != rows.at(i)) {
                m_rows[i] = rows.at(i);
                emit dataChanged(index(i), index(i));
            }
        } else {
            beginInsertRows({}, i, i);
            m_rows.insert(i, rows.at(i));
            endInsertRows();
        }
    }

    if (before != m_rows.size())
        emit countChanged();
}

} // namespace omakeyboard
