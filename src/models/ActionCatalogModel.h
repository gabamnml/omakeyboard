#pragma once

#include <QAbstractListModel>
#include <QVariantList>

namespace omakeyboard {

// A searchable catalog, used for every picker in the app.
//
// One model with a `source` switch rather than four near-identical ones: the
// pickers for actions, physical keys, remap targets and keyboard options all
// need the same behaviour (a label, a detail line, a category, and a filter),
// and they all read from catalogs derived from the system.
class ActionCatalogModel : public QAbstractListModel {
    Q_OBJECT

    // "actions" | "keys" | "keysyms" | "xkbOptions" | "targets"
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    // For source "targets": the physical key being remapped. Ready-made
    // behaviours are key-specific -- caps:escape only means anything for Caps
    // Lock -- so the list only offers the ones that apply to this key.
    Q_PROPERTY(QString forKey READ forKey WRITE setForKey NOTIFY forKeyChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(QStringList categories READ categories NOTIFY sourceChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        LabelRole,
        DetailRole,
        CategoryRole,
        ValueRole,
        KindRole,
    };

    explicit ActionCatalogModel(QObject *parent = nullptr);

    QString source() const { return m_source; }
    void setSource(const QString &source);

    QString forKey() const { return m_forKey; }
    void setForKey(const QString &code);

    QString query() const { return m_query; }
    void setQuery(const QString &query);

    QString category() const { return m_category; }
    void setCategory(const QString &category);

    QStringList categories() const { return m_categories; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Look a row up by its id without walking the model from QML.
    Q_INVOKABLE QVariantMap entry(const QString &id) const;
    // The visible row at this index, for keyboard selection in a picker.
    Q_INVOKABLE QVariantMap at(int row) const;

signals:
    void sourceChanged();
    void forKeyChanged();
    void queryChanged();
    void categoryChanged();
    void countChanged();

private:
    void rebuild();
    void buildTargets();
    void applyFilter();

    QString m_source = QStringLiteral("actions");
    QString m_forKey;
    QString m_query;
    QString m_category;
    QStringList m_categories;
    QVariantList m_all;
    QVariantList m_rows;
};

} // namespace omakeyboard
