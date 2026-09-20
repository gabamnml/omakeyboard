#pragma once

#include "RowListModel.h"

namespace omakeyboard {

class ConfigRepository;

// The shortcut rows, projected from the repository the same way mappings are.
class ShortcutModel : public RowListModel {
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        EnabledRole,
        KeysRole,
        DescriptionRole,
        ActionKindRole,
        ActionLabelRole,
        ActionValueRole,
        FocusMatchRole,
        UnbindFirstRole,
    };

    explicit ShortcutModel(QObject *parent = nullptr);

    void setRepository(ConfigRepository *repository);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

public slots:
    void refresh();

private:
    ConfigRepository *m_repository = nullptr;
};

} // namespace omakeyboard
