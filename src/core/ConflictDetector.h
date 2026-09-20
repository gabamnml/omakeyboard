#pragma once

#include "OmarchyConfigParser.h"
#include "Types.h"

namespace omakeyboard {

// Finds every way a rule set can contradict itself or the system it is going
// to be applied to.
//
// Four kinds of conflict, all of them real cases on a stock Omarchy install:
//
//  1. Two OmaKeyboard rules claiming the same physical key.
//  2. An XKB option fighting a keymap rule for the same key -- for instance
//     "Caps Lock as Ctrl" against Omarchy's default compose:caps, which puts
//     Compose on Caps Lock. Both claim CAPS and only one can win.
//  3. Two XKB options from the same group, which XKB resolves unpredictably.
//  4. A shortcut landing on a combination that is already bound, whether by
//     Omarchy's defaults or by the user's own bindings.lua.
class ConflictDetector {
public:
    struct Inputs {
        RuleSet ruleSet;
        QVector<ExistingBinding> existingBindings;
        // kb_options currently in effect, from hyprctl getoption.
        QStringList inheritedXkbOptions;
    };

    QVector<Conflict> detect(const Inputs &inputs) const;

    // Whether anything found is bad enough to refuse an apply.
    static bool hasBlockingConflict(const QVector<Conflict> &conflicts);
    static int countBySeverity(const QVector<Conflict> &conflicts, ConflictSeverity severity);

private:
    void detectDuplicateKeys(const RuleSet &ruleSet, QVector<Conflict> *out) const;
    void detectOptionClashes(const RuleSet &ruleSet, const QStringList &inherited,
                             QVector<Conflict> *out) const;
    void detectShortcutClashes(const RuleSet &ruleSet,
                               const QVector<ExistingBinding> &existing,
                               QVector<Conflict> *out) const;
};

} // namespace omakeyboard
