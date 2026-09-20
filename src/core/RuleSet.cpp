#include "RuleSet.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <xkbcommon/xkbcommon.h>

namespace omakeyboard {

// --- Types.h helpers --------------------------------------------------------

namespace {

struct KindName { MappingKind kind; const char *name; const char *label; };

const KindName kKindNames[] = {
    // Badges, not taxonomy: short enough to scan, plain enough to skip.
    { MappingKind::KeyRemap,      "keyRemap",      "Key" },
    { MappingKind::ModifierRemap, "modifierRemap", "Modifier" },
    { MappingKind::XkbOption,     "xkbOption",     "Built-in" },
    { MappingKind::LayerChooser,  "layerChooser",  "Layer key" },
    { MappingKind::LayerKey,      "layerKey",      "On layer" },
    { MappingKind::Shortcut,      "shortcut",      "Shortcut" },
};

struct ActionName { ActionKind kind; const char *name; const char *label; };

const ActionName kActionNames[] = {
    { ActionKind::Command,         "command",        "Run command" },
    { ActionKind::OmarchyLaunch,   "omarchyLaunch",  "Omarchy app" },
    { ActionKind::DesktopApp,      "desktopApp",     "Application launcher" },
    { ActionKind::WebApp,          "webApp",         "Web app" },
    { ActionKind::Tui,             "tui",            "Terminal app" },
    { ActionKind::Media,           "media",          "Media action" },
    { ActionKind::WindowAction,    "windowAction",   "Window action" },
    { ActionKind::WorkspaceAction, "workspaceAction","Workspace action" },
    { ActionKind::Toggle,          "toggle",         "System toggle" },
};

} // namespace

QString mappingKindName(MappingKind kind)
{
    for (const auto &entry : kKindNames)
        if (entry.kind == kind)
            return QString::fromLatin1(entry.name);
    return QStringLiteral("unknown");
}

QString mappingKindLabel(MappingKind kind)
{
    for (const auto &entry : kKindNames)
        if (entry.kind == kind)
            return QString::fromLatin1(entry.label);
    return QStringLiteral("Unknown");
}

MappingKind mappingKindFromName(const QString &name)
{
    for (const auto &entry : kKindNames)
        if (name == QLatin1String(entry.name))
            return entry.kind;
    return MappingKind::Unknown;
}

QString tristateName(Tristate value)
{
    switch (value) {
    case Tristate::On:  return QStringLiteral("on");
    case Tristate::Off: return QStringLiteral("off");
    case Tristate::Inherit: break;
    }
    return QStringLiteral("inherit");
}

Tristate tristateFromName(const QString &name)
{
    if (name == QStringLiteral("on"))
        return Tristate::On;
    if (name == QStringLiteral("off"))
        return Tristate::Off;
    return Tristate::Inherit;
}

int layerOfMapping(const Remap &remap)
{
    if (remap.kind != MappingKind::LayerKey)
        return 0;
    for (int level = 4; level < remap.levels.size(); ++level)
        if (!remap.levels.at(level).isEmpty())
            return 2;
    return 1;
}

int layerOfChooser(const Remap &remap)
{
    if (remap.kind != MappingKind::LayerChooser)
        return 0;
    const QString keysym = remap.levels.value(0);
    if (keysym == QStringLiteral("ISO_Level3_Shift"))
        return 1;
    if (keysym == QStringLiteral("ISO_Level5_Shift"))
        return 2;
    return 0;
}

QString layerOutput(const Remap &remap)
{
    switch (layerOfMapping(remap)) {
    case 1: return remap.levels.value(2);
    case 2: return remap.levels.value(4);
    default: return {};
    }
}

QStringList layerLevels(int layer, const QString &keysym)
{
    // Shift makes no difference inside a layer: both levels carry the symbol.
    if (layer == 2)
        return { QString(), QString(), QString(), QString(), keysym, keysym };
    return { QString(), QString(), keysym, keysym };
}

QString actionKindName(ActionKind kind)
{
    for (const auto &entry : kActionNames)
        if (entry.kind == kind)
            return QString::fromLatin1(entry.name);
    return QStringLiteral("none");
}

QString actionKindLabel(ActionKind kind)
{
    for (const auto &entry : kActionNames)
        if (entry.kind == kind)
            return QString::fromLatin1(entry.label);
    return QStringLiteral("None");
}

ActionKind actionKindFromName(const QString &name)
{
    for (const auto &entry : kActionNames)
        if (name == QLatin1String(entry.name))
            return entry.kind;
    return ActionKind::None;
}

const Profile *RuleSet::profileById(const QString &id) const
{
    for (const Profile &profile : profiles)
        if (profile.id == id)
            return &profile;
    return nullptr;
}

Profile *RuleSet::profileById(const QString &id)
{
    for (Profile &profile : profiles)
        if (profile.id == id)
            return &profile;
    return nullptr;
}

// --- Serialization ----------------------------------------------------------

QString RuleSetSerializer::newId(const QString &prefix)
{
    const QString raw = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return prefix + QStringLiteral("-") + raw.left(8);
}

QJsonObject RuleSetSerializer::toJson(const RuleSet &ruleSet)
{
    QJsonArray profiles;
    for (const Profile &profile : ruleSet.profiles) {
        QJsonArray remaps;
        for (const Remap &remap : profile.remaps) {
            QJsonObject entry{
                { QStringLiteral("id"), remap.id },
                { QStringLiteral("kind"), mappingKindName(remap.kind) },
                { QStringLiteral("enabled"), remap.enabled },
                { QStringLiteral("sourceKey"), remap.sourceKey },
            };
            if (!remap.levels.isEmpty())
                entry[QStringLiteral("levels")] = QJsonArray::fromStringList(remap.levels);
            if (!remap.xkbOption.isEmpty())
                entry[QStringLiteral("xkbOption")] = remap.xkbOption;
            if (!remap.note.isEmpty())
                entry[QStringLiteral("note")] = remap.note;
            remaps.append(entry);
        }

        QJsonObject entry{
            { QStringLiteral("id"), profile.id },
            { QStringLiteral("name"), profile.name },
            { QStringLiteral("deviceName"), profile.deviceName },
            { QStringLiteral("enabled"), profile.enabled },
            { QStringLiteral("remaps"), remaps },
        };
        if (!profile.layout.isEmpty())
            entry[QStringLiteral("layout")] = profile.layout;
        if (!profile.variant.isEmpty())
            entry[QStringLiteral("variant")] = profile.variant;
        if (!profile.model.isEmpty())
            entry[QStringLiteral("model")] = profile.model;
        if (!profile.xkbOptions.isEmpty())
            entry[QStringLiteral("xkbOptions")] = QJsonArray::fromStringList(profile.xkbOptions);

        // Only settings the user actually chose are written. An absent field
        // means "inherit", which is what keeps a profile following Omarchy's
        // defaults instead of freezing today's values.
        if (!profile.behaviour.isEmpty()) {
            QJsonObject behaviour;
            if (profile.behaviour.repeatRate >= 0)
                behaviour[QStringLiteral("repeatRate")] = profile.behaviour.repeatRate;
            if (profile.behaviour.repeatDelay >= 0)
                behaviour[QStringLiteral("repeatDelay")] = profile.behaviour.repeatDelay;
            if (profile.behaviour.numlockOnStart != Tristate::Inherit)
                behaviour[QStringLiteral("numlockOnStart")] =
                    tristateName(profile.behaviour.numlockOnStart);
            if (profile.behaviour.resolveBindsBySym != Tristate::Inherit)
                behaviour[QStringLiteral("resolveBindsBySym")] =
                    tristateName(profile.behaviour.resolveBindsBySym);
            if (profile.behaviour.shortcutsEnabled != Tristate::Inherit)
                behaviour[QStringLiteral("shortcutsEnabled")] =
                    tristateName(profile.behaviour.shortcutsEnabled);
            entry[QStringLiteral("behaviour")] = behaviour;
        }

        profiles.append(entry);
    }

    QJsonArray shortcuts;
    for (const Shortcut &shortcut : ruleSet.shortcuts) {
        QJsonObject entry{
            { QStringLiteral("id"), shortcut.id },
            { QStringLiteral("enabled"), shortcut.enabled },
            { QStringLiteral("keys"), shortcut.keys },
            { QStringLiteral("description"), shortcut.description },
            { QStringLiteral("actionKind"), actionKindName(shortcut.actionKind) },
            { QStringLiteral("actionValue"), shortcut.actionValue },
            { QStringLiteral("unbindFirst"), shortcut.unbindFirst },
        };
        if (!shortcut.focusMatch.isEmpty())
            entry[QStringLiteral("focusMatch")] = shortcut.focusMatch;
        if (!shortcut.flags.isEmpty())
            entry[QStringLiteral("flags")] = QJsonArray::fromStringList(shortcut.flags);
        shortcuts.append(entry);
    }

    return QJsonObject{
        { QStringLiteral("schema"), ruleSet.schema },
        { QStringLiteral("manageHyprlandBlock"), ruleSet.manageHyprlandBlock },
        { QStringLiteral("profiles"), profiles },
        { QStringLiteral("shortcuts"), shortcuts },
    };
}

QByteArray RuleSetSerializer::toJsonBytes(const RuleSet &ruleSet)
{
    return QJsonDocument(toJson(ruleSet)).toJson(QJsonDocument::Indented);
}

RuleSet RuleSetSerializer::fromJson(const QJsonObject &object, QStringList *warnings)
{
    const auto warn = [warnings](const QString &message) {
        if (warnings)
            warnings->append(message);
    };

    RuleSet ruleSet;
    ruleSet.schema = object.value(QStringLiteral("schema")).toInt(1);
    ruleSet.manageHyprlandBlock =
        object.value(QStringLiteral("manageHyprlandBlock")).toBool(true);

    const QJsonArray profiles = object.value(QStringLiteral("profiles")).toArray();
    for (const QJsonValue &value : profiles) {
        const QJsonObject entry = value.toObject();
        Profile profile;
        profile.id = entry.value(QStringLiteral("id")).toString();
        if (profile.id.isEmpty())
            profile.id = newId(QStringLiteral("profile"));
        profile.name = entry.value(QStringLiteral("name")).toString();
        profile.deviceName = entry.value(QStringLiteral("deviceName")).toString();
        profile.enabled = entry.value(QStringLiteral("enabled")).toBool(true);
        profile.layout = entry.value(QStringLiteral("layout")).toString();
        profile.variant = entry.value(QStringLiteral("variant")).toString();
        profile.model = entry.value(QStringLiteral("model")).toString();

        const QJsonArray options = entry.value(QStringLiteral("xkbOptions")).toArray();
        for (const QJsonValue &option : options) {
            const QString text = option.toString();
            if (RuleSetValidator::isValidXkbOption(text))
                profile.xkbOptions.append(text);
            else
                warn(QStringLiteral("Dropped malformed XKB option \"%1\"").arg(text));
        }

        const QJsonObject behaviour = entry.value(QStringLiteral("behaviour")).toObject();
        if (!behaviour.isEmpty()) {
            if (behaviour.contains(QStringLiteral("repeatRate")))
                profile.behaviour.repeatRate = behaviour.value(QStringLiteral("repeatRate")).toInt(-1);
            if (behaviour.contains(QStringLiteral("repeatDelay")))
                profile.behaviour.repeatDelay = behaviour.value(QStringLiteral("repeatDelay")).toInt(-1);
            profile.behaviour.numlockOnStart =
                tristateFromName(behaviour.value(QStringLiteral("numlockOnStart")).toString());
            profile.behaviour.resolveBindsBySym =
                tristateFromName(behaviour.value(QStringLiteral("resolveBindsBySym")).toString());
            profile.behaviour.shortcutsEnabled =
                tristateFromName(behaviour.value(QStringLiteral("shortcutsEnabled")).toString());

            // A device-only setting on the all-keyboards profile cannot be
            // honoured, so it is dropped rather than silently ignored later.
            if (profile.deviceName.isEmpty()
                && profile.behaviour.shortcutsEnabled != Tristate::Inherit) {
                profile.behaviour.shortcutsEnabled = Tristate::Inherit;
                warn(QStringLiteral("Dropped \"shortcutsEnabled\" on the all-keyboards "
                                    "profile: Hyprland only supports it per device."));
            }
        }

        const QJsonArray remaps = entry.value(QStringLiteral("remaps")).toArray();
        for (const QJsonValue &remapValue : remaps) {
            const QJsonObject remapEntry = remapValue.toObject();
            Remap remap;
            remap.id = remapEntry.value(QStringLiteral("id")).toString();
            if (remap.id.isEmpty())
                remap.id = newId(QStringLiteral("remap"));
            remap.kind = mappingKindFromName(remapEntry.value(QStringLiteral("kind")).toString());
            if (remap.kind == MappingKind::Unknown) {
                warn(QStringLiteral("Dropped mapping %1 with unknown kind").arg(remap.id));
                continue;
            }
            remap.enabled = remapEntry.value(QStringLiteral("enabled")).toBool(true);
            remap.sourceKey = remapEntry.value(QStringLiteral("sourceKey")).toString();
            remap.xkbOption = remapEntry.value(QStringLiteral("xkbOption")).toString();
            remap.note = remapEntry.value(QStringLiteral("note")).toString();

            const QJsonArray levels = remapEntry.value(QStringLiteral("levels")).toArray();
            for (const QJsonValue &level : levels)
                remap.levels.append(level.toString());

            profile.remaps.append(remap);
        }

        ruleSet.profiles.append(profile);
    }

    const QJsonArray shortcuts = object.value(QStringLiteral("shortcuts")).toArray();
    for (const QJsonValue &value : shortcuts) {
        const QJsonObject entry = value.toObject();
        Shortcut shortcut;
        shortcut.id = entry.value(QStringLiteral("id")).toString();
        if (shortcut.id.isEmpty())
            shortcut.id = newId(QStringLiteral("shortcut"));
        shortcut.enabled = entry.value(QStringLiteral("enabled")).toBool(true);
        shortcut.keys = entry.value(QStringLiteral("keys")).toString();
        shortcut.description = entry.value(QStringLiteral("description")).toString();
        shortcut.actionKind =
            actionKindFromName(entry.value(QStringLiteral("actionKind")).toString());
        shortcut.actionValue = entry.value(QStringLiteral("actionValue")).toString();
        shortcut.focusMatch = entry.value(QStringLiteral("focusMatch")).toString();
        shortcut.unbindFirst = entry.value(QStringLiteral("unbindFirst")).toBool(true);

        const QJsonArray flags = entry.value(QStringLiteral("flags")).toArray();
        const QStringList known = RuleSetValidator::knownBindFlags();
        for (const QJsonValue &flag : flags) {
            const QString text = flag.toString();
            if (known.contains(text))
                shortcut.flags.append(text);
            else
                warn(QStringLiteral("Dropped unknown bind flag \"%1\"").arg(text));
        }

        ruleSet.shortcuts.append(shortcut);
    }

    return ruleSet;
}

RuleSet RuleSetSerializer::fromJsonBytes(const QByteArray &bytes, QStringList *warnings,
                                         QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = parseError.errorString();
        return {};
    }
    return fromJson(document.object(), warnings);
}

