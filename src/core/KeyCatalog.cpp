#include "KeyCatalog.h"

#include <xkbcommon/xkbcommon.h>

#include "Paths.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QTextStream>

namespace omakeyboard {

namespace {

// Readable names for the codes a user actually reaches for. Anything not in
// here falls back to a generated label, so the catalog still covers all 518
// codes in keycodes/evdev without spelling each one out.
struct CodeLabel { const char *code; const char *label; };

const CodeLabel kCodeLabels[] = {
    { "ESC", "Escape" },          { "CAPS", "Caps Lock" },
    { "TAB", "Tab" },             { "RTRN", "Enter" },
    { "BKSP", "Backspace" },      { "SPCE", "Space" },
    { "LFSH", "Left Shift" },     { "RTSH", "Right Shift" },
    { "LCTL", "Left Control" },   { "RCTL", "Right Control" },
    { "LALT", "Left Alt" },       { "RALT", "Right Alt" },
    { "LWIN", "Left Super" },     { "RWIN", "Right Super" },
    { "COMP", "Menu" },           { "MENU", "Menu" },
    { "LSGT", "Less/Greater" },   { "BKSL", "Backslash" },
    { "UP", "Up" },               { "DOWN", "Down" },
    { "LEFT", "Left" },           { "RGHT", "Right" },
    { "HOME", "Home" },           { "END", "End" },
    { "PGUP", "Page Up" },        { "PGDN", "Page Down" },
    { "INS", "Insert" },          { "DELE", "Delete" },
    { "PRSC", "Print Screen" },   { "SCLK", "Scroll Lock" },
    { "PAUS", "Pause" },          { "NMLK", "Num Lock" },
    { "TLDE", "Grave / Tilde" },  { "AB09", "Period" },
    { "AB08", "Comma" },          { "AB10", "Slash" },
    { "AE11", "Minus" },          { "AE12", "Equal" },
};

// Targets worth offering in a dropdown, grouped the way the UI shows them.
struct SymbolEntry { const char *keysym; const char *label; const char *category; };

const SymbolEntry kSymbols[] = {
    // Modifiers -- the Karabiner bread and butter.
    { "Control_L", "Left Control", "Modifiers" },
    { "Control_R", "Right Control", "Modifiers" },
    { "Alt_L", "Left Alt", "Modifiers" },
    { "Alt_R", "Right Alt", "Modifiers" },
    { "Super_L", "Left Super", "Modifiers" },
    { "Super_R", "Right Super", "Modifiers" },
    { "Shift_L", "Left Shift", "Modifiers" },
    { "Shift_R", "Right Shift", "Modifiers" },
    { "Hyper_L", "Hyper", "Modifiers" },
    { "Meta_L", "Meta", "Modifiers" },
    { "Caps_Lock", "Caps Lock", "Modifiers" },
    { "ISO_Level3_Shift", "Layer 3 (Fn-style)", "Modifiers" },
    { "ISO_Level5_Shift", "Layer 5 (second layer)", "Modifiers" },
    { "Multi_key", "Compose", "Modifiers" },
    { "Mode_switch", "Mode switch", "Modifiers" },

    // Editing and navigation.
    { "Escape", "Escape", "Navigation" },
    { "Return", "Enter", "Navigation" },
    { "Tab", "Tab", "Navigation" },
    { "BackSpace", "Backspace", "Navigation" },
    { "Delete", "Delete", "Navigation" },
    { "Insert", "Insert", "Navigation" },
    { "Home", "Home", "Navigation" },
    { "End", "End", "Navigation" },
    { "Prior", "Page Up", "Navigation" },
    { "Next", "Page Down", "Navigation" },
    { "Left", "Left arrow", "Navigation" },
    { "Right", "Right arrow", "Navigation" },
    { "Up", "Up arrow", "Navigation" },
    { "Down", "Down arrow", "Navigation" },
    { "Menu", "Menu", "Navigation" },

    // Media and hardware keys. These are the XF86 keysyms Omarchy already
    // binds in default/hypr/bindings/media.lua, so a layer that emits them
    // lands on the existing bindings with no extra configuration.
    { "XF86AudioRaiseVolume", "Volume up", "Media" },
    { "XF86AudioLowerVolume", "Volume down", "Media" },
    { "XF86AudioMute", "Mute", "Media" },
    { "XF86AudioMicMute", "Mute microphone", "Media" },
    { "XF86AudioPlay", "Play", "Media" },
    { "XF86AudioPause", "Pause", "Media" },
    { "XF86AudioNext", "Next track", "Media" },
    { "XF86AudioPrev", "Previous track", "Media" },
    { "XF86MonBrightnessUp", "Brightness up", "Media" },
    { "XF86MonBrightnessDown", "Brightness down", "Media" },
    { "XF86KbdBrightnessUp", "Keyboard backlight up", "Media" },
    { "XF86KbdBrightnessDown", "Keyboard backlight down", "Media" },
    { "XF86TouchpadToggle", "Toggle touchpad", "Media" },
    { "XF86Calculator", "Calculator", "Media" },
    { "XF86Search", "Search", "Media" },
    { "XF86Sleep", "Sleep", "Media" },
    { "XF86PowerOff", "Power", "Media" },

    // Function row.
    { "F1", "F1", "Function" },   { "F2", "F2", "Function" },
    { "F3", "F3", "Function" },   { "F4", "F4", "Function" },
    { "F5", "F5", "Function" },   { "F6", "F6", "Function" },
    { "F7", "F7", "Function" },   { "F8", "F8", "Function" },
    { "F9", "F9", "Function" },   { "F10", "F10", "Function" },
    { "F11", "F11", "Function" }, { "F12", "F12", "Function" },
    { "F13", "F13", "Function" }, { "F14", "F14", "Function" },
    { "F15", "F15", "Function" }, { "F16", "F16", "Function" },

    // Nothing at all -- the equivalent of Karabiner's "disable key".
    { "NoSymbol", "Disable key", "Other" },
    { "VoidSymbol", "Do nothing", "Other" },
};

} // namespace

KeyCatalog &KeyCatalog::instance()
{
    static KeyCatalog catalog;
    return catalog;
}

void KeyCatalog::load()
{
    if (m_loaded)
        return;
    loadKeycodes();
    buildKeysyms();
    m_loaded = true;
}

QString KeyCatalog::humanizeCode(const QString &code)
{
    for (const auto &entry : kCodeLabels)
        if (code == QLatin1String(entry.code))
            return QString::fromLatin1(entry.label);

    // Positional names: AE01..AE12 is the digit row, AD/AC/AB the letter rows,
    // FK01.. the function row, KP* the keypad.
    static const QRegularExpression functionKey(QStringLiteral("^FK(\\d{2})$"));
    const auto fn = functionKey.match(code);
    if (fn.hasMatch())
        return QStringLiteral("F%1").arg(fn.captured(1).toInt());

    static const QRegularExpression keypad(QStringLiteral("^KP(.+)$"));
    const auto kp = keypad.match(code);
    if (kp.hasMatch())
        return QStringLiteral("Keypad %1").arg(kp.captured(1).toLower());

    static const QRegularExpression positional(QStringLiteral("^A([BCDE])(\\d{2})$"));
    const auto pos = positional.match(code);
    if (pos.hasMatch()) {
        static const QHash<QString, QString> rows{
            { QStringLiteral("E"), QStringLiteral("digit row") },
            { QStringLiteral("D"), QStringLiteral("top row") },
            { QStringLiteral("C"), QStringLiteral("home row") },
            { QStringLiteral("B"), QStringLiteral("bottom row") },
        };
        return QStringLiteral("%1 key %2")
            .arg(rows.value(pos.captured(1)), pos.captured(2));
    }

    return code;
}

QString KeyCatalog::sectionForCode(const QString &code)
{
    static const QStringList modifiers{
        QStringLiteral("CAPS"), QStringLiteral("LFSH"), QStringLiteral("RTSH"),
        QStringLiteral("LCTL"), QStringLiteral("RCTL"), QStringLiteral("LALT"),
        QStringLiteral("RALT"), QStringLiteral("LWIN"), QStringLiteral("RWIN"),
        QStringLiteral("COMP"), QStringLiteral("MENU"),
    };
    if (modifiers.contains(code))
        return QStringLiteral("Modifiers");
    if (code.startsWith(QStringLiteral("FK")))
        return QStringLiteral("Function row");
    if (code.startsWith(QStringLiteral("KP")))
        return QStringLiteral("Keypad");
    if (code.startsWith(QStringLiteral("XF86")) || code.startsWith(QStringLiteral("I")))
        return QStringLiteral("Hardware keys");
    static const QRegularExpression positional(QStringLiteral("^A[BCDE]\\d{2}$"));
    if (positional.match(code).hasMatch())
        return QStringLiteral("Main block");
    return QStringLiteral("Other");
}

void KeyCatalog::loadKeycodes()
{
    QFile file(Paths::xkbKeycodesEvdev());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    // Lines look like "\t<CAPS> = 66;" plus "alias <ALGR> = <RALT>;" which we
    // deliberately skip: an alias is the same physical key under another name.
    static const QRegularExpression entry(
        QStringLiteral("^\\s*<([A-Z0-9_+]+)>\\s*=\\s*(\\d+)\\s*;"));

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.contains(QStringLiteral("alias")))
            continue;
        const auto match = entry.match(line);
        if (!match.hasMatch())
            continue;

