#pragma once

#include "ConfigBackupService.h"
#include "ConflictDetector.h"
#include "DeviceService.h"
#include "HyprlandReloadService.h"
#include "OmarchyConfigParser.h"
#include "OmarchyConfigWriter.h"
#include "Types.h"

#include "../engine/NativeEngine.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace omakeyboard {

// The single door between the interface and the configuration engine.
//
// QML never touches a file, a process or a parser: it reads properties and
// calls the invokables here. Everything below this class is plain C++ with no
// Qt Quick dependency, which is what makes the engine testable on its own.
class ConfigRepository : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList engineWarnings READ engineWarnings CONSTANT)
    Q_PROPERTY(QVariantMap capabilities READ capabilities CONSTANT)
    Q_PROPERTY(QVariantList conflicts READ conflictList NOTIFY conflictsChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY conflictsChanged)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY conflictsChanged)
    Q_PROPERTY(bool hyprlandRunning READ hyprlandRunning NOTIFY stateReloaded)
    // Whether this system is one the app can write to at all, and if not,
    // what is missing said in plain words.
    Q_PROPERTY(QVariantMap environment READ environmentReport NOTIFY stateReloaded)
    Q_PROPERTY(QString activeProfileId READ activeProfileId WRITE setActiveProfileId
                   NOTIFY activeProfileChanged)
    // Exposed as a property, not just an invokable: a binding on a plain
    // function call never re-evaluates, so the keyboard chips would keep
    // showing the state they had when the page was built.
    Q_PROPERTY(QVariantList keyboards READ keyboardList NOTIFY keyboardsChanged)
    // Same reason as `keyboards`: applying takes a snapshot, and a list read
    // once when the page was built would never show it.
    Q_PROPERTY(QVariantList backups READ backupList NOTIFY backupsChanged)
    // What the last deletion removed, for the "Undo" notice; empty when there
    // is nothing to undo.
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY undoChanged)

public:
    explicit ConfigRepository(QObject *parent = nullptr);

    // --- lifecycle ---
    Q_INVOKABLE void reload();

    bool isDirty() const { return m_dirty; }
    bool isBusy() const { return m_busy; }
    QString status() const { return m_status; }
    QStringList engineWarnings() const { return m_engine.missingRequirements(); }
    QVariantMap capabilities() const;
    QVariantList conflictList() const;
    int errorCount() const;
    int warningCount() const;
    bool hyprlandRunning() const { return m_hyprlandRunning; }
    QVariantMap environmentReport() const;

    const RuleSet &ruleSet() const { return m_ruleSet; }
    // What actually gets written: the rule set minus profiles that hold
    // nothing, which selecting a keyboard creates as a side effect.
    RuleSet ruleSetForSaving() const;
    void setRuleSet(const RuleSet &ruleSet);   // used by the tests

    DeviceService *devices() { return &m_devices; }

    // --- profiles ---
    QString activeProfileId() const { return m_activeProfileId; }
    void setActiveProfileId(const QString &id);
    Q_INVOKABLE QVariantList profileList() const;

    // Every keyboard worth showing, whether or not it has rules yet: the
    // all-keyboards entry, each connected keyboard, and any keyboard that has
    // rules but is currently unplugged. There is no "add a keyboard" step --
    // the keyboards you have are the keyboards you see.
    Q_INVOKABLE QVariantList keyboardList() const;
    // Create the profile for a keyboard the first time one is needed, and
    // return its id. Selecting a keyboard is enough to call this.
    Q_INVOKABLE QString ensureProfileForDevice(const QString &deviceName, const QString &name);
    Q_INVOKABLE QString addProfileForDevice(const QString &deviceName, const QString &name);
    Q_INVOKABLE bool removeProfile(const QString &profileId);
    Q_INVOKABLE bool setProfileEnabled(const QString &profileId, bool enabled);

    // --- mappings ---
    Q_INVOKABLE QVariantList mappingList(const QString &profileId) const;
    Q_INVOKABLE QString addMapping(const QString &profileId, const QString &kind,
                                   const QString &sourceKey, const QVariantList &levels,
                                   const QString &xkbOption, const QString &note);
    Q_INVOKABLE bool updateMapping(const QString &profileId, const QString &mappingId,
                                   const QString &kind, const QString &sourceKey,
                                   const QVariantList &levels, const QString &xkbOption,
                                   const QString &note);
    Q_INVOKABLE bool removeMapping(const QString &profileId, const QString &mappingId);
    Q_INVOKABLE bool setMappingEnabled(const QString &profileId, const QString &mappingId,
                                       bool enabled);

    // Ready-made setups a first-time user can add in one click. Each entry
    // says what it does and whether it still fits the keyboard: a recipe that
    // would remap a key the profile already remaps is marked unavailable
    // rather than silently doubled up.
    Q_INVOKABLE QVariantList recipeList(const QString &profileId) const;
    Q_INVOKABLE bool addRecipe(const QString &profileId, const QString &recipeId);

    // --- keyboard behaviour (Advanced) ---
    // The profile's own settings, each marked as chosen or inherited, plus the
    // value in effect right now so the UI can show what "inherit" means.
    Q_INVOKABLE QVariantMap behaviourFor(const QString &profileId) const;
    Q_INVOKABLE bool setBehaviourNumber(const QString &profileId, const QString &key, int value);
    Q_INVOKABLE bool setBehaviourFlag(const QString &profileId, const QString &key,
                                      const QString &state);
    Q_INVOKABLE bool clearBehaviour(const QString &profileId);

    // --- shortcuts ---
    Q_INVOKABLE QVariantList shortcutList() const;
    Q_INVOKABLE QString addShortcut(const QString &keys, const QString &description,
                                    const QString &actionKind, const QString &actionValue,
                                    const QString &focusMatch);
    Q_INVOKABLE bool updateShortcut(const QString &shortcutId, const QString &keys,
                                    const QString &description, const QString &actionKind,
                                    const QString &actionValue, const QString &focusMatch);
    Q_INVOKABLE bool removeShortcut(const QString &shortcutId);
    Q_INVOKABLE bool setShortcutEnabled(const QString &shortcutId, bool enabled);
    Q_INVOKABLE bool setShortcutUnbindFirst(const QString &shortcutId, bool unbindFirst);

    // Ready-made shortcuts. Each picks the first of its key combinations that
    // nothing is bound to yet -- Omarchy's own bindings included -- and is
    // unavailable when every one is taken.
    Q_INVOKABLE QVariantList shortcutRecipeList() const;
    Q_INVOKABLE bool addShortcutRecipe(const QString &recipeId);

    // --- sharing ---
    // A configuration travels as one JSON file. Exporting writes what would be
    // saved; importing never overwrites, it adds to what is here, so a shared
    // file cannot wipe someone's own keyboards. Nothing reaches the system
    // until the user applies.
    Q_INVOKABLE QVariantMap exportToFile(const QString &filePath) const;
    // What a file contains and where each keyboard could land, without
    // changing anything.
    Q_INVOKABLE QVariantMap inspectImport(const QString &filePath) const;
    // `targets` holds one entry per profile in the file, in the same order:
    // a device name, "global" for every keyboard, or "skip".
    Q_INVOKABLE QVariantMap importFromFile(const QString &filePath, const QVariantList &targets,
                                           bool includeShortcuts);

    // --- the keyboard map ---
    // The block every keyboard from 60% up shares, as rows of keys with widths
    // in key units, each carrying what it does on this profile: on its own and
    // while the layer key is held. Nothing about the physical keyboard is
    // guessed beyond ISO versus ANSI, which follows the layout.
    Q_INVOKABLE QVariantMap keyboardMap(const QString &profileId) const;

    // --- undoing a deletion ---
    // Only the most recent deletion is kept: a mapping, a shortcut or a whole
    // keyboard's rules. It goes back where it was, and if nothing else has
    // changed since the last save, the unsaved-changes state goes with it.
    QString undoLabel() const { return m_removal.label; }
    Q_INVOKABLE bool undoRemoval();
    Q_INVOKABLE void dismissUndo();

    // Bindings that exist but are not ours, for the "what is already taken" view.
    Q_INVOKABLE QVariantList existingBindings() const;

    // --- the pipeline ---
    // Validate, detect conflicts, back up, write atomically, dry-run, reload,
    // verify, and roll back if any of it fails.
    Q_INVOKABLE QVariantMap apply();
    // Same checks, no writes: what the Apply button can show before committing.
    Q_INVOKABLE QVariantMap preview();
    Q_INVOKABLE bool discardChanges();

    // --- defaults and backups ---
    Q_INVOKABLE QVariantMap restoreDefaults();
    Q_INVOKABLE QVariantList backupList() const;
    Q_INVOKABLE QVariantMap restoreBackup(const QString &backupId);
    // Deleting a backup cannot be undone -- the files are gone -- so the
    // interface asks first.
    Q_INVOKABLE bool deleteBackup(const QString &backupId);
    Q_INVOKABLE int deleteAllBackups();

    // The status line reports what just happened, so it should also stop
    // reporting it. Tests shorten the wait rather than sleeping for it.
    void setStatusTimeout(int milliseconds);

    // The generated artifacts, for the Advanced page. Read-only: seeing them is
    // supported, editing them by hand is not.
    Q_INVOKABLE QVariantMap generatedPreview() const;

