#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <xkbcommon/xkbcommon.h>

#include "core/ConfigBackupService.h"
#include "core/ConfigRepository.h"
#include "core/ConflictDetector.h"
#include "core/HyprlandReloadService.h"
#include "core/KeyCaptureService.h"
#include "core/KeyCatalog.h"
#include "core/KeymapService.h"
#include "core/LuaScanner.h"
#include "core/OmarchyConfigParser.h"
#include "core/OmarchyConfigWriter.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "core/RuleSet.h"
#include "core/XkbOptionCatalog.h"
#include "engine/NativeEngine.h"
#include "models/RowListModel.h"

using namespace omakeyboard;

// Every test runs against a throwaway XDG root, so nothing here can touch the
// real configuration. Paths reads XDG_CONFIG_HOME and XDG_STATE_HOME on every
// call, which is what makes the redirection complete.
class TestOmaKeyboard : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_root;

    QString configHome() const { return m_root.path() + QStringLiteral("/config"); }
    QString stateHome() const { return m_root.path() + QStringLiteral("/state"); }

    static Profile makeProfile(const QString &id, const QString &device = {})
    {
        Profile profile;
        profile.id = id;
        profile.name = id;
        profile.deviceName = device;
        profile.layout = QStringLiteral("us");
        return profile;
    }

    static Remap keyRemap(const QString &id, const QString &source, const QString &target)
    {
        Remap remap;
        remap.id = id;
        remap.kind = MappingKind::KeyRemap;
        remap.sourceKey = source;
        remap.levels = { target };
        return remap;
    }

    static Remap modifierRemap(const QString &id, const QString &source, const QString &target)
    {
        Remap remap = keyRemap(id, source, target);
        remap.kind = MappingKind::ModifierRemap;
        return remap;
    }

    static Shortcut shortcut(const QString &id, const QString &keys, const QString &value)
    {
        Shortcut entry;
        entry.id = id;
        entry.keys = keys;
        entry.description = QStringLiteral("Test");
        entry.actionKind = ActionKind::Command;
        entry.actionValue = value;
        return entry;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        qputenv("XDG_CONFIG_HOME", configHome().toUtf8());
        qputenv("XDG_STATE_HOME", stateHome().toUtf8());
        QDir().mkpath(configHome() + QStringLiteral("/hypr"));
        QDir().mkpath(stateHome());

        KeyCatalog::instance().load();
        XkbOptionCatalog::instance().load();
    }

    void init()
    {
        // A fresh hyprland.lua for each test, shaped like the real one.
        const QByteArray hyprland = R"LUA(-- Learn how to configure Hyprland
dofile((os.getenv("OMARCHY_PATH") or "/usr/share/omarchy") .. "/default/hypr/bootstrap.lua")
require("default.hypr.omarchy")
require("hypr.monitors")
require("hypr.input")
require("hypr.bindings")
)LUA";
        QFile file(Paths::hyprlandLuaFile());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(hyprland);
        file.close();

        QFile::remove(Paths::rulesFile());
        QFile::remove(Paths::generatedLuaFile());
        QFile::remove(Paths::loaderLuaFile());
        QDir(Paths::keymapDir()).removeRecursively();
        QDir(Paths::backupDir()).removeRecursively();
    }

    // --- paths ------------------------------------------------------------

    void paths_honourXdgOverrides()
    {
        QCOMPARE(Paths::configHome(), configHome());
        QCOMPARE(Paths::stateHome(), stateHome());
        QVERIFY(Paths::rulesFile().startsWith(configHome()));
        // Backups must not land inside ~/.config.
        QVERIFY(Paths::backupDir().startsWith(stateHome()));
        QVERIFY(!Paths::backupDir().startsWith(configHome()));
    }

    void paths_writeAtomicallyCreatesDirectories()
    {
        const QString target = stateHome() + QStringLiteral("/deep/nested/file.txt");
        QString error;
        QVERIFY2(Paths::writeAtomically(target, QByteArrayLiteral("hello"), &error),
                 qPrintable(error));
        bool ok = false;
        QCOMPARE(Paths::read(target, &ok), QByteArrayLiteral("hello"));
        QVERIFY(ok);
    }

    // --- rules.json parsing and round-trip --------------------------------

    void ruleSet_roundTripPreservesEverything()
    {
        RuleSet original;
        Profile global = makeProfile(QStringLiteral("all-keyboards"));
        global.remaps.append(keyRemap(QStringLiteral("m1"), QStringLiteral("CAPS"),
                                      QStringLiteral("Escape")));
        global.xkbOptions << QStringLiteral("compose:ralt");

        Profile apple = makeProfile(QStringLiteral("apple"),
                                    QStringLiteral("apple-inc.-apple-internal-keyboard-/-trackpad"));
        Remap layer;
        layer.id = QStringLiteral("m2");
        layer.kind = MappingKind::LayerChooser;
        layer.sourceKey = QStringLiteral("CAPS");
        layer.levels = { QStringLiteral("ISO_Level3_Shift") };
        apple.remaps.append(layer);

        Remap layerKey;
        layerKey.id = QStringLiteral("m3");
        layerKey.kind = MappingKind::LayerKey;
        layerKey.sourceKey = QStringLiteral("FK01");
        layerKey.levels = { QString(), QString(), QStringLiteral("XF86MonBrightnessDown"),
                            QStringLiteral("XF86MonBrightnessDown") };
        layerKey.note = QStringLiteral("brightness on the layer");
        apple.remaps.append(layerKey);

        original.profiles << global << apple;
        original.shortcuts << shortcut(QStringLiteral("s1"), QStringLiteral("SUPER + E"),
                                       QStringLiteral("nautilus"));
        original.shortcuts.last().flags << QStringLiteral("locked");

        const QByteArray json = RuleSetSerializer::toJsonBytes(original);

        QStringList warnings;
        QString error;
        const RuleSet parsed = RuleSetSerializer::fromJsonBytes(json, &warnings, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QStringLiteral("; "))));

        QCOMPARE(parsed.profiles.size(), 2);
        QCOMPARE(parsed.shortcuts.size(), 1);

        const Profile &reparsedApple = parsed.profiles.at(1);
        QCOMPARE(reparsedApple.deviceName,
                 QStringLiteral("apple-inc.-apple-internal-keyboard-/-trackpad"));
        QCOMPARE(reparsedApple.remaps.size(), 2);
        QCOMPARE(reparsedApple.remaps.at(0).kind, MappingKind::LayerChooser);
        QCOMPARE(reparsedApple.remaps.at(1).kind, MappingKind::LayerKey);
        // The empty levels that let the base keymap show through must survive.
        QCOMPARE(reparsedApple.remaps.at(1).levels.size(), 4);
        QVERIFY(reparsedApple.remaps.at(1).levels.at(0).isEmpty());
        QCOMPARE(reparsedApple.remaps.at(1).levels.at(2),
                 QStringLiteral("XF86MonBrightnessDown"));

        QCOMPARE(parsed.shortcuts.at(0).flags, QStringList{ QStringLiteral("locked") });

        // Serialising the parsed copy must produce identical bytes.
        QCOMPARE(RuleSetSerializer::toJsonBytes(parsed), json);
    }

    void ruleSet_dropsMalformedEntriesInsteadOfFailing()
    {
        const QByteArray json = R"JSON({
          "schema": 1,
          "profiles": [{
            "id": "p1",
            "xkbOptions": ["caps:escape", "not an option", "rm -rf /"],
            "remaps": [
              { "id": "good", "kind": "keyRemap", "sourceKey": "CAPS", "levels": ["Escape"] },
              { "id": "bad", "kind": "somethingNew", "sourceKey": "CAPS" }
            ]
          }],
          "shortcuts": [{
            "id": "s1", "keys": "SUPER + E", "actionKind": "command",
            "actionValue": "true", "flags": ["locked", "madeUpFlag"]
          }]
        })JSON";

        QStringList warnings;
        QString error;
        const RuleSet parsed = RuleSetSerializer::fromJsonBytes(json, &warnings, &error);
        QVERIFY(error.isEmpty());

        QCOMPARE(parsed.profiles.size(), 1);
        // Only the well-formed option survives.
        QCOMPARE(parsed.profiles.at(0).xkbOptions, QStringList{ QStringLiteral("caps:escape") });
        // The unknown mapping kind is dropped, the good one kept.
        QCOMPARE(parsed.profiles.at(0).remaps.size(), 1);
        QCOMPARE(parsed.profiles.at(0).remaps.at(0).id, QStringLiteral("good"));
        QCOMPARE(parsed.shortcuts.at(0).flags, QStringList{ QStringLiteral("locked") });
        QVERIFY(!warnings.isEmpty());
    }

    void validator_rejectsUnsafeValues()
    {
        QVERIFY(RuleSetValidator::isValidKeysym(QStringLiteral("XF86AudioPlay")));
        QVERIFY(RuleSetValidator::isValidKeysym(QStringLiteral("U20AC")));
        QVERIFY(!RuleSetValidator::isValidKeysym(QStringLiteral("Escape ] }; evil")));
        QVERIFY(!RuleSetValidator::isValidKeysym(QStringLiteral("a\nb")));
        QVERIFY(!RuleSetValidator::isValidKeysym(QString()));

        QVERIFY(RuleSetValidator::isValidKeycodeName(QStringLiteral("CAPS")));
        QVERIFY(!RuleSetValidator::isValidKeycodeName(QStringLiteral("caps>")));

        QVERIFY(RuleSetValidator::isValidXkbOption(QStringLiteral("ctrl:nocaps")));
        QVERIFY(!RuleSetValidator::isValidXkbOption(QStringLiteral("ctrl:nocaps,evil:thing")));

        QVERIFY(RuleSetValidator::isValidBindKeys(QStringLiteral("SUPER + SHIFT + E")));
        QVERIFY(RuleSetValidator::isValidBindKeys(QStringLiteral("XF86AudioPlay")));
        QVERIFY(RuleSetValidator::isValidBindKeys(QStringLiteral("SUPER + code:20")));
        QVERIFY(!RuleSetValidator::isValidBindKeys(QStringLiteral("SUPER + E\"); evil()")));

        QVERIFY(RuleSetValidator::isValidDeviceName(
            QStringLiteral("apple-inc.-apple-internal-keyboard-/-trackpad")));
        QVERIFY(!RuleSetValidator::isValidDeviceName(QStringLiteral("Bad Name With Spaces")));
    }

    void hyprctl_parsesAnUnsetOptionAsEmpty()
    {
        // The reply for a set option.
        QCOMPARE(HyprlandReloadService::parseOptionValue(
                     QStringLiteral("str: us\nset: true\n")),
                 QStringLiteral("us"));

        // And for an unset one, where the value line is bare. Skipping the
        // space after "str:" with \s would swallow the newline too and return
        // "set: true", which then travels into an XKB variant and makes every
        // generated keymap fail to compile.
        QCOMPARE(HyprlandReloadService::parseOptionValue(
                     QStringLiteral("str: \nset: true\n")),
                 QString());
        QCOMPARE(HyprlandReloadService::parseOptionValue(
                     QStringLiteral("str:\nset: true\n")),
                 QString());

        // A value with internal spaces survives intact.
        QCOMPARE(HyprlandReloadService::parseOptionValue(
                     QStringLiteral("str: compose:caps,shift:both_capslock_cancel\nset: true")),
                 QStringLiteral("compose:caps,shift:both_capslock_cancel"));

        // Numbers and booleans have their own prefixes and are read too, so
        // the tester and the Advanced page can show what is in effect.
        QCOMPARE(HyprlandReloadService::parseOptionValue(QStringLiteral("int: 40\nset: true")),
                 QStringLiteral("40"));
        QCOMPARE(HyprlandReloadService::parseOptionValue(QStringLiteral("bool: true\nset: true")),
                 QStringLiteral("true"));
        QVERIFY(HyprlandReloadService::parseOptionValue(QStringLiteral("nonsense")).isEmpty());
    }

    // --- key and option catalogues ---------------------------------------

    void keyCatalog_readsTheSystemKeycodes()
    {
        const KeyCatalog &catalog = KeyCatalog::instance();
        QVERIFY(catalog.keys().size() > 100);
        QVERIFY(catalog.hasCode(QStringLiteral("CAPS")));
        QCOMPARE(catalog.labelForCode(QStringLiteral("CAPS")), QStringLiteral("Caps Lock"));
        QCOMPARE(catalog.labelForCode(QStringLiteral("FK01")), QStringLiteral("F1"));

        // Qt reports XKB keycodes, which are evdev codes plus eight: Caps Lock
        // is evdev 58 and XKB 66. The raw evdev value must NOT also resolve to
        // Caps Lock -- 58 is a valid XKB keycode for a different key, so
        // accepting both conventions would silently capture the wrong one.
        QCOMPARE(catalog.codeForNativeScanCode(66), QStringLiteral("CAPS"));
        QVERIFY(catalog.codeForNativeScanCode(58) != QStringLiteral("CAPS"));
        // An unknown code resolves to nothing rather than to a guess.
        QVERIFY(catalog.codeForNativeScanCode(99999).isEmpty());
    }

    void keyCatalog_labelsPositionalKeysByWhatTheyType()
    {
        if (!KeymapService::isAvailable())
            QSKIP("xkbcli is not installed");

        KeyCatalog &catalog = KeyCatalog::instance();
        // Before the layout is known, AC06 can only be described by position.
        QVERIFY(catalog.hasCode(QStringLiteral("AC06")));

        KeymapService service;
        Profile probe = makeProfile(QStringLiteral("probe"));
        const KccgstComponents components =
            service.resolveComponents(probe, QStringLiteral("us"), {}, {}, {});
        QVERIFY(components.isValid());

        const QHash<QString, QStringList> base = service.baseSymbols(components);
        QVERIFY2(base.contains(QStringLiteral("AC06")),
                 "the base keymap did not report the home-row key");
        qInfo() << "AC06 level-1 symbol:" << base.value(QStringLiteral("AC06"));

        catalog.applyLayoutSymbols(base);

        // On a US layout the home-row key at position 06 types "h", so that is
        // what it should be called.
        QCOMPARE(catalog.labelForCode(QStringLiteral("AC06")), QStringLiteral("H"));
        // Named keys keep their better label rather than becoming "Caps_Lock".
        QCOMPARE(catalog.labelForCode(QStringLiteral("CAPS")), QStringLiteral("Caps Lock"));
    }

    void xkbOptionCatalog_knowsWhichKeysAnOptionClaims()
    {
        const XkbOptionCatalog &catalog = XkbOptionCatalog::instance();
        QVERIFY(catalog.options().size() > 50);
        QVERIFY(catalog.contains(QStringLiteral("ctrl:nocaps")));
        QVERIFY(catalog.contains(QStringLiteral("compose:caps")));

        const XkbOption *nocaps = catalog.find(QStringLiteral("ctrl:nocaps"));
        QVERIFY(nocaps);
        QVERIFY(nocaps->claimedKeys.contains(QStringLiteral("CAPS")));

        const XkbOption *composeCaps = catalog.find(QStringLiteral("compose:caps"));
        QVERIFY(composeCaps);
        QVERIFY(composeCaps->claimedKeys.contains(QStringLiteral("CAPS")));
    }

    // --- keymap generation ------------------------------------------------

    void keymap_compilesASimpleRemap()
    {
        if (!KeymapService::isAvailable())
            QSKIP("xkbcli is not installed");

        KeymapService service;
        Profile profile = makeProfile(QStringLiteral("p1"));
        profile.remaps.append(modifierRemap(QStringLiteral("m1"), QStringLiteral("CAPS"),
                                            QStringLiteral("Control_L")));

        const KccgstComponents components =
            service.resolveComponents(profile, QStringLiteral("us"), {}, {}, {});
        QVERIFY(components.isValid());
        QVERIFY(components.symbols.contains(QStringLiteral("us")));

        const KeymapBuild build =
            service.build(profile, components, service.baseSymbols(components));
        QVERIFY2(build.ok, qPrintable(build.error));

        QVERIFY(build.source.contains(QStringLiteral("replace key <CAPS>")));
        QVERIFY(build.source.contains(QStringLiteral("Control_L")));
        // Without this line the key emits the symbol but never acts as a
        // modifier, which is the whole point of the mapping.
        QVERIFY(build.source.contains(QStringLiteral("modifier_map Control { <CAPS> }")));
    }

    void keymap_compilesAnFnStyleLayer()
    {
        if (!KeymapService::isAvailable())
            QSKIP("xkbcli is not installed");

        KeymapService service;
        Profile profile = makeProfile(QStringLiteral("p1"));

        Remap chooser;
        chooser.id = QStringLiteral("chooser");
        chooser.kind = MappingKind::LayerChooser;
        chooser.sourceKey = QStringLiteral("CAPS");
        chooser.levels = { QStringLiteral("ISO_Level3_Shift") };
        profile.remaps.append(chooser);

        Remap brightness;
        brightness.id = QStringLiteral("fk01");
        brightness.kind = MappingKind::LayerKey;
        brightness.sourceKey = QStringLiteral("FK01");
        brightness.levels = { QString(), QString(), QStringLiteral("XF86MonBrightnessDown"),
                              QStringLiteral("XF86MonBrightnessDown") };
        profile.remaps.append(brightness);

        const KccgstComponents components =
            service.resolveComponents(profile, QStringLiteral("us"), {}, {}, {});
        const QHash<QString, QStringList> base = service.baseSymbols(components);
        // The base keymap has to be readable, or a layer mapping would wipe
        // out what the key normally types.
        QVERIFY(base.contains(QStringLiteral("FK01")));

        const KeymapBuild build = service.build(profile, components, base);
        QVERIFY2(build.ok, qPrintable(build.error));

        QVERIFY(build.source.contains(QStringLiteral("ISO_Level3_Shift")));
        QVERIFY(build.source.contains(QStringLiteral("XF86MonBrightnessDown")));
        // A virtual modifier cannot appear in a modifier_map; the compat
        // section binds it instead.
        QVERIFY(!build.source.contains(QStringLiteral("modifier_map LevelThree")));
        // Level 1 keeps F1, so the key still works normally.
        QVERIFY(build.source.contains(QStringLiteral("F1")));
    }

    void keysym_validationIsExactNotSyntactic()
    {
        // libxkbcommon is the authority, because the compiler is not: running
        // `xkbcli compile-keymap --test` over a keymap containing
        // "NotAKeysymAtAll" exits 0. Validation therefore cannot lean on a
        // successful compile.
        QVERIFY(RuleSetValidator::isValidKeysym(QStringLiteral("Control_L")));
        QVERIFY(RuleSetValidator::isValidKeysym(QStringLiteral("XF86MonBrightnessDown")));
        QVERIFY(RuleSetValidator::isValidKeysym(QStringLiteral("ISO_Level3_Shift")));
        QVERIFY(!RuleSetValidator::isValidKeysym(QStringLiteral("NotAKeysymAtAll")));
        QVERIFY(!RuleSetValidator::isValidKeysym(QStringLiteral("Contrl_L")));

        // Disabling a key is a real target even though the library reports it
        // as "no symbol".
        QVERIFY(RuleSetValidator::isValidKeysym(QStringLiteral("NoSymbol")));
        QVERIFY(RuleSetValidator::isValidKeysym(QStringLiteral("VoidSymbol")));
    }

    void keymap_refusesAnInvalidKeysym()
    {
        if (!KeymapService::isAvailable())
            QSKIP("xkbcli is not installed");

        Profile profile = makeProfile(QStringLiteral("p1"));
        profile.remaps.append(keyRemap(QStringLiteral("m1"), QStringLiteral("CAPS"),
                                       QStringLiteral("NotAKeysymAtAll")));

        RuleSet ruleSet;
        ruleSet.profiles << profile;

        // First line of defence: the rule set does not validate, so an apply
        // never gets as far as generating anything.
        QVector<Conflict> issues;
        QVERIFY(!RuleSetValidator::validate(ruleSet, &issues));
        bool reported = false;
        for (const Conflict &issue : issues) {
            if (issue.severity == ConflictSeverity::Error
                && issue.detail.contains(QStringLiteral("NotAKeysymAtAll"))) {
                reported = true;
            }
        }
        QVERIFY2(reported, "the invalid keysym was not reported as an error");

        // Second line: even asked directly, the generator drops it rather than
        // writing it into a keymap.
        KeymapService service;
        const KccgstComponents components =
            service.resolveComponents(profile, QStringLiteral("us"), {}, {}, {});
        const KeymapBuild build =
            service.build(profile, components, service.baseSymbols(components));
        QVERIFY(!build.source.contains(QStringLiteral("NotAKeysymAtAll")));
        QVERIFY(!build.notes.isEmpty());
    }

    // --- writing ----------------------------------------------------------

    void writer_escapesEveryStringItEmits()
    {
        QCOMPARE(OmarchyConfigWriter::luaQuote(QStringLiteral("plain")),
                 QStringLiteral("\"plain\""));
        // A quote, a backslash or a newline must never end the literal early.
        QCOMPARE(OmarchyConfigWriter::luaQuote(QStringLiteral("say \"hi\"")),
                 QStringLiteral("\"say \\\"hi\\\"\""));
        QCOMPARE(OmarchyConfigWriter::luaQuote(QStringLiteral("a\nb")),
                 QStringLiteral("\"a\\nb\""));
        QCOMPARE(OmarchyConfigWriter::luaQuote(QStringLiteral("back\\slash")),
                 QStringLiteral("\"back\\\\slash\""));
    }

    void writer_generatesValidLua()
    {
        if (!KeymapService::isAvailable())
            QSKIP("xkbcli is not installed");

        RuleSet ruleSet;
        Profile profile = makeProfile(QStringLiteral("all-keyboards"));
        profile.remaps.append(modifierRemap(QStringLiteral("m1"), QStringLiteral("CAPS"),
                                            QStringLiteral("Control_L")));
        ruleSet.profiles << profile;
        ruleSet.shortcuts << shortcut(QStringLiteral("s1"), QStringLiteral("SUPER + E"),
                                      QStringLiteral("nautilus"));

        OmarchyConfigWriter writer;
        const GeneratedConfig config =
            writer.generate(ruleSet, QStringLiteral("us"), {}, {}, {});
        QVERIFY2(config.ok, qPrintable(config.error));

        // The data file must be a table, with no calls in it at all.
        QVERIFY(config.generatedLua.contains(QStringLiteral("return {")));
        QVERIFY(config.generatedLua.contains(QStringLiteral("schema = 1")));
        QVERIFY(!config.generatedLua.contains(QStringLiteral("o.bind")));
        QVERIFY(!config.generatedLua.contains(QStringLiteral("hl.")));

        // The loader is where the calls live, and it carries no user data.
        QVERIFY(config.loaderLua.contains(QStringLiteral("o.bind")));
        QVERIFY(config.loaderLua.contains(QStringLiteral("hl.device")));
        QVERIFY(!config.loaderLua.contains(QStringLiteral("nautilus")));
        QVERIFY(!config.loaderLua.contains(QStringLiteral("CAPS")));

        QString error;
        QVERIFY2(writer.commit(config, ruleSet, &error), qPrintable(error));

        // Both generated files must be syntactically valid Lua.
        LuaScanner scanner;
        if (LuaScanner::isAvailable()) {
            QVERIFY2(scanner.checkSyntax(Paths::generatedLuaFile(), &error), qPrintable(error));
            QVERIFY2(scanner.checkSyntax(Paths::loaderLuaFile(), &error), qPrintable(error));
        }
    }

    void writer_managesOnlyItsOwnBlockInHyprlandLua()
    {
        bool read = false;
        const QString before = QString::fromUtf8(Paths::read(Paths::hyprlandLuaFile(), &read));
        QVERIFY(read);
        QVERIFY(!OmarchyConfigWriter::hasManagedBlock(before));

        const QString withBlock = OmarchyConfigWriter::withManagedBlock(before);
        QVERIFY(OmarchyConfigWriter::hasManagedBlock(withBlock));
        QVERIFY(withBlock.contains(QStringLiteral("require(\"hypr.omakeyboard\")")));
        // Every original line has to still be there.
        for (const QString &line : before.split(QLatin1Char('\n'))) {
            if (!line.trimmed().isEmpty())
                QVERIFY2(withBlock.contains(line), qPrintable(line));
        }

        // Adding the block twice must not produce two of them.
        const QString twice = OmarchyConfigWriter::withManagedBlock(withBlock);
        QCOMPARE(twice.count(OmarchyConfigWriter::blockBeginMarker()), 1);
        QCOMPARE(twice, withBlock);

        // And removing it must give back exactly what we started with.
        const QString stripped = OmarchyConfigWriter::withoutManagedBlock(twice);
        QVERIFY(!OmarchyConfigWriter::hasManagedBlock(stripped));
        QCOMPARE(stripped.trimmed(), before.trimmed());
    }

    // --- keyboard behaviour (Advanced) ------------------------------------

    void behaviour_roundTripsAndInheritsByDefault()
    {
        RuleSet original;
        Profile global = makeProfile(QStringLiteral("all-keyboards"));
        global.behaviour.repeatRate = 55;
        global.behaviour.numlockOnStart = Tristate::Off;

        Profile apple = makeProfile(QStringLiteral("apple"), QStringLiteral("kbd-apple"));
        apple.behaviour.repeatDelay = 180;
        apple.behaviour.shortcutsEnabled = Tristate::Off;
        apple.behaviour.resolveBindsBySym = Tristate::On;

        original.profiles << global << apple;

        const QByteArray json = RuleSetSerializer::toJsonBytes(original);
        // An untouched setting must not appear at all: absence is what makes a
        // profile keep following Omarchy instead of pinning today's value.
        QVERIFY(!json.contains("repeatDelay\": -1"));
        QVERIFY(!json.contains("inherit"));

        QStringList warnings;
        const RuleSet parsed = RuleSetSerializer::fromJsonBytes(json, &warnings);
        QVERIFY(warnings.isEmpty());
        QCOMPARE(parsed.profiles.size(), 2);

        QCOMPARE(parsed.profiles.at(0).behaviour.repeatRate, 55);
        QCOMPARE(parsed.profiles.at(0).behaviour.repeatDelay, -1);
        QCOMPARE(parsed.profiles.at(0).behaviour.numlockOnStart, Tristate::Off);
        QCOMPARE(parsed.profiles.at(0).behaviour.resolveBindsBySym, Tristate::Inherit);

        QCOMPARE(parsed.profiles.at(1).behaviour.repeatDelay, 180);
        QCOMPARE(parsed.profiles.at(1).behaviour.shortcutsEnabled, Tristate::Off);
        QCOMPARE(parsed.profiles.at(1).behaviour.resolveBindsBySym, Tristate::On);

        QCOMPARE(RuleSetSerializer::toJsonBytes(parsed), json);
    }

    void behaviour_rejectsOutOfRangeAndGlobalOnlySettings()
    {
        RuleSet ruleSet;
        Profile profile = makeProfile(QStringLiteral("p1"));
        profile.behaviour.repeatRate = 900;     // over 255
        profile.behaviour.repeatDelay = 10;     // under 100 ms
        ruleSet.profiles << profile;

        QVector<Conflict> issues;
        QVERIFY(!RuleSetValidator::validate(ruleSet, &issues));
        QCOMPARE(ConflictDetector::countBySeverity(issues, ConflictSeverity::Error), 2);

        // Turning shortcuts off is a per-device setting: Hyprland has no
        // input.keybinds, so asking for it globally has to be refused rather
        // than written and silently ignored.
        RuleSet globalOnly;
        Profile global = makeProfile(QStringLiteral("all-keyboards"));
        global.behaviour.shortcutsEnabled = Tristate::Off;
        globalOnly.profiles << global;

        QVector<Conflict> globalIssues;
        QVERIFY(!RuleSetValidator::validate(globalOnly, &globalIssues));

        // And a rules.json that carries it anyway is repaired on load.
        QStringList warnings;
        const QByteArray json = R"JSON({"schema":1,"profiles":[
          {"id":"all-keyboards","deviceName":"","behaviour":{"shortcutsEnabled":"off"}}]})JSON";
        const RuleSet parsed = RuleSetSerializer::fromJsonBytes(json, &warnings);
        QCOMPARE(parsed.profiles.at(0).behaviour.shortcutsEnabled, Tristate::Inherit);
        QVERIFY(!warnings.isEmpty());
    }

    void behaviour_reachesTheGeneratedConfig()
    {
        if (!KeymapService::isAvailable())
            QSKIP("xkbcli is not installed");

        RuleSet ruleSet;
        Profile global = makeProfile(QStringLiteral("all-keyboards"));
        global.behaviour.repeatRate = 55;
        global.behaviour.numlockOnStart = Tristate::Off;

        Profile apple = makeProfile(QStringLiteral("apple"), QStringLiteral("kbd-apple"));
        apple.behaviour.repeatDelay = 180;
        apple.behaviour.shortcutsEnabled = Tristate::Off;

        ruleSet.profiles << global << apple;

        OmarchyConfigWriter writer;
        const GeneratedConfig config =
            writer.generate(ruleSet, QStringLiteral("us"), {}, {}, {});
        QVERIFY2(config.ok, qPrintable(config.error));

        // A profile with only behaviour and no key rules still has to be
        // emitted, or the setting would be silently dropped.
        QCOMPARE(config.keymaps.size(), 2);

        QVERIFY(config.generatedLua.contains(QStringLiteral("repeat_rate = 55")));
        QVERIFY(config.generatedLua.contains(QStringLiteral("numlock_by_default = false")));
        QVERIFY(config.generatedLua.contains(QStringLiteral("repeat_delay = 180")));
        QVERIFY(config.generatedLua.contains(QStringLiteral("keybinds = false")));
        // Untouched settings must be absent, not written as a default.
        QVERIFY(!config.generatedLua.contains(QStringLiteral("resolve_binds_by_sym")));

        // The loader must know how to apply all of them.
        QVERIFY(config.loaderLua.contains(QStringLiteral("repeat_rate")));
        QVERIFY(config.loaderLua.contains(QStringLiteral("numlock_by_default")));
        QVERIFY(config.loaderLua.contains(QStringLiteral("keybinds")));

        QString error;
        QVERIFY2(writer.commit(config, ruleSet, &error), qPrintable(error));
        LuaScanner scanner;
        if (LuaScanner::isAvailable()) {
            QVERIFY2(scanner.checkSyntax(Paths::generatedLuaFile(), &error), qPrintable(error));
            QVERIFY2(scanner.checkSyntax(Paths::loaderLuaFile(), &error), qPrintable(error));
        }
    }

    // --- conflicts --------------------------------------------------------

    void conflicts_catchTheOmarchyComposeCapsClash()
    {
        // The real case on a stock install: Omarchy's defaults put Compose on
        // Caps Lock, so mapping Caps Lock to Control takes it away.
        RuleSet ruleSet;
        Profile profile = makeProfile(QStringLiteral("p1"));
        profile.remaps.append(modifierRemap(QStringLiteral("m1"), QStringLiteral("CAPS"),
                                            QStringLiteral("Control_L")));
        ruleSet.profiles << profile;

        ConflictDetector detector;
        ConflictDetector::Inputs inputs;
        inputs.ruleSet = ruleSet;
        inputs.inheritedXkbOptions = { QStringLiteral("compose:caps"),
                                       QStringLiteral("shift:both_capslock_cancel") };

        const QVector<Conflict> conflicts = detector.detect(inputs);

        bool found = false;
        for (const Conflict &conflict : conflicts) {
            if (conflict.conflictsWith == QStringLiteral("compose:caps")) {
                found = true;
                QCOMPARE(conflict.severity, ConflictSeverity::Warning);
                QVERIFY(conflict.detail.contains(QStringLiteral("Caps Lock")));
                QVERIFY(!conflict.suggestion.isEmpty());
            }
        }
        QVERIFY2(found, "the compose:caps clash was not reported");

        // A warning must not block an apply: the user's rule does win.
        QVERIFY(!ConflictDetector::hasBlockingConflict(conflicts));
    }

    void conflicts_catchTwoMappingsForOneKey()
    {
        RuleSet ruleSet;
        Profile profile = makeProfile(QStringLiteral("p1"));
        profile.remaps.append(keyRemap(QStringLiteral("m1"), QStringLiteral("CAPS"),
                                       QStringLiteral("Escape")));
        profile.remaps.append(keyRemap(QStringLiteral("m2"), QStringLiteral("CAPS"),
                                       QStringLiteral("Control_L")));
        ruleSet.profiles << profile;

        ConflictDetector detector;
        ConflictDetector::Inputs inputs;
        inputs.ruleSet = ruleSet;

        const QVector<Conflict> conflicts = detector.detect(inputs);
        QVERIFY(ConflictDetector::hasBlockingConflict(conflicts));
    }

    void conflicts_catchTwoProfilesForOneKeyboard()
    {
        RuleSet ruleSet;
        ruleSet.profiles << makeProfile(QStringLiteral("p1"), QStringLiteral("kbd-one"))
                         << makeProfile(QStringLiteral("p2"), QStringLiteral("kbd-one"));

        ConflictDetector detector;
        ConflictDetector::Inputs inputs;
        inputs.ruleSet = ruleSet;

        QVERIFY(ConflictDetector::hasBlockingConflict(detector.detect(inputs)));
    }

    void conflicts_treatShortcutReplacementAsInformation()
    {
        RuleSet ruleSet;
        ruleSet.profiles << makeProfile(QStringLiteral("p1"));
        Shortcut entry = shortcut(QStringLiteral("s1"), QStringLiteral("SUPER + E"),
                                  QStringLiteral("nautilus"));
        entry.unbindFirst = true;
        ruleSet.shortcuts << entry;

        ExistingBinding existing;
        existing.keys = QStringLiteral("SUPER + E");
        existing.normalizedKeys = OmarchyConfigParser::normalizeKeys(existing.keys);
        existing.description = QStringLiteral("Email");

        ConflictDetector detector;
        ConflictDetector::Inputs inputs;
        inputs.ruleSet = ruleSet;
        inputs.existingBindings = { existing };

        QVector<Conflict> conflicts = detector.detect(inputs);
        QVERIFY(!ConflictDetector::hasBlockingConflict(conflicts));
        QCOMPARE(ConflictDetector::countBySeverity(conflicts, ConflictSeverity::Info), 1);

        // With replacement turned off, the same clash becomes an error.
        ruleSet.shortcuts[0].unbindFirst = false;
        inputs.ruleSet = ruleSet;
        conflicts = detector.detect(inputs);
        QVERIFY(ConflictDetector::hasBlockingConflict(conflicts));
    }

    void parser_normalizesKeysForComparison()
    {
        // Spelling, order and case must not hide a clash.
        QCOMPARE(OmarchyConfigParser::normalizeKeys(QStringLiteral("SUPER + SHIFT + E")),
                 OmarchyConfigParser::normalizeKeys(QStringLiteral("shift+super+e")));
        QCOMPARE(OmarchyConfigParser::normalizeKeys(QStringLiteral("CTRL + A")),
                 OmarchyConfigParser::normalizeKeys(QStringLiteral("CONTROL + A")));
        QVERIFY(OmarchyConfigParser::normalizeKeys(QStringLiteral("SUPER + E"))
                != OmarchyConfigParser::normalizeKeys(QStringLiteral("SUPER + SHIFT + E")));
    }

    // --- backups and rollback ---------------------------------------------

    void backups_captureAndRestoreEveryTrackedFile()
    {
        ConfigBackupService backups;

        QString error;
        QVERIFY(Paths::writeAtomically(Paths::rulesFile(), QByteArrayLiteral("{\"v\":1}"), &error));
        QVERIFY(Paths::writeAtomically(Paths::keymapFile(QStringLiteral("p1")),
                                       QByteArrayLiteral("original keymap"), &error));

        const QString id = backups.createBackup(QStringLiteral("test"), &error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));

        const QVector<BackupEntry> list = backups.backups();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).id, id);
        QCOMPARE(list.at(0).label, QStringLiteral("test"));
        QVERIFY(list.at(0).files.contains(QStringLiteral("rules.json")));
        QVERIFY(list.at(0).files.contains(QStringLiteral("p1.xkb")));

        // Change everything, then roll back.
        QVERIFY(Paths::writeAtomically(Paths::rulesFile(), QByteArrayLiteral("{\"v\":2}"), &error));
        QVERIFY(Paths::writeAtomically(Paths::keymapFile(QStringLiteral("p1")),
                                       QByteArrayLiteral("broken keymap"), &error));

        QVERIFY2(backups.restore(id, &error), qPrintable(error));

        bool ok = false;
        QCOMPARE(Paths::read(Paths::rulesFile(), &ok), QByteArrayLiteral("{\"v\":1}"));
        QCOMPARE(Paths::read(Paths::keymapFile(QStringLiteral("p1")), &ok),
                 QByteArrayLiteral("original keymap"));
    }

    void backups_rollbackRemovesFilesTheApplyCreated()
    {
        ConfigBackupService backups;
        QString error;

        // Snapshot a state with no generated files at all.
        const QString id = backups.createBackup(QStringLiteral("empty"), &error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));

        // An apply then creates them.
        QVERIFY(Paths::writeAtomically(Paths::generatedLuaFile(),
                                       QByteArrayLiteral("return {}"), &error));
        QVERIFY(Paths::writeAtomically(Paths::loaderLuaFile(),
                                       QByteArrayLiteral("-- loader"), &error));
        QVERIFY(QFile::exists(Paths::generatedLuaFile()));

        QVERIFY2(backups.restore(id, &error), qPrintable(error));

        // Rolling back has to leave no trace of the failed apply.
        QVERIFY(!QFile::exists(Paths::generatedLuaFile()));
        QVERIFY(!QFile::exists(Paths::loaderLuaFile()));
    }

    void backups_pruneKeepsTheNewest()
    {
        ConfigBackupService backups;
        QString error;
        QVERIFY(Paths::writeAtomically(Paths::rulesFile(), QByteArrayLiteral("{}"), &error));

        // Ids are unix timestamps, so they are written directly to get
        // distinct snapshots without waiting a second between them.
        for (const QString &id : { QStringLiteral("1000"), QStringLiteral("2000"),
                                   QStringLiteral("3000") }) {
            const QString directory = Paths::backupDir() + QLatin1Char('/') + id;
            QVERIFY(Paths::ensureDir(directory));
            QVERIFY(Paths::writeAtomically(directory + QStringLiteral("/manifest.json"),
                                           QStringLiteral("{\"id\":\"%1\",\"files\":[]}")
                                               .arg(id).toUtf8(), &error));
        }

        QCOMPARE(backups.backups().size(), 3);
        QCOMPARE(backups.prune(1), 2);

        const QVector<BackupEntry> remaining = backups.backups();
        QCOMPARE(remaining.size(), 1);
        QCOMPARE(remaining.at(0).id, QStringLiteral("3000"));
    }

    // --- keyboard selection (no "add a keyboard" step) --------------------

    void devices_bluetoothRadioHidProxyIsNotAKeyboard()
    {
        // The real paths on this machine. The Bluetooth adapter and the HID
        // endpoint that answers "I am a keyboard" sit on the same USB device
        // (1-3); the built-in keyboard is a different device (1-5).
        const QStringList bluetooth{
            QStringLiteral("/sys/devices/pci0000:00/0000:00:14.0/usb1/1-3"),
        };

        const QString proxy = QStringLiteral(
            "/sys/devices/pci0000:00/0000:00:14.0/usb1/1-3/1-3:1.0/"
            "0003:05AC:8290.0001/input/input4/event4");
        QVERIFY(DeviceService::isBluetoothHidProxy(proxy, bluetooth));

        const QString builtIn = QStringLiteral(
            "/sys/devices/pci0000:00/0000:00:14.0/usb1/1-5/1-5:1.1/"
            "0003:05AC:0273.0004/input/input6/event6");
        QVERIFY(!DeviceService::isBluetoothHidProxy(builtIn, bluetooth));

        // A paired Bluetooth keyboard arrives through bluez as its own uhid
        // device, with no USB parent at all, and must still be offered.
        const QString paired = QStringLiteral(
            "/sys/devices/virtual/misc/uhid/0005:046D:B35B.0007/input/input20/event20");
        QVERIFY(!DeviceService::isBluetoothHidProxy(paired, bluetooth));

        // A 2.4 GHz receiver is not a Bluetooth adapter, so its keyboard
        // endpoint is a real keyboard.
        const QString receiver = QStringLiteral(
            "/sys/devices/pci0000:00/0000:00:14.0/usb1/1-7/1-7:1.0/"
            "0003:046D:C52B.0003/input/input9/event9");
        QVERIFY(!DeviceService::isBluetoothHidProxy(receiver, bluetooth));

        // A near-miss path must not match: 1-30 is not inside 1-3.
        const QString sibling = QStringLiteral(
            "/sys/devices/pci0000:00/0000:00:14.0/usb1/1-30/1-30:1.0/input/input9/event9");
        QVERIFY(!DeviceService::isBluetoothHidProxy(sibling, bluetooth));

        // No adapters, nothing to deny.
        QVERIFY(!DeviceService::isBluetoothHidProxy(proxy, {}));
        QVERIFY(!DeviceService::isBluetoothHidProxy(QString(), bluetooth));
    }

    void devices_verdictDecidesWhatIsOffered()
    {
        // Confirmed keyboards are offered, denied ones are not, and an unknown
        // verdict falls back to the name: hiding a real keyboard is worse than
        // showing a power button, so unknown fails open.
        KeyboardDevice confirmed;
        confirmed.verdict = KeyboardVerdict::Confirmed;
        QVERIFY(confirmed.isConfigurable());

        // Even a device whose name looks synthetic is offered once udev
        // confirms it -- a keyboard called "Power Pro" must not disappear.
        KeyboardDevice oddName;
        oddName.name = QStringLiteral("power-button-pro-keyboard");
        oddName.isVirtual = true;
        oddName.verdict = KeyboardVerdict::Confirmed;
        QVERIFY(oddName.isConfigurable());

        KeyboardDevice denied;
        denied.verdict = KeyboardVerdict::Denied;
        QVERIFY(!denied.isConfigurable());

        // A denied verdict wins over a plausible-looking name, too.
        KeyboardDevice deniedRealName;
        deniedRealName.name = QStringLiteral("some-usb-keyboard");
        deniedRealName.verdict = KeyboardVerdict::Denied;
        QVERIFY(!deniedRealName.isConfigurable());

        KeyboardDevice unknownReal;
        unknownReal.name = QStringLiteral("keychron-k2");
        QCOMPARE(unknownReal.verdict, KeyboardVerdict::Unknown);
        QVERIFY(unknownReal.isConfigurable());

        KeyboardDevice unknownVirtual;
        unknownVirtual.name = QStringLiteral("hl-virtual-keyboard-fcitx5");
        unknownVirtual.isVirtual = true;
        QVERIFY(!unknownVirtual.isConfigurable());
    }

    void devices_prettifyDropsLegalFormsAndStutter()
    {
        // The real name on this machine, and the stutter it used to produce:
        // "Apple apple internal keyboard / trackpad".
        QCOMPARE(DeviceService::prettify(
                     QStringLiteral("apple-inc.-apple-internal-keyboard-/-trackpad")),
                 QStringLiteral("Apple internal keyboard / trackpad"));
        QCOMPARE(DeviceService::prettify(
                     QStringLiteral("broadcom-corp.-bluetooth-usb-host-controller")),
                 QStringLiteral("Broadcom bluetooth usb host controller"));
        // A name with nothing to clean is left alone apart from the casing.
        QCOMPARE(DeviceService::prettify(QStringLiteral("keychron-k2")),
                 QStringLiteral("Keychron k2"));
        // Never return an empty label, whatever comes in.
        QVERIFY(!DeviceService::prettify(QStringLiteral("inc.")).isEmpty());
    }

    void keyboards_selectingOneIsNotAnUnsavedChange()
    {
        ConfigRepository repository;
        QVERIFY(!repository.isDirty());

        // Picking a keyboard to look at must not offer to save anything: an
        // empty profile emits nothing, so there is nothing to apply.
        const QString id = repository.ensureProfileForDevice(
            QStringLiteral("test-keyboard-two"), QStringLiteral("Two"));
        QVERIFY(!id.isEmpty());
        QVERIFY2(!repository.isDirty(), "selecting a keyboard marked the config dirty");

        // It also must not reach rules.json, or the file would collect a
        // profile for every keyboard ever clicked.
        bool present = false;
        for (const Profile &profile : repository.ruleSetForSaving().profiles) {
            if (profile.deviceName == QStringLiteral("test-keyboard-two"))
                present = true;
        }
        QVERIFY2(!present, "an empty profile would have been written to disk");

        // The first real mapping is what makes it a change, and what makes the
        // profile worth saving.
        repository.addMapping(id, QStringLiteral("keyRemap"), QStringLiteral("CAPS"),
                              QVariantList{ QStringLiteral("Escape") }, QString(), QString());
        QVERIFY(repository.isDirty());

        present = false;
        for (const Profile &profile : repository.ruleSetForSaving().profiles) {
            if (profile.deviceName == QStringLiteral("test-keyboard-two"))
                present = true;
        }
        QVERIFY(present);
    }

    void backups_listAnnouncesItselfWhenAnApplyEnds()
    {
        // The Settings page binds to `backups`. If finishing an apply did not
        // announce a change, a snapshot taken on disk would stay invisible
        // until the app restarted -- which is exactly what happened.
        ConfigRepository repository;
        QSignalSpy changed(&repository, &ConfigRepository::backupsChanged);

        emit repository.applyFinished(true, QStringLiteral("Applied."));
        QCOMPARE(changed.count(), 1);

        // A rolled-back apply still leaves its snapshot behind.
        emit repository.applyFinished(false, QStringLiteral("Rolled back."));
        QCOMPARE(changed.count(), 2);

        // Restores reload the state, and the list follows.
        emit repository.stateReloaded();
        QCOMPARE(changed.count(), 3);

        // And the property reads what is on disk right now.
        ConfigBackupService backups;
        QString error;
        QVERIFY(Paths::writeAtomically(Paths::rulesFile(), QByteArrayLiteral("{}"), &error));
        const int before = repository.backupList().size();
        QVERIFY(!backups.createBackup(QStringLiteral("test"), &error).isEmpty());
        QCOMPARE(repository.property("backups").toList().size(), before + 1);
    }

    void keyboards_neverClaimAKeyboardIsInUseWithoutEvidence()
    {
        // Hyprland marks its own virtual keyboard as "main" when an input
        // method is running, so no real device carries the flag. Claiming one
        // anyway once labelled a Bluetooth host controller as the keyboard in
        // use, on a laptop whose built-in keyboard was right there.
        ConfigRepository repository;

        int claimed = 0;
        for (const QVariant &value : repository.keyboardList()) {
            const QVariantMap entry = value.toMap();
            if (!entry.value(QStringLiteral("isMain")).toBool())
                continue;
            ++claimed;
            // Whatever is claimed must be a device Hyprland itself flagged.
            const QString name = entry.value(QStringLiteral("deviceName")).toString();
            const KeyboardDevice *device = repository.devices()->find(name);
            QVERIFY(device);
            QVERIFY(device->isMain);
        }
        // At most one keyboard can be the one in use.
        QVERIFY(claimed <= 1);
    }

    void keyboards_alwaysOfferTheAllKeyboardsEntryFirst()
    {
        ConfigRepository repository;
        const QVariantList keyboards = repository.keyboardList();

        // Even with nothing configured and no compositor to ask, there is
        // always somewhere to put a rule.
        QVERIFY(!keyboards.isEmpty());
        const QVariantMap first = keyboards.constFirst().toMap();
        QVERIFY(first.value(QStringLiteral("isGlobal")).toBool());
        QVERIFY(!first.value(QStringLiteral("profileId")).toString().isEmpty());
        QVERIFY(first.value(QStringLiteral("connected")).toBool());
        QCOMPARE(first.value(QStringLiteral("deviceName")).toString(), QString());
    }

    void keyboards_selectingOneCreatesItsProfileOnce()
    {
        ConfigRepository repository;

        const QString device = QStringLiteral("test-keyboard-one");
        const int before = repository.ruleSet().profiles.size();

        // Selecting a keyboard is what creates its rules; there is no separate
        // "add" action to get wrong.
        const QString first = repository.ensureProfileForDevice(device, QStringLiteral("One"));
        QVERIFY(!first.isEmpty());
        QCOMPARE(repository.ruleSet().profiles.size(), before + 1);

        // Selecting it again must reuse the same profile rather than pile up
        // duplicates -- two profiles for one keyboard is a blocking conflict.
        const QString second = repository.ensureProfileForDevice(device, QStringLiteral("One"));
        QCOMPARE(second, first);
        QCOMPARE(repository.ruleSet().profiles.size(), before + 1);

        // An empty device name means the all-keyboards profile.
        const QString global = repository.ensureProfileForDevice(QString(), QString());
        QVERIFY(!global.isEmpty());
        QVERIFY(global != first);

        // A keyboard with rules but no connection still has to be listed, or
        // the interface would hide configuration that is still in force.
        repository.addMapping(first, QStringLiteral("keyRemap"), QStringLiteral("CAPS"),
                              QVariantList{ QStringLiteral("Escape") }, QString(), QString());
        bool listed = false;
        for (const QVariant &value : repository.keyboardList()) {
            const QVariantMap entry = value.toMap();
            if (entry.value(QStringLiteral("deviceName")).toString() == device) {
                listed = true;
                QCOMPARE(entry.value(QStringLiteral("mappingCount")).toInt(), 1);
                QVERIFY(!entry.value(QStringLiteral("connected")).toBool());
            }
        }
        QVERIFY2(listed, "a configured but unplugged keyboard was not listed");
    }

    // --- key capture ------------------------------------------------------

    void capture_alwaysReportsWhatHappenedToAKey()
    {
        HyprlandReloadService reload;
        KeyCaptureService capture(&reload);

        QSignalSpy captured(&capture, &KeyCaptureService::keyCaptured);
        QSignalSpy failed(&capture, &KeyCaptureService::captureFailed);

        capture.start(QStringLiteral("key"));
        QVERIFY(capture.isCapturing());

        // XKB keycode 66 is Caps Lock. A resolvable key must report itself:
        // the interface has nothing else to go on, and a silent capture looks
        // exactly like a broken one -- "listening" forever with nothing
        // registering.
        QVERIFY(capture.handleKey(66, Qt::Key_CapsLock, Qt::NoModifier));
        QCOMPARE(captured.count(), 1);
        QCOMPARE(captured.first().at(0).toString(), QStringLiteral("CAPS"));
        QCOMPARE(failed.count(), 0);
        QVERIFY(!capture.isCapturing());

        // And a code that resolves to nothing must say so rather than go quiet.
        capture.start(QStringLiteral("key"));
        QVERIFY(!capture.handleKey(99999, 0, Qt::NoModifier));
        QCOMPARE(captured.count(), 1);
        QCOMPARE(failed.count(), 1);

        // A combination reports through the shortcut signal instead.
        QSignalSpy shortcut(&capture, &KeyCaptureService::shortcutCaptured);
        capture.start(QStringLiteral("shortcut"));
        QVERIFY(capture.handleKey(0, Qt::Key_E, Qt::MetaModifier | Qt::ShiftModifier));
        QCOMPARE(shortcut.count(), 1);
        QCOMPARE(shortcut.first().at(0).toString(), QStringLiteral("SUPER + SHIFT + E"));
    }

    // --- the Lua sandbox --------------------------------------------------

    void luaScanner_readsBindingsFromAConfig()
    {
        if (!LuaScanner::isAvailable())
            QSKIP("lua is not installed");

        // A config in Omarchy's own idiom, but self-contained so the test does
        // not depend on the installed Omarchy.
        const QString path = m_root.path() + QStringLiteral("/standalone.lua");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(R"LUA(
o = o or {}
function o.bind(keys, description, dispatcher, options)
  local opts = options or {}
  if description then opts.description = description end
  if type(dispatcher) == "table" and dispatcher.omarchy then
    dispatcher = "omarchy-launch-" .. dispatcher.omarchy
  end
  if type(dispatcher) == "string" then dispatcher = hl.dsp.exec_cmd(dispatcher) end
  hl.bind(keys, dispatcher, opts)
end

o.bind("SUPER + RETURN", "Terminal", { omarchy = "terminal" })
o.bind("SUPER + K", "Keybindings", "omarchy-menu-keybindings")
hl.unbind("SUPER + SPACE")
o.bind("SUPER + CTRL + Z", "Zoom in", function() end)
)LUA");
        file.close();

        LuaScanner scanner;
        const ScanResult result = scanner.scanConfigFile(path);
        QVERIFY2(result.ok, qPrintable(result.error));

        QCOMPARE(result.binds.size(), 3);
        QCOMPARE(result.unbinds, QStringList{ QStringLiteral("SUPER + SPACE") });

        QCOMPARE(result.binds.at(0).keys, QStringLiteral("SUPER + RETURN"));
        QCOMPARE(result.binds.at(0).description, QStringLiteral("Terminal"));
        QCOMPARE(result.binds.at(0).kind, QStringLiteral("exec"));
        QCOMPARE(result.binds.at(0).argument, QStringLiteral("omarchy-launch-terminal"));

        // A function dispatcher has no command to show, but the binding is
        // still recorded so it can be found in a conflict check.
        QCOMPARE(result.binds.at(2).keys, QStringLiteral("SUPER + CTRL + Z"));
        QCOMPARE(result.binds.at(2).kind, QStringLiteral("lua"));
    }

    void luaScanner_reportsASyntaxError()
    {
        if (!LuaScanner::isAvailable())
            QSKIP("lua is not installed");

        const QString path = m_root.path() + QStringLiteral("/broken.lua");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("this is not ( valid lua\n");
        file.close();

        LuaScanner scanner;
        QString error;
        QVERIFY(!scanner.checkSyntax(path, &error));
        QVERIFY(!error.isEmpty());
    }

    // --- the engine's own account of itself -------------------------------

    void engine_reportsWhatItCanDo()
    {
        NativeEngine engine;
        const EngineCapabilities &caps = engine.capabilities();

        if (KeymapService::isAvailable()) {
            QVERIFY(caps.simpleRemap);
            QVERIFY(caps.modifierRemap);
            QVERIFY(caps.layers);
            QVERIFY(engine.supports(MappingKind::KeyRemap));
            QVERIFY(engine.supports(MappingKind::LayerChooser));
        }

        // The limits are explanations, not blanks: the UI shows them verbatim.
        QVERIFY(!caps.tapHoldLimit.isEmpty());
        QVERIFY(!caps.combosLimit.isEmpty());
        QVERIFY(!caps.macrosLimit.isEmpty());
        QVERIFY(!caps.perAppLimit.isEmpty());
    }
    // --- row diffs, which the list animations depend on ------------------

    void rows_updateInPlaceInsteadOfResetting()
    {
        struct Rows : RowListModel {
            QVariant data(const QModelIndex &, int) const override { return {}; }
            void set(const QVariantList &rows, bool reset = false) { replaceRows(rows, reset); }
        } model;

        const auto row = [](const char *id, int value) {
            return QVariant(QVariantMap{ { QStringLiteral("id"), QString::fromLatin1(id) },
                                         { QStringLiteral("v"), value } });
        };
        const auto ids = [&model]() {
            QStringList out;
            for (const QVariant &r : model.rows())
                out << r.toMap().value(QStringLiteral("id")).toString();
            return out.join(QLatin1Char(','));
        };

        model.set({ row("a", 1), row("b", 1), row("c", 1) }, true);

        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy moved(&model, &QAbstractItemModel::rowsMoved);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

        // One new row: one insertion, nothing else.
        model.set({ row("a", 1), row("b", 1), row("n", 1), row("c", 1) });
        QCOMPARE(ids(), QStringLiteral("a,b,n,c"));
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(resets.count(), 0);

        // One edited, one deleted, one moved.
        model.set({ row("c", 1), row("a", 2), row("n", 1) });
        QCOMPARE(ids(), QStringLiteral("c,a,n"));
        QCOMPARE(removed.count(), 1);
        QVERIFY(moved.count() >= 1);
        QVERIFY(changed.count() >= 1);
        QCOMPARE(model.rows().at(1).toMap().value(QStringLiteral("v")).toInt(), 2);
        QCOMPARE(resets.count(), 0);

        // Another subject entirely is a reset, on request.
        model.set({ row("x", 1) }, true);
        QCOMPARE(resets.count(), 1);
        QCOMPARE(model.rowCount(), 1);
    }

    // --- recipes ----------------------------------------------------------

    void recipes_eachOneCompilesAndCannotBeAddedTwice()
    {
        if (!KeymapService::isAvailable())
            QSKIP("xkbcli is not installed");

        ConfigRepository probe;
        const QVariantList recipes = probe.recipeList(QStringLiteral("all-keyboards"));
        QVERIFY(recipes.size() >= 4);

        for (const QVariant &value : recipes) {
            const QString id = value.toMap().value(QStringLiteral("id")).toString();

            ConfigRepository repository;
            const QString profile = repository.ensureProfileForDevice(
                QStringLiteral("recipe-keyboard"), QStringLiteral("Recipe"));
            QVERIFY(repository.addRecipe(profile, id));

            // The same checks Apply runs, without writing anything.
            const QVariantMap preview = repository.preview();
            QVERIFY2(preview.value(QStringLiteral("ok")).toBool(),
                     qPrintable(id + QStringLiteral(": ")
                                + preview.value(QStringLiteral("message")).toString()));

            // Adding it again would double up the same keys.
            QVERIFY2(!repository.addRecipe(profile, id), qPrintable(id));
        }
    }

    void recipes_twoLayerKeysAreNotOffered()
    {
        ConfigRepository repository;
        const QString profile = repository.ensureProfileForDevice(
            QStringLiteral("layer-keyboard"), QStringLiteral("Layer"));
        repository.addMapping(profile, QStringLiteral("layerChooser"), QStringLiteral("RALT"),
                              QVariantList{ QStringLiteral("ISO_Level3_Shift") }, QString(),
                              QString());
        for (const QVariant &value : repository.recipeList(profile)) {
            const QVariantMap recipe = value.toMap();
            if (recipe.value(QStringLiteral("id")).toString() == QStringLiteral("caps-arrows"))
                QVERIFY(!recipe.value(QStringLiteral("available")).toBool());
        }
    }

    // --- the apply pipeline, as the step-by-step view sees it ------------

    void apply_reportsTheStageThatStoppedIt()
    {
        // Validation fails before anything is written or reloaded, so this is
        // safe to run on a live desktop.
        ConfigRepository repository;
        const QString profile = repository.ensureProfileForDevice(
            QStringLiteral("stage-keyboard"), QStringLiteral("Stage"));
        repository.addMapping(profile, QStringLiteral("keyRemap"), QStringLiteral("CAPS"),
                              QVariantList{ QStringLiteral("NotARealKeysym") }, QString(),
                              QString());

        const QVariantMap result = repository.apply();
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());

        const QVariantList stages = result.value(QStringLiteral("stages")).toList();
        QCOMPARE(stages.size(), 7);
        QCOMPARE(stages.at(0).toMap().value(QStringLiteral("state")).toString(),
                 QStringLiteral("failed"));
        for (int i = 1; i < stages.size(); ++i)
            QCOMPARE(stages.at(i).toMap().value(QStringLiteral("state")).toString(),
                     QStringLiteral("pending"));

        // A preview is not a pipeline run and carries no stages.
        QVERIFY(repository.preview().value(QStringLiteral("stages")).toList().isEmpty());
    }

    void devices_transportFollowsTheKernelBus()
    {
        QCOMPARE(DeviceService::transportFor(0x05, QStringLiteral("Keychron K2")),
                 QStringLiteral("bluetooth"));
        QCOMPARE(DeviceService::transportFor(0x03, QStringLiteral("Keychron K2")),
                 QStringLiteral("usb"));
        QCOMPARE(DeviceService::transportFor(0x11, QStringLiteral("AT Translated Set 2 keyboard")),
                 QStringLiteral("internal"));
        // Apple puts the built-in keyboard on an internal USB bus.
        QCOMPARE(DeviceService::transportFor(
                     0x03, QStringLiteral("Apple Inc. Apple Internal Keyboard / Trackpad")),
                 QStringLiteral("internal"));
        QVERIFY(DeviceService::transportFor(0x00, QStringLiteral("x")).isEmpty());
    }
    // --- undoing a deletion -----------------------------------------------

    void undo_putsAMappingBackWhereItWas()
    {
        ConfigRepository repository;
        const QString profile = repository.ensureProfileForDevice(
            QStringLiteral("undo-keyboard"), QStringLiteral("Undo"));
        const auto add = [&](const char *key) {
            return repository.addMapping(profile, QStringLiteral("keyRemap"),
                                         QString::fromLatin1(key),
                                         QVariantList{ QStringLiteral("Escape") }, QString(),
                                         QString());
        };
        add("AC01");
        const QString middle = add("AC02");
        add("AC03");
        QVERIFY(repository.undoLabel().isEmpty());

        QVERIFY(repository.removeMapping(profile, middle));
        QVERIFY(!repository.undoLabel().isEmpty());
        QVERIFY(repository.undoRemoval());
        QVERIFY(repository.undoLabel().isEmpty());

        const QVariantList rows = repository.mappingList(profile);
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("id")).toString(), middle);

        // Nothing left to undo.
        QVERIFY(!repository.undoRemoval());
    }

    void undo_bringsBackAClearedKeyboard()
    {
        ConfigRepository repository;
        const QString profile = repository.ensureProfileForDevice(
            QStringLiteral("cleared-keyboard"), QStringLiteral("Cleared"));
        repository.addMapping(profile, QStringLiteral("keyRemap"), QStringLiteral("CAPS"),
                              QVariantList{ QStringLiteral("Escape") }, QString(), QString());

        QVERIFY(repository.removeProfile(profile));
        QVERIFY(repository.undoLabel().startsWith(QStringLiteral("Cleared")));
        QVERIFY(repository.undoRemoval());
        QCOMPARE(repository.activeProfileId(), profile);
        QCOMPARE(repository.mappingList(profile).size(), 1);
    }

    void undo_deletingAndUndoingIsNotAnUnsavedChange()
    {
        ConfigRepository repository;
        const QString id = repository.addShortcut(
            QStringLiteral("SUPER + CTRL + ALT + SHIFT + F11"), QStringLiteral("Test"),
            QStringLiteral("command"), QStringLiteral("true"), QString());
        QVERIFY(repository.isDirty());

        // Save it for real, into the test's own XDG directories.
        QString error;
        QVERIFY(Paths::writeAtomically(
            Paths::rulesFile(), RuleSetSerializer::toJsonBytes(repository.ruleSetForSaving()),
            &error));
        repository.reload();
        QVERIFY(!repository.isDirty());

        QVERIFY(repository.removeShortcut(id));
        QVERIFY(repository.isDirty());
        QVERIFY(repository.undoRemoval());
        QVERIFY2(!repository.isDirty(), "undo left a phantom unsaved change");

        // Leave the sandbox as the other tests expect it.
        QFile::remove(Paths::rulesFile());
        repository.reload();
    }

    // --- shortcut recipes ---------------------------------------------------

    void shortcutRecipes_neverTakeABoundCombination()
    {
        ConfigRepository repository;
        const QVariantList recipes = repository.shortcutRecipeList();
        QVERIFY(recipes.size() >= 4);

        QSet<QString> taken;
        for (const QVariant &value : repository.existingBindings())
            taken.insert(OmarchyConfigParser::normalizeKeys(
                value.toMap().value(QStringLiteral("keys")).toString()));

        for (const QVariant &value : recipes) {
            const QVariantMap recipe = value.toMap();
            if (!recipe.value(QStringLiteral("available")).toBool())
                continue;
            const QString keys = recipe.value(QStringLiteral("keys")).toString();
            QVERIFY2(!taken.contains(OmarchyConfigParser::normalizeKeys(keys)), qPrintable(keys));

            const QString id = recipe.value(QStringLiteral("id")).toString();
            QVERIFY(repository.addShortcutRecipe(id));
            // Once added it is not offered again.
            QVERIFY(!repository.addShortcutRecipe(id));
        }

        // Whatever was added is valid and clashes with nothing.
        QVector<Conflict> problems;
        QVERIFY(RuleSetValidator::validate(repository.ruleSetForSaving(), &problems));
        QCOMPARE(repository.errorCount(), 0);
    }
    // --- the keyboard map ---------------------------------------------------

    void keyboardMap_rowsAreWholeAndCarryTheirMappings()
    {
        ConfigRepository repository;
        const QString profile = repository.ensureProfileForDevice(
            QStringLiteral("map-keyboard"), QStringLiteral("Map"));
        repository.addRecipe(profile, QStringLiteral("caps-arrows"));
        repository.addMapping(profile, QStringLiteral("modifierRemap"), QStringLiteral("LCTL"),
                              QVariantList{ QStringLiteral("Super_L") }, QString(), QString());

        const QVariantMap map = repository.keyboardMap(profile);
        const QVariantList rows = map.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), 5);

        QHash<QString, QVariantMap> byCode;
        for (const QVariant &row : rows) {
            // Every row spans the same 15 units, or the drawing is ragged.
            double total = 0;
            for (const QVariant &value : row.toList()) {
                const QVariantMap key = value.toMap();
                total += key.value(QStringLiteral("width")).toDouble();
                byCode.insert(key.value(QStringLiteral("code")).toString(), key);
            }
            QCOMPARE(total, 15.0);
        }

        QCOMPARE(map.value(QStringLiteral("layerKey")).toString(), QStringLiteral("CAPS"));
        QCOMPARE(byCode.value(QStringLiteral("CAPS")).value(QStringLiteral("base")).toMap()
                     .value(QStringLiteral("kind")).toString(),
                 QStringLiteral("layerKey"));
        QCOMPARE(byCode.value(QStringLiteral("AC06")).value(QStringLiteral("layer")).toMap()
                     .value(QStringLiteral("label")).toString(),
                 QStringLiteral("←"));
        QCOMPARE(byCode.value(QStringLiteral("LCTL")).value(QStringLiteral("base")).toMap()
                     .value(QStringLiteral("label")).toString(),
                 QStringLiteral("Super"));
        // Each lit key knows which mapping it came from, so a click can open it.
        QVERIFY(!byCode.value(QStringLiteral("AC06")).value(QStringLiteral("layer")).toMap()
                     .value(QStringLiteral("id")).toString().isEmpty());
    }

    void keyboardMap_onlyALevelThreeKeyShowsTheLayer()
    {
        // Layer mappings are written to level 3. A level-5 chooser does not
        // reach them, so offering "while holding" it would show a lie.
        ConfigRepository repository;
        const QString profile = repository.ensureProfileForDevice(
            QStringLiteral("lv5-keyboard"), QStringLiteral("Lv5"));
        repository.addMapping(profile, QStringLiteral("layerChooser"), QStringLiteral("MENU"),
                              QVariantList{ QStringLiteral("ISO_Level5_Shift") }, QString(),
                              QString());
        QVERIFY(repository.keyboardMap(profile).value(QStringLiteral("layerKey")).toString()
                    .isEmpty());
    }
    // --- two layers, end to end --------------------------------------------

    void layers_bothLayersTypeWhatTheySay()
    {
        if (!KeymapService::isAvailable())
            QSKIP("xkbcli is not installed");

        // Caps Lock selects layer 1, Menu layer 2. H does one thing on each,
        // J only on layer 2. Then the keymap is compiled by libxkbcommon --
        // the library Hyprland uses -- and the keys are pressed in software.
        Profile profile;
        profile.id = QStringLiteral("two-layers");
        const auto remap = [](MappingKind kind, const char *key, const QStringList &levels) {
            Remap r;
            r.id = QString::fromLatin1(key) + QString::number(levels.size());
            r.kind = kind;
            r.sourceKey = QString::fromLatin1(key);
            r.levels = levels;
            return r;
        };
        profile.remaps = {
            remap(MappingKind::LayerChooser, "CAPS", { QStringLiteral("ISO_Level3_Shift") }),
            remap(MappingKind::LayerChooser, "MENU", { QStringLiteral("ISO_Level5_Shift") }),
            remap(MappingKind::LayerKey, "AC06", layerLevels(1, QStringLiteral("Left"))),
            remap(MappingKind::LayerKey, "AC06", layerLevels(2, QStringLiteral("Home"))),
            remap(MappingKind::LayerKey, "AC07", layerLevels(2, QStringLiteral("Next"))),
        };

        KeymapService keymaps;
        const KccgstComponents components =
            keymaps.resolveComponents(profile, QStringLiteral("us"), {}, {}, {});
        QVERIFY(components.isValid());
        const KeymapBuild build =
            keymaps.build(profile, components, keymaps.baseSymbols(components));
        QVERIFY2(build.ok, qPrintable(build.error));

        xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        const QByteArray source = build.source.toUtf8();
        xkb_keymap *keymap = xkb_keymap_new_from_string(
            context, source.constData(), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
        QVERIFY(keymap);

        const auto typed = [keymap](const char *held, const char *key) {
            xkb_state *state = xkb_state_new(keymap);
            if (held)
                xkb_state_update_key(state, xkb_keymap_key_by_name(keymap, held), XKB_KEY_DOWN);
            const xkb_keysym_t sym =
                xkb_state_key_get_one_sym(state, xkb_keymap_key_by_name(keymap, key));
            xkb_state_unref(state);
            char name[64];
            xkb_keysym_get_name(sym, name, sizeof name);
            return QString::fromLatin1(name);
        };

        QCOMPARE(typed(nullptr, "AC06"), QStringLiteral("h"));
        QCOMPARE(typed("CAPS", "AC06"), QStringLiteral("Left"));
        QCOMPARE(typed("MENU", "AC06"), QStringLiteral("Home"));
        // A key with a job only on layer 2 still types itself on layer 1.
        QCOMPARE(typed(nullptr, "AC07"), QStringLiteral("j"));
        QCOMPARE(typed("CAPS", "AC07"), QStringLiteral("j"));
        QCOMPARE(typed("MENU", "AC07"), QStringLiteral("Next"));
        // Keys with no layer job are untouched by either layer key.
        QCOMPARE(typed("MENU", "AC01"), QStringLiteral("a"));

        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
    }

    void layers_aKeyCanHaveOneJobPerLayer()
    {
        const auto conflictsFor = [](const QVector<Remap> &remaps) {
            RuleSet ruleSet;
            Profile profile;
            profile.id = QStringLiteral("p");
            profile.remaps = remaps;
            ruleSet.profiles = { profile };
            ConflictDetector detector;
            ConflictDetector::Inputs inputs;
            inputs.ruleSet = ruleSet;
            return ConflictDetector::countBySeverity(detector.detect(inputs),
                                                     ConflictSeverity::Error);
        };
        const auto make = [](const char *id, MappingKind kind, const QStringList &levels) {
            Remap r;
            r.id = QString::fromLatin1(id);
            r.kind = kind;
            r.sourceKey = QStringLiteral("AC06");
            r.levels = levels;
            return r;
        };

        // One on each layer: fine.
        QCOMPARE(conflictsFor({ make("a", MappingKind::LayerKey, layerLevels(1, QStringLiteral("Left"))),
                                make("b", MappingKind::LayerKey, layerLevels(2, QStringLiteral("Home"))) }),
                 0);
        // Two on the same layer: one key, two answers.
        QCOMPARE(conflictsFor({ make("a", MappingKind::LayerKey, layerLevels(2, QStringLiteral("Left"))),
                                make("b", MappingKind::LayerKey, layerLevels(2, QStringLiteral("Home"))) }),
                 1);
        // A job of its own and a job on a layer do not mix.
        QCOMPARE(conflictsFor({ make("a", MappingKind::KeyRemap, { QStringLiteral("Escape") }),
                                make("b", MappingKind::LayerKey, layerLevels(2, QStringLiteral("Home"))) }),
                 1);
    }
    void layers_theListNamesTheRightLayerKey()
    {
        ConfigRepository repository;
        const QString profile = repository.ensureProfileForDevice(
            QStringLiteral("names-keyboard"), QStringLiteral("Names"));
        repository.addMapping(profile, QStringLiteral("layerChooser"), QStringLiteral("CAPS"),
                              QVariantList{ QStringLiteral("ISO_Level3_Shift") }, QString(), QString());
        repository.addMapping(profile, QStringLiteral("layerChooser"), QStringLiteral("COMP"),
                              QVariantList{ QStringLiteral("ISO_Level5_Shift") }, QString(), QString());
        QVariantList second;
        for (const QString &level : layerLevels(2, QStringLiteral("Home")))
            second.append(level);
        const QString id = repository.addMapping(profile, QStringLiteral("layerKey"),
                                                 QStringLiteral("AC06"), second, QString(), QString());

        for (const QVariant &value : repository.mappingList(profile)) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("id")).toString() != id)
                continue;
            QCOMPARE(row.value(QStringLiteral("layer")).toInt(), 2);
            QVERIFY2(row.value(QStringLiteral("sourceLabel")).toString().startsWith(QStringLiteral("Menu + ")),
                     qPrintable(row.value(QStringLiteral("sourceLabel")).toString()));
            return;
        }
        QFAIL("the layer-2 mapping is missing from the list");
    }
    // --- sharing a configuration -------------------------------------------

    void sharing_exportThenImportOntoAnotherKeyboard()
    {
        const QString path = m_root.filePath(QStringLiteral("shared.json"));

        ConfigRepository source;
        const QString profile = source.ensureProfileForDevice(
            QStringLiteral("their-keyboard"), QStringLiteral("Theirs"));
        source.addRecipe(profile, QStringLiteral("caps-arrows"));
        source.addShortcut(QStringLiteral("SUPER + CTRL + ALT + F9"), QStringLiteral("Shared"),
                           QStringLiteral("command"), QStringLiteral("true"), QString());

        const QVariantMap exported = source.exportToFile(path);
        QVERIFY2(exported.value(QStringLiteral("ok")).toBool(),
                 qPrintable(exported.value(QStringLiteral("message")).toString()));
        QVERIFY(QFile::exists(path));

        // The file says what is in it, and where each keyboard could land.
        ConfigRepository destination;
        const QVariantMap report = destination.inspectImport(path);
        QVERIFY(report.value(QStringLiteral("ok")).toBool());
        const QVariantList profiles = report.value(QStringLiteral("profiles")).toList();
        QCOMPARE(report.value(QStringLiteral("shortcutCount")).toInt(), 1);

        // A keyboard that is not attached here has no suggestion: the user says.
        int theirs = -1;
        for (int i = 0; i < profiles.size(); ++i) {
            if (profiles.at(i).toMap().value(QStringLiteral("deviceName")).toString()
                == QStringLiteral("their-keyboard"))
                theirs = i;
        }
        QVERIFY(theirs >= 0);
        QVERIFY(profiles.at(theirs).toMap().value(QStringLiteral("suggested")).toString().isEmpty());

        // Send it to every keyboard instead, and skip anything else.
        QVariantList targets;
        for (int i = 0; i < profiles.size(); ++i)
            targets.append(i == theirs ? QStringLiteral("global") : QStringLiteral("skip"));

        const int before = destination.mappingList(QStringLiteral("all-keyboards")).size();
        const QVariantMap result = destination.importFromFile(path, targets, true);
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("mappings")).toInt(), 5);   // chooser + H J K L
        QCOMPARE(result.value(QStringLiteral("shortcuts")).toInt(), 1);
        QCOMPARE(destination.mappingList(QStringLiteral("all-keyboards")).size(), before + 5);
        QVERIFY(destination.isDirty());

        // Ids are rewritten, so importing the same file twice adds a second
        // copy instead of overwriting the first.
        const QVariantList rows = destination.mappingList(QStringLiteral("all-keyboards"));
        QSet<QString> ids;
        for (const QVariant &row : rows)
            ids.insert(row.toMap().value(QStringLiteral("id")).toString());
        QCOMPARE(ids.size(), rows.size());
    }

    void sharing_skippingEverythingChangesNothing()
    {
        const QString path = m_root.filePath(QStringLiteral("shared2.json"));
        ConfigRepository source;
        source.addRecipe(QStringLiteral("all-keyboards"), QStringLiteral("caps-escape"));
        QVERIFY(source.exportToFile(path).value(QStringLiteral("ok")).toBool());

        ConfigRepository destination;
        const QVariantList profiles =
            destination.inspectImport(path).value(QStringLiteral("profiles")).toList();
        QVariantList targets;
        for (int i = 0; i < profiles.size(); ++i)
            targets.append(QStringLiteral("skip"));

        const QVariantMap result = destination.importFromFile(path, targets, false);
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("mappings")).toInt(), 0);
    }

    void sharing_arefusedFileSaysWhy()
    {
        const QString path = m_root.filePath(QStringLiteral("not-ours.json"));
        QString error;
        QVERIFY(Paths::writeAtomically(path, QByteArrayLiteral("not json at all"), &error));

        ConfigRepository repository;
        const QVariantMap report = repository.inspectImport(path);
        QVERIFY(!report.value(QStringLiteral("ok")).toBool());
        QVERIFY(!report.value(QStringLiteral("error")).toString().isEmpty());
        QVERIFY(!repository.isDirty());
    }
    // --- deleting backups ---------------------------------------------------

    void backups_canBeDeletedOneByOneOrAllAtOnce()
    {
        ConfigBackupService backups;
        QString error;
        QVERIFY(Paths::writeAtomically(Paths::rulesFile(), QByteArrayLiteral("{}"), &error));

        // Ids are unix timestamps, so two snapshots in the same second share
        // a directory; the count is what matters here.
        const QString first = backups.createBackup(QStringLiteral("test one"), &error);
        QVERIFY2(!first.isEmpty(), qPrintable(error));

        ConfigRepository repository;
        QSignalSpy changed(&repository, &ConfigRepository::backupsChanged);
        const int before = repository.backupList().size();
        QVERIFY(before > 0);

        QVERIFY(repository.deleteBackup(first));
        QCOMPARE(repository.backupList().size(), before - 1);
        QCOMPARE(changed.count(), 1);
        QVERIFY(!QDir(Paths::backupDir() + QLatin1Char('/') + first).exists());

        // Deleting one that is already gone says so instead of pretending.
        QVERIFY(!repository.deleteBackup(first));

        backups.createBackup(QStringLiteral("test two"), &error);
        QVERIFY(repository.deleteAllBackups() >= 1);
        QVERIFY(repository.backupList().isEmpty());
        QCOMPARE(repository.deleteAllBackups(), 0);
    }

    void backups_deletionCannotEscapeTheBackupDirectory()
    {
        // The id comes from the interface and is pasted onto a path, so a
        // traversal has to be refused before it reaches removeRecursively.
        QVERIFY(!ConfigBackupService::isValidBackupId(QStringLiteral("../omakeyboard")));
        QVERIFY(!ConfigBackupService::isValidBackupId(QStringLiteral("..")));
        QVERIFY(!ConfigBackupService::isValidBackupId(QStringLiteral("a/b")));
        QVERIFY(!ConfigBackupService::isValidBackupId(QString()));
        QVERIFY(ConfigBackupService::isValidBackupId(QStringLiteral("1789402016")));

        const QString victim = m_root.filePath(QStringLiteral("keep-me"));
        QDir().mkpath(victim);
        ConfigRepository repository;
        QVERIFY(!repository.deleteBackup(QStringLiteral("../../keep-me")));
        QVERIFY(QDir(victim).exists());
    }
    void status_clearsItselfAfterAMoment()
    {
        // "Backup deleted." described something that already happened; left on
        // screen for the rest of the session it reads as current state.
        ConfigRepository repository;
        repository.setStatusTimeout(80);

        ConfigBackupService backups;
        QString error;
        QVERIFY(Paths::writeAtomically(Paths::rulesFile(), QByteArrayLiteral("{}"), &error));
        QVERIFY(!backups.createBackup(QStringLiteral("status test"), &error).isEmpty());

        QVERIFY(repository.deleteAllBackups() >= 1);
        QVERIFY(!repository.status().isEmpty());

        QSignalSpy cleared(&repository, &ConfigRepository::statusChanged);
        QVERIFY(cleared.wait(2000));
        QVERIFY2(repository.status().isEmpty(), qPrintable(repository.status()));
    }
};

QTEST_MAIN(TestOmaKeyboard)
#include "tst_omakeyboard.moc"