        KeyEntry key;
        key.code = match.captured(1);
        key.keycode = match.captured(2).toInt();
        key.label = humanizeCode(key.code);
        key.section = sectionForCode(key.code);

        if (m_codeIndex.contains(key.code))
            continue;
        m_codeIndex.insert(key.code, m_keys.size());
        m_keycodeToCode.insert(key.keycode, key.code);
        m_keys.append(key);
    }
}

void KeyCatalog::buildKeysyms()
{
    for (const auto &entry : kSymbols) {
        KeysymEntry symbol;
        symbol.keysym = QString::fromLatin1(entry.keysym);
        symbol.label = QString::fromLatin1(entry.label);
        symbol.category = QString::fromLatin1(entry.category);
        m_keysymIndex.insert(symbol.keysym, m_keysyms.size());
        m_keysyms.append(symbol);
    }
}

void KeyCatalog::applyLayoutSymbols(const QHash<QString, QStringList> &baseSymbols)
{
    // Only the positional families are renamed. Named keys already have better
    // labels than their keysym would give -- "Caps Lock" beats "Caps_Lock" --
    // and a key with no printable symbol keeps its generated name.
    static const QRegularExpression positional(QStringLiteral("^(A[BCDE]\\d{2}|TLDE|BKSL|LSGT)$"));

    for (KeyEntry &key : m_keys) {
        if (!positional.match(key.code).hasMatch())
            continue;

        const QStringList levels = baseSymbols.value(key.code);
        if (levels.isEmpty())
            continue;

        const QString symbol = levels.constFirst();
        if (symbol.isEmpty() || symbol == QStringLiteral("NoSymbol"))
            continue;

        const QString character = characterForKeysym(symbol);
        if (!character.isEmpty())
            m_capCharacters.insert(key.code, character.toUpper());

        // A single character is the letter or digit on the keycap. Anything
        // longer is a keysym name, which reads better through the keysym
        // labels than raw.
        if (symbol.size() == 1 && symbol.at(0).isLetterOrNumber()) {
            key.label = symbol.toUpper();
        } else {
            const QString pretty = labelForKeysym(symbol);
            if (pretty != symbol)
                key.label = pretty;
        }
    }
}