// --- Validation -------------------------------------------------------------

QStringList RuleSetValidator::knownBindFlags()
{
    // Exactly the flags HL.BindOptions documents in /usr/share/hypr/stubs.
    return { QStringLiteral("locked"),        QStringLiteral("repeating"),
             QStringLiteral("release"),       QStringLiteral("non_consuming"),
             QStringLiteral("transparent"),   QStringLiteral("ignore_mods"),
             QStringLiteral("dont_inhibit"),  QStringLiteral("long_press") };
}

bool RuleSetValidator::isValidKeysym(const QString &keysym)
{
    // Two gates. The regex keeps anything that is not a bare identifier out of
    // a generated keymap; libxkbcommon then says whether the identifier names a
    // real keysym.
    //
    // The second gate is not optional: `xkbcli compile-keymap` happily accepts
    // an unknown name like "NotAKeysymAtAll" and exits 0, so a compile is not
    // the safety net it looks like. This is the same library Hyprland resolves
    // keymaps with, so agreement is exact by construction.
    static const QRegularExpression pattern(
        QStringLiteral("^(?:[A-Za-z0-9_]+|U[0-9A-Fa-f]{4,6}|0x[0-9A-Fa-f]+)$"));
    if (keysym.isEmpty() || keysym.size() > 64 || !pattern.match(keysym).hasMatch())
        return false;

    // "no symbol at all" is a legitimate target -- it is how a key is
    // disabled -- but xkb_keysym_from_name reports it as a failure.
    if (keysym == QStringLiteral("NoSymbol") || keysym == QStringLiteral("VoidSymbol"))
        return true;

    const xkb_keysym_t resolved =
        xkb_keysym_from_name(keysym.toUtf8().constData(), XKB_KEYSYM_NO_FLAGS);
    return resolved != XKB_KEY_NoSymbol;
}

