#include "KeymapService.h"

#include "KeyCatalog.h"
#include "Process.h"
#include "RuleSet.h"

#include <QRegularExpression>
#include <QTemporaryFile>

namespace omakeyboard {

namespace {

constexpr const char *kXkbcli = "xkbcli";

// Emitting a modifier keysym is not enough: a key only *acts* as a modifier
// once it appears in a modifier_map for a real modifier. These pairings are
// taken from the stock XKB symbol files rather than guessed --
// symbols/ctrl("nocaps") is `replace key <CAPS> { [ Control_L ] }` plus
// `modifier_map Control { <CAPS> }`, and capslock("super") uses Mod4.
struct RealModifier { const char *keysym; const char *modifier; };

const RealModifier kRealModifiers[] = {
    { "Control_L", "Control" }, { "Control_R", "Control" },
    { "Shift_L", "Shift" },     { "Shift_R", "Shift" },
    { "Alt_L", "Mod1" },        { "Alt_R", "Mod1" },
    { "Meta_L", "Mod1" },       { "Meta_R", "Mod1" },
    { "Super_L", "Mod4" },      { "Super_R", "Mod4" },
    { "Hyper_L", "Mod3" },      { "Hyper_R", "Mod3" },
    { "Caps_Lock", "Lock" },
};

const char *realModifierFor(const QString &keysym)
{
    for (const auto &entry : kRealModifiers)
        if (keysym == QLatin1String(entry.keysym))
            return entry.modifier;
    return nullptr;
}

// Layer choosers are the exception: LevelThree and LevelFive are *virtual*
// modifiers, and `modifier_map LevelThree { <CAPS> }` is rejected outright
// ("Illegal modifier map definition ... non-modifier"). The binding comes from
// the compat section's interpret rules instead, which is why the stock
// level3("caps_switch") is a bare key definition with no modifier_map at all.
bool isLayerChooser(const QString &keysym)
{
    return keysym == QStringLiteral("ISO_Level3_Shift")
        || keysym == QStringLiteral("ISO_Level5_Shift")
        || keysym == QStringLiteral("Mode_switch");
}

// XKB type that exposes as many levels as the mapping actually uses. Asking for
// more levels than needed is harmless but noisy in the generated file.
QString typeForLevelCount(int levels)
{
    if (levels <= 1)
        return QStringLiteral("ONE_LEVEL");
    if (levels <= 2)
        return QStringLiteral("TWO_LEVEL");
    if (levels <= 4)
        return QStringLiteral("FOUR_LEVEL");
    return QStringLiteral("EIGHT_LEVEL");
}

} // namespace

bool KeymapService::isAvailable()
{
    return Process::isAvailable(QString::fromLatin1(kXkbcli));
}

QString KeymapService::optionsToString(const QStringList &options)
{
    QStringList valid;
    for (const QString &option : options) {
        if (RuleSetValidator::isValidXkbOption(option) && !valid.contains(option))
            valid.append(option);
    }
    return valid.join(QLatin1Char(','));
}

KccgstComponents KeymapService::resolveComponents(const Profile &profile,
                                                  const QString &fallbackLayout,
                                                  const QString &fallbackVariant,
                                                  const QString &fallbackModel,
                                                  const QStringList &fallbackOptions) const
{
    const QString layout = profile.layout.isEmpty()
        ? (fallbackLayout.isEmpty() ? QStringLiteral("us") : fallbackLayout)
        : profile.layout;
    const QString variant = profile.variant.isEmpty() ? fallbackVariant : profile.variant;
    const QString model = profile.model.isEmpty() ? fallbackModel : profile.model;

    // A profile's own options replace the inherited ones when it defines any:
    // the UI presents the option list as the complete set for that keyboard.
    const QStringList options =
        profile.xkbOptions.isEmpty() ? fallbackOptions : profile.xkbOptions;

    QStringList arguments{ QStringLiteral("compile-keymap"), QStringLiteral("--kccgst-yaml"),
                           QStringLiteral("--layout"), layout };
    if (!variant.isEmpty())
        arguments << QStringLiteral("--variant") << variant;
    if (!model.isEmpty())
        arguments << QStringLiteral("--model") << model;
    const QString optionString = optionsToString(options);
    if (!optionString.isEmpty())
        arguments << QStringLiteral("--options") << optionString;

    KccgstComponents components;
    const ProcessResult result =
        Process::run(QString::fromLatin1(kXkbcli), arguments);
    if (!result.ok())
        return components;

    // Output is flat YAML: `keycodes: "evdev+aliases(qwerty)"` per line.
    static const QRegularExpression line(
        QStringLiteral("^([a-z]+):\\s*\"?([^\"]*)\"?\\s*$"));
    const QStringList lines = result.stdOut.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &text : lines) {
        const auto match = line.match(text);
        if (!match.hasMatch())
            continue;
        const QString key = match.captured(1);
        const QString value = match.captured(2);
        if (key == QStringLiteral("keycodes"))
            components.keycodes = value;
        else if (key == QStringLiteral("types"))
            components.types = value;
        else if (key == QStringLiteral("compat"))
            components.compat = value;
        else if (key == QStringLiteral("symbols"))
            components.symbols = value;
        else if (key == QStringLiteral("geometry"))
            components.geometry = value;
    }

