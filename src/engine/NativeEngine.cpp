#include "NativeEngine.h"

#include "../core/HyprlandReloadService.h"
#include "../core/KeymapService.h"
#include "../core/LuaScanner.h"
#include "../core/Process.h"

namespace omakeyboard {

NativeEngine::NativeEngine()
{
    detect();
}

void NativeEngine::detect()
{
    m_hasXkbcli = KeymapService::isAvailable();
    m_hasLua = LuaScanner::isAvailable();
    m_hasHyprctl = Process::isAvailable(QStringLiteral("hyprctl"));
    m_hyprlandVersion = HyprlandReloadService::version();

    // xkbcli ships with libxkbcommon, which Hyprland already depends on, so a
    // missing one means something is genuinely broken rather than merely
    // uninstalled. Reported, never auto-fixed.
    if (!m_hasXkbcli)
        m_missing << QStringLiteral(
            "xkbcli is missing. It comes with libxkbcommon, which Hyprland "
            "requires, so key remapping cannot be generated or validated.");
    if (!m_hasHyprctl)
        m_missing << QStringLiteral(
            "hyprctl is missing, so changes cannot be applied to the running "
            "session. Edits will still be saved and take effect at next login.");
    if (!m_hasLua)
        m_missing << QStringLiteral(
            "lua is missing, so existing keybindings cannot be read and "
            "conflicts cannot be detected.");

    EngineCapabilities &caps = m_capabilities;
    caps.simpleRemap = m_hasXkbcli;
    caps.modifierRemap = m_hasXkbcli;
    caps.disableKey = m_hasXkbcli;
    // Per-device rules rely on Hyprland's per-keyboard kb_file setting.
    caps.perDeviceRules = m_hasXkbcli && m_hasHyprctl;
    // Layers are XKB levels: level 3 through a chooser key, level 5 for a
    // second one. Both come from the stock types and compat sections.
    caps.layers = m_hasXkbcli;
    caps.secondLayer = m_hasXkbcli;
    caps.shortcuts = m_hasHyprctl;

    // The honest limits. These are properties of XKB and Hyprland, not of a
    // missing add-on, so the wording explains the mechanism.
    caps.tapHoldLimit = QStringLiteral(
        "XKB has no notion of time, so a key cannot mean one thing when tapped "
        "and another when held.");
    caps.combosLimit = QStringLiteral(
        "XKB resolves one key at a time, so two keys pressed together cannot "
        "produce a third result.");
    caps.macrosLimit = QStringLiteral(
        "A key maps to a single symbol, so it cannot emit a sequence of "
        "keystrokes.");
    caps.perAppLimit = QStringLiteral(
        "The keymap belongs to the keyboard, not to the focused window, so a "
        "remap cannot change per application. Shortcuts can still run an "
        "action that behaves differently depending on the focused window.");
}


bool NativeEngine::supports(MappingKind kind) const
{
    switch (kind) {
    case MappingKind::KeyRemap:
    case MappingKind::ModifierRemap:
        return m_capabilities.simpleRemap;
    case MappingKind::XkbOption:
        return m_capabilities.simpleRemap;
    case MappingKind::LayerChooser:
    case MappingKind::LayerKey:
        return m_capabilities.layers;
    case MappingKind::Shortcut:
        return m_capabilities.shortcuts;
    case MappingKind::Unknown:
        break;
    }
    return false;
}

QString NativeEngine::limitFor(MappingKind kind) const
{
    if (supports(kind))
        return {};
    if (!m_hasXkbcli)
        return QStringLiteral("xkbcli is not available on this system.");
    if (!m_hasHyprctl && kind == MappingKind::Shortcut)
        return QStringLiteral("hyprctl is not available, so shortcuts cannot be applied now.");
    return QStringLiteral("This mapping is not supported.");
}

} // namespace omakeyboard
