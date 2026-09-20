#include "ConfigBackupService.h"

#include "Paths.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace omakeyboard {

QString ConfigBackupService::manifestName()
{
    return QStringLiteral("manifest.json");
}

QStringList ConfigBackupService::trackedFiles()
{
    QStringList files{
        Paths::rulesFile(),
        Paths::generatedLuaFile(),
        Paths::loaderLuaFile(),
        Paths::hyprlandLuaFile(),
    };

    // Generated keymaps are part of the state an apply replaces, so a rollback
    // has to restore the exact ones that were in effect.
    QDir keymaps(Paths::keymapDir());
    if (keymaps.exists()) {
        const QStringList entries =
            keymaps.entryList({ QStringLiteral("*.xkb") }, QDir::Files, QDir::Name);
        for (const QString &entry : entries)
            files.append(keymaps.filePath(entry));
    }

    return files;
}

QString ConfigBackupService::createBackup(const QString &label, QString *error) const
{
    const QDateTime now = QDateTime::currentDateTime();
    const QString id = QString::number(now.toSecsSinceEpoch());
    const QString directory = Paths::backupDir() + QLatin1Char('/') + id;

    if (!Paths::ensureDir(directory)) {
        if (error)
            *error = QStringLiteral("Could not create %1").arg(directory);
        return {};
    }

    QJsonArray captured;
    for (const QString &source : trackedFiles()) {
        const QFileInfo info(source);
        if (!info.exists())
            continue;

        // Flatten into the snapshot directory, recording the original path in
        // the manifest so restore knows where each file belongs.
        const QString target = directory + QLatin1Char('/') + info.fileName();
        QFile::remove(target);
        if (!QFile::copy(source, target)) {
            if (error)
                *error = QStringLiteral("Could not back up %1").arg(source);
            return {};
        }

        captured.append(QJsonObject{
            { QStringLiteral("original"), source },
            { QStringLiteral("stored"), info.fileName() },
        });
    }

    const QJsonObject manifest{
        { QStringLiteral("id"), id },
        { QStringLiteral("created"), now.toString(Qt::ISODate) },
        { QStringLiteral("label"), label },
        { QStringLiteral("files"), captured },
    };

    if (!Paths::writeAtomically(directory + QLatin1Char('/') + manifestName(),
                                QJsonDocument(manifest).toJson(QJsonDocument::Indented),
                                error)) {
        return {};
    }

    return id;
}

bool ConfigBackupService::restore(const QString &backupId, QString *error) const
{
    const QString directory = Paths::backupDir() + QLatin1Char('/') + backupId;
    bool read = false;
    const QByteArray raw = Paths::read(directory + QLatin1Char('/') + manifestName(), &read);
    if (!read) {
        if (error)
            *error = QStringLiteral("Backup %1 has no manifest").arg(backupId);
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(raw);
    if (!document.isObject()) {
        if (error)
            *error = QStringLiteral("Backup %1 has an unreadable manifest").arg(backupId);
        return false;
    }

    QStringList restored;
    const QJsonArray files = document.object().value(QStringLiteral("files")).toArray();
    for (const QJsonValue &value : files) {
        const QJsonObject entry = value.toObject();
        const QString original = entry.value(QStringLiteral("original")).toString();
        const QString stored = entry.value(QStringLiteral("stored")).toString();
        if (original.isEmpty() || stored.isEmpty())
            continue;

        bool ok = false;
        const QByteArray contents = Paths::read(directory + QLatin1Char('/') + stored, &ok);
        if (!ok) {
            if (error)
                *error = QStringLiteral("Backup %1 is missing %2").arg(backupId, stored);
            return false;
        }
        if (!Paths::writeAtomically(original, contents, error))
            return false;
        restored.append(original);
    }

    // Files the snapshot did not contain were created by the apply being rolled
    // back, so they are removed rather than left behind. Only files the app
    // owns are eligible: a user file simply absent at snapshot time is left be.
    for (const QString &current : trackedFiles()) {
        if (restored.contains(current))
            continue;
        if (current.startsWith(Paths::appConfigDir())
            || current.startsWith(Paths::keymapDir())
            || current == Paths::loaderLuaFile()) {
            QFile::remove(current);
        }
    }

    return true;
}

QVector<BackupEntry> ConfigBackupService::backups() const
{
    QVector<BackupEntry> result;

    QDir root(Paths::backupDir());
    if (!root.exists())
        return result;

    const QStringList directories =
        root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : directories) {
        const QString directory = root.filePath(name);
        bool read = false;
        const QByteArray raw = Paths::read(directory + QLatin1Char('/') + manifestName(), &read);
        if (!read)
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(raw);
        if (!document.isObject())
            continue;
        const QJsonObject manifest = document.object();

        BackupEntry entry;
        entry.id = name;
        entry.path = directory;
        entry.label = manifest.value(QStringLiteral("label")).toString();
        entry.created = QDateTime::fromString(
            manifest.value(QStringLiteral("created")).toString(), Qt::ISODate);
        const QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
        for (const QJsonValue &value : files)
            entry.files.append(value.toObject().value(QStringLiteral("stored")).toString());
        result.append(entry);
    }

    // Newest first: the id is a unix timestamp, so numeric order is time order.
    std::sort(result.begin(), result.end(), [](const BackupEntry &a, const BackupEntry &b) {
        return a.id.toLongLong() > b.id.toLongLong();
    });

    return result;
}

bool ConfigBackupService::isValidBackupId(const QString &backupId)
{
    if (backupId.isEmpty() || backupId == QStringLiteral(".") || backupId == QStringLiteral(".."))
        return false;
    if (backupId.contains(QLatin1Char('/')) || backupId.contains(QLatin1Char('\\')))
        return false;
    return true;
}

int ConfigBackupService::removeAll() const
{
    int removed = 0;
    for (const BackupEntry &entry : backups())
        if (remove(entry.id))
            ++removed;
    return removed;
}

bool ConfigBackupService::remove(const QString &backupId) const
{
    if (!isValidBackupId(backupId))
        return false;
    QDir directory(Paths::backupDir() + QLatin1Char('/') + backupId);
    if (!directory.exists())
        return false;
    return directory.removeRecursively();
}

int ConfigBackupService::prune(int keep) const
{
    if (keep < 0)
        return 0;
    const QVector<BackupEntry> existing = backups();
    int removed = 0;
    for (int index = keep; index < existing.size(); ++index) {
        if (remove(existing.at(index).id))
            ++removed;
    }
    return removed;
}

} // namespace omakeyboard