    return components;
}

QHash<QString, QStringList> KeymapService::baseSymbols(const KccgstComponents &components) const
{
    QHash<QString, QStringList> symbols;
    if (!components.isValid())
        return symbols;

    // Compile the untouched base keymap and read back what each key types, so
    // a layer mapping preserves levels 1 and 2 exactly.
    const QString source = QStringLiteral(
        "xkb_keymap {\n"
        "  xkb_keycodes { include \"%1\" };\n"
        "  xkb_types    { include \"%2\" };\n"
        "  xkb_compat   { include \"%3\" };\n"
        "  xkb_symbols  { include \"%4\" };\n"
        "};\n")
        .arg(components.keycodes,
             components.types.isEmpty() ? QStringLiteral("complete") : components.types,
             components.compat.isEmpty() ? QStringLiteral("complete") : components.compat,
             components.symbols);

    const ProcessResult result = Process::run(
        QString::fromLatin1(kXkbcli),
        { QStringLiteral("compile-keymap"), QStringLiteral("--keymap"), QStringLiteral("-") },
        8000, source.toUtf8());
    if (!result.ok())
        return symbols;

    // Parse `key <FK01> { type= "...", symbols[1]= [ F1, F1 ] };`, which the
    // pretty printer may spread over several lines.
    static const QRegularExpression keyBlock(
        QStringLiteral("key\\s+<([A-Z0-9_+]+)>\\s*\\{(.*?)\\}\\s*;"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression symbolList(
        QStringLiteral("symbols\\[\\d+\\]\\s*=\\s*\\[([^\\]]*)\\]"));

    auto it = keyBlock.globalMatch(result.stdOut);
    while (it.hasNext()) {
        const auto match = it.next();
        const QString code = match.captured(1);
        const QString body = match.captured(2);

        QString list;
        const auto symbolMatch = symbolList.match(body);
        if (symbolMatch.hasMatch()) {
            list = symbolMatch.captured(1);
        } else {
            // Short form: `key <CAPS> { [ Escape ] };`
            static const QRegularExpression shortForm(QStringLiteral("\\[([^\\]]*)\\]"));
            const auto shortMatch = shortForm.match(body);
            if (!shortMatch.hasMatch())
                continue;
            list = shortMatch.captured(1);
        }

        QStringList levels;
        const QStringList parts = list.split(QLatin1Char(','));
        for (const QString &part : parts) {
            const QString keysym = part.trimmed();
            if (!keysym.isEmpty())
                levels.append(keysym);
        }
        if (!levels.isEmpty() && !symbols.contains(code))
            symbols.insert(code, levels);
    }

    return symbols;
}

QString KeymapService::renderKeymap(const Profile &profile,
                                    const KccgstComponents &components,
                                    const QHash<QString, QStringList> &baseSymbols,
                                    QStringList *notes) const
{
    const auto note = [notes](const QString &message) {
        if (notes)
            notes->append(message);
    };

    // Collect the overrides first so a later rule for the same key wins and the
    // generated file never contains two definitions of one key.
    struct Override {
        QStringList levels;
        QString modifierMap;   // real modifier this key joins, empty for none
        bool oneLevel = false;
    };
    QHash<QString, Override> overrides;
    QStringList order;
    QHash<QString, QStringList> layerLevelsByKey;

    for (const Remap &remap : profile.remaps) {
        if (!remap.enabled || remap.kind == MappingKind::XkbOption)
            continue;
        if (!RuleSetValidator::isValidKeycodeName(remap.sourceKey))
            continue;

        // Every keysym is validated before it reaches the file; an invalid one
        // is dropped rather than escaped, because there is no safe escaping for
        // a keymap identifier.
        QStringList levels = remap.levels;
        for (QString &level : levels) {
            if (!level.isEmpty() && !RuleSetValidator::isValidKeysym(level)) {
                note(QStringLiteral("Skipped invalid keysym \"%1\" on %2")
                         .arg(level, remap.sourceKey));
                level.clear();
            }
        }
        while (!levels.isEmpty() && levels.constLast().isEmpty())
            levels.removeLast();
        if (levels.isEmpty())
            continue;

        Override entry;
        entry.modifierMap.clear();

        if (remap.kind == MappingKind::LayerChooser) {
            if (!isLayerChooser(levels.constFirst())) {
                note(QStringLiteral("%1 is not a layer chooser keysym")
                         .arg(levels.constFirst()));
                continue;
            }
            entry.levels = { levels.constFirst() };
            entry.oneLevel = true;
        } else if (remap.kind == MappingKind::LayerKey) {
            // A key can do one thing on each layer, and each of those is its
            // own mapping; they are merged into one key definition below.
            QStringList &merged = layerLevelsByKey[remap.sourceKey];
            while (merged.size() < levels.size())
                merged.append(QString());
            for (int level = 0; level < levels.size(); ++level)
                if (!levels.at(level).isEmpty())
                    merged[level] = levels.at(level);
            if (!overrides.contains(remap.sourceKey) && !order.contains(remap.sourceKey))
                order.append(remap.sourceKey);
            continue;
        } else {
            // KeyRemap / ModifierRemap: a plain replacement. A single symbol
            // becomes a one-level key, which is what a modifier wants.
            entry.levels = levels;
            entry.oneLevel = levels.size() == 1;
            if (levels.size() == 1) {
                if (const char *modifier = realModifierFor(levels.constFirst()))
                    entry.modifierMap = QString::fromLatin1(modifier);
            }
        }

        if (!order.contains(remap.sourceKey))
            order.append(remap.sourceKey);
        overrides.insert(remap.sourceKey, entry);
    }

    // Layer keys: levels 1-2 are what the key types on its own, 3-4 layer 1
    // (level 3), 5-6 layer 2 (level 5), 7-8 both layers at once. Whatever a
    // mapping leaves empty keeps what the key would type without it, so Caps+H
    // gives Left while h still types h, and a key with a job only on layer 2
    // types as usual while the layer 1 key is held.
    for (auto it = layerLevelsByKey.constBegin(); it != layerLevelsByKey.constEnd(); ++it) {
        if (overrides.contains(it.key()))
            continue;   // a plain remap of the same key; ConflictDetector reports it
        const QStringList &set = it.value();
        const QStringList base = baseSymbols.value(it.key());
        const auto at = [](const QStringList &list, int index) {
            return index < list.size() ? list.at(index) : QString();
        };
        const auto firstOf = [](std::initializer_list<QString> candidates) {
            for (const QString &candidate : candidates)
                if (!candidate.isEmpty())
                    return candidate;
            return QStringLiteral("NoSymbol");
        };

        bool usesLevelFive = false;
        for (int level = 4; level < set.size(); ++level)
            if (!set.at(level).isEmpty())
                usesLevelFive = true;

        const QString one = firstOf({ at(set, 0), at(base, 0) });
        const QString two = firstOf({ at(set, 1), at(base, 1), one });
        const QString three = firstOf({ at(set, 2), at(base, 2), one });
        const QString four = firstOf({ at(set, 3), at(set, 2), at(base, 3), two });

        Override entry;
        entry.levels = { one, two, three, four };
        if (usesLevelFive) {
            const QString five = firstOf({ at(set, 4), one });
            const QString six = firstOf({ at(set, 5), at(set, 4), two });
            entry.levels << five << six << QStringLiteral("NoSymbol") << QStringLiteral("NoSymbol");
        }
        overrides.insert(it.key(), entry);
    }

    QStringList symbolLines;
    QStringList modifierLines;

    for (const QString &code : order) {
        if (!overrides.contains(code))
            continue;
        const Override entry = overrides.value(code);
        const QString joined = entry.levels.join(QStringLiteral(", "));

        if (entry.oneLevel) {
            symbolLines.append(QStringLiteral("    replace key <%1> { type[Group1] = \"ONE_LEVEL\", "
                                              "symbols[Group1] = [ %2 ] };")
                                   .arg(code, joined));
        } else {
            symbolLines.append(QStringLiteral("    replace key <%1> { type[Group1] = \"%2\", "
                                              "symbols[Group1] = [ %3 ] };")
                                   .arg(code, typeForLevelCount(entry.levels.size()), joined));
        }

        if (!entry.modifierMap.isEmpty()) {
            modifierLines.append(QStringLiteral("    modifier_map %1 { <%2> };")
                                     .arg(entry.modifierMap, code));
        }
    }

    const QString header = QStringLiteral(
        "// Generated by OmaKeyboard. Do not edit: the app rewrites this file.\n"
        "// Profile: %1\n"
        "// Device:  %2\n")
        .arg(profile.name.isEmpty() ? profile.id : profile.name,
             profile.deviceName.isEmpty() ? QStringLiteral("(all keyboards)")
                                          : profile.deviceName);

    QString body;
    body += QStringLiteral("xkb_keymap {\n");
    body += QStringLiteral("  xkb_keycodes { include \"%1\" };\n").arg(components.keycodes);
    body += QStringLiteral("  xkb_types    { include \"%1\" };\n")
                .arg(components.types.isEmpty() ? QStringLiteral("complete") : components.types);
    body += QStringLiteral("  xkb_compat   { include \"%1\" };\n")
                .arg(components.compat.isEmpty() ? QStringLiteral("complete") : components.compat);
    body += QStringLiteral("  xkb_symbols  {\n");
    body += QStringLiteral("    include \"%1\"\n").arg(components.symbols);
    if (!symbolLines.isEmpty())
        body += symbolLines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    if (!modifierLines.isEmpty())
        body += modifierLines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    body += QStringLiteral("  };\n");
    if (!components.geometry.isEmpty())
        body += QStringLiteral("  xkb_geometry { include \"%1\" };\n").arg(components.geometry);
    body += QStringLiteral("};\n");

    return header + body;
}

KeymapBuild KeymapService::build(const Profile &profile, const KccgstComponents &components,
                                 const QHash<QString, QStringList> &baseSymbols) const
{
    KeymapBuild build;
    if (!components.isValid()) {
        build.error = QStringLiteral(
            "Could not resolve XKB components for this keyboard. Is xkbcli installed?");
        return build;
    }

    build.source = renderKeymap(profile, components, baseSymbols, &build.notes);

    QString error;
    if (!validate(build.source, &error)) {
        build.error = error;
        return build;
    }

    build.ok = true;
    return build;
}

bool KeymapService::validate(const QString &source, QString *error) const
{
    // --test compiles and throws the result away. Feeding the keymap on stdin
    // keeps the check entirely off the filesystem.
    const ProcessResult result = Process::run(
        QString::fromLatin1(kXkbcli),
        { QStringLiteral("compile-keymap"), QStringLiteral("--test"),
          QStringLiteral("--keymap"), QStringLiteral("-") },
        8000, source.toUtf8());

    if (result.ok())
        return true;

    if (error) {
        *error = result.stdErr.trimmed();
        if (error->isEmpty()) {
            *error = result.started
                ? QStringLiteral("xkbcli rejected the keymap (exit %1)").arg(result.exitCode)
                : QStringLiteral("xkbcli could not be started");
        }
    }
    return false;
}

} // namespace omakeyboard
