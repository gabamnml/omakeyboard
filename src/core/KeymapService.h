#pragma once

#include "Types.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace omakeyboard {

// The XKB component names for one RMLVO combination, as reported by
// `xkbcli compile-keymap --kccgst-yaml`. Deriving these instead of hardcoding
// "pc+us+inet(evdev)" is what keeps a generated keymap correct on any layout.
struct KccgstComponents {
    QString keycodes;
    QString types;
    QString compat;
    QString symbols;
    QString geometry;

    bool isValid() const { return !keycodes.isEmpty() && !symbols.isEmpty(); }
};

struct KeymapBuild {
    bool ok = false;
    QString source;      // the generated .xkb text
    QString error;       // compiler diagnostics when ok is false
    QStringList notes;
};

// Turns a Profile into a real XKB keymap file and validates it offline.
//
// This is the piece that lifts the app past what input.kb_options can express:
// arbitrary key-to-key remapping and Fn-style layers, per keyboard, using
// nothing but libxkbcommon and Hyprland's per-device kb_file setting.
class KeymapService {
public:
    // RMLVO resolution order: what the profile pins, then what Hyprland
    // currently reports, then the XKB defaults. A profile that pins nothing
    // inherits Omarchy's defaults instead of freezing a copy of them.
    KccgstComponents resolveComponents(const Profile &profile,
                                       const QString &fallbackLayout,
                                       const QString &fallbackVariant,
                                       const QString &fallbackModel,
                                       const QStringList &fallbackOptions) const;

    // Base symbols per key for the profile's RMLVO, so a layer mapping can keep
    // levels 1 and 2 intact instead of guessing what the key used to type.
    QHash<QString, QStringList> baseSymbols(const KccgstComponents &components) const;

    KeymapBuild build(const Profile &profile, const KccgstComponents &components,
                      const QHash<QString, QStringList> &baseSymbols) const;

    // `xkbcli compile-keymap --test`: a real compile, no output, no side
    // effects. Runs before Hyprland is ever handed the file.
    bool validate(const QString &source, QString *error) const;

    static bool isAvailable();

    // Escape hatch used by the tests and by Advanced: the RMLVO string form.
    static QString optionsToString(const QStringList &options);

private:
    QString renderKeymap(const Profile &profile, const KccgstComponents &components,
                         const QHash<QString, QStringList> &baseSymbols,
                         QStringList *notes) const;
};

} // namespace omakeyboard
