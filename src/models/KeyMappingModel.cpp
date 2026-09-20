#include "KeyMappingModel.h"

#include "../core/ConfigRepository.h"

namespace omakeyboard {

KeyMappingModel::KeyMappingModel(QObject *parent)
    : RowListModel(parent)
{
}

void KeyMappingModel::setRepository(ConfigRepository *repository)
{
    if (m_repository == repository)
        return;

    if (m_repository)
        disconnect(m_repository, nullptr, this, nullptr);

    m_repository = repository;

    if (m_repository) {
        connect(m_repository, &ConfigRepository::mappingsChanged, this,
                [this](const QString &profileId) {
                    // A change to another profile cannot affect these rows.
                    if (profileId.isEmpty() || profileId == m_profileId)
                        refresh();
                });
        connect(m_repository, &ConfigRepository::stateReloaded, this,
                &KeyMappingModel::refresh);
    }

    refresh();
}

void KeyMappingModel::setProfileId(const QString &profileId)
{
    if (m_profileId == profileId)
        return;
    m_profileId = profileId;
    emit profileIdChanged();
    // Another keyboard is another list, not an edit of this one.
    m_resetNext = true;
    refresh();
}

void KeyMappingModel::refresh()
{
    const bool reset = m_resetNext;
    m_resetNext = false;
    replaceRows(m_repository && !m_profileId.isEmpty()
                    ? m_repository->mappingList(m_profileId)
                    : QVariantList(),
                reset);
}

QVariant KeyMappingModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return {};

    const QVariantMap row = rowAt(index.row());

    switch (role) {
    case IdRole:          return row.value(QStringLiteral("id"));
    case KindRole:        return row.value(QStringLiteral("kind"));
    case KindLabelRole:   return row.value(QStringLiteral("kindLabel"));
    case EnabledRole:     return row.value(QStringLiteral("enabled"));
    case SourceKeyRole:   return row.value(QStringLiteral("sourceKey"));
    case SourceLabelRole: return row.value(QStringLiteral("sourceLabel"));
    case TargetLabelRole: return row.value(QStringLiteral("targetLabel"));
    case LevelsRole:      return row.value(QStringLiteral("levels"));
    case LayerRole:       return row.value(QStringLiteral("layer"));
    case XkbOptionRole:   return row.value(QStringLiteral("xkbOption"));
    case NoteRole:        return row.value(QStringLiteral("note"));
    case SupportedRole:   return row.value(QStringLiteral("supported"));
    case LimitRole:       return row.value(QStringLiteral("limit"));
    default:              return {};
    }
}

QHash<int, QByteArray> KeyMappingModel::roleNames() const
{
    return {
        { IdRole, "mappingId" },
        { KindRole, "kind" },
        { KindLabelRole, "kindLabel" },
        // "enabled" would shadow Item::enabled inside a delegate, which silently
        // changes whether the row accepts input. The role is spelled differently.
        { EnabledRole, "isEnabled" },
        { SourceKeyRole, "sourceKey" },
        { SourceLabelRole, "sourceLabel" },
        { TargetLabelRole, "targetLabel" },
        { LevelsRole, "levels" },
        { LayerRole, "layer" },
        { XkbOptionRole, "xkbOption" },
        { NoteRole, "note" },
        { SupportedRole, "supported" },
        { LimitRole, "limit" },
    };
}

} // namespace omakeyboard
