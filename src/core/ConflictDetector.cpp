#include "ConflictDetector.h"

#include "KeyCatalog.h"
#include "RuleSet.h"
#include "XkbOptionCatalog.h"

#include <QHash>
#include <QSet>

namespace omakeyboard {

namespace {

Conflict make(ConflictSeverity severity, const QString &subject, const QString &title,
              const QString &detail, const QString &suggestion = {},
              const QString &conflictsWith = {}, const Profile *profile = nullptr)
{
    Conflict conflict;
    conflict.severity = severity;
    conflict.subjectId = subject;
    conflict.title = title;
    conflict.detail = detail;
    conflict.suggestion = suggestion;
    conflict.conflictsWith = conflictsWith;
    if (profile) {
        conflict.profileId = profile->id;
        conflict.profileName = profile->name.isEmpty() ? profile->id : profile->name;
    }
    return conflict;
}

// base.lst describes an option by the key it uses, not by the feature it
// provides: compose:caps is described as "Caps Lock". Saying "Caps Lock stops
// working" would therefore be backwards -- what stops working is Compose. Name
// the feature from the option's group instead, and fall back to the
// description only when the group has no label of its own.
QString featureNameFor(const XkbOption &option)
{
    const QString group = option.name.section(QLatin1Char(':'), 0, 0);
    const QString label = XkbOptionCatalog::groupLabel(group);
    if (label != group)
        return label;
    return option.description;
}

} // namespace

bool ConflictDetector::hasBlockingConflict(const QVector<Conflict> &conflicts)
{
    for (const Conflict &conflict : conflicts)
        if (conflict.severity == ConflictSeverity::Error)
            return true;
    return false;
}

int ConflictDetector::countBySeverity(const QVector<Conflict> &conflicts,
                                      ConflictSeverity severity)
{
    int count = 0;
    for (const Conflict &conflict : conflicts)
        if (conflict.severity == severity)
            ++count;
    return count;
}

void ConflictDetector::detectDuplicateKeys(const RuleSet &ruleSet,
                                           QVector<Conflict> *out) const
{
    const KeyCatalog &keys = KeyCatalog::instance();

    for (const Profile &profile : ruleSet.profiles) {
        if (!profile.enabled)
            continue;

        // Within one profile, the last rule for a key would silently win in the
        // generated keymap, so the earlier one is reported instead.
        //
        // A key has one job of its own, and at most one job on each layer:
        // H can be Left on one layer and Home on the other. What cannot mix
        // is a job of its own with a job on a layer -- including a layer
        // chooser, which cannot both select the layer and be remapped by it.
        QHash<QString, QString> ownJob;                 // sourceKey -> remap id
        QHash<QString, QHash<int, QString>> layerJob;   // sourceKey -> layer -> id

        for (const Remap &remap : profile.remaps) {
            if (!remap.enabled || remap.kind == MappingKind::XkbOption)
                continue;
            if (remap.sourceKey.isEmpty())
                continue;

            const QString label = keys.labelForCode(remap.sourceKey);
            const int layer = layerOfMapping(remap);
            const auto report = [&](const QString &detail, const QString &other) {
                out->append(make(
                    ConflictSeverity::Error, remap.id,
                    QStringLiteral("Two mappings for %1").arg(label), detail,
                    QStringLiteral("Remove one of the two mappings for %1.").arg(label),
                    other, &profile));
            };

            if (layer == 0) {
                if (ownJob.contains(remap.sourceKey)) {
                    report(QStringLiteral("\"%1\" already has a mapping in this profile, and a "
                                          "key can only do one thing.").arg(label),
                           ownJob.value(remap.sourceKey));
                } else if (layerJob.contains(remap.sourceKey)) {
                    report(QStringLiteral("\"%1\" already does something on a layer, so it "
                                          "cannot also be remapped on its own.").arg(label),
                           layerJob.value(remap.sourceKey).begin().value());
                } else {
                    ownJob.insert(remap.sourceKey, remap.id);
                }
                continue;
            }

            if (ownJob.contains(remap.sourceKey)) {
                report(QStringLiteral("\"%1\" is already remapped on its own, so it cannot "
                                      "also do something on a layer.").arg(label),
                       ownJob.value(remap.sourceKey));
            } else if (layerJob.value(remap.sourceKey).contains(layer)) {
                report(QStringLiteral("\"%1\" already does something on this layer.")
                           .arg(label),
                       layerJob.value(remap.sourceKey).value(layer));
            } else {
                layerJob[remap.sourceKey].insert(layer, remap.id);
            }
        }
    }

    // The same physical keyboard cannot be driven by two profiles, because a
    // device gets exactly one kb_file.
    QHash<QString, QString> deviceOwner;
    for (const Profile &profile : ruleSet.profiles) {
        if (!profile.enabled || profile.deviceName.isEmpty())
            continue;
        const auto existing = deviceOwner.constFind(profile.deviceName);
        if (existing != deviceOwner.constEnd()) {
            out->append(make(
                ConflictSeverity::Error, profile.id,
                QStringLiteral("Two profiles for one keyboard"),
                QStringLiteral("\"%1\" is already configured by another profile. A keyboard "
                               "can only load one keymap.")
                    .arg(profile.name.isEmpty() ? profile.deviceName : profile.name),
                QStringLiteral("Merge the two profiles, or disable one of them."),
                *existing, &profile));
        } else {
            deviceOwner.insert(profile.deviceName, profile.id);
        }
    }
}

void ConflictDetector::detectOptionClashes(const RuleSet &ruleSet,
                                           const QStringList &inherited,
                                           QVector<Conflict> *out) const
{
    const XkbOptionCatalog &catalog = XkbOptionCatalog::instance();
    const KeyCatalog &keys = KeyCatalog::instance();

    for (const Profile &profile : ruleSet.profiles) {
        if (!profile.enabled)
            continue;

        // Everything that will end up in this profile's kb_options string.
        QStringList options = profile.xkbOptions;
        QHash<QString, QString> optionOwner;  // option -> remap id ("" for profile-level)
        for (const QString &option : profile.xkbOptions)
            optionOwner.insert(option, profile.id);

        for (const Remap &remap : profile.remaps) {
            if (!remap.enabled || remap.kind != MappingKind::XkbOption)
                continue;
            if (remap.xkbOption.isEmpty())
                continue;
            if (!options.contains(remap.xkbOption)) {
                options.append(remap.xkbOption);
                optionOwner.insert(remap.xkbOption, remap.id);
            }
        }

        // An option the app does not know cannot be validated, and xkbcli would
        // reject the whole keymap, so it is caught here with a better message.
        for (const QString &option : options) {
            if (catalog.isLoaded() && !catalog.contains(option)) {
                out->append(make(
                    ConflictSeverity::Error, optionOwner.value(option, profile.id),
                    QStringLiteral("Unknown keyboard option"),
                    QStringLiteral("\"%1\" is not an option this system's XKB rules define.")
                        .arg(option),
                    QStringLiteral("Pick an option from the list instead of typing one."),
                    {}, &profile));
            }
        }

        // Two options from the same group: XKB applies them in order and the
        // result is rarely what either one promised on its own.
        QHash<QString, QString> groupOwner;
        for (const QString &option : options) {
            const QString group = option.section(QLatin1Char(':'), 0, 0);
            const auto existing = groupOwner.constFind(group);
            if (existing != groupOwner.constEnd() && *existing != option) {
                out->append(make(
                    ConflictSeverity::Warning, optionOwner.value(option, profile.id),
                    QStringLiteral("Two %1 options").arg(XkbOptionCatalog::groupLabel(group)),
                    QStringLiteral("\"%1\" and \"%2\" both configure %3. XKB applies both and "
                                   "the outcome depends on their order.")
                        .arg(*existing, option, XkbOptionCatalog::groupLabel(group)),
                    QStringLiteral("Keep only one of them."),
                    *existing, &profile));
            } else {
                groupOwner.insert(group, option);
            }
        }

        // The heart of it: an option and a keymap rule fighting for one key.
        // On a stock Omarchy this is the "Caps Lock as Ctrl" case, because the
        // default options put Compose on Caps Lock.
        QHash<QString, QString> keyClaimedByOption;
        const QStringList allOptions = options + inherited;
        for (const QString &option : allOptions) {
            const XkbOption *definition = catalog.find(option);
            if (!definition)
                continue;
            for (const QString &key : definition->claimedKeys) {
                if (!keyClaimedByOption.contains(key))
                    keyClaimedByOption.insert(key, option);
            }
        }

        for (const Remap &remap : profile.remaps) {
            if (!remap.enabled || remap.kind == MappingKind::XkbOption)
                continue;
            const auto claimed = keyClaimedByOption.constFind(remap.sourceKey);
            if (claimed == keyClaimedByOption.constEnd())
                continue;

            const QString option = *claimed;
            const XkbOption *definition = catalog.find(option);
            const QString feature = definition ? featureNameFor(*definition) : option;
            const QString label = keys.labelForCode(remap.sourceKey);
            const bool inheritedOption = !options.contains(option);

            // Said in plain terms: what the key already does, what will happen,
            // and what to do about it. The underlying option name is an
            // implementation detail and stays out of it.
            out->append(make(
                ConflictSeverity::Warning, remap.id,
                QStringLiteral("%1 already carries %2").arg(label, feature),
                inheritedOption
                    ? QStringLiteral("Omarchy already uses %1 for %2. Your mapping loads "
                                     "afterwards and wins, so %2 will stop working.")
                          .arg(label, feature)
                    : QStringLiteral("Another of your settings puts %1 on %2, and only one of "
                                     "the two can apply.")
                          .arg(feature, label),
                inheritedOption
                    ? QStringLiteral("To keep %1, move it to another key first.").arg(feature)
                    : QStringLiteral("Remove the other setting, or map a different key."),
                option, &profile));
        }
    }
}

void ConflictDetector::detectShortcutClashes(const RuleSet &ruleSet,
                                             const QVector<ExistingBinding> &existing,
                                             QVector<Conflict> *out) const
{
    // Index what is already bound, ignoring bindings OmaKeyboard itself installed:
    // replacing your own shortcut is not a conflict.
    QHash<QString, ExistingBinding> bound;
    for (const ExistingBinding &binding : existing) {
        if (binding.fromOmaKeyboard || binding.normalizedKeys.isEmpty())
            continue;
        if (!bound.contains(binding.normalizedKeys))
            bound.insert(binding.normalizedKeys, binding);
    }

    QHash<QString, QString> ours;
    for (const Shortcut &shortcut : ruleSet.shortcuts) {
        if (!shortcut.enabled)
            continue;

        const QString normalized = OmarchyConfigParser::normalizeKeys(shortcut.keys);
        if (normalized.isEmpty())
            continue;

        const auto duplicate = ours.constFind(normalized);
        if (duplicate != ours.constEnd()) {
            out->append(make(
                ConflictSeverity::Error, shortcut.id,
                QStringLiteral("Duplicate shortcut"),
                QStringLiteral("%1 is used by two of your shortcuts.").arg(shortcut.keys),
                QStringLiteral("Change one of them to a different combination."),
                *duplicate));
            continue;
        }
        ours.insert(normalized, shortcut.id);

        const auto clash = bound.constFind(normalized);
        if (clash == bound.constEnd())
            continue;

        const ExistingBinding &other = *clash;
        const QString what = other.description.isEmpty()
            ? (other.action.isEmpty() ? QStringLiteral("an existing binding") : other.action)
            : other.description;

        if (shortcut.unbindFirst) {
            // Handled, but worth saying out loud: the user is taking a
            // combination away from something that works today.
            out->append(make(
                ConflictSeverity::Info, shortcut.id,
                QStringLiteral("%1 replaces \"%2\"").arg(shortcut.keys, what),
                QStringLiteral("%1 is currently bound to \"%2\". OmaKeyboard releases it first, so "
                               "your shortcut takes over.")
                    .arg(shortcut.keys, what),
                {}, what));
        } else {
            out->append(make(
                ConflictSeverity::Error, shortcut.id,
                QStringLiteral("%1 is already taken").arg(shortcut.keys),
                QStringLiteral("%1 is bound to \"%2\". Two bindings on one combination make "
                               "the result unpredictable.")
                    .arg(shortcut.keys, what),
                QStringLiteral("Turn on \"replace the existing binding\", or pick another "
                               "combination."),
                what));
        }
    }
}

QVector<Conflict> ConflictDetector::detect(const Inputs &inputs) const
{
    QVector<Conflict> conflicts;

    // Structural problems first: a rule set that cannot be generated from is
    // not worth cross-checking against the system.
    RuleSetValidator::validate(inputs.ruleSet, &conflicts);

    detectDuplicateKeys(inputs.ruleSet, &conflicts);
    detectOptionClashes(inputs.ruleSet, inputs.inheritedXkbOptions, &conflicts);
    detectShortcutClashes(inputs.ruleSet, inputs.existingBindings, &conflicts);

    return conflicts;
}

} // namespace omakeyboard
