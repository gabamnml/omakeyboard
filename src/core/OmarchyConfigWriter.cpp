#include "OmarchyConfigWriter.h"

#include "KeymapService.h"
#include "Paths.h"
#include "RuleSet.h"

#include <QFileInfo>

namespace omakeyboard {

QString OmarchyConfigWriter::blockBeginMarker()
{
    return QStringLiteral("-- >>> OmaKeyboard BEGIN (managed by OmaKeyboard) >>>");
}

QString OmarchyConfigWriter::blockEndMarker()
{
    return QStringLiteral("-- <<< OmaKeyboard END <<<");
}

QString OmarchyConfigWriter::managedBlock()
{
    // Loaded last on purpose: Omarchy's hyprland.lua requires the user's own
    // modules before this point, so OmaKeyboard' unbinds and binds win over both
    // the defaults and hand-written overrides.
    return blockBeginMarker() + QStringLiteral("\n")
        + QStringLiteral("-- Edit your mappings in the OmaKeyboard app, not here.\n")
        + QStringLiteral("require(\"%1\")\n").arg(Paths::loaderModuleName())
        + blockEndMarker() + QStringLiteral("\n");
}

bool OmarchyConfigWriter::hasManagedBlock(const QString &contents)
{
    return contents.contains(blockBeginMarker()) && contents.contains(blockEndMarker());
}

QString OmarchyConfigWriter::withoutManagedBlock(const QString &contents)
{
    const int begin = contents.indexOf(blockBeginMarker());
    if (begin < 0)
        return contents;
    const int end = contents.indexOf(blockEndMarker(), begin);
    if (end < 0)
        return contents;

    const int endOfLine = end + blockEndMarker().size();
    QString result = contents.left(begin) + contents.mid(endOfLine);
    // Drop the newline the removed block left behind, but keep the file's own
    // trailing newline.
    while (result.endsWith(QStringLiteral("\n\n\n")))
        result.chop(1);
    return result;
}

QString OmarchyConfigWriter::withManagedBlock(const QString &contents)
{
    // Replacing rather than editing in place keeps this idempotent: the block
    // always ends up in exactly one place with exactly the current content.
    QString base = withoutManagedBlock(contents);
    if (!base.isEmpty() && !base.endsWith(QLatin1Char('\n')))
        base += QLatin1Char('\n');
    if (!base.endsWith(QStringLiteral("\n\n")))
        base += QLatin1Char('\n');
    return base + managedBlock();
}

QString OmarchyConfigWriter::luaQuote(const QString &value)
{
    // Equivalent to Lua's string.format("%q"): escape the delimiter, the
    // escape character itself, and every control character. A newline becomes
    // "\n" rather than a literal break, so a value can never split a statement.
    QString out;
    out.reserve(value.size() + 2);
    out += QLatin1Char('"');
    for (const QChar character : value) {
        const ushort code = character.unicode();
        switch (code) {
        case '"':  out += QStringLiteral("\\\""); break;
        case '\\': out += QStringLiteral("\\\\"); break;
        case '\n': out += QStringLiteral("\\n"); break;
        case '\r': out += QStringLiteral("\\r"); break;
        case '\t': out += QStringLiteral("\\t"); break;
        case '\0': out += QStringLiteral("\\0"); break;
        default:
            if (code < 0x20 || code == 0x7f)
                out += QStringLiteral("\\%1").arg(code);
            else
                out += character;
            break;
        }
    }
    out += QLatin1Char('"');
    return out;
}

GeneratedConfig OmarchyConfigWriter::generate(const RuleSet &ruleSet,
                                              const QString &fallbackLayout,
                                              const QString &fallbackVariant,
                                              const QString &fallbackModel,
                                              const QStringList &fallbackOptions) const
{
    GeneratedConfig config;

    KeymapService keymaps;
    QHash<QString, QHash<QString, QStringList>> symbolCache;

    for (const Profile &profile : ruleSet.profiles) {
        if (!profile.enabled)
            continue;

        // Options declared as mappings join the profile's own option list; both
        // end up in kb_options, which is a single string rather than a list.
        QStringList options = profile.xkbOptions;
        bool hasKeymapRules = false;
        for (const Remap &remap : profile.remaps) {
            if (!remap.enabled)
                continue;
            if (remap.kind == MappingKind::XkbOption) {
                if (!remap.xkbOption.isEmpty() && !options.contains(remap.xkbOption))
                    options.append(remap.xkbOption);
            } else {
                hasKeymapRules = true;
            }
        }

        Profile resolved = profile;
        resolved.xkbOptions = options;

        GeneratedKeymap keymap;
        keymap.profileId = profile.id;
        keymap.deviceName = profile.deviceName;
        keymap.kbOptions = KeymapService::optionsToString(options);
        keymap.behaviour = profile.behaviour;

        if (hasKeymapRules) {
            const KccgstComponents components = keymaps.resolveComponents(
                resolved, fallbackLayout, fallbackVariant, fallbackModel, fallbackOptions);
            if (!components.isValid()) {
                config.error = QStringLiteral(
                    "Could not resolve the XKB layout for \"%1\". Is xkbcli available?")
                    .arg(profile.name.isEmpty() ? profile.id : profile.name);
                return config;
            }

            // Compiling the base keymap is the expensive step; profiles that
            // share an RMLVO share the result.
            const QString cacheKey = components.keycodes + QLatin1Char('|')
                + components.types + QLatin1Char('|') + components.compat
                + QLatin1Char('|') + components.symbols;
            if (!symbolCache.contains(cacheKey))
                symbolCache.insert(cacheKey, keymaps.baseSymbols(components));

            const KeymapBuild build =
                keymaps.build(resolved, components, symbolCache.value(cacheKey));
            config.notes += build.notes;
            if (!build.ok) {
                config.error = QStringLiteral("The keymap for \"%1\" did not compile:\n%2")
                                   .arg(profile.name.isEmpty() ? profile.id : profile.name,
                                        build.error);
                return config;
            }

            keymap.source = build.source;
            keymap.path = Paths::keymapFile(profile.id);
        }

        // A profile carrying only behaviour settings still has to be emitted.
        if (keymap.source.isEmpty() && keymap.kbOptions.isEmpty()
            && keymap.behaviour.isEmpty()) {
            continue;
        }

        config.keymaps.append(keymap);
    }

    config.generatedLua = renderGeneratedLua(ruleSet, config.keymaps);
    config.loaderLua = renderLoaderLua();
    config.ok = true;
    return config;
}

namespace {

// Behaviour fields as Lua key/value lines. Only chosen settings appear, so the
// loader can tell "set this" from "leave it alone" by presence alone.
QString behaviourLines(const KeyboardBehaviour &behaviour, const QString &indent,
                       bool includeShortcuts)
{
    QString out;
    const auto boolean = [&](const QString &key, Tristate value) {
        if (value == Tristate::Inherit)
            return;
        out += QStringLiteral("%1%2 = %3,\n")
                   .arg(indent, key, value == Tristate::On ? QStringLiteral("true")
                                                           : QStringLiteral("false"));
    };

    if (behaviour.repeatRate >= 0)
        out += QStringLiteral("%1repeat_rate = %2,\n").arg(indent).arg(behaviour.repeatRate);
    if (behaviour.repeatDelay >= 0)
        out += QStringLiteral("%1repeat_delay = %2,\n").arg(indent).arg(behaviour.repeatDelay);
    boolean(QStringLiteral("numlock_by_default"), behaviour.numlockOnStart);
    boolean(QStringLiteral("resolve_binds_by_sym"), behaviour.resolveBindsBySym);
    // keybinds exists only on a device, never on input.
    if (includeShortcuts)
        boolean(QStringLiteral("keybinds"), behaviour.shortcutsEnabled);

    return out;
}

} // namespace

QString OmarchyConfigWriter::renderGeneratedLua(const RuleSet &ruleSet,
                                                const QVector<GeneratedKeymap> &keymaps) const
{
    QString out;
    out += QStringLiteral("-- Generated by OmaKeyboard. Do not edit.\n");
    out += QStringLiteral("-- This file is data only: a table of validated values. The code that\n");
    out += QStringLiteral("-- applies it lives in ~/.config/hypr/omakeyboard.lua and never changes.\n");
    out += QStringLiteral("return {\n");
    out += QStringLiteral("  schema = 1,\n");

    // Global profile: applied through input.kb_*, so it covers every keyboard.
    for (const GeneratedKeymap &keymap : keymaps) {
        if (!keymap.deviceName.isEmpty())
            continue;
        out += QStringLiteral("  global = {\n");
        if (!keymap.kbOptions.isEmpty())
            out += QStringLiteral("    kb_options = %1,\n").arg(luaQuote(keymap.kbOptions));
        if (!keymap.path.isEmpty())
            out += QStringLiteral("    kb_file = %1,\n").arg(luaQuote(keymap.path));
        out += behaviourLines(keymap.behaviour, QStringLiteral("    "), false);
        out += QStringLiteral("  },\n");
        break;
    }

    out += QStringLiteral("  devices = {\n");
    for (const GeneratedKeymap &keymap : keymaps) {
        if (keymap.deviceName.isEmpty())
            continue;
        out += QStringLiteral("    {\n");
        out += QStringLiteral("      name = %1,\n").arg(luaQuote(keymap.deviceName));
        if (!keymap.kbOptions.isEmpty())
            out += QStringLiteral("      kb_options = %1,\n").arg(luaQuote(keymap.kbOptions));
        if (!keymap.path.isEmpty())
            out += QStringLiteral("      kb_file = %1,\n").arg(luaQuote(keymap.path));
        out += behaviourLines(keymap.behaviour, QStringLiteral("      "), true);
        out += QStringLiteral("    },\n");
    }
    out += QStringLiteral("  },\n");

    // Unbinds come first at load time, so a replaced shortcut loses its old
    // meaning before the new one is installed.
    out += QStringLiteral("  unbinds = {\n");
    QStringList emitted;
    for (const Shortcut &shortcut : ruleSet.shortcuts) {
        if (!shortcut.enabled || !shortcut.unbindFirst)
            continue;
        if (!RuleSetValidator::isValidBindKeys(shortcut.keys) || emitted.contains(shortcut.keys))
            continue;
        emitted.append(shortcut.keys);
        out += QStringLiteral("    %1,\n").arg(luaQuote(shortcut.keys));
    }
    out += QStringLiteral("  },\n");

    out += QStringLiteral("  binds = {\n");
    for (const Shortcut &shortcut : ruleSet.shortcuts) {
        if (!shortcut.enabled)
            continue;
        if (!RuleSetValidator::isValidBindKeys(shortcut.keys))
            continue;
        if (shortcut.actionKind == ActionKind::None || shortcut.actionValue.isEmpty())
            continue;

        out += QStringLiteral("    {\n");
        out += QStringLiteral("      keys = %1,\n").arg(luaQuote(shortcut.keys));
        out += QStringLiteral("      description = %1,\n").arg(luaQuote(shortcut.description));
        out += QStringLiteral("      kind = %1,\n").arg(luaQuote(actionKindName(shortcut.actionKind)));
        out += QStringLiteral("      value = %1,\n").arg(luaQuote(shortcut.actionValue));
        if (!shortcut.focusMatch.isEmpty())
            out += QStringLiteral("      focus = %1,\n").arg(luaQuote(shortcut.focusMatch));
        if (!shortcut.flags.isEmpty()) {
            QStringList flags;
            const QStringList known = RuleSetValidator::knownBindFlags();
            for (const QString &flag : shortcut.flags)
                if (known.contains(flag))
                    flags.append(QStringLiteral("%1 = true").arg(flag));
            if (!flags.isEmpty())
                out += QStringLiteral("      options = { %1 },\n").arg(flags.join(QStringLiteral(", ")));
        }
        out += QStringLiteral("    },\n");
    }
    out += QStringLiteral("  },\n");
    out += QStringLiteral("}\n");

    return out;
}

QString OmarchyConfigWriter::renderLoaderLua()
{
    // Fixed content: no value from the rule set is ever interpolated here.
    // Every field read out of the data table is type-checked, and the whole
    // load is wrapped so a damaged file degrades to "no mappings" instead of
    // breaking the Hyprland configuration.
    return QStringLiteral(R"LUA(-- Managed by OmaKeyboard. Do not edit.
--
-- This file is fixed: it contains no user data. It reads the validated table
-- in ~/.config/omakeyboard/generated.lua and applies it through Omarchy's own
-- helpers. Editing your mappings is done in the OmaKeyboard app.

local loaded, rules = pcall(require, "omakeyboard.generated")
if not loaded or type(rules) ~= "table" or rules.schema ~= 1 then
  return
end

-- Copy only the fields that are present and of the expected type. A field the
-- app did not write is a field the user chose to inherit, so it must stay
-- untouched rather than be set to a default.
local function copy_settings(spec, target)
  if type(spec.repeat_rate) == "number" then
    target.repeat_rate = spec.repeat_rate
  end
  if type(spec.repeat_delay) == "number" then
    target.repeat_delay = spec.repeat_delay
  end
  if type(spec.numlock_by_default) == "boolean" then
    target.numlock_by_default = spec.numlock_by_default
  end
  if type(spec.resolve_binds_by_sym) == "boolean" then
    target.resolve_binds_by_sym = spec.resolve_binds_by_sym
  end
  return target
end

local function apply_input(spec)
  if type(spec) ~= "table" then
    return
  end

  local input = {}
  if type(spec.kb_options) == "string" then
    input.kb_options = spec.kb_options
  end
  if type(spec.kb_file) == "string" then
    input.kb_file = spec.kb_file
  end
  copy_settings(spec, input)

  if next(input) ~= nil then
    hl.config({ input = input })
  end
end

apply_input(rules.global)

-- Per-keyboard rules. Hyprland matches hl.device{} by name when a device
-- appears, so a keyboard gets its own keymap automatically on connect.
for _, device in ipairs(rules.devices or {}) do
  if type(device) == "table" and type(device.name) == "string" then
    local spec = { name = device.name }
    if type(device.kb_file) == "string" then
      spec.kb_file = device.kb_file
    end
    if type(device.kb_options) == "string" then
      spec.kb_options = device.kb_options
    end
    copy_settings(device, spec)
    -- keybinds is a device-only setting: it decides whether shortcuts fire
    -- from this keyboard at all.
    if type(device.keybinds) == "boolean" then
      spec.keybinds = device.keybinds
    end
    hl.device(spec)
  end
end

for _, keys in ipairs(rules.unbinds or {}) do
  if type(keys) == "string" then
    hl.unbind(keys)
  end
end

-- Window and workspace actions resolve through this fixed table. A rule names
-- an entry; it never carries Lua to run. The calls match the ones Omarchy uses
-- in default/hypr/bindings/tiling.lua.
local dispatchers = {
  ["window.close"] = function() return hl.dsp.window.close() end,
  ["window.fullscreen"] = function() return hl.dsp.window.fullscreen({ mode = "fullscreen" }) end,
  ["window.maximize"] = function() return hl.dsp.window.fullscreen({ mode = "maximized" }) end,
  ["window.float"] = function() return hl.dsp.window.float({ action = "toggle" }) end,
  ["window.center"] = function() return hl.dsp.window.center() end,
  ["window.bring_to_top"] = function() return hl.dsp.window.bring_to_top() end,
  ["window.cycle_next"] = function() return hl.dsp.window.cycle_next() end,
  ["focus.left"] = function() return hl.dsp.focus({ direction = "l" }) end,
  ["focus.right"] = function() return hl.dsp.focus({ direction = "r" }) end,
  ["focus.up"] = function() return hl.dsp.focus({ direction = "u" }) end,
  ["focus.down"] = function() return hl.dsp.focus({ direction = "d" }) end,
  ["workspace.next"] = function() return hl.dsp.focus({ workspace = "e+1" }) end,
  ["workspace.previous"] = function() return hl.dsp.focus({ workspace = "e-1" }) end,
  ["workspace.last"] = function() return hl.dsp.focus({ workspace = "previous" }) end,
  ["workspace.scratchpad"] = function() return hl.dsp.workspace.toggle_special({ name = "scratchpad" }) end,
  ["monitor.next"] = function() return hl.dsp.focus({ monitor = "+1" }) end,
  ["monitor.previous"] = function() return hl.dsp.focus({ monitor = "-1" }) end,
}

for _, bind in ipairs(rules.binds or {}) do
  if type(bind) == "table" and type(bind.keys) == "string" and type(bind.value) == "string" then
    local description = type(bind.description) == "string" and bind.description or nil
    local options = type(bind.options) == "table" and bind.options or nil
    local kind = bind.kind
    local target = nil

    if kind == "command" or kind == "media" then
      target = bind.value
    elseif kind == "omarchyLaunch" then
      target = { omarchy = bind.value }
    elseif kind == "desktopApp" then
      if type(bind.focus) == "string" then
        target = { launch = bind.value, focus = bind.focus }
      else
        target = { launch = bind.value }
      end
    elseif kind == "webApp" then
      if type(bind.focus) == "string" then
        target = { webapp = bind.value, focus = true }
      else
        target = { webapp = bind.value }
      end
    elseif kind == "tui" then
      target = { tui = bind.value, focus = true }
    elseif kind == "toggle" then
      o.bind_toggle(bind.keys, description, bind.value, options)
    elseif kind == "windowAction" or kind == "workspaceAction" then
      local dispatcher = dispatchers[bind.value]
      if dispatcher then
        o.bind(bind.keys, description, dispatcher(), options)
      end
    end

    if target ~= nil then
      o.bind(bind.keys, description, target, options)
    end
  end
end
)LUA");
}

bool OmarchyConfigWriter::commit(const GeneratedConfig &config, const RuleSet &ruleSet,
                                 QString *error) const
{
    if (!config.ok) {
        if (error)
            *error = config.error;
        return false;
    }

    Paths::ensureDir(Paths::keymapDir());
    Paths::ensureDir(Paths::appConfigDir());

    // Keymaps first: the data table references them by path, so they must exist
    // before anything can read it.
    for (const GeneratedKeymap &keymap : config.keymaps) {
        if (keymap.source.isEmpty() || keymap.path.isEmpty())
            continue;
        if (!Paths::writeAtomically(keymap.path, keymap.source.toUtf8(), error))
            return false;
    }

    if (!Paths::writeAtomically(Paths::generatedLuaFile(), config.generatedLua.toUtf8(), error))
        return false;
    if (!Paths::writeAtomically(Paths::loaderLuaFile(), config.loaderLua.toUtf8(), error))
        return false;
    if (!Paths::writeAtomically(Paths::rulesFile(), RuleSetSerializer::toJsonBytes(ruleSet), error))
        return false;

    // The hyprland.lua block goes last, because it is what makes any of the
    // above take effect.
    if (ruleSet.manageHyprlandBlock) {
        bool read = false;
        const QByteArray current = Paths::read(Paths::hyprlandLuaFile(), &read);
        if (read) {
            const QString contents = QString::fromUtf8(current);
            const QString updated = withManagedBlock(contents);
            if (updated != contents) {
                if (!Paths::writeAtomically(Paths::hyprlandLuaFile(), updated.toUtf8(), error))
                    return false;
            }
        } else if (error) {
            *error = QStringLiteral("Could not read %1").arg(Paths::hyprlandLuaFile());
            return false;
        }
    }

    return true;
}

} // namespace omakeyboard
