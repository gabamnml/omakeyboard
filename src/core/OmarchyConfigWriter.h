#pragma once

#include "Types.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace omakeyboard {

// One generated keymap, ready to be written.
struct GeneratedKeymap {
    QString profileId;
    QString deviceName;   // empty for the all-keyboards profile
    QString path;         // where it will live
    QString source;       // the .xkb text
    QString kbOptions;    // options that stay on kb_options rather than the keymap
    KeyboardBehaviour behaviour;
};

struct GeneratedConfig {
    bool ok = false;
    QString error;
    QStringList notes;

    QVector<GeneratedKeymap> keymaps;
    QString generatedLua;   // the data-only table
    QString loaderLua;      // the static loader
};

// Turns a validated RuleSet into the exact bytes that go on disk.
//
// Three files, with a deliberate split:
//   * the keymaps are XKB, compiled and checked by xkbcli before anything else;
//   * generated.lua is a data-only `return { ... }` table -- no calls, no
//     interpolated code, every string escaped with Lua's own %q rules;
//   * omakeyboard.lua is a fixed loader that reads that table and applies it.
//
// Omarchy learned this the hard way: default/hypr/toggles.lua carries a comment
// about older generated Lua that "could carry an injected USB device name" and
// "must never be loaded as code again". Generating data and shipping the code
// separately is the fix.
class OmarchyConfigWriter {
public:
    // Build every artifact without touching the filesystem, so a caller can
    // validate and diff first.
    GeneratedConfig generate(const RuleSet &ruleSet, const QString &fallbackLayout,
                             const QString &fallbackVariant, const QString &fallbackModel,
                             const QStringList &fallbackOptions) const;

    // Commit a generated config. Every write is atomic and the set is ordered
    // so a half-applied state still loads: keymaps first, then the data table,
    // then the loader, then the hyprland.lua block.
    bool commit(const GeneratedConfig &config, const RuleSet &ruleSet,
                QString *error) const;

    // --- hyprland.lua managed block ---
    static QString blockBeginMarker();
    static QString blockEndMarker();
    static QString managedBlock();

    static bool hasManagedBlock(const QString &contents);
    // Append or refresh the block, leaving every other line untouched.
    static QString withManagedBlock(const QString &contents);
    static QString withoutManagedBlock(const QString &contents);

    // Lua's own quoting rules, so a string can never end a literal early.
    static QString luaQuote(const QString &value);

private:
    QString renderGeneratedLua(const RuleSet &ruleSet,
                               const QVector<GeneratedKeymap> &keymaps) const;
    static QString renderLoaderLua();
};

} // namespace omakeyboard
