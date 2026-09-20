#include "ConfigRepository.h"

#include "ActionCatalog.h"
#include "KeyCatalog.h"
#include "KeymapService.h"
#include "LuaScanner.h"
#include "Paths.h"
#include "RuleSet.h"
#include "XkbOptionCatalog.h"

#include <QDir>
#include <QCoreApplication>
#include <QUrl>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>
#include <QHash>
#include <QSet>
#include <QFile>

namespace omakeyboard {

namespace {
// How many snapshots to keep. Enough to walk back a few bad ideas without
// letting the directory grow without bound.
constexpr int kBackupsToKeep = 20;
} // namespace

ConfigRepository::ConfigRepository(QObject *parent)
    : QObject(parent)
{
    m_statusTimer.setSingleShot(true);
    m_statusTimer.setInterval(7000);
    connect(&m_statusTimer, &QTimer::timeout, this, [this]() { setStatus(QString()); });

    KeyCatalog::instance().load();
    XkbOptionCatalog::instance().load();
    ActionCatalog::instance().load();

    // Anything that changes profiles or their mappings changes the chips, so
    // derive the one signal from the two that already exist rather than
    // remembering to emit it in a dozen places.
    connect(this, &ConfigRepository::stateReloaded, this, &ConfigRepository::keyboardsChanged);
    connect(this, &ConfigRepository::mappingsChanged, this, &ConfigRepository::keyboardsChanged);
    // Every path that snapshots ends in one of these: apply finishes (even when
    // it rolls back, the snapshot stays), and restores reload the state.
    connect(this, &ConfigRepository::applyFinished, this, &ConfigRepository::backupsChanged);
    connect(this, &ConfigRepository::stateReloaded, this, &ConfigRepository::backupsChanged);

    reload();
}

void ConfigRepository::setDirty(bool dirty)
{
    // A clean state is, by definition, what is saved: remember it, so undoing
    // a deletion can tell whether it brought the rules back to it.
    if (!dirty)
        m_savedRules = RuleSetSerializer::toJsonBytes(ruleSetForSaving());
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged();
}

void ConfigRepository::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void ConfigRepository::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    // A line that says "Backup deleted." is about something that already
    // finished; leaving it there all session makes it look like state.
    if (status.isEmpty())
        m_statusTimer.stop();
    else
        m_statusTimer.start();
    emit statusChanged();
}

void ConfigRepository::setStatusTimeout(int milliseconds)
{
    m_statusTimer.setInterval(milliseconds);
}

void ConfigRepository::reload()
{
    setBusy(true);

    m_hyprlandRunning = HyprlandReloadService::isHyprlandRunning();
    m_devices.refresh();
    m_inheritedOptions = m_devices.effectiveOptions();

    // Name keys after what they type on this keyboard, once the effective
    // layout is known.
    if (KeymapService::isAvailable() && !m_layoutSymbolsLoaded) {
        KeymapService keymaps;
        Profile probe;
        probe.id = QStringLiteral("probe");
        const KccgstComponents components = keymaps.resolveComponents(
            probe, m_devices.effectiveLayout(), m_devices.effectiveVariant(),
            m_devices.effectiveModel(), m_inheritedOptions);
        if (components.isValid()) {
            const QHash<QString, QStringList> symbols = keymaps.baseSymbols(components);
            if (!symbols.isEmpty()) {
                KeyCatalog::instance().applyLayoutSymbols(symbols);
                m_layoutSymbolsLoaded = true;
            }
        }
    }

    const ParsedState state = m_parser.load();
    m_ruleSet = state.ruleSet;
    m_existingBindings = state.bindings;

    ensureDefaultProfile();

    if (m_activeProfileId.isEmpty() && !m_ruleSet.profiles.isEmpty()) {
        m_activeProfileId = m_ruleSet.profiles.constFirst().id;
        emit activeProfileChanged();
    }

    refreshConflicts();
    setDirty(false);
    dismissUndo();

    QStringList notes;
    if (!state.ruleWarnings.isEmpty())
        notes += state.ruleWarnings;
    if (!state.error.isEmpty())
        notes << state.error;
    if (state.rulesFilePresent && !state.hyprlandBlockPresent && m_ruleSet.manageHyprlandBlock)
        notes << QStringLiteral("Your mappings are saved but not loaded: the OmaKeyboard block is "
                                "missing from hyprland.lua. Applying will restore it.");

    // Nothing to report means nothing on screen, rather than a line that
    // says "Ready" forever.
    setStatus(notes.join(QStringLiteral(" · ")));

    setBusy(false);
    emit stateReloaded();
    emit mappingsChanged(m_activeProfileId);
    emit shortcutsChanged();
}

void ConfigRepository::ensureDefaultProfile()
{
    // Always offer a place to put a rule. The all-keyboards profile applies
    // through input.kb_*, which is what a user who has never thought about
    // per-device rules expects.
    for (const Profile &profile : m_ruleSet.profiles)
        if (profile.isGlobal())
            return;

    Profile global;
    global.id = QStringLiteral("all-keyboards");
    global.name = QStringLiteral("All keyboards");
    global.enabled = true;
    m_ruleSet.profiles.prepend(global);
}

void ConfigRepository::setRuleSet(const RuleSet &ruleSet)
{
    m_ruleSet = ruleSet;
    ensureDefaultProfile();
    refreshConflicts();
    setDirty(true);
    emit mappingsChanged(m_activeProfileId);
    emit shortcutsChanged();
}

void ConfigRepository::refreshConflicts()
{
    ConflictDetector::Inputs inputs;
    inputs.ruleSet = m_ruleSet;
    inputs.existingBindings = m_existingBindings;
    inputs.inheritedXkbOptions = m_inheritedOptions;
    m_conflicts = m_detector.detect(inputs);
    emit conflictsChanged();
}

Profile *ConfigRepository::profileFor(const QString &profileId)
{
    if (profileId.isEmpty())
        return m_ruleSet.profiles.isEmpty() ? nullptr : &m_ruleSet.profiles[0];
    return m_ruleSet.profileById(profileId);
}

QVariantMap ConfigRepository::capabilities() const
{
    const EngineCapabilities &caps = m_engine.capabilities();
    return QVariantMap{
        { QStringLiteral("simpleRemap"), caps.simpleRemap },
        { QStringLiteral("modifierRemap"), caps.modifierRemap },
        { QStringLiteral("perDeviceRules"), caps.perDeviceRules },
        { QStringLiteral("layers"), caps.layers },
        { QStringLiteral("secondLayer"), caps.secondLayer },
        { QStringLiteral("shortcuts"), caps.shortcuts },
        { QStringLiteral("disableKey"), caps.disableKey },
        { QStringLiteral("tapHoldLimit"), caps.tapHoldLimit },
        { QStringLiteral("combosLimit"), caps.combosLimit },
        { QStringLiteral("macrosLimit"), caps.macrosLimit },
        { QStringLiteral("perAppLimit"), caps.perAppLimit },
    };
}

QVariantMap ConfigRepository::conflictToVariant(const Conflict &conflict)
{
    QString severity = QStringLiteral("info");
    if (conflict.severity == ConflictSeverity::Warning)
        severity = QStringLiteral("warning");
    else if (conflict.severity == ConflictSeverity::Error)
        severity = QStringLiteral("error");

    return QVariantMap{
        { QStringLiteral("severity"), severity },
        { QStringLiteral("subjectId"), conflict.subjectId },
        { QStringLiteral("profileId"), conflict.profileId },
        { QStringLiteral("profileName"), conflict.profileName },
        { QStringLiteral("title"), conflict.title },
        { QStringLiteral("detail"), conflict.detail },
        { QStringLiteral("suggestion"), conflict.suggestion },
        { QStringLiteral("conflictsWith"), conflict.conflictsWith },
    };
}

QVariantList ConfigRepository::conflictList() const
{
    QVariantList list;
    for (const Conflict &conflict : m_conflicts)
        list.append(conflictToVariant(conflict));
    return list;
}

int ConfigRepository::errorCount() const
{
    return ConflictDetector::countBySeverity(m_conflicts, ConflictSeverity::Error);
}

int ConfigRepository::warningCount() const
{
    return ConflictDetector::countBySeverity(m_conflicts, ConflictSeverity::Warning);
}

void ConfigRepository::setActiveProfileId(const QString &id)
{
    if (m_activeProfileId == id)
        return;
    m_activeProfileId = id;
    emit activeProfileChanged();
    emit mappingsChanged(id);
}

QVariantList ConfigRepository::profileList() const
{
    QVariantList list;
    for (const Profile &profile : m_ruleSet.profiles) {
        const KeyboardDevice *device =
            profile.deviceName.isEmpty() ? nullptr : m_devices.find(profile.deviceName);

        int activeMappings = 0;
        for (const Remap &remap : profile.remaps)
            if (remap.enabled)
                ++activeMappings;

        list.append(QVariantMap{
            { QStringLiteral("id"), profile.id },
            { QStringLiteral("name"), profile.name },
            { QStringLiteral("deviceName"), profile.deviceName },
            { QStringLiteral("isGlobal"), profile.isGlobal() },
            { QStringLiteral("enabled"), profile.enabled },
            { QStringLiteral("mappingCount"), activeMappings },
            // A profile for a keyboard that is not plugged in right now is
            // still valid: it applies again the moment that keyboard appears.
            { QStringLiteral("connected"), profile.isGlobal() || device != nullptr },
            { QStringLiteral("layout"), device ? device->layout : profile.layout },
        });
    }
    return list;
}

