#include "ShortcutModel.h"

#include "../core/ConfigRepository.h"

namespace omakeyboard {

ShortcutModel::ShortcutModel(QObject *parent)
    : RowListModel(parent)
{
}

void ShortcutModel::setRepository(ConfigRepository *repository)
{
    if (m_repository == repository)
        return;

    if (m_repository)
        disconnect(m_repository, nullptr, this, nullptr);

    m_repository = repository;

    if (m_repository) {
        connect(m_repository, &ConfigRepository::shortcutsChanged, this,
                &ShortcutModel::refresh);
        connect(m_repository, &ConfigRepository::stateReloaded, this, &ShortcutModel::refresh);
    }

    refresh();
}

void ShortcutModel::refresh()
{
    replaceRows(m_repository ? m_repository->shortcutList() : QVariantList());
}

QVariant ShortcutModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return {};

    const QVariantMap row = rowAt(index.row());

    switch (role) {
    case IdRole:          return row.value(QStringLiteral("id"));
    case EnabledRole:     return row.value(QStringLiteral("enabled"));
    case KeysRole:        return row.value(QStringLiteral("keys"));
    case DescriptionRole: return row.value(QStringLiteral("description"));
    case ActionKindRole:  return row.value(QStringLiteral("actionKind"));
    case ActionLabelRole: return row.value(QStringLiteral("actionLabel"));
    case ActionValueRole: return row.value(QStringLiteral("actionValue"));
    case FocusMatchRole:  return row.value(QStringLiteral("focusMatch"));
    case UnbindFirstRole: return row.value(QStringLiteral("unbindFirst"));
    default:              return {};
    }
}

QHash<int, QByteArray> ShortcutModel::roleNames() const
{
    return {
        { IdRole, "shortcutId" },
        // "enabled" would shadow Item::enabled inside a delegate, which silently
        // changes whether the row accepts input. The role is spelled differently.
        { EnabledRole, "isEnabled" },
        { KeysRole, "keys" },
        { DescriptionRole, "description" },
        { ActionKindRole, "actionKind" },
        { ActionLabelRole, "actionLabel" },
        { ActionValueRole, "actionValue" },
        { FocusMatchRole, "focusMatch" },
        { UnbindFirstRole, "unbindFirst" },
    };
}

} // namespace omakeyboard
