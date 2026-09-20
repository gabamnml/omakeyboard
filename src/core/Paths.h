#pragma once

#include <QString>
#include <QStringList>

namespace omakeyboard {

// Every path the app reads or writes, in one place, so an audit of the
// filesystem surface is a single file. Honours XDG_* the same way Omarchy's
// default/hypr/paths.lua does: a set-but-empty variable counts as unset.
class Paths {
public:
    static QString home();
    static QString configHome();  // ~/.config
    static QString stateHome();   // ~/.local/state
    static QString omarchyPath(); // /usr/share/omarchy

    // --- written by OmaKeyboard ---
    static QString appConfigDir();      // ~/.config/omakeyboard
    static QString rulesFile();         // ~/.config/omakeyboard/rules.json
    static QString generatedLuaFile();  // ~/.config/omakeyboard/generated.lua
    static QString loaderLuaFile();     // ~/.config/hypr/omakeyboard.lua
    static QString keymapDir();         // ~/.local/state/omakeyboard/keymaps
    static QString keymapFile(const QString &profileId);
    static QString backupDir();         // ~/.local/state/omakeyboard/backups

    // --- read only ---
    static QString hyprlandLuaFile();   // ~/.config/hypr/hyprland.lua
    static QString hyprUserDir();       // ~/.config/hypr
    static QString omarchyThemeColors();// ~/.local/state/omarchy/current/theme/colors.toml
    static QString omarchyCurrentDir(); // ~/.local/state/omarchy/current
    static QString xkbRulesLst();       // /usr/share/X11/xkb/rules/base.lst
    static QString xkbKeycodesEvdev();  // /usr/share/X11/xkb/keycodes/evdev
    static QStringList desktopAppDirs();

    // The Lua module name the loader is required under, matching the
    // package.path that Omarchy's bootstrap.lua installs.
    static QString loaderModuleName();      // "hypr.omakeyboard"
    static QString generatedModuleName();   // "omakeyboard.generated"


    static bool ensureDir(const QString &path);

    // Write via a temporary file in the same directory, fsync, then rename(2),
    // so a reader never sees a half-written config.
    static bool writeAtomically(const QString &path, const QByteArray &contents,
                                QString *error = nullptr);
    static QByteArray read(const QString &path, bool *ok = nullptr);

private:
    static QString envOr(const char *name, const QString &fallback);
};

} // namespace omakeyboard
