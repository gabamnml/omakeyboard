#pragma once

#include "Types.h"

#include <QJsonObject>

namespace omakeyboard {

// Serialization for the app's own source of truth (~/.config/omakeyboard/rules.json).
// JSON on purpose: it is data the app parses, never something a Lua interpreter
// executes. The generated Lua is derived from this, never the reverse.
class RuleSetSerializer {
public:
    static QJsonObject toJson(const RuleSet &ruleSet);
    static QByteArray toJsonBytes(const RuleSet &ruleSet);

    // Tolerant by design: unknown fields are ignored and malformed entries are
    // dropped with a note, so a rules.json from a newer version degrades into
    // something usable instead of refusing to load.
    static RuleSet fromJson(const QJsonObject &object, QStringList *warnings = nullptr);
    static RuleSet fromJsonBytes(const QByteArray &bytes, QStringList *warnings = nullptr,
                                 QString *error = nullptr);

    static QString newId(const QString &prefix);
};

// Structural validation, independent of the filesystem and of Hyprland, so it
// is cheap to run on every edit and trivial to unit test.
class RuleSetValidator {
public:
    // Returns true when the rule set is safe to generate from. Errors that make
    // generation unsafe are reported as ConflictSeverity::Error.
    static bool validate(const RuleSet &ruleSet, QVector<Conflict> *issues);

    // A keysym has to look like an XKB keysym name before it can be pasted into
    // a keymap. Anything outside this grammar is rejected rather than escaped.
    static bool isValidKeysym(const QString &keysym);
    // XKB keycode name, without the angle brackets.
    static bool isValidKeycodeName(const QString &name);
    // "caps:escape" style option.
    static bool isValidXkbOption(const QString &option);
    // Omarchy's o.bind key spelling: "SUPER + SHIFT + E", "XF86AudioPlay",
    // "SUPER + code:20".
    static bool isValidBindKeys(const QString &keys);
    // hyprctl device names are lowercased and dash-joined by Hyprland.
    static bool isValidDeviceName(const QString &name);

    static QStringList knownBindFlags();
};

} // namespace omakeyboard
