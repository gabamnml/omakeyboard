#pragma once

#include <QHash>
#include <QString>
#include <QVector>

namespace omakeyboard {

struct KeyEntry {
    QString code;      // XKB keycode name without brackets: "CAPS", "FK01"
    int keycode = 0;   // XKB keycode number (evdev code + 8)
    QString label;     // "Caps Lock"
    QString section;   // "Modifiers", "Function row", "Letters"…
};

struct KeysymEntry {
    QString keysym;    // "Escape", "XF86AudioPlay" style XKB name
    QString label;     // "Escape"
    QString category;  // "Modifiers", "Media", "Navigation"…
};

// The set of keys the UI can offer, derived from the system's own XKB data
// (/usr/share/X11/xkb/keycodes/evdev) rather than a hardcoded table, plus a
// curated catalog of useful targets.
class KeyCatalog {
public:
    static KeyCatalog &instance();

    void load();
    bool isLoaded() const { return m_loaded; }

    // Label positional codes by what they actually type on the active layout.
    // AC06 is "H" on a US keyboard; calling it "home row key 06" is accurate
    // and useless. Takes the level-1 symbols from a compiled base keymap, so
    // an AZERTY user sees their own letters.
    void applyLayoutSymbols(const QHash<QString, QStringList> &baseSymbols);

    const QVector<KeyEntry> &keys() const { return m_keys; }
    const QVector<KeysymEntry> &keysyms() const { return m_keysyms; }

    // "CAPS" -> "Caps Lock". Falls back to the code itself.
    QString labelForCode(const QString &code) const;
    // The character printed on a positional key under the active layout
    // ("[", "Ñ", "7"), or empty when the layout is unknown or the key types
    // nothing printable. For drawing keycaps, where "Left bracket" won't fit.
    QString capCharacter(const QString &code) const { return m_capCharacters.value(code); }
    // The character a keysym types, or empty: "semicolon" -> ";".
    static QString characterForKeysym(const QString &keysym);
    QString labelForKeysym(const QString &keysym) const;
    bool hasCode(const QString &code) const;
    bool hasKeysym(const QString &keysym) const;

    // Qt hands a native scan code on Wayland; depending on the platform plugin
    // it is either the evdev code or the XKB keycode (evdev + 8). Both are
    // tried so key capture works either way.
    QString codeForNativeScanCode(quint32 nativeScanCode) const;

private:
    KeyCatalog() = default;
    void loadKeycodes();
    void buildKeysyms();
    static QString humanizeCode(const QString &code);
    static QString sectionForCode(const QString &code);

    bool m_loaded = false;
    QVector<KeyEntry> m_keys;
    QHash<QString, QString> m_capCharacters;
    QVector<KeysymEntry> m_keysyms;
    QHash<QString, int> m_codeIndex;
    QHash<int, QString> m_keycodeToCode;
    QHash<QString, int> m_keysymIndex;
};

} // namespace omakeyboard