QVariantMap ConfigRepository::environmentReport() const
{
    // Omarchy 4 configures Hyprland in Lua, and everything this app writes
    // hangs off that file. Without it there is nothing to attach to, and the
    // failure a user would otherwise meet is a write error halfway through an
    // apply -- which says what broke, not what is wrong.
    if (!QFile::exists(Paths::hyprlandLuaFile())) {
        const bool legacy = QFile::exists(Paths::hyprUserDir() + QStringLiteral("/hyprland.conf"));
        return QVariantMap{
            { QStringLiteral("ok"), false },
            { QStringLiteral("title"), QStringLiteral("This does not look like an Omarchy 4 desktop") },
            { QStringLiteral("detail"),
              legacy
                  ? QStringLiteral("Your Hyprland is configured with hyprland.conf. OmaKeyboard "
                                   "writes to hyprland.lua, the Lua configuration Omarchy 4 "
                                   "introduced, and it is not here.")
                  : QStringLiteral("OmaKeyboard writes to ~/.config/hypr/hyprland.lua, the Lua "
                                   "configuration Omarchy 4 uses, and that file does not exist.") },
            { QStringLiteral("suggestion"),
              legacy
                  ? QStringLiteral("Update Omarchy to version 4 or newer, then open this again.")
                  : QStringLiteral("Install Omarchy 4 or newer, then open this again. Nothing "
                                   "will be written until then.") },
        };
    }

    return QVariantMap{ { QStringLiteral("ok"), true } };
}

QVariantList ConfigRepository::keyboardList() const
{
    QVariantList list;

    const auto entryFor = [this](const Profile *profile, const KeyboardDevice *device) {
        int mappings = 0;
        if (profile) {
            for (const Remap &remap : profile->remaps)
                if (remap.enabled)
                    ++mappings;
            if (!profile->behaviour.isEmpty())
                ++mappings;
        }

        const bool isGlobal = device == nullptr && (!profile || profile->isGlobal());
        QString name;
        if (device)
            name = device->label;
        else if (profile && !profile->name.isEmpty())
            name = profile->name;
        else if (profile)
            name = DeviceService::prettify(profile->deviceName);

        return QVariantMap{
            { QStringLiteral("profileId"), profile ? profile->id : QString() },
            { QStringLiteral("deviceName"), device ? device->name
                                                   : (profile ? profile->deviceName : QString()) },
            { QStringLiteral("name"), isGlobal ? QStringLiteral("All keyboards") : name },
            { QStringLiteral("isGlobal"), isGlobal },
            { QStringLiteral("enabled"), profile ? profile->enabled : true },
            { QStringLiteral("mappingCount"), mappings },
            { QStringLiteral("connected"), isGlobal || device != nullptr },
            { QStringLiteral("transport"), device ? device->transport : QString() },
            // Only what Hyprland itself asserts. It marks its own virtual
            // keyboard as main whenever an input method is running, leaving
            // every real device unmarked -- and there is no other way to know
            // which keyboard someone is actually typing on. Guessing "the
            // first one" labelled a Bluetooth host controller as the keyboard
            // in use, which is worse than saying nothing.
            { QStringLiteral("isMain"), device && device->isMain && device->isConfigurable() },
        };
    };

    // The all-keyboards entry always leads.
    for (const Profile &profile : m_ruleSet.profiles) {
        if (profile.isGlobal()) {
            list.append(entryFor(&profile, nullptr));
            break;
        }
    }

    // Then each keyboard actually attached. A profile may or may not exist yet.
    QStringList shown;
    for (const KeyboardDevice &device : m_devices.keyboards()) {
        // The system's verdict, not a guess about the name.
        if (!device.isConfigurable())
            continue;
        const Profile *profile = nullptr;
        for (const Profile &candidate : m_ruleSet.profiles) {
            if (candidate.deviceName == device.name) {
                profile = &candidate;
                break;
            }
        }
        list.append(entryFor(profile, &device));
        shown.append(device.name);
    }

    // Finally, keyboards with rules that are not plugged in right now. Their
    // rules still apply the moment they come back, so hiding them would be a
    // lie about what is configured.
    for (const Profile &profile : m_ruleSet.profiles) {
        if (profile.isGlobal() || shown.contains(profile.deviceName))
            continue;
        list.append(entryFor(&profile, nullptr));
    }

    return list;
}

QString ConfigRepository::ensureProfileForDevice(const QString &deviceName, const QString &name)
{
    if (deviceName.isEmpty()) {
        for (const Profile &profile : m_ruleSet.profiles)
            if (profile.isGlobal())
                return profile.id;
        return {};
    }

    for (const Profile &profile : m_ruleSet.profiles)
        if (profile.deviceName == deviceName)
            return profile.id;

    return addProfileForDevice(deviceName, name);
}

QString ConfigRepository::addProfileForDevice(const QString &deviceName, const QString &name)
{
    if (!deviceName.isEmpty() && !RuleSetValidator::isValidDeviceName(deviceName))
        return {};

    Profile profile;
    profile.id = RuleSetSerializer::newId(QStringLiteral("kbd"));
    profile.deviceName = deviceName;
    if (!name.isEmpty())
        profile.name = name;
    else if (const KeyboardDevice *device = m_devices.find(deviceName))
        profile.name = device->label;
    else
        profile.name = DeviceService::prettify(deviceName);
    profile.enabled = true;

    m_ruleSet.profiles.append(profile);

    // Deliberately not marked as an unsaved change. Selecting a keyboard is
    // navigation: an empty profile emits nothing into the generated config, so
    // offering to apply it would be offering to save nothing. The first actual
    // mapping is what makes the configuration dirty.
    refreshConflicts();
    setActiveProfileId(profile.id);
    emit stateReloaded();
    return profile.id;
}

bool ConfigRepository::removeProfile(const QString &profileId)
{
    for (int index = 0; index < m_ruleSet.profiles.size(); ++index) {
        if (m_ruleSet.profiles.at(index).id != profileId)
            continue;
        // The all-keyboards profile is the fallback home for a rule, so it can
        // be emptied but not deleted.
        if (m_ruleSet.profiles.at(index).isGlobal())
            return false;
        Removal removal;
        removal.kind = Removal::Kind::Profile;
        removal.profile = m_ruleSet.profiles.at(index);
        removal.index = index;
        removal.label = QStringLiteral("Cleared %1").arg(
            removal.profile.name.isEmpty() ? DeviceService::prettify(removal.profile.deviceName)
                                           : removal.profile.name);
        m_ruleSet.profiles.removeAt(index);
        if (m_activeProfileId == profileId)
            setActiveProfileId(m_ruleSet.profiles.isEmpty()
                                   ? QString()
                                   : m_ruleSet.profiles.constFirst().id);
        setDirty(true);
        refreshConflicts();
        emit stateReloaded();
        rememberRemoval(removal);
        return true;
    }
    return false;
}

bool ConfigRepository::setProfileEnabled(const QString &profileId, bool enabled)
{
    Profile *profile = m_ruleSet.profileById(profileId);
    if (!profile)
        return false;
    profile->enabled = enabled;
    setDirty(true);
    refreshConflicts();
    emit stateReloaded();
    return true;
}

QVariantList ConfigRepository::mappingList(const QString &profileId) const
{
    QVariantList list;
    const Profile *profile = m_ruleSet.profileById(profileId);
    if (!profile)
        return list;

    const KeyCatalog &keys = KeyCatalog::instance();
    const XkbOptionCatalog &options = XkbOptionCatalog::instance();

    // Each layer's key, so a layer mapping can be shown as the combination a
    // person actually presses rather than as a level number.
    QHash<int, QString> layerKeyLabel;
    for (const Remap &remap : profile->remaps) {
        const int layer = layerOfChooser(remap);
        if (remap.enabled && layer > 0 && !layerKeyLabel.contains(layer))
            layerKeyLabel.insert(layer, keys.labelForCode(remap.sourceKey));
    }

    for (const Remap &remap : profile->remaps) {
        QString sourceLabel;
        QString targetLabel;

        if (remap.kind == MappingKind::XkbOption) {
            const XkbOption *option = options.find(remap.xkbOption);
            sourceLabel = option && !option->claimedKeys.isEmpty()
                ? keys.labelForCode(option->claimedKeys.constFirst())
                : XkbOptionCatalog::groupLabel(
                      remap.xkbOption.section(QLatin1Char(':'), 0, 0));
            targetLabel = option ? option->description : remap.xkbOption;
        } else {
            sourceLabel = keys.labelForCode(remap.sourceKey);
            if (remap.kind == MappingKind::LayerKey) {
                // "Caps Lock + F1 -> Brightness down" reads as the thing the
                // user does; "Layer: Brightness down" does not.
                const QString layerKey = layerKeyLabel.value(layerOfMapping(remap));
                if (!layerKey.isEmpty())
                    sourceLabel = layerKey + QStringLiteral(" + ") + sourceLabel;
                targetLabel = keys.labelForKeysym(layerOutput(remap));
            } else if (remap.kind == MappingKind::LayerChooser) {
                // Short, because it sits in a key-sized label; the list says
                // what holding it does alongside.
                targetLabel = remap.levels.value(0) == QStringLiteral("ISO_Level5_Shift")
                    ? QStringLiteral("Layer key 2")
                    : QStringLiteral("Layer key");
            } else {
                targetLabel = keys.labelForKeysym(remap.levels.value(0));
            }
        }

        QVariantList levels;
        for (const QString &level : remap.levels)
            levels.append(level);

        list.append(QVariantMap{
            { QStringLiteral("id"), remap.id },
            { QStringLiteral("kind"), mappingKindName(remap.kind) },
            { QStringLiteral("kindLabel"), mappingKindLabel(remap.kind) },
            { QStringLiteral("enabled"), remap.enabled },
            { QStringLiteral("sourceKey"), remap.sourceKey },
            { QStringLiteral("sourceLabel"), sourceLabel },
            { QStringLiteral("targetLabel"), targetLabel },
            { QStringLiteral("levels"), levels },
            // 1 or 2 for a layer mapping or a layer key; 0 otherwise.
            { QStringLiteral("layer"), qMax(layerOfMapping(remap), layerOfChooser(remap)) },
            { QStringLiteral("xkbOption"), remap.xkbOption },
            { QStringLiteral("note"), remap.note },
            { QStringLiteral("supported"), m_engine.supports(remap.kind) },
            { QStringLiteral("limit"), m_engine.limitFor(remap.kind) },
        });
    }

    return list;
}