bool RuleSetValidator::isValidKeycodeName(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Z0-9_+]{1,16}$"));
    return pattern.match(name).hasMatch();
}

bool RuleSetValidator::isValidXkbOption(const QString &option)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[a-z0-9_]{1,24}:[a-z0-9_]{1,48}$"));
    return pattern.match(option).hasMatch();
}

bool RuleSetValidator::isValidBindKeys(const QString &keys)
{
    if (keys.isEmpty() || keys.size() > 96)
        return false;
    // Modifiers joined with "+", last token is a keysym, "code:NN", "mouse:NNN"
    // or a switch spec ("switch:on:Lid Switch"), all of which Omarchy uses.
    static const QRegularExpression pattern(QStringLiteral(
        "^\\s*(?:[A-Za-z0-9_]+\\s*\\+\\s*)*"
        "(?:[A-Za-z0-9_]+|code:[0-9]{1,4}|mouse:[0-9]{1,4}|switch:[a-z]+:[A-Za-z0-9 _-]+)\\s*$"));
    return pattern.match(keys).hasMatch();
}

bool RuleSetValidator::isValidDeviceName(const QString &name)
{
    if (name.isEmpty() || name.size() > 128)
        return false;
    // Hyprland lowercases and dash-joins device names; "/" survives, as in
    // "apple-inc.-apple-internal-keyboard-/-trackpad".
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9._:/-]+$"));
    return pattern.match(name).hasMatch();
}

