#include "ActionCatalogModel.h"

#include "../core/ActionCatalog.h"
#include "../core/Types.h"
#include "../core/KeyCatalog.h"
#include "../core/XkbOptionCatalog.h"

namespace omakeyboard {

ActionCatalogModel::ActionCatalogModel(QObject *parent)
    : QAbstractListModel(parent)
{
    rebuild();
}

void ActionCatalogModel::setSource(const QString &source)
{
    if (m_source == source)
        return;
    m_source = source;
    emit sourceChanged();
    rebuild();
}

void ActionCatalogModel::setForKey(const QString &code)
{
    if (m_forKey == code)
        return;
    m_forKey = code;
    emit forKeyChanged();
    if (m_source == QStringLiteral("targets"))
        rebuild();
}

void ActionCatalogModel::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    emit queryChanged();
    applyFilter();
}

void ActionCatalogModel::setCategory(const QString &category)
{
    if (m_category == category)
        return;
    m_category = category;
    emit categoryChanged();
    applyFilter();
}

void ActionCatalogModel::rebuild()
{
    m_all.clear();
    m_categories.clear();

    const auto append = [this](const QString &id, const QString &label, const QString &detail,
                               const QString &category, const QString &value,
                               const QString &kind) {
        m_all.append(QVariantMap{
            { QStringLiteral("id"), id },
            { QStringLiteral("label"), label },
            { QStringLiteral("detail"), detail },
            { QStringLiteral("category"), category },
            { QStringLiteral("value"), value },
            { QStringLiteral("kind"), kind },
        });
        if (!category.isEmpty() && !m_categories.contains(category))
            m_categories.append(category);
    };

    if (m_source == QStringLiteral("actions")) {
        ActionCatalog::instance().load();
        for (const ActionEntry &entry : ActionCatalog::instance().actions())
            append(entry.id, entry.label, entry.detail, entry.category, entry.value,
                   actionKindName(entry.kind));
    } else if (m_source == QStringLiteral("keys")) {
        KeyCatalog::instance().load();
        for (const KeyEntry &entry : KeyCatalog::instance().keys()) {
            append(entry.code, entry.label,
                   QStringLiteral("XKB <%1>, keycode %2").arg(entry.code).arg(entry.keycode),
                   entry.section, entry.code, QStringLiteral("key"));
        }
    } else if (m_source == QStringLiteral("keysyms")) {
        KeyCatalog::instance().load();
        for (const KeysymEntry &entry : KeyCatalog::instance().keysyms())
            append(entry.keysym, entry.label, entry.keysym, entry.category, entry.keysym,
                   QStringLiteral("keysym"));
    } else if (m_source == QStringLiteral("targets")) {
        buildTargets();
    } else if (m_source == QStringLiteral("xkbOptions")) {
        XkbOptionCatalog::instance().load();
        for (const XkbOption &option : XkbOptionCatalog::instance().remappingOptions()) {
            append(option.name, option.description, option.name,
                   XkbOptionCatalog::groupLabel(option.group), option.name,
                   QStringLiteral("xkbOption"));
        }
    }

    applyFilter();
}