namespace {

// A recipe is a list of ordinary mappings: adding one is exactly what adding
// its mappings by hand in the editor would do, nothing the editor cannot.
struct Recipe {
    QString id;
    QString title;
    QString detail;
    QStringList from;    // keys pressed, drawn as keycaps
    QStringList to;      // what they become
    QVector<Remap> remaps;
};

Remap recipeRemap(MappingKind kind, const QString &key, const QStringList &levels,
                  const QString &option = {})
{
    Remap remap;
    remap.kind = kind;
    remap.sourceKey = key;
    remap.levels = levels;
    remap.xkbOption = option;
    return remap;
}

Remap layerRemap(const QString &key, const QString &keysym)
{
    return recipeRemap(MappingKind::LayerKey, key, { QString(), QString(), keysym, keysym });
}

const QVector<Recipe> &recipes()
{
    static const QVector<Recipe> list = {
        { QStringLiteral("caps-control"),
          QStringLiteral("Caps Lock becomes Control"),
          QStringLiteral("The classic: Control where your little finger already rests."),
          { QStringLiteral("Caps Lock") }, { QStringLiteral("Ctrl") },
          { recipeRemap(MappingKind::ModifierRemap, QStringLiteral("CAPS"),
                        { QStringLiteral("Control_L") }) } },
        { QStringLiteral("caps-escape"),
          QStringLiteral("Caps Lock becomes Escape"),
          QStringLiteral("For Vim and anything else that lives on Escape."),
          { QStringLiteral("Caps Lock") }, { QStringLiteral("Esc") },
          { recipeRemap(MappingKind::KeyRemap, QStringLiteral("CAPS"),
                        { QStringLiteral("Escape") }) } },
        { QStringLiteral("caps-arrows"),
          QStringLiteral("Arrows under your right hand"),
          QStringLiteral("Hold Caps Lock and H J K L move the cursor, the way Vim does."),
          { QStringLiteral("Caps Lock"), QStringLiteral("H"), QStringLiteral("J"),
            QStringLiteral("K"), QStringLiteral("L") },
          { QStringLiteral("←"), QStringLiteral("↓"), QStringLiteral("↑"),
            QStringLiteral("→") },
          { recipeRemap(MappingKind::LayerChooser, QStringLiteral("CAPS"),
                        { QStringLiteral("ISO_Level3_Shift") }),
            layerRemap(QStringLiteral("AC06"), QStringLiteral("Left")),
            layerRemap(QStringLiteral("AC07"), QStringLiteral("Down")),
            layerRemap(QStringLiteral("AC08"), QStringLiteral("Up")),
            layerRemap(QStringLiteral("AC09"), QStringLiteral("Right")) } },
        { QStringLiteral("swap-alt-super"),
          QStringLiteral("Swap Alt and Super"),
          QStringLiteral("Puts Super next to the space bar, where Command sits on a Mac."),
          { QStringLiteral("Alt") }, { QStringLiteral("Super") },
          { recipeRemap(MappingKind::XkbOption, QString(), {},
                        QStringLiteral("altwin:swap_alt_win")) } },
    };
    return list;
}

} // namespace

QVariantList ConfigRepository::recipeList(const QString &profileId) const
{
    const Profile *profile = m_ruleSet.profileById(profileId);
    const KeyCatalog &keys = KeyCatalog::instance();

    QVariantList list;
    for (const Recipe &recipe : recipes()) {
        bool available = profile != nullptr;
        if (profile) {
            for (const Remap &wanted : recipe.remaps) {
                for (const Remap &existing : profile->remaps) {
                    const bool sameKey = !wanted.sourceKey.isEmpty()
                        && existing.sourceKey == wanted.sourceKey;
                    const bool sameOption = !wanted.xkbOption.isEmpty()
                        && existing.xkbOption == wanted.xkbOption;
                    // Only another key for the same layer is in the way; a
                    // layer-2 key does not stop a layer-1 recipe.
                    const bool secondLayerKey = wanted.kind == MappingKind::LayerChooser
                        && existing.kind == MappingKind::LayerChooser && existing.enabled
                        && layerOfChooser(existing) == layerOfChooser(wanted);
                    if (sameKey || sameOption || secondLayerKey)
                        available = false;
                }
            }
        }

        // The letters follow the keyboard's layout: the keys are positions,
        // and on a layout where H is somewhere else the label says so.
        QStringList from = recipe.from;
        if (recipe.id == QStringLiteral("caps-arrows")) {
            for (int i = 1; i < recipe.remaps.size() && i < from.size(); ++i)
                from[i] = keys.labelForCode(recipe.remaps.at(i).sourceKey);
        }

        list.append(QVariantMap{
            { QStringLiteral("id"), recipe.id },
            { QStringLiteral("title"), recipe.title },
            { QStringLiteral("detail"), recipe.detail },
            { QStringLiteral("from"), from },
            { QStringLiteral("to"), recipe.to },
            { QStringLiteral("available"), available },
        });
    }
    return list;
}

bool ConfigRepository::addRecipe(const QString &profileId, const QString &recipeId)
{
    for (const Recipe &recipe : recipes()) {
        if (recipe.id != recipeId)
            continue;

        const QVariantList current = recipeList(profileId);
        for (const QVariant &entry : current) {
            const QVariantMap map = entry.toMap();
            if (map.value(QStringLiteral("id")).toString() == recipeId
                && !map.value(QStringLiteral("available")).toBool())
                return false;
        }

        bool added = false;
        for (const Remap &remap : recipe.remaps) {
            QVariantList levels;
            for (const QString &level : remap.levels)
                levels.append(level);
            added = !addMapping(profileId, mappingKindName(remap.kind), remap.sourceKey,
                                levels, remap.xkbOption, QString()).isEmpty()
                || added;
        }
        return added;
    }
    return false;
}

QString ConfigRepository::addMapping(const QString &profileId, const QString &kind,
                                     const QString &sourceKey, const QVariantList &levels,
                                     const QString &xkbOption, const QString &note)
{
    Profile *profile = profileFor(profileId);
    if (!profile)
        return {};

    Remap remap;
    remap.id = RuleSetSerializer::newId(QStringLiteral("map"));
    remap.kind = mappingKindFromName(kind);
    if (remap.kind == MappingKind::Unknown)
        return {};
    remap.sourceKey = sourceKey;
    remap.xkbOption = xkbOption;
    remap.note = note;
    for (const QVariant &level : levels)
        remap.levels.append(level.toString());

    profile->remaps.append(remap);
    setDirty(true);
    refreshConflicts();
    emit mappingsChanged(profile->id);
    return remap.id;
}

bool ConfigRepository::updateMapping(const QString &profileId, const QString &mappingId,
                                     const QString &kind, const QString &sourceKey,
                                     const QVariantList &levels, const QString &xkbOption,
                                     const QString &note)
{
    Profile *profile = profileFor(profileId);
    if (!profile)
        return false;

    for (Remap &remap : profile->remaps) {
        if (remap.id != mappingId)
            continue;
        const MappingKind parsed = mappingKindFromName(kind);
        if (parsed != MappingKind::Unknown)
            remap.kind = parsed;
        remap.sourceKey = sourceKey;
        remap.xkbOption = xkbOption;
        remap.note = note;
        remap.levels.clear();
        for (const QVariant &level : levels)
            remap.levels.append(level.toString());

        setDirty(true);
        refreshConflicts();
        emit mappingsChanged(profile->id);
        return true;
    }
    return false;
}

bool ConfigRepository::removeMapping(const QString &profileId, const QString &mappingId)
{
    Profile *profile = profileFor(profileId);
    if (!profile)
        return false;

    for (int index = 0; index < profile->remaps.size(); ++index) {
        if (profile->remaps.at(index).id != mappingId)
            continue;
        Removal removal;
        removal.kind = Removal::Kind::Mapping;
        removal.profileId = profile->id;
        removal.index = index;
        removal.remap = profile->remaps.at(index);
        const Remap &gone = removal.remap;
        const QString key = gone.kind == MappingKind::XkbOption
            ? QStringLiteral("keyboard option")
            : KeyCatalog::instance().labelForCode(gone.sourceKey);
        removal.label = QStringLiteral("Deleted the %1 mapping").arg(key);
        profile->remaps.removeAt(index);
        setDirty(true);
        refreshConflicts();
        emit mappingsChanged(profile->id);
        rememberRemoval(removal);
        return true;
    }
    return false;
}

bool ConfigRepository::setMappingEnabled(const QString &profileId, const QString &mappingId,
                                         bool enabled)
{
    Profile *profile = profileFor(profileId);
    if (!profile)
        return false;

    for (Remap &remap : profile->remaps) {
        if (remap.id != mappingId)
            continue;
        remap.enabled = enabled;
        setDirty(true);
        refreshConflicts();
        emit mappingsChanged(profile->id);
        return true;
    }
    return false;
}

QVariantMap ConfigRepository::behaviourFor(const QString &profileId) const
{
    const Profile *profile = m_ruleSet.profileById(profileId);
    if (!profile)
        return {};

    const KeyboardBehaviour &behaviour = profile->behaviour;

    // What the system is doing right now, so an inherited setting can be shown
    // as the number it actually resolves to instead of a blank.
    const auto liveNumber = [this](const QString &key) -> int {
        bool ok = false;
        const int parsed = m_reload.option(QStringLiteral("input:") + key).toInt(&ok);
        return ok ? parsed : -1;
    };

    return QVariantMap{
        { QStringLiteral("profileId"), profile->id },
        { QStringLiteral("isGlobal"), profile->isGlobal() },
        { QStringLiteral("repeatRate"), behaviour.repeatRate },
        { QStringLiteral("repeatDelay"), behaviour.repeatDelay },
        { QStringLiteral("numlockOnStart"), tristateName(behaviour.numlockOnStart) },
        { QStringLiteral("resolveBindsBySym"), tristateName(behaviour.resolveBindsBySym) },
        { QStringLiteral("shortcutsEnabled"), tristateName(behaviour.shortcutsEnabled) },
        { QStringLiteral("liveRepeatRate"), liveNumber(QStringLiteral("repeat_rate")) },
        { QStringLiteral("liveRepeatDelay"), liveNumber(QStringLiteral("repeat_delay")) },
        // Only a device can turn shortcuts off; Hyprland has no input.keybinds.
        { QStringLiteral("shortcutsSettable"), !profile->isGlobal() },
        { QStringLiteral("isEmpty"), behaviour.isEmpty() },
    };
}

