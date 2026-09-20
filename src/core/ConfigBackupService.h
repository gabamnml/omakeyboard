#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace omakeyboard {

struct BackupEntry {
    QString id;            // the directory name, a unix timestamp
    QString path;
    QDateTime created;
    QStringList files;     // basenames captured in this snapshot
    QString label;
};

// Snapshots of every file the app is about to touch, taken before each apply.
//
// Backups live under ~/.local/state/omakeyboard/backups, not as .bak siblings in
// ~/.config: the config directory stays readable, and a snapshot keeps the
// whole set of files consistent with each other rather than one file at a time.
class ConfigBackupService {
public:
    // Capture the current state of everything an apply may rewrite. Returns the
    // snapshot id, or an empty string on failure.
    QString createBackup(const QString &label, QString *error = nullptr) const;

    // Put every file in a snapshot back where it came from.
    bool restore(const QString &backupId, QString *error = nullptr) const;

    QVector<BackupEntry> backups() const;
    // Delete one snapshot. The id has to be a plain directory name: it comes
    // from the interface, and a path with ".." in it must never reach
    // removeRecursively.
    bool remove(const QString &backupId) const;
    // Delete every snapshot; returns how many went.
    int removeAll() const;

    static bool isValidBackupId(const QString &backupId);

    // Keep the most recent `keep` snapshots and delete the rest.
    int prune(int keep) const;

    static QStringList trackedFiles();

private:
    static QString manifestName();
};

} // namespace omakeyboard
