#pragma once

#include "RowListModel.h"

namespace omakeyboard {

class ConfigRepository;

// The mapping rows for one profile. A thin projection of the repository: the
// model holds no truth of its own, so there is exactly one place a mapping can
// be changed and the view can never drift from the engine.
class KeyMappingModel : public RowListModel {
    Q_OBJECT

    Q_PROPERTY(QString profileId READ profileId WRITE setProfileId NOTIFY profileIdChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        KindRole,
        KindLabelRole,
        EnabledRole,
        SourceKeyRole,
        SourceLabelRole,
        TargetLabelRole,
        LevelsRole,
        LayerRole,
        XkbOptionRole,
        NoteRole,
        SupportedRole,
        LimitRole,
    };

    explicit KeyMappingModel(QObject *parent = nullptr);

    void setRepository(ConfigRepository *repository);

    QString profileId() const { return m_profileId; }
    void setProfileId(const QString &profileId);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

public slots:
    void refresh();

signals:
    void profileIdChanged();

private:
    ConfigRepository *m_repository = nullptr;
    QString m_profileId;
    bool m_resetNext = true;
};

} // namespace omakeyboard
