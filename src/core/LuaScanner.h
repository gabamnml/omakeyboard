#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace omakeyboard {

// One binding recovered from the Lua configuration by evaluating it in a
// sandbox. `hyprctl binds` reports Lua binds as dispatcher "__lua" with an
// opaque numeric argument, so the config itself is the only place the action
// can be read from. Omarchy's own keybindings menu works exactly this way.
struct ScannedBind {
    QString keys;          // "SUPER + SHIFT + E" as written in the config
    QString description;
    QString kind;          // "exec" | "lua" | "" 
    QString argument;      // the command, or the Lua expression
    QString source;        // which module defined it, when known
};

struct ScanResult {
    bool ok = false;
    QVector<ScannedBind> binds;
    QStringList unbinds;   // keys the config explicitly releases
    QString error;
};

// Evaluates Hyprland's Lua configuration with stubbed `hl` and `o` globals.
//
// Two jobs:
//   * read the bindings that already exist, so conflicts can be detected
//     against Omarchy's defaults and the user's own overrides;
//   * dry-run the configuration after a write, catching an error before
//     Hyprland is ever asked to load it.
//
// Nothing here runs as part of the compositor: it is a plain `lua` process with
// no access to hl's real API, so evaluating the config cannot change the
// running session.
class LuaScanner {
public:
    static bool isAvailable();

    // Scan the user's live configuration (~/.config/hypr/hyprland.lua).
    ScanResult scanConfig() const;
    ScanResult scanConfigFile(const QString &path) const;

    // `luac -p`: syntax only, no execution. The cheapest possible gate.
    bool checkSyntax(const QString &path, QString *error) const;

    // Load the whole configuration in the sandbox and report whether it threw.
    // This is the step that keeps a broken generated file away from Hyprland.
    bool dryRun(const QString &configPath, QString *error) const;

private:
    static QString sandboxScript();
};

} // namespace omakeyboard