signals:
    void dirtyChanged();
    void busyChanged();
    void statusChanged();
    void conflictsChanged();
    void stateReloaded();
    void keyboardsChanged();
    void backupsChanged();
    void activeProfileChanged();
    void mappingsChanged(const QString &profileId);
    void shortcutsChanged();
    void applyFinished(bool ok, const QString &message);
    void undoChanged();

private:
    struct Removal {
        enum class Kind { None, Mapping, Shortcut, Profile };
        Kind kind = Kind::None;
        QString label;
        QString profileId;
        int index = -1;
        Remap remap;
        Shortcut shortcut;
        Profile profile;
    };
    void rememberRemoval(const Removal &removal);
    // Whether the rules differ from what was last loaded or applied.
    bool differsFromSaved() const;

    void setDirty(bool dirty);
    void setBusy(bool busy);
    void setStatus(const QString &status);
    void refreshConflicts();
    void ensureDefaultProfile();
    Profile *profileFor(const QString &profileId);

    static QVariantMap conflictToVariant(const Conflict &conflict);
    static QVariantMap applyResultToVariant(const ApplyResult &result);
    GeneratedConfig generateCurrent() const;

    RuleSet m_ruleSet;
    QVector<ExistingBinding> m_existingBindings;
    QVector<Conflict> m_conflicts;
    QStringList m_inheritedOptions;

    NativeEngine m_engine;
    DeviceService m_devices;
    HyprlandReloadService m_reload;
    OmarchyConfigParser m_parser;
    OmarchyConfigWriter m_writer;
    ConflictDetector m_detector;
    ConfigBackupService m_backups;

    QTimer m_statusTimer;
    Removal m_removal;
    QByteArray m_savedRules;

    QString m_activeProfileId;
    QString m_status;
    bool m_dirty = false;
    bool m_busy = false;
    bool m_hyprlandRunning = false;
    bool m_layoutSymbolsLoaded = false;
};

} // namespace omakeyboard