bool ConfigRepository::setBehaviourNumber(const QString &profileId, const QString &key, int value)
{
    Profile *profile = profileFor(profileId);
    if (!profile)
        return false;

    // A negative value is how the UI says "go back to inheriting".
    const int resolved = value < 0 ? -1 : value;
    if (key == QStringLiteral("repeatRate"))
        profile->behaviour.repeatRate = resolved;
    else if (key == QStringLiteral("repeatDelay"))
        profile->behaviour.repeatDelay = resolved;
    else
        return false;

    setDirty(true);
    refreshConflicts();
    emit stateReloaded();
    return true;
}

bool ConfigRepository::setBehaviourFlag(const QString &profileId, const QString &key,
                                        const QString &state)
{
    Profile *profile = profileFor(profileId);
    if (!profile)
        return false;

    const Tristate value = tristateFromName(state);
    if (key == QStringLiteral("numlockOnStart"))
        profile->behaviour.numlockOnStart = value;
    else if (key == QStringLiteral("resolveBindsBySym"))
        profile->behaviour.resolveBindsBySym = value;
    else if (key == QStringLiteral("shortcutsEnabled")) {
        if (profile->isGlobal())
            return false;
        profile->behaviour.shortcutsEnabled = value;
    } else {
        return false;
    }

    setDirty(true);
    refreshConflicts();
    emit stateReloaded();
    return true;
}

bool ConfigRepository::clearBehaviour(const QString &profileId)
{
    Profile *profile = profileFor(profileId);
    if (!profile)
        return false;
    profile->behaviour = KeyboardBehaviour();
    setDirty(true);
    refreshConflicts();
    emit stateReloaded();
    return true;
}

QVariantList ConfigRepository::shortcutList() const
{
    QVariantList list;
    for (const Shortcut &shortcut : m_ruleSet.shortcuts) {
        list.append(QVariantMap{
            { QStringLiteral("id"), shortcut.id },
            { QStringLiteral("enabled"), shortcut.enabled },
            { QStringLiteral("keys"), shortcut.keys },
            { QStringLiteral("description"), shortcut.description },
            { QStringLiteral("actionKind"), actionKindName(shortcut.actionKind) },
            { QStringLiteral("actionLabel"), actionKindLabel(shortcut.actionKind) },
            { QStringLiteral("actionValue"), shortcut.actionValue },
            { QStringLiteral("focusMatch"), shortcut.focusMatch },
            { QStringLiteral("unbindFirst"), shortcut.unbindFirst },
        });
    }
    return list;
}

QString ConfigRepository::addShortcut(const QString &keys, const QString &description,
                                      const QString &actionKind, const QString &actionValue,
                                      const QString &focusMatch)
{
    Shortcut shortcut;
    shortcut.id = RuleSetSerializer::newId(QStringLiteral("sc"));
    shortcut.keys = keys;
    shortcut.description = description;
    shortcut.actionKind = actionKindFromName(actionKind);
    shortcut.actionValue = actionValue;
    shortcut.focusMatch = focusMatch;

    m_ruleSet.shortcuts.append(shortcut);
    setDirty(true);
    refreshConflicts();
    emit shortcutsChanged();
    return shortcut.id;
}

bool ConfigRepository::updateShortcut(const QString &shortcutId, const QString &keys,
                                      const QString &description, const QString &actionKind,
                                      const QString &actionValue, const QString &focusMatch)
{
    for (Shortcut &shortcut : m_ruleSet.shortcuts) {
        if (shortcut.id != shortcutId)
            continue;
        shortcut.keys = keys;
        shortcut.description = description;
        shortcut.actionKind = actionKindFromName(actionKind);
        shortcut.actionValue = actionValue;
        shortcut.focusMatch = focusMatch;
        setDirty(true);
        refreshConflicts();
        emit shortcutsChanged();
        return true;
    }
    return false;
}

bool ConfigRepository::removeShortcut(const QString &shortcutId)
{
    for (int index = 0; index < m_ruleSet.shortcuts.size(); ++index) {
        if (m_ruleSet.shortcuts.at(index).id != shortcutId)
            continue;
        Removal removal;
        removal.kind = Removal::Kind::Shortcut;
        removal.index = index;
        removal.shortcut = m_ruleSet.shortcuts.at(index);
        removal.label = QStringLiteral("Deleted %1").arg(removal.shortcut.keys);
        m_ruleSet.shortcuts.removeAt(index);
        setDirty(true);
        refreshConflicts();
        emit shortcutsChanged();
        rememberRemoval(removal);
        return true;
    }
    return false;
}

bool ConfigRepository::setShortcutEnabled(const QString &shortcutId, bool enabled)
{
    for (Shortcut &shortcut : m_ruleSet.shortcuts) {
        if (shortcut.id != shortcutId)
            continue;
        shortcut.enabled = enabled;
        setDirty(true);
        refreshConflicts();
        emit shortcutsChanged();
        return true;
    }
    return false;
}

bool ConfigRepository::setShortcutUnbindFirst(const QString &shortcutId, bool unbindFirst)
{
    for (Shortcut &shortcut : m_ruleSet.shortcuts) {
        if (shortcut.id != shortcutId)
            continue;
        shortcut.unbindFirst = unbindFirst;
        setDirty(true);
        refreshConflicts();
        emit shortcutsChanged();
        return true;
    }
    return false;
}

void ConfigRepository::rememberRemoval(const Removal &removal)
{
    m_removal = removal;
    emit undoChanged();
}

void ConfigRepository::dismissUndo()
{
    if (m_removal.kind == Removal::Kind::None)
        return;
    m_removal = Removal();
    emit undoChanged();
}

bool ConfigRepository::differsFromSaved() const
{
    return RuleSetSerializer::toJsonBytes(ruleSetForSaving()) != m_savedRules;
}

bool ConfigRepository::undoRemoval()
{
    const Removal removal = m_removal;
    bool restored = false;

    switch (removal.kind) {
    case Removal::Kind::None:
        return false;

    case Removal::Kind::Mapping: {
        Profile *profile = profileFor(removal.profileId);
        if (!profile)
            break;
        // Other edits may have happened since; the position is a preference,
        // not a promise.
        const int at = qBound(0, removal.index, static_cast<int>(profile->remaps.size()));
        profile->remaps.insert(at, removal.remap);
        refreshConflicts();
        emit mappingsChanged(profile->id);
        restored = true;
        break;
    }

    case Removal::Kind::Shortcut: {
        const int at = qBound(0, removal.index, static_cast<int>(m_ruleSet.shortcuts.size()));
        m_ruleSet.shortcuts.insert(at, removal.shortcut);
        refreshConflicts();
        emit shortcutsChanged();
        restored = true;
        break;
    }

    case Removal::Kind::Profile: {
        // Selecting the keyboard again may have created an empty profile for
        // it in the meantime; the restored one replaces it.
        for (int i = m_ruleSet.profiles.size() - 1; i >= 0; --i) {
            const Profile &existing = m_ruleSet.profiles.at(i);
            if (existing.id == removal.profile.id
                || (!existing.deviceName.isEmpty()
                    && existing.deviceName == removal.profile.deviceName
                    && existing.remaps.isEmpty()))
                m_ruleSet.profiles.removeAt(i);
        }
        const int at = qBound(0, removal.index, static_cast<int>(m_ruleSet.profiles.size()));
        m_ruleSet.profiles.insert(at, removal.profile);
        refreshConflicts();
        emit stateReloaded();
        setActiveProfileId(removal.profile.id);
        restored = true;
        break;
    }
    }

    m_removal = Removal();
    emit undoChanged();
    if (restored)
        setDirty(differsFromSaved());
    return restored;
}

namespace {

struct ShortcutRecipe {
    QString id;
    QString title;
    QString detail;
    QStringList candidates;   // key combinations, in order of preference
    ActionKind kind;
    QString value;
    QString description;      // what the binding is called in Omarchy's list
};

const QVector<ShortcutRecipe> &shortcutRecipes()
{
    // Things Omarchy does not bind by default, so a recipe adds something
    // rather than duplicating a shortcut the user already has.
    static const QVector<ShortcutRecipe> list = {
        { QStringLiteral("close-super-q"),
          QStringLiteral("Close windows like a Mac"),
          QStringLiteral("Super + Q closes the window, as Command-Q does. Super + W keeps working."),
          { QStringLiteral("SUPER + Q") },
          ActionKind::WindowAction, QStringLiteral("window.close"),
          QStringLiteral("Close window") },
        { QStringLiteral("cycle-windows"),
          QStringLiteral("Cycle windows"),
          QStringLiteral("Step through the windows on this workspace, like Command-` on a Mac."),
          { QStringLiteral("SUPER + grave"), QStringLiteral("ALT + grave") },
          ActionKind::WindowAction, QStringLiteral("window.cycle_next"),
          QStringLiteral("Cycle windows") },
        { QStringLiteral("centre-window"),
          QStringLiteral("Centre the window"),
          QStringLiteral("Puts a floating window back in the middle of the screen."),
          { QStringLiteral("SUPER + ALT + C"), QStringLiteral("SUPER + CTRL + ALT + C") },
          ActionKind::WindowAction, QStringLiteral("window.center"),
          QStringLiteral("Centre window") },
        { QStringLiteral("open-omakeyboard"),
          QStringLiteral("Open OmaKeyboard"),
          QStringLiteral("Next to Super + K, which lists every shortcut."),
          { QStringLiteral("SUPER + SHIFT + K"), QStringLiteral("SUPER + CTRL + SHIFT + K") },
          ActionKind::DesktopApp, QStringLiteral("omakeyboard"),
          QStringLiteral("OmaKeyboard") },
    };
    return list;
}

} // namespace