QString KeyCatalog::characterForKeysym(const QString &keysym)
{
    const xkb_keysym_t sym =
        xkb_keysym_from_name(keysym.toUtf8().constData(), XKB_KEYSYM_NO_FLAGS);
    if (sym == XKB_KEY_NoSymbol)
        return {};
    const uint32_t codepoint = xkb_keysym_to_utf32(sym);
    // Control characters (Tab, Return, BackSpace) are not something to print.
    if (codepoint < 0x20 || codepoint == 0x7f)
        return {};
    const char32_t text[] = { static_cast<char32_t>(codepoint) };
    return QString::fromUcs4(text, 1);
}

QString KeyCatalog::labelForCode(const QString &code) const
{
    const auto it = m_codeIndex.constFind(code);
    if (it == m_codeIndex.constEnd())
        return code;
    return m_keys.at(*it).label;
}

QString KeyCatalog::labelForKeysym(const QString &keysym) const
{
    const auto it = m_keysymIndex.constFind(keysym);
    if (it == m_keysymIndex.constEnd())
        return keysym;
    return m_keysyms.at(*it).label;
}

bool KeyCatalog::hasCode(const QString &code) const
{
    return m_codeIndex.contains(code);
}

bool KeyCatalog::hasKeysym(const QString &keysym) const
{
    return m_keysymIndex.contains(keysym);
}

QString KeyCatalog::codeForNativeScanCode(quint32 nativeScanCode) const
{
    // Qt reports the XKB keycode here, on both the Wayland and the XCB plugin:
    // an evdev code offset by 8. Caps Lock is evdev 58 and XKB 66.
    //
    // Do NOT fall back to trying the raw evdev value as well. Almost every
    // evdev code is also a valid XKB keycode for a different key (58 is the
    // "m" row position), so a fallback would silently resolve to the wrong
    // key instead of failing. An unresolved code returns empty, and the
    // caller offers the key list instead.
    const auto found = m_keycodeToCode.constFind(static_cast<int>(nativeScanCode));
    if (found != m_keycodeToCode.constEnd())
        return *found;
    return {};
}

} // namespace omakeyboard