bool RuleSetValidator::validate(const RuleSet &ruleSet, QVector<Conflict> *issues)
{
    const auto add = [issues](ConflictSeverity severity, const QString &subject,
                              const QString &title, const QString &detail,
                              const QString &suggestion = {}) {
        if (!issues)
            return;
        Conflict conflict;
        conflict.severity = severity;
        conflict.subjectId = subject;
        conflict.title = title;
        conflict.detail = detail;
        conflict.suggestion = suggestion;
        issues->append(conflict);
    };

    bool ok = true;
    QSet<QString> profileIds;

    for (const Profile &profile : ruleSet.profiles) {
        if (profileIds.contains(profile.id)) {
            add(ConflictSeverity::Error, profile.id, QStringLiteral("Duplicate profile id"),
                QStringLiteral("More than one profile uses the id \"%1\".").arg(profile.id));
            ok = false;
        }
        profileIds.insert(profile.id);

        // The id becomes a filename for the generated keymap.
        static const QRegularExpression idPattern(QStringLiteral("^[A-Za-z0-9._-]{1,64}$"));
        if (!idPattern.match(profile.id).hasMatch()) {
            add(ConflictSeverity::Error, profile.id, QStringLiteral("Unsafe profile id"),
                QStringLiteral("\"%1\" cannot be used as a keymap filename.").arg(profile.id));
            ok = false;
        }

        if (!profile.deviceName.isEmpty() && !isValidDeviceName(profile.deviceName)) {
            add(ConflictSeverity::Error, profile.id, QStringLiteral("Invalid device name"),
                QStringLiteral("\"%1\" is not a name Hyprland would report.")
                    .arg(profile.deviceName),
                QStringLiteral("Pick the keyboard from the device list instead of typing it."));
            ok = false;
        }

        // Ranges libinput will actually accept. Out-of-range numbers are
        // rejected here rather than handed to Hyprland to complain about.
        if (profile.behaviour.repeatRate >= 0
            && (profile.behaviour.repeatRate < 1 || profile.behaviour.repeatRate > 255)) {
            add(ConflictSeverity::Error, profile.id, QStringLiteral("Repeat rate out of range"),
                QStringLiteral("%1 keys per second is outside the 1-255 range.")
                    .arg(profile.behaviour.repeatRate));
            ok = false;
        }
        if (profile.behaviour.repeatDelay >= 0
            && (profile.behaviour.repeatDelay < 100 || profile.behaviour.repeatDelay > 4000)) {
            add(ConflictSeverity::Error, profile.id, QStringLiteral("Repeat delay out of range"),
                QStringLiteral("%1 ms is outside the 100-4000 ms range.")
                    .arg(profile.behaviour.repeatDelay));
            ok = false;
        }
        if (profile.isGlobal() && profile.behaviour.shortcutsEnabled != Tristate::Inherit) {
            add(ConflictSeverity::Error, profile.id,
                QStringLiteral("Setting needs a specific keyboard"),
                QStringLiteral("Turning shortcuts off applies to one keyboard at a time; "
                               "Hyprland has no setting for all of them."),
                QStringLiteral("Add the keyboard you mean and set it there."));
            ok = false;
        }

        for (const QString &option : profile.xkbOptions) {
            if (!isValidXkbOption(option)) {
                add(ConflictSeverity::Error, profile.id, QStringLiteral("Invalid XKB option"),
                    QStringLiteral("\"%1\" is not a group:option pair.").arg(option));
                ok = false;
            }
        }

        for (const Remap &remap : profile.remaps) {
            if (!remap.enabled)
                continue;

            if (remap.kind == MappingKind::XkbOption) {
                if (!isValidXkbOption(remap.xkbOption)) {
                    add(ConflictSeverity::Error, remap.id, QStringLiteral("Invalid XKB option"),
                        QStringLiteral("\"%1\" is not a group:option pair.").arg(remap.xkbOption));
                    ok = false;
                }
                continue;
            }

            if (!isValidKeycodeName(remap.sourceKey)) {
                add(ConflictSeverity::Error, remap.id, QStringLiteral("Invalid source key"),
                    QStringLiteral("\"%1\" is not an XKB keycode name.").arg(remap.sourceKey),
                    QStringLiteral("Use the key picker or press the key to capture it."));
                ok = false;
                continue;
            }

            bool hasTarget = false;
            for (const QString &level : remap.levels) {
                if (level.isEmpty())
                    continue;
                hasTarget = true;
                if (!isValidKeysym(level)) {
                    add(ConflictSeverity::Error, remap.id, QStringLiteral("Invalid keysym"),
                        QStringLiteral("\"%1\" is not a keysym name.").arg(level));
                    ok = false;
                }
            }
            if (!hasTarget) {
                add(ConflictSeverity::Warning, remap.id, QStringLiteral("Mapping has no target"),
                    QStringLiteral("%1 maps to nothing and will be skipped.").arg(remap.sourceKey));
            }

            if (remap.kind == MappingKind::LayerKey && layerOutput(remap).isEmpty()) {
                add(ConflictSeverity::Warning, remap.id, QStringLiteral("Layer mapping is empty"),
                    QStringLiteral("A layer mapping needs a symbol for its layer."));
            }
        }
    }

    QSet<QString> shortcutIds;
    for (const Shortcut &shortcut : ruleSet.shortcuts) {
        if (shortcutIds.contains(shortcut.id)) {
            add(ConflictSeverity::Error, shortcut.id, QStringLiteral("Duplicate shortcut id"),
                QStringLiteral("More than one shortcut uses the id \"%1\".").arg(shortcut.id));
            ok = false;
        }
        shortcutIds.insert(shortcut.id);

        if (!shortcut.enabled)
            continue;

        if (!isValidBindKeys(shortcut.keys)) {
            add(ConflictSeverity::Error, shortcut.id, QStringLiteral("Invalid shortcut"),
                QStringLiteral("\"%1\" is not a key combination Hyprland accepts.")
                    .arg(shortcut.keys));
            ok = false;
        }

        if (shortcut.actionKind == ActionKind::None || shortcut.actionValue.isEmpty()) {
            add(ConflictSeverity::Error, shortcut.id, QStringLiteral("Shortcut has no action"),
                QStringLiteral("%1 does not do anything yet.").arg(shortcut.keys));
            ok = false;
        }

        // The generator shell-quotes commands, but a newline would still break
        // the generated Lua into two statements, so it is rejected outright.
        if (shortcut.actionValue.contains(QLatin1Char('\n'))
            || shortcut.description.contains(QLatin1Char('\n'))) {
            add(ConflictSeverity::Error, shortcut.id, QStringLiteral("Illegal newline"),
                QStringLiteral("Shortcut fields cannot contain line breaks."));
            ok = false;
        }
    }

    return ok;
}

} // namespace omakeyboard
