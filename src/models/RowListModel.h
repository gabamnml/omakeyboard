#pragma once

#include <QAbstractListModel>
#include <QVariantList>

namespace omakeyboard {

// A list model over rows that each carry an "id". Updates are applied as the
// smallest set of removals, moves, insertions and changes rather than a
// reset, so a view can animate one row arriving or leaving instead of the
// whole list flashing.
class RowListModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;

    // Rows as they are now; exposed for the tests.
    const QVariantList &rows() const { return m_rows; }

signals:
    void countChanged();

protected:
    // `reset` is for a change of subject (another keyboard), where rows
    // sliding in one by one would suggest an edit that did not happen.
    void replaceRows(const QVariantList &rows, bool reset = false);
    QVariantMap rowAt(int row) const;

private:
    QVariantList m_rows;
};

} // namespace omakeyboard