QVariantList ConfigRepository::shortcutRecipeList() const
{
    QSet<QString> taken;
    for (const ExistingBinding &binding : m_existingBindings)
        taken.insert(binding.normalizedKeys);
    for (const Shortcut &shortcut : m_ruleSet.shortcuts)
        taken.insert(OmarchyConfigParser::normalizeKeys(shortcut.keys));

    QVariantList list;
    for (const ShortcutRecipe &recipe : shortcutRecipes()) {
        // Already added: the same action is bound by one of the user's own
        // shortcuts.
        bool added = false;
        for (const Shortcut &shortcut : m_ruleSet.shortcuts)
            if (shortcut.actionKind == recipe.kind && shortcut.actionValue == recipe.value)
                added = true;

        QString keys;
        if (!added) {
            for (const QString &candidate : recipe.candidates) {
                if (!taken.contains(OmarchyConfigParser::normalizeKeys(candidate))) {
                    keys = candidate;
                    break;
                }
            }
        }

        list.append(QVariantMap{
            { QStringLiteral("id"), recipe.id },
            { QStringLiteral("title"), recipe.title },
            { QStringLiteral("detail"), recipe.detail },
            { QStringLiteral("keys"), keys.isEmpty() ? recipe.candidates.constFirst() : keys },
            { QStringLiteral("action"), recipe.description },
            { QStringLiteral("available"), !keys.isEmpty() },
            { QStringLiteral("reason"), added
                  ? QStringLiteral("Already in your shortcuts.")
                  : QStringLiteral("Every key combination it could use is taken.") },
        });
    }
    return list;
}

bool ConfigRepository::addShortcutRecipe(const QString &recipeId)
{
    for (const QVariant &value : shortcutRecipeList()) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("id")).toString() != recipeId)
            continue;
        if (!entry.value(QStringLiteral("available")).toBool())
            return false;
        for (const ShortcutRecipe &recipe : shortcutRecipes()) {
            if (recipe.id != recipeId)
                continue;
            return !addShortcut(entry.value(QStringLiteral("keys")).toString(),
                                recipe.description, actionKindName(recipe.kind),
                                recipe.value, QString()).isEmpty();
        }
    }
    return false;
}

namespace {

struct MapKey {
    const char *code;
    double width;
};

// Short names for keys whose full name would not fit on a cap.
QString capLabelForCode(const QString &code)
{
    static const QHash<QString, QString> names = {
        { QStringLiteral("TLDE"), QString() }, { QStringLiteral("BKSP"), QStringLiteral("⌫") },
        { QStringLiteral("TAB"), QStringLiteral("Tab") }, { QStringLiteral("CAPS"), QStringLiteral("Caps") },
        { QStringLiteral("RTRN"), QStringLiteral("Enter") }, { QStringLiteral("LFSH"), QStringLiteral("Shift") },
        { QStringLiteral("RTSH"), QStringLiteral("Shift") }, { QStringLiteral("LCTL"), QStringLiteral("Ctrl") },
        { QStringLiteral("RCTL"), QStringLiteral("Ctrl") }, { QStringLiteral("LALT"), QStringLiteral("Alt") },
        { QStringLiteral("RALT"), QStringLiteral("AltGr") }, { QStringLiteral("LWIN"), QStringLiteral("Super") },
        { QStringLiteral("RWIN"), QStringLiteral("Super") }, { QStringLiteral("MENU"), QStringLiteral("Menu") },
        { QStringLiteral("COMP"), QStringLiteral("Menu") },
        { QStringLiteral("SPCE"), QString() }, { QStringLiteral("ESC"), QStringLiteral("Esc") },
    };
    const auto found = names.constFind(code);
    const QString label = found != names.constEnd() ? *found : QString();
    if (found != names.constEnd() && !label.isEmpty())
        return label;
    if (code == QStringLiteral("SPCE"))
        return QString();
    // Positional keys take what they type on this layout ("H", "Ñ", "[").
    const QString character = KeyCatalog::instance().capCharacter(code);
    if (!character.isEmpty())
        return character;
    const QString typed = KeyCatalog::instance().labelForCode(code);
    return typed.size() <= 2 ? typed.toUpper() : typed;
}

// What a key produces, short enough for a cap.
QString capLabelForKeysym(const QString &keysym)
{
    static const QHash<QString, QString> names = {
        { QStringLiteral("Left"), QStringLiteral("←") }, { QStringLiteral("Right"), QStringLiteral("→") },
        { QStringLiteral("Up"), QStringLiteral("↑") }, { QStringLiteral("Down"), QStringLiteral("↓") },
        { QStringLiteral("Home"), QStringLiteral("Home") }, { QStringLiteral("End"), QStringLiteral("End") },
        { QStringLiteral("Prior"), QStringLiteral("PgUp") }, { QStringLiteral("Next"), QStringLiteral("PgDn") },
        { QStringLiteral("BackSpace"), QStringLiteral("⌫") }, { QStringLiteral("Delete"), QStringLiteral("Del") },
        { QStringLiteral("Escape"), QStringLiteral("Esc") }, { QStringLiteral("Return"), QStringLiteral("Enter") },
        { QStringLiteral("Tab"), QStringLiteral("Tab") }, { QStringLiteral("space"), QStringLiteral("Space") },
        { QStringLiteral("Control_L"), QStringLiteral("Ctrl") }, { QStringLiteral("Control_R"), QStringLiteral("Ctrl") },
        { QStringLiteral("Super_L"), QStringLiteral("Super") }, { QStringLiteral("Super_R"), QStringLiteral("Super") },
        { QStringLiteral("Alt_L"), QStringLiteral("Alt") }, { QStringLiteral("Alt_R"), QStringLiteral("Alt") },
        { QStringLiteral("Shift_L"), QStringLiteral("Shift") }, { QStringLiteral("Shift_R"), QStringLiteral("Shift") },
        { QStringLiteral("Hyper_L"), QStringLiteral("Hyper") }, { QStringLiteral("Caps_Lock"), QStringLiteral("Caps") },
        { QStringLiteral("Multi_key"), QStringLiteral("Compose") }, { QStringLiteral("NoSymbol"), QStringLiteral("—") },
        { QStringLiteral("XF86AudioRaiseVolume"), QStringLiteral("Vol+") },
        { QStringLiteral("XF86AudioLowerVolume"), QStringLiteral("Vol−") },
        { QStringLiteral("XF86AudioMute"), QStringLiteral("Mute") },
        { QStringLiteral("XF86AudioPlay"), QStringLiteral("Play") },
        { QStringLiteral("XF86AudioNext"), QStringLiteral("Next") },
        { QStringLiteral("XF86AudioPrev"), QStringLiteral("Prev") },
        { QStringLiteral("XF86MonBrightnessUp"), QStringLiteral("Bri+") },
        { QStringLiteral("XF86MonBrightnessDown"), QStringLiteral("Bri−") },
    };
    const auto found = names.constFind(keysym);
    if (found != names.constEnd())
        return *found;
    if (keysym.size() == 1)
        return keysym.toUpper();
    const QString character = KeyCatalog::characterForKeysym(keysym);
    if (!character.isEmpty())
        return character.toUpper();
    return KeyCatalog::instance().labelForKeysym(keysym);
}

// Layouts drawn with the ANSI Enter and left Shift; everything else is ISO.
// A pc104 model says ANSI and pc105 says ISO outright, whatever the layout.
bool usesIso(const QString &layout, const QString &model)
{
    if (model == QStringLiteral("pc104"))
        return false;
    if (model == QStringLiteral("pc105"))
        return true;
    static const QSet<QString> ansi = {
        QStringLiteral("us"), QStringLiteral("ru"), QStringLiteral("ua"), QStringLiteral("il"),
        QStringLiteral("th"), QStringLiteral("kr"), QStringLiteral("cn"), QStringLiteral("tw"),
        QStringLiteral("ara"), QStringLiteral("ir"),
    };
    const QString first = layout.section(QLatin1Char(','), 0, 0).trimmed();
    return !first.isEmpty() && !ansi.contains(first);
}

} // namespace

namespace {

constexpr const char *kExportKind = "omakeyboard-configuration";

// The rules inside an exported file, which is also a plain rules.json when
// someone shares that instead.
QJsonObject rulesObjectOf(const QJsonObject &document)
{
    const QJsonValue rules = document.value(QStringLiteral("rules"));
    if (rules.isObject())
        return rules.toObject();
    return document;
}

QJsonObject readJsonFile(const QString &filePath, QString *error)
{
    bool read = false;
    const QByteArray raw = Paths::read(filePath, &read);
    if (!read) {
        if (error)
            *error = QStringLiteral("Could not read %1.").arg(filePath);
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("This is not an OmaKeyboard file: %1")
                         .arg(parseError.errorString());
        return {};
    }
    return document.object();
}

} // namespace

