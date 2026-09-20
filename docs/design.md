# How OmaKeyboard works

Notes for anyone working on the app, or deciding whether to trust it with
their configuration.

## Files it writes

Four, and one marked block in a fifth:

```
~/.config/omakeyboard/rules.json        your rules; the app's source of truth
~/.config/omakeyboard/generated.lua     generated data table, no code
~/.config/hypr/omakeyboard.lua          the loader; fixed content, no user data
~/.local/state/omakeyboard/keymaps/     one generated .xkb per keyboard
~/.local/state/omakeyboard/backups/     a snapshot before every change

~/.config/hypr/hyprland.lua         one block, between these markers:
                                    -- >>> OmaKeyboard BEGIN …
                                    -- <<< OmaKeyboard END <<<
```

Nothing else in your configuration is touched. The block is appended after
Omarchy's own requires so its bindings win, it is idempotent, and
"Restore defaults" takes it back out.

### Why the split

The rules are JSON, which is parsed and never executed. The generated Lua is a
data-only `return { … }` table whose every string is escaped with Lua's own
quoting rules. The code that applies it is a separate, fixed file that contains
no user data at all and type-checks every field it reads; a damaged data file
degrades to "no mappings" instead of breaking your Hyprland configuration.

This follows a lesson Omarchy learned in the open — `default/hypr/toggles.lua`
carries a note about older generated Lua that "could carry an injected USB
device name" and "must never be loaded as code again".

## What happens when you press Apply

1. **Validate** every field against the system's own catalogues. A keysym is
   checked with `xkb_keysym_from_name`, because `xkbcli compile-keymap` accepts
   an unknown name and exits 0 — a successful compile is not proof.
2. **Detect conflicts**, against your rules, Omarchy's defaults, and the XKB
   option groups.
3. **Compile every keymap** with `xkbcli compile-keymap --test`.
4. **Back up** everything the apply may rewrite, as one snapshot.
5. **Write atomically** — a temporary file in the same directory, then
   `rename(2)`.
6. **Test-load** the whole Hyprland configuration in a sandboxed `lua` process
   with stubbed `hl` and `o`, so a broken file never reaches the compositor.
7. **Reload and verify** with `hyprctl reload`, `hyprctl configerrors` and a
   read-back of `input:kb_options`.

Any step failing restores the backup and reloads again. The dialog shows which
step it got to.

![What an apply did, stage by stage](docs/screenshots/apply.png)

## About "press a key"

Capture reads what the compositor forwards, and nothing more. There is no
keyboard grab, because on Hyprland there is nothing to grab with — both routes
are dead ends, and both were checked rather than assumed:

- the **shortcuts-inhibit protocol** is how a client asks the compositor to
  stop resolving bindings. Hyprland implements it; Qt's Wayland backend does
  not (no `zwp_keyboard_shortcuts_inhibit` symbols in `libQt6WaylandClient`),
  so there is nothing to ask with.
- an **empty Hyprland submap** does suspend every binding. It also stops keys
  reaching the focused client at all (verified against 0.56.2: with a submap
  active, an injected keypress never arrives; with it reset, the same key
  does). So the window would see less, not more.

What this means in practice:

- **Capturing a remap's source key works.** A physical key is almost never
  bound on its own, which is the common case for a keyboard app.
- **Capturing a shortcut that is already taken does not.** Hyprland consumes
  it first. The app says so, and the key list and the "already bound" view are
  the way to pick those.

## Architecture

```
Qt Quick / QML  ── reads properties, calls invokables, writes nothing
      │
ConfigRepository ── the only door between interface and engine
      │
      ├── OmarchyConfigParser   rules.json + a sandboxed scan of the Lua config
      ├── ConflictDetector      your rules vs. the system
      ├── KeymapService         XKB keymap generation and compilation
      ├── OmarchyConfigWriter   data table, loader, managed block, atomic writes
      ├── ConfigBackupService   snapshots and rollback
      ├── HyprlandReloadService hyprctl: devices, reload, configerrors
      └── NativeEngine          what this system can and cannot do
```

QML never touches a file or a process. Everything below `ConfigRepository` is
plain C++ with no Qt Quick dependency, which is what lets the engine be tested
on its own.

## Releases

Two PKGBUILDs, because they build different things:

- `packaging/PKGBUILD` builds the working tree. This is what `./bin/install`
  uses.
- `packaging/release/PKGBUILD` builds a published tag's source archive. One
  recipe, two destinations — the Omarchy package repository and the AUR —
  described in [`packaging/release/README.md`](../packaging/release/README.md).

Cutting a release:

```bash
./bin/test                                  # engine and interface
git tag -a v0.1.0 -m "OmaKeyboard 0.1.0"
git push origin v0.1.0

cd packaging/release
# update pkgver, then:
makepkg -g                                  # the tag's checksum
makepkg --printsrcinfo > .SRCINFO
makepkg -f                                  # builds from the tag; attach this
```
