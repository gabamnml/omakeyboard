#pragma once

#include "../core/Types.h"

#include <QString>
#include <QStringList>

namespace omakeyboard {

// What this machine's native stack can and cannot do, resolved at runtime
// rather than assumed. The UI reads this to describe honest limits instead of
// offering a control that would silently do nothing.
struct EngineCapabilities {
    bool simpleRemap = false;      // <CAPS> emits Escape
    bool modifierRemap = false;    // <CAPS> acts as Control
    bool perDeviceRules = false;   // different rules per connected keyboard
    bool layers = false;           // Fn-style layer via a chooser key
    bool secondLayer = false;      // a second, independent layer
    bool shortcuts = false;        // key combinations that run actions
    bool disableKey = false;       // make a key do nothing

    // Deliberately absent from the native stack. Each carries the reason, so
    // the app can explain rather than just refuse.
    QString tapHoldLimit;
    QString combosLimit;
    QString macrosLimit;
    QString perAppLimit;
};

// The one engine: XKB for keys, Hyprland for bindings. Nothing else is
// involved and nothing else needs to be installed.
class NativeEngine {
public:
    NativeEngine();

    const EngineCapabilities &capabilities() const { return m_capabilities; }

    // Missing pieces of the native toolchain, as user-facing sentences.
    // Empty means everything the engine needs is present.
    QStringList missingRequirements() const { return m_missing; }
    bool isUsable() const { return m_missing.isEmpty(); }


    // Whether a mapping of this kind can be expressed at all.
    bool supports(MappingKind kind) const;
    // Why not, when it cannot. Empty when it can.
    QString limitFor(MappingKind kind) const;

private:
    void detect();

    EngineCapabilities m_capabilities;
    QStringList m_missing;
    QString m_hyprlandVersion;
    bool m_hasXkbcli = false;
    bool m_hasLua = false;
    bool m_hasHyprctl = false;
};

} // namespace omakeyboard
