#pragma once

#include "Types.h"

#include <QString>
#include <QVector>

namespace omakeyboard {

struct ActionEntry {
    QString id;            // stable identifier for rules.json
    QString label;         // "Files", "Volume up"
    QString category;      // groups the searchable list
    ActionKind kind = ActionKind::Command;
    QString value;         // what goes into the generated o.bind call
    QString detail;        // shown as secondary text; the command, url, …
    QString icon;          // .desktop Icon= when known
};

// Everything a shortcut can be pointed at. Two sources, both native:
//   * Omarchy's own launchers and commands, discovered by listing
//     $OMARCHY_PATH/bin rather than hardcoding a list that would rot;
//   * installed applications, read from their .desktop files.
class ActionCatalog {
public:
    static ActionCatalog &instance();

    void load();
    void reloadApplications();
    bool isLoaded() const { return m_loaded; }

    const QVector<ActionEntry> &actions() const { return m_actions; }
    QVector<ActionEntry> byCategory(const QString &category) const;
    QStringList categories() const;
    const ActionEntry *find(const QString &id) const;

private:
    ActionCatalog() = default;
    void loadOmarchyLaunchers();
    void loadBuiltinActions();
    void loadDesktopApplications();

    bool m_loaded = false;
    QVector<ActionEntry> m_actions;
};

} // namespace omakeyboard
