#pragma once

#include <QHash>
#include <QString>
#include <QVector>

namespace omakeyboard {

struct XkbOption {
    QString name;        // "caps:escape"
    QString group;       // "caps"
    QString description; // "Make Caps Lock an additional Esc"
    // Physical keys this option claims, derived from the group and name. Used
    // by ConflictDetector: two options claiming Caps Lock cannot both apply.
    QStringList claimedKeys;
};

// The authoritative list of simple remaps, read from the system's XKB rules
// rather than hardcoded, so it never drifts from what libxkbcommon accepts.
class XkbOptionCatalog {
public:
    static XkbOptionCatalog &instance();

    void load();
    bool isLoaded() const { return m_loaded; }

    const QVector<XkbOption> &options() const { return m_options; }
    bool contains(const QString &name) const;
    const XkbOption *find(const QString &name) const;

    // Options whose group is one of the remapping groups a user cares about,
    // in the order the UI should show them.
    QVector<XkbOption> remappingOptions() const;

    static QStringList remappingGroups();
    static QString groupLabel(const QString &group);

private:
    XkbOptionCatalog() = default;
    static QStringList claimedKeysFor(const QString &group, const QString &name);

    bool m_loaded = false;
    QVector<XkbOption> m_options;
    QHash<QString, int> m_index;
};

} // namespace omakeyboard
