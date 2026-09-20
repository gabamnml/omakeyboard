#include "OmarchyConfigParser.h"

#include "Paths.h"
#include "RuleSet.h"

#include <QFile>

namespace omakeyboard {

bool OmarchyConfigParser::isModifierToken(const QString &token)
{
    // The modifier names Hyprland accepts, matching the table
    // omarchy-menu-keybindings uses to turn a modmask back into text.
    static const QStringList modifiers{
        QStringLiteral("SUPER"), QStringLiteral("SHIFT"), QStringLiteral("CTRL"),
        QStringLiteral("CONTROL"), QStringLiteral("ALT"), QStringLiteral("MOD1"),
        QStringLiteral("MOD2"), QStringLiteral("MOD3"), QStringLiteral("MOD4"),
        QStringLiteral("MOD5"), QStringLiteral("CAPS"), QStringLiteral("MOD"),
    };
    return modifiers.contains(token.toUpper());
}

QStringList OmarchyConfigParser::splitKeys(const QString &keys)
{
    QStringList parts;
    const QStringList raw = keys.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const QString &token : raw) {
        const QString trimmed = token.trimmed();
        if (!trimmed.isEmpty())
            parts.append(trimmed);
    }
    return parts;
}

QString OmarchyConfigParser::normalizeKeys(const QString &keys)
{
    const QStringList parts = splitKeys(keys);
    if (parts.isEmpty())
        return {};

    QStringList modifiers;
    QStringList rest;
    for (const QString &part : parts) {
        if (isModifierToken(part)) {
            // CONTROL and CTRL are the same modifier; fold them together so a
            // conflict is detected regardless of spelling.
            QString name = part.toUpper();
            if (name == QStringLiteral("CONTROL"))
                name = QStringLiteral("CTRL");
            if (!modifiers.contains(name))
                modifiers.append(name);
        } else {
            rest.append(part);
        }
    }

    modifiers.sort();

    // The key itself keeps its case folded but not reordered: "code:20" and
    // "XF86AudioPlay" are single tokens.
    QStringList key;
    for (const QString &part : rest)
        key.append(part.toLower());

    return (modifiers.join(QLatin1Char('+')) + QLatin1Char('|') + key.join(QLatin1Char('+')))
        .toLower();
}

RuleSet OmarchyConfigParser::loadRuleSet(QStringList *warnings, bool *present) const
{
    bool read = false;
    const QByteArray raw = Paths::read(Paths::rulesFile(), &read);
    if (present)
        *present = read;
    if (!read) {
        // A first run is not an error: start from an empty set.
        RuleSet empty;
        return empty;
    }

    QString error;
    RuleSet ruleSet = RuleSetSerializer::fromJsonBytes(raw, warnings, &error);
    if (!error.isEmpty() && warnings)
        warnings->append(QStringLiteral("rules.json could not be parsed: %1").arg(error));
    return ruleSet;
}

QVector<ExistingBinding> OmarchyConfigParser::fromScan(const ScanResult &scan) const
{
    QVector<ExistingBinding> bindings;
    for (const ScannedBind &bind : scan.binds) {
        ExistingBinding entry;
        entry.keys = bind.keys;
        entry.normalizedKeys = normalizeKeys(bind.keys);
        entry.description = bind.description;
        entry.action = bind.argument.isEmpty() ? bind.kind : bind.argument;
        bindings.append(entry);
    }
    return bindings;
}

QVector<ExistingBinding> OmarchyConfigParser::loadExistingBindings(QString *error) const
{
    LuaScanner scanner;
    const ScanResult scan = scanner.scanConfig();

    // A scan that threw part way through still returns the bindings it reached,
    // which is better than pretending nothing is bound.
    if (!scan.ok && error)
        *error = scan.error;

    return fromScan(scan);
}

ParsedState OmarchyConfigParser::load() const
{
    ParsedState state;

    state.ruleSet = loadRuleSet(&state.ruleWarnings, &state.rulesFilePresent);

    QString scanError;
    LuaScanner scanner;
    const ScanResult scan = scanner.scanConfig();
    state.bindings = fromScan(scan);
    state.declaredUnbinds = scan.unbinds;
    if (!scan.ok)
        state.error = scan.error;

    // Mark which of the live bindings OmaKeyboard is responsible for, so the UI can
    // separate "yours" from "Omarchy's".
    for (ExistingBinding &binding : state.bindings) {
        for (const Shortcut &shortcut : state.ruleSet.shortcuts) {
            if (normalizeKeys(shortcut.keys) == binding.normalizedKeys) {
                binding.fromOmaKeyboard = true;
                break;
            }
        }
    }

    state.loaderPresent = QFile::exists(Paths::loaderLuaFile());

    bool read = false;
    const QByteArray hyprland = Paths::read(Paths::hyprlandLuaFile(), &read);
    if (read) {
        state.hyprlandBlockPresent =
            QString::fromUtf8(hyprland).contains(QStringLiteral("OmaKeyboard BEGIN"));
    }

    state.ok = true;
    return state;
}

} // namespace omakeyboard
