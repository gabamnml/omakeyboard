#pragma once

#include "LuaScanner.h"
#include "Types.h"

#include <QString>
#include <QStringList>

namespace omakeyboard {

// A binding that exists on the system but was not created by OmaKeyboard: an
// Omarchy default, or something the user wrote by hand in bindings.lua.
struct ExistingBinding {
    QString keys;
    QString normalizedKeys;  // canonical form for comparison
    QString description;
    QString action;
    bool fromOmaKeyboard = false;
};

struct ParsedState {
    bool ok = false;
    QString error;

    RuleSet ruleSet;                      // what OmaKeyboard itself owns
    QStringList ruleWarnings;             // fields dropped while loading

    QVector<ExistingBinding> bindings;    // everything currently bound
    QStringList declaredUnbinds;

    bool hyprlandBlockPresent = false;
    bool loaderPresent = false;
    bool rulesFilePresent = false;
};

// Reads the world as it is: the app's own rules, plus the bindings already in
// effect. Never writes.
class OmarchyConfigParser {
public:
    ParsedState load() const;

    // Read just the app's rules, without scanning Lua. Cheap, and what the
    // tests exercise.
    RuleSet loadRuleSet(QStringList *warnings = nullptr, bool *present = nullptr) const;

    // Bindings currently in effect, recovered by evaluating the Lua config in a
    // sandbox. Falls back to an empty list when lua is unavailable.
    QVector<ExistingBinding> loadExistingBindings(QString *error = nullptr) const;

    // "super+shift+e" for "SUPER + SHIFT + E": modifiers sorted, case folded,
    // whitespace removed, so two spellings of one combination compare equal.
    static QString normalizeKeys(const QString &keys);
    static QStringList splitKeys(const QString &keys);
    static bool isModifierToken(const QString &token);

private:
    QVector<ExistingBinding> fromScan(const ScanResult &scan) const;
};

} // namespace omakeyboard