// One list of outcomes, phrased as what the key will do rather than as the
// mechanism that gets it done. Each entry carries the mapping kind the engine
// should use, so the interface never has to ask the user to pick one.
void ActionCatalogModel::buildTargets()
{
    KeyCatalog::instance().load();
    XkbOptionCatalog::instance().load();

    const auto append = [this](const QString &id, const QString &label, const QString &detail,
                               const QString &category, const QString &value,
                               const QString &kind) {
        m_all.append(QVariantMap{
            { QStringLiteral("id"), id },
            { QStringLiteral("label"), label },
            { QStringLiteral("detail"), detail },
            { QStringLiteral("category"), category },
            { QStringLiteral("value"), value },
            { QStringLiteral("kind"), kind },
        });
        if (!m_categories.contains(category))
            m_categories.append(category);
    };

    // Groups in the order a user is likely to want them.
    const QString modifiers = QStringLiteral("Act as a modifier");
    const QString layers = QStringLiteral("Turn it into a layer key");
    const QString typing = QStringLiteral("Send another key");
    const QString media = QStringLiteral("Media and hardware");
    const QString other = QStringLiteral("Other");

    for (const KeysymEntry &entry : KeyCatalog::instance().keysyms()) {
        const QString &keysym = entry.keysym;

        if (keysym == QStringLiteral("ISO_Level3_Shift")) {
            append(keysym, QStringLiteral("Hold for a second set of keys"),
                   QStringLiteral("An Fn key. Afterwards you say what the other keys do "
                                  "while it is held."),
                   layers, keysym, QStringLiteral("layerChooser"));
            continue;
        }
        if (keysym == QStringLiteral("ISO_Level5_Shift")) {
            append(keysym, QStringLiteral("Hold for a third set of keys"),
                   QStringLiteral("A second, independent layer."),
                   layers, keysym, QStringLiteral("layerChooser"));
            continue;
        }
        if (keysym == QStringLiteral("Mode_switch"))
            continue;   // same idea as level 3, and more confusing

        if (keysym == QStringLiteral("NoSymbol") || keysym == QStringLiteral("VoidSymbol")) {
            if (keysym == QStringLiteral("NoSymbol")) {
                append(keysym, QStringLiteral("Do nothing"),
                       QStringLiteral("The key stops working entirely."),
                       other, keysym, QStringLiteral("keyRemap"));
            }
            continue;
        }

        // A modifier target has to be a modifier remap so the generated keymap
        // also puts the key in the right modifier map; anything else is a
        // plain key remap. The user never sees the difference.
        if (entry.category == QStringLiteral("Modifiers")) {
            append(keysym, entry.label, QString(), modifiers, keysym,
                   QStringLiteral("modifierRemap"));
        } else if (entry.category == QStringLiteral("Media")) {
            append(keysym, entry.label, keysym, media, keysym, QStringLiteral("keyRemap"));
        } else {
            append(keysym, entry.label, QString(), typing, keysym,
                   QStringLiteral("keyRemap"));
        }
    }

    // Ready-made behaviours, but only the ones that concern this key. Their
    // descriptions come from the system's own XKB rules, which are already
    // written for people ("Make Caps Lock an additional Esc").
    if (!m_forKey.isEmpty()) {
        const QString group = QStringLiteral("Ready-made for this key");
        for (const XkbOption &option : XkbOptionCatalog::instance().remappingOptions()) {
            if (!option.claimedKeys.contains(m_forKey))
                continue;
            append(QStringLiteral("option:") + option.name, option.description, option.name,
                   group, option.name, QStringLiteral("xkbOption"));
        }
    }
}

void ActionCatalogModel::applyFilter()
{
    beginResetModel();
    m_rows.clear();

    // Match on every visible field, so searching "brightness", "XF86" or
    // "caps" all find what the user means.
    const QString needle = m_query.trimmed();
    for (const QVariant &value : m_all) {
        const QVariantMap row = value.toMap();

        if (!m_category.isEmpty()
            && row.value(QStringLiteral("category")).toString() != m_category) {
            continue;
        }

        if (!needle.isEmpty()) {
            const QString haystack = row.value(QStringLiteral("label")).toString()
                + QLatin1Char(' ') + row.value(QStringLiteral("detail")).toString()
                + QLatin1Char(' ') + row.value(QStringLiteral("category")).toString()
                + QLatin1Char(' ') + row.value(QStringLiteral("value")).toString();
            if (!haystack.contains(needle, Qt::CaseInsensitive))
                continue;
        }

        m_rows.append(row);
    }

    endResetModel();
    emit countChanged();
}

int ActionCatalogModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant ActionCatalogModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const QVariantMap row = m_rows.at(index.row()).toMap();

    switch (role) {
    case IdRole:       return row.value(QStringLiteral("id"));
    case LabelRole:    return row.value(QStringLiteral("label"));
    case DetailRole:   return row.value(QStringLiteral("detail"));
    case CategoryRole: return row.value(QStringLiteral("category"));
    case ValueRole:    return row.value(QStringLiteral("value"));
    case KindRole:     return row.value(QStringLiteral("kind"));
    default:           return {};
    }
}

QHash<int, QByteArray> ActionCatalogModel::roleNames() const
{
    return {
        { IdRole, "entryId" },
        { LabelRole, "label" },
        { DetailRole, "detail" },
        { CategoryRole, "category" },
        { ValueRole, "value" },
        { KindRole, "kind" },
    };
}

QVariantMap ActionCatalogModel::at(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return m_rows.at(row).toMap();
}

QVariantMap ActionCatalogModel::entry(const QString &id) const
{
    for (const QVariant &value : m_all) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("id")).toString() == id)
            return row;
    }
    return {};
}

} // namespace omakeyboard
