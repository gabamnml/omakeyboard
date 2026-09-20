#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace omakeyboard {

// How a mapping is implemented on the native backend. The UI shows this so a
// user can tell at a glance whether a row rewrites the keymap, appends an XKB
// option, or installs a Hyprland bind.
enum class MappingKind {
    Unknown = 0,
    KeyRemap,       // <CAPS> emits Escape                      -> generated keymap
    ModifierRemap,  // <CAPS> emits Control_L                   -> generated keymap
    XkbOption,      // caps:escape                              -> input.kb_options
    LayerChooser,   // <CAPS> selects level 3 (a "Fn" key)      -> generated keymap
    LayerKey,       // Caps + <FK01> emits XF86MonBrightnessDown-> generated keymap
    Shortcut,       // SUPER + E runs an action                 -> hl.unbind + o.bind
};

// What a shortcut actually does. Mirrors the dispatcher vocabulary in
// /usr/share/omarchy/default/hypr/helpers.lua (o.bind) so nothing is invented.
enum class ActionKind {
    None = 0,
    Command,        // plain exec; o.bind(keys, desc, "cmd")
    OmarchyLaunch,  // { omarchy = "terminal" } -> omarchy-launch-terminal
    DesktopApp,     // { launch = "obsidian" }  -> uwsm-app -- obsidian
    WebApp,         // { webapp = "https://…" }
    Tui,            // { tui = "btop" }
    Media,          // exec of an omarchy media/brightness command
    WindowAction,   // hl.dsp.window.* / hl.dsp.focus
    WorkspaceAction,// hl.dsp.workspace.*
    Toggle,         // o.bind_toggle(keys, desc, "<toggle>")
};

// A setting that can be left alone. Omarchy's defaults already set repeat
// rate, repeat delay and numlock, so "inherit" has to be a real state rather
// than a number the app guesses and then pins.
enum class Tristate {
    Inherit = 0,
    On,
    Off,
};

// Keyboard behaviour that is not a key mapping. Applied through input.* for the
// all-keyboards profile and through hl.device{} for one keyboard.
struct KeyboardBehaviour {
    int repeatRate = -1;    // -1 inherits; otherwise keys per second
    int repeatDelay = -1;   // -1 inherits; otherwise milliseconds
    Tristate numlockOnStart = Tristate::Inherit;
    Tristate resolveBindsBySym = Tristate::Inherit;

    // Whether shortcuts fire from this keyboard at all. Hyprland exposes this
    // only per device (HL.DeviceSpec.keybinds); there is no input.keybinds, so
    // it cannot be set for every keyboard at once.
    Tristate shortcutsEnabled = Tristate::Inherit;

    bool isEmpty() const
    {
        return repeatRate < 0 && repeatDelay < 0
            && numlockOnStart == Tristate::Inherit
            && resolveBindsBySym == Tristate::Inherit
            && shortcutsEnabled == Tristate::Inherit;
    }
};

// Severity of a finding from ConflictDetector.
enum class ConflictSeverity {
    Info = 0,
    Warning,    // works, but something is shadowed
    Error,      // must be resolved before applying
};

struct Conflict {
    ConflictSeverity severity = ConflictSeverity::Info;
    QString subjectId;      // Remap::id or Shortcut::id this is attached to
    // Which profile this belongs to, so a page showing one keyboard does not
    // repeat the findings of another. Empty means it is not profile-specific.
    QString profileId;
    QString profileName;
    QString title;
    QString detail;
    QString suggestion;
    QString conflictsWith;  // human label of the other side
};

// One key-level rule inside a profile.
struct Remap {
    QString id;
    MappingKind kind = MappingKind::KeyRemap;
    bool enabled = true;

    // XKB keycode name without angle brackets, e.g. "CAPS", "FK01", "AC06".
    QString sourceKey;

    // Keysyms per XKB level, index 0 = level 1. An empty entry inherits the
    // base keymap's symbol for that level. LayerKey rows fill index 2/3.
    QStringList levels;

    // Only for kind == XkbOption, e.g. "caps:escape".
    QString xkbOption;

    QString note;
};

// One Hyprland keybinding.
struct Shortcut {
    QString id;
    bool enabled = true;

    QString keys;           // "SUPER + SHIFT + E", Omarchy's o.bind spelling
    QString description;
    ActionKind actionKind = ActionKind::Command;
    QString actionValue;    // command, omarchy launcher name, url, .desktop Exec…
    QString focusMatch;     // optional window class for launch-or-focus
    bool unbindFirst = true;
    QStringList flags;      // "locked", "repeating", "release", "long_press"…
};

// A set of key rules bound to one keyboard, or to every keyboard.
struct Profile {
    QString id;
    QString name;
    // hyprctl device name, e.g. "apple-inc.-apple-internal-keyboard-/-trackpad".
    // Empty means "all keyboards" and is applied through input.kb_* instead of
    // a per-device hl.device{} block.
    QString deviceName;
    bool enabled = true;

    // RMLVO the generated keymap is built on top of. Empty values fall back to
    // whatever Hyprland currently reports, so a profile inherits Omarchy's
    // defaults rather than pinning a stale copy of them.
    QString layout;
    QString variant;
    QString model;
    QStringList xkbOptions;

    QVector<Remap> remaps;
    KeyboardBehaviour behaviour;

    bool isGlobal() const { return deviceName.isEmpty(); }
};

struct RuleSet {
    int schema = 1;
    QVector<Profile> profiles;
    QVector<Shortcut> shortcuts;

    // When false the app leaves ~/.config/hypr/hyprland.lua alone, even if its
    // managed block is missing. Set when a user deliberately removed it.
    bool manageHyprlandBlock = true;

    const Profile *profileById(const QString &id) const;
    Profile *profileById(const QString &id);
};

// Result of running the apply pipeline.
struct ApplyResult {
    bool ok = false;
    bool rolledBack = false;
    QString message;
    QStringList steps;       // human-readable trace of the pipeline
    QVector<Conflict> conflicts;
    QString backupPath;

    // The pipeline's stages as they ran, for the step-by-step view. Only an
    // apply fills these; a preview or a restore leaves them empty.
    QStringList stagesDone;     // ids, in order
    QStringList stagesSkipped;  // ids that did not apply on this system
    QString stageFailed;        // id of the stage that stopped the pipeline
};

// Layers. Layer 1 lives on XKB level 3 (ISO_Level3_Shift), layer 2 on level
// 5 (ISO_Level5_Shift). A LayerKey mapping for layer 1 stores its symbol at
// levels[2]; one for layer 2 at levels[4]. A key can carry one of each.
int layerOfMapping(const Remap &remap);        // 1 or 2 for LayerKey, else 0
int layerOfChooser(const Remap &remap);        // 1 or 2 for LayerChooser, else 0
QString layerOutput(const Remap &remap);       // the symbol the layer types
QStringList layerLevels(int layer, const QString &keysym);

QString mappingKindName(MappingKind kind);
QString mappingKindLabel(MappingKind kind);
MappingKind mappingKindFromName(const QString &name);

QString tristateName(Tristate value);
Tristate tristateFromName(const QString &name);

QString actionKindName(ActionKind kind);
QString actionKindLabel(ActionKind kind);
ActionKind actionKindFromName(const QString &name);

} // namespace omakeyboard

Q_DECLARE_METATYPE(omakeyboard::Conflict)