QVariantMap ConfigRepository::exportToFile(const QString &filePath) const
{
    QString path = filePath;
    if (path.startsWith(QStringLiteral("file://")))
        path = QUrl(path).toLocalFile();
    if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
        path += QStringLiteral(".json");

    const RuleSet rules = ruleSetForSaving();

    QJsonObject document;
    document.insert(QStringLiteral("kind"), QString::fromLatin1(kExportKind));
    document.insert(QStringLiteral("app"), QCoreApplication::applicationVersion());
    document.insert(QStringLiteral("exported"),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    document.insert(QStringLiteral("rules"), RuleSetSerializer::toJson(rules));

    QString error;
    const QByteArray bytes = QJsonDocument(document).toJson(QJsonDocument::Indented);
    if (!Paths::writeAtomically(path, bytes, &error)) {
        return QVariantMap{ { QStringLiteral("ok"), false },
                            { QStringLiteral("message"), error } };
    }

    int mappings = 0;
    for (const Profile &profile : rules.profiles)
        mappings += profile.remaps.size();

    return QVariantMap{
        { QStringLiteral("ok"), true },
        { QStringLiteral("path"), path },
        { QStringLiteral("profiles"), rules.profiles.size() },
        { QStringLiteral("mappings"), mappings },
        { QStringLiteral("shortcuts"), rules.shortcuts.size() },
    };
}

QVariantMap ConfigRepository::inspectImport(const QString &filePath) const
{
    QString path = filePath;
    if (path.startsWith(QStringLiteral("file://")))
        path = QUrl(path).toLocalFile();

    QString error;
    const QJsonObject document = readJsonFile(path, &error);
    if (!error.isEmpty())
        return QVariantMap{ { QStringLiteral("ok"), false }, { QStringLiteral("error"), error } };

    QStringList warnings;
    const RuleSet rules = RuleSetSerializer::fromJson(rulesObjectOf(document), &warnings);
    if (rules.profiles.isEmpty() && rules.shortcuts.isEmpty()) {
        return QVariantMap{ { QStringLiteral("ok"), false },
                            { QStringLiteral("error"),
                              QStringLiteral("This file has no keyboards and no shortcuts in it.") } };
    }

    // Where each keyboard in the file could land here. A device that is
    // attached by the same name is the obvious answer; anything else is a
    // question for the user, because device names differ between machines.
    QVariantList profiles;
    for (const Profile &profile : rules.profiles) {
        int mappings = 0;
        for (const Remap &remap : profile.remaps)
            if (remap.enabled)
                ++mappings;

        const bool isGlobal = profile.isGlobal();
        const bool attached = !profile.deviceName.isEmpty()
            && m_devices.find(profile.deviceName) != nullptr;

        profiles.append(QVariantMap{
            { QStringLiteral("name"), profile.name.isEmpty()
                                          ? DeviceService::prettify(profile.deviceName)
                                          : profile.name },
            { QStringLiteral("deviceName"), profile.deviceName },
            { QStringLiteral("isGlobal"), isGlobal },
            { QStringLiteral("mappingCount"), mappings },
            { QStringLiteral("attached"), attached },
            { QStringLiteral("suggested"), isGlobal ? QStringLiteral("global")
                          : attached ? profile.deviceName
                          : QString() },
        });
    }

    return QVariantMap{
        { QStringLiteral("ok"), true },
        { QStringLiteral("path"), path },
        { QStringLiteral("exported"), document.value(QStringLiteral("exported")).toString() },
        { QStringLiteral("profiles"), profiles },
        { QStringLiteral("shortcutCount"), rules.shortcuts.size() },
        { QStringLiteral("warnings"), warnings },
    };
}

QVariantMap ConfigRepository::importFromFile(const QString &filePath, const QVariantList &targets,
                                             bool includeShortcuts)
{
    QString path = filePath;
    if (path.startsWith(QStringLiteral("file://")))
        path = QUrl(path).toLocalFile();

    QString error;
    const QJsonObject document = readJsonFile(path, &error);
    if (!error.isEmpty())
        return QVariantMap{ { QStringLiteral("ok"), false }, { QStringLiteral("error"), error } };

    QStringList warnings;
    const RuleSet incoming = RuleSetSerializer::fromJson(rulesObjectOf(document), &warnings);

    int addedMappings = 0;
    int addedShortcuts = 0;
    int skipped = 0;
    QStringList touched;

    for (int index = 0; index < incoming.profiles.size(); ++index) {
        const Profile &source = incoming.profiles.at(index);
        const QString target = index < targets.size() ? targets.at(index).toString() : QString();
        if (target.isEmpty() || target == QStringLiteral("skip")) {
            ++skipped;
            continue;
        }

        const QString deviceName = target == QStringLiteral("global") ? QString() : target;
        const QString profileId = ensureProfileForDevice(
            deviceName, deviceName.isEmpty() ? QString() : DeviceService::prettify(deviceName));
        Profile *destination = profileFor(profileId);
        if (!destination) {
            ++skipped;
            continue;
        }

        // Ids are rewritten: two people's files would otherwise collide, and a
        // mapping brought in twice must not overwrite the first copy.
        for (const Remap &remap : source.remaps) {
            Remap copy = remap;
            copy.id = RuleSetSerializer::newId(QStringLiteral("map"));
            destination->remaps.append(copy);
            ++addedMappings;
        }
        for (const QString &option : source.xkbOptions)
            if (!destination->xkbOptions.contains(option))
                destination->xkbOptions.append(option);
        // Keyboard behaviour only fills in what this keyboard does not set
        // itself, so importing never silently changes someone's repeat rate.
        if (destination->behaviour.isEmpty() && !source.behaviour.isEmpty())
            destination->behaviour = source.behaviour;

        const QString label = destination->isGlobal()
            ? QStringLiteral("All keyboards")
            : (destination->name.isEmpty() ? DeviceService::prettify(destination->deviceName)
                                           : destination->name);
        if (!touched.contains(label))
            touched.append(label);
        emit mappingsChanged(destination->id);
    }

    if (includeShortcuts) {
        for (const Shortcut &shortcut : incoming.shortcuts) {
            Shortcut copy = shortcut;
            copy.id = RuleSetSerializer::newId(QStringLiteral("sc"));
            m_ruleSet.shortcuts.append(copy);
            ++addedShortcuts;
        }
        if (addedShortcuts > 0)
            emit shortcutsChanged();
    }

    if (addedMappings > 0 || addedShortcuts > 0) {
        setDirty(true);
        dismissUndo();
        refreshConflicts();
        emit stateReloaded();
    }

    return QVariantMap{
        { QStringLiteral("ok"), addedMappings > 0 || addedShortcuts > 0 },
        { QStringLiteral("mappings"), addedMappings },
        { QStringLiteral("shortcuts"), addedShortcuts },
        { QStringLiteral("skipped"), skipped },
        { QStringLiteral("keyboards"), touched },
        { QStringLiteral("warnings"), warnings },
    };
}

QVariantMap ConfigRepository::keyboardMap(const QString &profileId) const
{
    const Profile *profile = m_ruleSet.profileById(profileId);
    const bool iso = usesIso(m_devices.effectiveLayout(), m_devices.effectiveModel());

    const QVector<QVector<MapKey>> ansiRows = {
        { { "TLDE", 1 }, { "AE01", 1 }, { "AE02", 1 }, { "AE03", 1 }, { "AE04", 1 }, { "AE05", 1 },
          { "AE06", 1 }, { "AE07", 1 }, { "AE08", 1 }, { "AE09", 1 }, { "AE10", 1 }, { "AE11", 1 },
          { "AE12", 1 }, { "BKSP", 2 } },
        { { "TAB", 1.5 }, { "AD01", 1 }, { "AD02", 1 }, { "AD03", 1 }, { "AD04", 1 }, { "AD05", 1 },
          { "AD06", 1 }, { "AD07", 1 }, { "AD08", 1 }, { "AD09", 1 }, { "AD10", 1 }, { "AD11", 1 },
          { "AD12", 1 }, { "BKSL", 1.5 } },
        { { "CAPS", 1.75 }, { "AC01", 1 }, { "AC02", 1 }, { "AC03", 1 }, { "AC04", 1 }, { "AC05", 1 },
          { "AC06", 1 }, { "AC07", 1 }, { "AC08", 1 }, { "AC09", 1 }, { "AC10", 1 }, { "AC11", 1 },
          { "RTRN", 2.25 } },
        { { "LFSH", 2.25 }, { "AB01", 1 }, { "AB02", 1 }, { "AB03", 1 }, { "AB04", 1 }, { "AB05", 1 },
          { "AB06", 1 }, { "AB07", 1 }, { "AB08", 1 }, { "AB09", 1 }, { "AB10", 1 }, { "RTSH", 2.75 } },
        { { "LCTL", 1.25 }, { "LWIN", 1.25 }, { "LALT", 1.25 }, { "SPCE", 6.25 }, { "RALT", 1.25 },
          { "RWIN", 1.25 }, { "COMP", 1.25 }, { "RCTL", 1.25 } },
    };
    QVector<QVector<MapKey>> rows = ansiRows;
    if (iso) {
        // ISO: the key beside Enter moves down a row, Enter grows into the
        // row above (drawn here as its lower half, 1.25 wide), and a key
        // appears beside the left Shift.
        rows[1].last() = { "RTRN", 1.5 };
        rows[2].last() = { "BKSL", 1 };
        rows[2].append({ "RTRN", 1.25 });
        rows[3].first() = { "LFSH", 1.25 };
        rows[3].insert(1, { "LSGT", 1 });
    }

    // What each key does on this profile.
    QHash<QString, QVariantMap> base;     // code -> {label, kind, enabled}
    QHash<QString, QVariantMap> layer;    // code -> {label, enabled}, layer 1
    QHash<QString, QVariantMap> layer2;   // the same for layer 2
    QString layerKey;
    QString layerKey2;
    if (profile) {
        const XkbOptionCatalog &options = XkbOptionCatalog::instance();
        for (const Remap &remap : profile->remaps) {
            switch (remap.kind) {
            case MappingKind::KeyRemap:
            case MappingKind::ModifierRemap:
                base.insert(remap.sourceKey, {
                    { QStringLiteral("label"), capLabelForKeysym(remap.levels.value(0)) },
                    { QStringLiteral("kind"), QStringLiteral("remap") },
                    { QStringLiteral("enabled"), remap.enabled },
                    { QStringLiteral("id"), remap.id } });
                break;
            case MappingKind::LayerChooser:
                base.insert(remap.sourceKey, {
                    { QStringLiteral("label"), QStringLiteral("Layer") },
                    { QStringLiteral("kind"), QStringLiteral("layerKey") },
                    { QStringLiteral("enabled"), remap.enabled },
                    { QStringLiteral("id"), remap.id } });
                if (remap.enabled && layerKey.isEmpty() && layerOfChooser(remap) == 1)
                    layerKey = remap.sourceKey;
                if (remap.enabled && layerKey2.isEmpty() && layerOfChooser(remap) == 2)
                    layerKey2 = remap.sourceKey;
                break;
            case MappingKind::LayerKey:
                (layerOfMapping(remap) == 2 ? layer2 : layer).insert(remap.sourceKey, {
                    { QStringLiteral("label"), capLabelForKeysym(layerOutput(remap)) },
                    { QStringLiteral("enabled"), remap.enabled },
                    { QStringLiteral("id"), remap.id } });
                break;
            case MappingKind::XkbOption:
                if (const XkbOption *option = options.find(remap.xkbOption)) {
                    for (const QString &key : option->claimedKeys) {
                        if (base.contains(key))
                            continue;
                        base.insert(key, {
                            { QStringLiteral("label"), QString() },
                            { QStringLiteral("kind"), QStringLiteral("option") },
                            { QStringLiteral("detail"), option->description },
                            { QStringLiteral("enabled"), remap.enabled },
                    { QStringLiteral("id"), remap.id } });
                    }
                }
                break;
            default:
                break;
            }
        }
    }

    // XKB aliases: a mapping written for <MENU> is the key the map draws as
    // <COMP>, so it is filed under that name.
    static const QHash<QString, QString> aliases = {
        { QStringLiteral("MENU"), QStringLiteral("COMP") },
    };
    for (auto *table : { &base, &layer, &layer2 }) {
        for (auto it = aliases.constBegin(); it != aliases.constEnd(); ++it) {
            if (table->contains(it.key()) && !table->contains(it.value()))
                table->insert(it.value(), table->take(it.key()));
        }
    }
    if (aliases.contains(layerKey))
        layerKey = aliases.value(layerKey);
    if (aliases.contains(layerKey2))
        layerKey2 = aliases.value(layerKey2);

    QVariantList outRows;
    for (const QVector<MapKey> &row : rows) {
        QVariantList keys;
        for (const MapKey &key : row) {
            const QString code = QString::fromLatin1(key.code);
            QVariantMap entry{
                { QStringLiteral("code"), code },
                { QStringLiteral("label"), capLabelForCode(code) },
                { QStringLiteral("name"), KeyCatalog::instance().labelForCode(code) },
                { QStringLiteral("width"), key.width },
            };
            if (base.contains(code))
                entry.insert(QStringLiteral("base"), base.value(code));
            if (layer.contains(code))
                entry.insert(QStringLiteral("layer"), layer.value(code));
            if (layer2.contains(code))
                entry.insert(QStringLiteral("layer2"), layer2.value(code));
            keys.append(entry);
        }
        outRows.append(QVariant(keys));
    }

    return QVariantMap{
        { QStringLiteral("rows"), outRows },
        { QStringLiteral("iso"), iso },
        { QStringLiteral("layerKey"), layerKey },
        { QStringLiteral("layerKeyLabel"),
          layerKey.isEmpty() ? QString() : KeyCatalog::instance().labelForCode(layerKey) },
        { QStringLiteral("layerCount"), layer.size() },
        { QStringLiteral("layerKey2"), layerKey2 },
        { QStringLiteral("layerKey2Label"),
          layerKey2.isEmpty() ? QString() : KeyCatalog::instance().labelForCode(layerKey2) },
        { QStringLiteral("layer2Count"), layer2.size() },
    };
}

QVariantList ConfigRepository::existingBindings() const
{
    QVariantList list;
    for (const ExistingBinding &binding : m_existingBindings) {
        list.append(QVariantMap{
            { QStringLiteral("keys"), binding.keys },
            { QStringLiteral("description"), binding.description },
            { QStringLiteral("action"), binding.action },
            { QStringLiteral("fromOmaKeyboard"), binding.fromOmaKeyboard },
        });
    }
    return list;
}

RuleSet ConfigRepository::ruleSetForSaving() const
{
    // A profile with no rules and no settings carries no information, and one
    // gets created every time a keyboard is selected. Keep them out of
    // rules.json so the file says only what the user actually configured.
    RuleSet saved = m_ruleSet;
    saved.profiles.clear();
    for (const Profile &profile : m_ruleSet.profiles) {
        if (!profile.isGlobal() && profile.remaps.isEmpty() && profile.behaviour.isEmpty())
            continue;
        saved.profiles.append(profile);
    }
    return saved;
}

GeneratedConfig ConfigRepository::generateCurrent() const
{
    return m_writer.generate(m_ruleSet, m_devices.effectiveLayout(),
                             m_devices.effectiveVariant(), m_devices.effectiveModel(),
                             m_inheritedOptions);
}

QVariantMap ConfigRepository::generatedPreview() const
{
    const GeneratedConfig config = generateCurrent();

    QVariantList keymaps;
    for (const GeneratedKeymap &keymap : config.keymaps) {
        keymaps.append(QVariantMap{
            { QStringLiteral("profileId"), keymap.profileId },
            { QStringLiteral("deviceName"), keymap.deviceName },
            { QStringLiteral("path"), keymap.path },
            { QStringLiteral("kbOptions"), keymap.kbOptions },
            { QStringLiteral("source"), keymap.source },
        });
    }

    return QVariantMap{
        { QStringLiteral("ok"), config.ok },
        { QStringLiteral("error"), config.error },
        { QStringLiteral("notes"), config.notes },
        { QStringLiteral("keymaps"), keymaps },
        { QStringLiteral("generatedLua"), config.generatedLua },
        { QStringLiteral("loaderLua"), config.loaderLua },
        { QStringLiteral("rulesJson"),
          QString::fromUtf8(RuleSetSerializer::toJsonBytes(m_ruleSet)) },
        { QStringLiteral("paths"),
          QVariantMap{
              { QStringLiteral("rules"), Paths::rulesFile() },
              { QStringLiteral("generated"), Paths::generatedLuaFile() },
              { QStringLiteral("loader"), Paths::loaderLuaFile() },
              { QStringLiteral("keymaps"), Paths::keymapDir() },
              { QStringLiteral("backups"), Paths::backupDir() },
              { QStringLiteral("hyprland"), Paths::hyprlandLuaFile() },
          } },
    };
}

QVariantMap ConfigRepository::applyResultToVariant(const ApplyResult &result)
{
    QVariantList conflicts;
    for (const Conflict &conflict : result.conflicts)
        conflicts.append(conflictToVariant(conflict));

    // Every stage is listed, reached or not, so the view can show how far the
    // pipeline got and what it never had to do.
    QVariantList stages;
    const bool ranPipeline = !result.stagesDone.isEmpty() || !result.stageFailed.isEmpty();
    if (ranPipeline) {
        static const QList<QPair<QString, QString>> kStages = {
            { QStringLiteral("validate"), QStringLiteral("Check your mappings") },
            { QStringLiteral("conflicts"), QStringLiteral("Look for conflicts") },
            { QStringLiteral("compile"), QStringLiteral("Compile the keymap") },
            { QStringLiteral("backup"), QStringLiteral("Take a backup") },
            { QStringLiteral("write"), QStringLiteral("Write the files") },
            { QStringLiteral("test"), QStringLiteral("Test-load the configuration") },
            { QStringLiteral("reload"), QStringLiteral("Reload Hyprland") },
        };
        for (const auto &stage : kStages) {
            QString state = QStringLiteral("pending");
            if (result.stagesDone.contains(stage.first))
                state = QStringLiteral("done");
            else if (result.stagesSkipped.contains(stage.first))
                state = QStringLiteral("skipped");
            else if (result.stageFailed == stage.first)
                state = QStringLiteral("failed");
            stages.append(QVariantMap{
                { QStringLiteral("id"), stage.first },
                { QStringLiteral("label"), stage.second },
                { QStringLiteral("state"), state },
            });
        }
        if (result.rolledBack) {
            stages.append(QVariantMap{
                { QStringLiteral("id"), QStringLiteral("rollback") },
                { QStringLiteral("label"), QStringLiteral("Put the previous configuration back") },
                { QStringLiteral("state"), QStringLiteral("undone") },
            });
        }
    }

    return QVariantMap{
        { QStringLiteral("ok"), result.ok },
        { QStringLiteral("rolledBack"), result.rolledBack },
        { QStringLiteral("message"), result.message },
        { QStringLiteral("steps"), result.steps },
        { QStringLiteral("stages"), stages },
        { QStringLiteral("conflicts"), conflicts },
        { QStringLiteral("backup"), result.backupPath },
    };
}

QVariantMap ConfigRepository::preview()
{
    ApplyResult result;
    refreshConflicts();
    result.conflicts = m_conflicts;

    // 1. structure
    QVector<Conflict> structural;
    if (!RuleSetValidator::validate(m_ruleSet, &structural)) {
        result.message = QStringLiteral("Some mappings are incomplete.");
        result.steps << QStringLiteral("Validation found problems that must be fixed first.");
        return applyResultToVariant(result);
    }
    result.steps << QStringLiteral("Mappings are valid.");

    // 2. conflicts
    if (ConflictDetector::hasBlockingConflict(m_conflicts)) {
        result.message = errorCount() == 1
            ? QStringLiteral("One conflict needs resolving.")
            : QStringLiteral("%1 conflicts need resolving.").arg(errorCount());
        return applyResultToVariant(result);
    }
    result.steps << (m_conflicts.isEmpty()
                         ? QStringLiteral("No conflicts.")
                         : (warningCount() == 1
                                ? QStringLiteral("One warning, nothing blocking.")
                                : QStringLiteral("%1 warnings, nothing blocking.")
                                      .arg(warningCount())));

    // 3. keymap compilation, which is the real test of a rule set
    const GeneratedConfig config = generateCurrent();
    if (!config.ok) {
        result.message = config.error;
        result.steps << QStringLiteral("Keymap did not compile.");
        return applyResultToVariant(result);
    }
    result.steps << (config.keymaps.size() == 1
                         ? QStringLiteral("The keymap compiles.")
                         : QStringLiteral("All %1 keymaps compile.")
                               .arg(config.keymaps.size()));

    result.ok = true;
    result.message = QStringLiteral("Ready to apply.");
    return applyResultToVariant(result);
}

QVariantMap ConfigRepository::apply()
{
    setBusy(true);

    ApplyResult result;
    refreshConflicts();
    result.conflicts = m_conflicts;

    // The stage being attempted; whatever stops the pipeline is charged to it.
    QString stage = QStringLiteral("validate");
    const auto passed = [&](const QString &next) {
        result.stagesDone << stage;
        stage = next;
    };

    const auto fail = [&](const QString &message) {
        if (result.stageFailed.isEmpty())
            result.stageFailed = stage;
        result.ok = false;
        result.message = message;
        setStatus(message);
        setBusy(false);
        emit applyFinished(false, message);
        return applyResultToVariant(result);
    };

    if (!environmentReport().value(QStringLiteral("ok")).toBool()) {
        setBusy(false);
        const QString message =
            environmentReport().value(QStringLiteral("detail")).toString();
        setStatus(message);
        emit applyFinished(false, message);
        result.message = message;
        return applyResultToVariant(result);
    }

    // --- 1. validate ---
    QVector<Conflict> structural;
    if (!RuleSetValidator::validate(m_ruleSet, &structural)) {
        result.conflicts = structural;
        return fail(QStringLiteral("Some mappings are incomplete, so nothing was changed."));
    }
    result.steps << QStringLiteral("Validated mappings");
    passed(QStringLiteral("conflicts"));

    // --- 2. conflicts ---
    if (ConflictDetector::hasBlockingConflict(m_conflicts))
        return fail(errorCount() == 1
                        ? QStringLiteral("One conflict has to be resolved first, so nothing "
                                         "was changed.")
                        : QStringLiteral("%1 conflicts have to be resolved first, so nothing "
                                         "was changed.").arg(errorCount()));
    result.steps << QStringLiteral("Checked for conflicts");
    passed(QStringLiteral("compile"));

    // --- 3. generate, which compiles and validates every keymap ---
    const GeneratedConfig config = generateCurrent();
    if (!config.ok)
        return fail(config.error);
    result.steps << (config.keymaps.size() == 1
                         ? QStringLiteral("Compiled the keymap")
                         : QStringLiteral("Compiled %1 keymaps").arg(config.keymaps.size()));
    passed(QStringLiteral("backup"));

    // --- 4. back up ---
    QString backupError;
    const QString backupId = m_backups.createBackup(
        QStringLiteral("before apply"), &backupError);
    if (backupId.isEmpty())
        return fail(QStringLiteral("Could not create a backup, so nothing was changed: %1")
                        .arg(backupError));
    result.backupPath = backupId;
    result.steps << QStringLiteral("Backed up current configuration (%1)").arg(backupId);
    passed(QStringLiteral("write"));

    const auto rollback = [&](const QString &reason) {
        result.stageFailed = stage;
        QString restoreError;
        const bool restored = m_backups.restore(backupId, &restoreError);
        result.rolledBack = restored;
        if (restored && m_hyprlandRunning)
            m_reload.reload();

        const QString message = restored
            ? QStringLiteral("%1 Your previous configuration has been restored.").arg(reason)
            : QStringLiteral("%1 Restoring the backup also failed: %2. The snapshot is kept at "
                             "%3/%4.")
                  .arg(reason, restoreError, Paths::backupDir(), backupId);
        result.steps << (restored ? QStringLiteral("Rolled back")
                                  : QStringLiteral("Rollback failed"));
        return fail(message);
    };

    // --- 5. write atomically ---
    QString writeError;
    if (!m_writer.commit(config, ruleSetForSaving(), &writeError))
        return rollback(QStringLiteral("Writing the configuration failed: %1.").arg(writeError));
    result.steps << QStringLiteral("Wrote configuration atomically");
    passed(QStringLiteral("test"));

    // --- 6. dry-run before Hyprland sees any of it ---
    LuaScanner scanner;
    QString syntaxError;
    if (LuaScanner::isAvailable()) {
        if (!scanner.checkSyntax(Paths::generatedLuaFile(), &syntaxError))
            return rollback(QStringLiteral("The generated configuration has a syntax error: %1.")
                                .arg(syntaxError));
        if (!scanner.checkSyntax(Paths::loaderLuaFile(), &syntaxError))
            return rollback(QStringLiteral("The loader has a syntax error: %1.")
                                .arg(syntaxError));

        QString dryRunError;
        if (!scanner.dryRun(Paths::hyprlandLuaFile(), &dryRunError))
            return rollback(QStringLiteral("Loading the configuration failed in a test run: %1.")
                                .arg(dryRunError));
        result.steps << QStringLiteral("Test-loaded the configuration");
        passed(QStringLiteral("reload"));
    } else {
        result.steps << QStringLiteral("Skipped the test run (lua is not installed)");
        result.stagesSkipped << stage;
        stage = QStringLiteral("reload");
    }

    // --- 7. reload and verify ---
    if (!m_hyprlandRunning) {
        result.ok = true;
        result.message = QStringLiteral(
            "Saved. Hyprland is not running, so the changes apply at next login.");
        result.steps << QStringLiteral("Saved without reloading");
        result.stagesSkipped << stage;
        m_backups.prune(kBackupsToKeep);
        setDirty(false);
        dismissUndo();
        setStatus(result.message);
        setBusy(false);
        emit applyFinished(true, result.message);
        return applyResultToVariant(result);
    }

    const ReloadReport report = m_reload.reload();
    if (!report.reloaded)
        return rollback(QStringLiteral("Hyprland did not reload: %1.").arg(report.message));
    if (!report.clean) {
        return rollback(QStringLiteral("Hyprland reported a configuration error: %1.")
                            .arg(report.errors.join(QStringLiteral("; "))));
    }
    result.steps << QStringLiteral("Reloaded Hyprland with no errors");
    passed(QString());

    // Prove the effect rather than assuming it: read a value back.
    m_devices.refresh();
    const QString liveOptions = m_reload.option(QStringLiteral("input:kb_options"));
    if (!liveOptions.isEmpty())
        result.steps << QStringLiteral("Keyboard options now: %1").arg(liveOptions);

    m_inheritedOptions = m_devices.effectiveOptions();
    m_existingBindings = m_parser.loadExistingBindings();
    for (ExistingBinding &binding : m_existingBindings) {
        for (const Shortcut &shortcut : m_ruleSet.shortcuts) {
            if (OmarchyConfigParser::normalizeKeys(shortcut.keys) == binding.normalizedKeys) {
                binding.fromOmaKeyboard = true;
                break;
            }
        }
    }
    refreshConflicts();

    m_backups.prune(kBackupsToKeep);
    setDirty(false);
    dismissUndo();

    result.ok = true;
    result.message = QStringLiteral("Applied.");
    setStatus(result.message);
    setBusy(false);
    emit applyFinished(true, result.message);
    return applyResultToVariant(result);
}

bool ConfigRepository::discardChanges()
{
    reload();
    return true;
}

QVariantMap ConfigRepository::restoreDefaults()
{
    // "Defaults" means removing everything OmaKeyboard added, leaving Omarchy's own
    // configuration exactly as it was. The block comes out of hyprland.lua and
    // the generated files are deleted; nothing the user wrote is touched.
    setBusy(true);

    ApplyResult result;

    QString backupError;
    const QString backupId =
        m_backups.createBackup(QStringLiteral("before restoring defaults"), &backupError);
    if (backupId.isEmpty()) {
        result.message = QStringLiteral("Could not create a backup, so nothing was changed: %1")
                             .arg(backupError);
        setBusy(false);
        return applyResultToVariant(result);
    }
    result.backupPath = backupId;
    result.steps << QStringLiteral("Backed up current configuration (%1)").arg(backupId);

    bool read = false;
    const QByteArray current = Paths::read(Paths::hyprlandLuaFile(), &read);
    if (read) {
        const QString stripped =
            OmarchyConfigWriter::withoutManagedBlock(QString::fromUtf8(current));
        QString error;
        if (!Paths::writeAtomically(Paths::hyprlandLuaFile(), stripped.toUtf8(), &error)) {
            m_backups.restore(backupId);
            result.rolledBack = true;
            result.message = QStringLiteral("Could not update hyprland.lua: %1").arg(error);
            setBusy(false);
            return applyResultToVariant(result);
        }
        result.steps << QStringLiteral("Removed the OmaKeyboard block from hyprland.lua");
    }

    QFile::remove(Paths::generatedLuaFile());
    QFile::remove(Paths::loaderLuaFile());
    QFile::remove(Paths::rulesFile());
    QDir(Paths::keymapDir()).removeRecursively();
    result.steps << QStringLiteral("Deleted the generated configuration");

    m_ruleSet = RuleSet();
    ensureDefaultProfile();

    if (m_hyprlandRunning) {
        const ReloadReport report = m_reload.reload();
        result.steps << (report.clean ? QStringLiteral("Reloaded Hyprland")
                                      : QStringLiteral("Reloaded Hyprland with warnings"));
    }

    reload();

    result.ok = true;
    result.message = QStringLiteral("Everything OmaKeyboard added has been removed.");
    setStatus(result.message);
    setBusy(false);
    return applyResultToVariant(result);
}

QVariantList ConfigRepository::backupList() const
{
    QVariantList list;
    for (const BackupEntry &entry : m_backups.backups()) {
        list.append(QVariantMap{
            { QStringLiteral("id"), entry.id },
            { QStringLiteral("label"), entry.label },
            { QStringLiteral("created"), entry.created },
            { QStringLiteral("fileCount"), entry.files.size() },
            { QStringLiteral("path"), entry.path },
        });
    }
    return list;
}

bool ConfigRepository::deleteBackup(const QString &backupId)
{
    if (!m_backups.remove(backupId))
        return false;
    setStatus(QStringLiteral("Backup deleted."));
    emit backupsChanged();
    return true;
}

int ConfigRepository::deleteAllBackups()
{
    const int removed = m_backups.removeAll();
    if (removed > 0) {
        setStatus(removed == 1 ? QStringLiteral("1 backup deleted.")
                               : QStringLiteral("%1 backups deleted.").arg(removed));
        emit backupsChanged();
    }
    return removed;
}

QVariantMap ConfigRepository::restoreBackup(const QString &backupId)
{
    setBusy(true);
    ApplyResult result;

    // Snapshot the current state first, so restoring a backup is itself
    // undoable.
    const QString safety = m_backups.createBackup(QStringLiteral("before restoring a backup"));
    if (!safety.isEmpty())
        result.steps << QStringLiteral("Backed up current configuration (%1)").arg(safety);

    QString error;
    if (!m_backups.restore(backupId, &error)) {
        result.message = QStringLiteral("Could not restore that backup: %1").arg(error);
        setBusy(false);
        return applyResultToVariant(result);
    }
    result.steps << QStringLiteral("Restored backup %1").arg(backupId);

    if (m_hyprlandRunning) {
        const ReloadReport report = m_reload.reload();
        if (!report.clean && !safety.isEmpty()) {
            m_backups.restore(safety);
            m_reload.reload();
            result.rolledBack = true;
            result.message = QStringLiteral(
                "That backup does not load cleanly, so the current configuration was kept.");
            setBusy(false);
            return applyResultToVariant(result);
        }
        result.steps << QStringLiteral("Reloaded Hyprland");
    }

    reload();

    result.ok = true;
    result.message = QStringLiteral("Backup restored.");
    setStatus(result.message);
    setBusy(false);
    return applyResultToVariant(result);
}

} // namespace omakeyboard
