#include "Paths.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>

namespace omakeyboard {

QString Paths::envOr(const char *name, const QString &fallback)
{
    const QString value = qEnvironmentVariable(name);
    return value.isEmpty() ? fallback : value;
}

QString Paths::home() { return QDir::homePath(); }

QString Paths::configHome()
{
    return envOr("XDG_CONFIG_HOME", home() + QStringLiteral("/.config"));
}

QString Paths::stateHome()
{
    return envOr("XDG_STATE_HOME", home() + QStringLiteral("/.local/state"));
}

QString Paths::omarchyPath()
{
    return envOr("OMARCHY_PATH", QStringLiteral("/usr/share/omarchy"));
}

QString Paths::appConfigDir() { return configHome() + QStringLiteral("/omakeyboard"); }
QString Paths::rulesFile() { return appConfigDir() + QStringLiteral("/rules.json"); }
QString Paths::generatedLuaFile() { return appConfigDir() + QStringLiteral("/generated.lua"); }
QString Paths::loaderLuaFile() { return hyprUserDir() + QStringLiteral("/omakeyboard.lua"); }
QString Paths::keymapDir() { return stateHome() + QStringLiteral("/omakeyboard/keymaps"); }

QString Paths::keymapFile(const QString &profileId)
{
    return keymapDir() + QStringLiteral("/") + profileId + QStringLiteral(".xkb");
}

// Backups live under ~/.local/state, never as .bak siblings inside ~/.config:
// the config directory stays something a user can read at a glance.
QString Paths::backupDir() { return stateHome() + QStringLiteral("/omakeyboard/backups"); }

QString Paths::hyprUserDir() { return configHome() + QStringLiteral("/hypr"); }
QString Paths::hyprlandLuaFile() { return hyprUserDir() + QStringLiteral("/hyprland.lua"); }

QString Paths::omarchyCurrentDir()
{
    return stateHome() + QStringLiteral("/omarchy/current");
}

QString Paths::omarchyThemeColors()
{
    return omarchyCurrentDir() + QStringLiteral("/theme/colors.toml");
}

QString Paths::xkbRulesLst()
{
    return QStringLiteral("/usr/share/X11/xkb/rules/base.lst");
}

QString Paths::xkbKeycodesEvdev()
{
    return QStringLiteral("/usr/share/X11/xkb/keycodes/evdev");
}

QStringList Paths::desktopAppDirs()
{
    QStringList dirs;
    const QString dataHome = envOr("XDG_DATA_HOME", home() + QStringLiteral("/.local/share"));
    dirs << dataHome + QStringLiteral("/applications");
    dirs << QStringLiteral("/usr/local/share/applications");
    dirs << QStringLiteral("/usr/share/applications");
    dirs << QStringLiteral("/var/lib/flatpak/exports/share/applications");
    dirs << dataHome + QStringLiteral("/flatpak/exports/share/applications");
    return dirs;
}

QString Paths::loaderModuleName() { return QStringLiteral("hypr.omakeyboard"); }
QString Paths::generatedModuleName() { return QStringLiteral("omakeyboard.generated"); }

bool Paths::ensureDir(const QString &path)
{
    return QDir().mkpath(path);
}

bool Paths::writeAtomically(const QString &path, const QByteArray &contents, QString *error)
{
    const QFileInfo info(path);
    if (!ensureDir(info.absolutePath())) {
        if (error)
            *error = QStringLiteral("Could not create %1").arg(info.absolutePath());
        return false;
    }

    // QSaveFile writes to a sibling temporary file and commits with rename(2),
    // which is atomic on the same filesystem.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    if (file.write(contents) != contents.size()) {
        if (error)
            *error = file.errorString();
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }

    return true;
}

QByteArray Paths::read(const QString &path, bool *ok)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return {};
    }
    if (ok)
        *ok = true;
    return file.readAll();
}

} // namespace omakeyboard
