#include "ActionCatalog.h"

#include "Paths.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QSet>
#include <QTextStream>

namespace omakeyboard {

namespace {

// Actions that are commands rather than launchers. Every value here is a
// command Omarchy actually ships, taken from default/hypr/bindings/*.lua.
struct Builtin {
    const char *label;
    const char *category;
    ActionKind kind;
    const char *value;
};

const Builtin kBuiltins[] = {
    // Media -- omarchy-audio-* / omarchy-brightness-* / omarchy-shell media
    { "Volume up", "Media", ActionKind::Media, "omarchy-audio-output-volume raise" },
    { "Volume down", "Media", ActionKind::Media, "omarchy-audio-output-volume lower" },
    { "Mute", "Media", ActionKind::Media, "omarchy-audio-output-volume mute-toggle" },
    { "Mute microphone", "Media", ActionKind::Media, "omarchy-audio-input-mute" },
    { "Switch audio output", "Media", ActionKind::Media, "omarchy-audio-output-switch" },
    { "Brightness up", "Media", ActionKind::Media, "omarchy-brightness-display +5%" },
    { "Brightness down", "Media", ActionKind::Media, "omarchy-brightness-display 5%-" },
    { "Keyboard backlight up", "Media", ActionKind::Media, "omarchy-brightness-keyboard up" },
    { "Keyboard backlight down", "Media", ActionKind::Media, "omarchy-brightness-keyboard down" },
    { "Play / pause", "Media", ActionKind::Media, "omarchy-shell media playPause" },
    { "Next track", "Media", ActionKind::Media, "omarchy-shell media next" },
    { "Previous track", "Media", ActionKind::Media, "omarchy-shell media previous" },

    // Window and workspace actions are identifiers, not Lua. The generated
    // loader resolves each one through a fixed table of real hl.dsp calls, so
    // no part of a rule is ever executed as code. The calls themselves are
    // taken from default/hypr/bindings/tiling.lua.
    { "Close window", "Window", ActionKind::WindowAction, "window.close" },
    { "Toggle fullscreen", "Window", ActionKind::WindowAction, "window.fullscreen" },
    { "Toggle maximized", "Window", ActionKind::WindowAction, "window.maximize" },
    { "Toggle floating", "Window", ActionKind::WindowAction, "window.float" },
    { "Centre window", "Window", ActionKind::WindowAction, "window.center" },
    { "Bring to front", "Window", ActionKind::WindowAction, "window.bring_to_top" },
    { "Focus left", "Window", ActionKind::WindowAction, "focus.left" },
    { "Focus right", "Window", ActionKind::WindowAction, "focus.right" },
    { "Focus up", "Window", ActionKind::WindowAction, "focus.up" },
    { "Focus down", "Window", ActionKind::WindowAction, "focus.down" },
    { "Cycle windows", "Window", ActionKind::WindowAction, "window.cycle_next" },

    { "Next workspace", "Workspace", ActionKind::WorkspaceAction, "workspace.next" },
    { "Previous workspace", "Workspace", ActionKind::WorkspaceAction, "workspace.previous" },
    { "Last workspace", "Workspace", ActionKind::WorkspaceAction, "workspace.last" },
    { "Next monitor", "Workspace", ActionKind::WorkspaceAction, "monitor.next" },
    { "Previous monitor", "Workspace", ActionKind::WorkspaceAction, "monitor.previous" },
    { "Toggle scratchpad", "Workspace", ActionKind::WorkspaceAction, "workspace.scratchpad" },

    // System toggles -- o.bind_toggle(keys, desc, value).
    { "Toggle night light", "System", ActionKind::Toggle, "nightlight" },
    { "Toggle top bar", "System", ActionKind::Toggle, "bar" },
    { "Toggle idle locking", "System", ActionKind::Toggle, "idle" },
    { "Toggle touchpad", "System", ActionKind::Toggle, "touchpad" },

    // Frequently wanted commands.
    { "Lock system", "System", ActionKind::Command, "omarchy-system-lock" },
    { "Screenshot", "System", ActionKind::Command, "omarchy-capture-screenshot" },
    { "Omarchy menu", "System", ActionKind::Command, "omarchy-menu toggle" },
    { "Keybindings list", "System", ActionKind::Command, "omarchy-menu-keybindings" },
    { "Emoji picker", "System", ActionKind::Command, "omarchy-shell shell toggle omarchy.emojis" },
    { "Colour picker", "System", ActionKind::Command, "pkill hyprpicker || hyprpicker -a" },
};

QString titleCase(QString text)
{
    text.replace(QLatin1Char('-'), QLatin1Char(' '));
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

} // namespace

ActionCatalog &ActionCatalog::instance()
{
    static ActionCatalog catalog;
    return catalog;
}

void ActionCatalog::load()
{
    if (m_loaded)
        return;
    loadBuiltinActions();
    loadOmarchyLaunchers();
    loadDesktopApplications();
    m_loaded = true;
}

void ActionCatalog::reloadApplications()
{
    m_actions.erase(std::remove_if(m_actions.begin(), m_actions.end(),
                                   [](const ActionEntry &entry) {
                                       return entry.kind == ActionKind::DesktopApp;
                                   }),
                    m_actions.end());
    loadDesktopApplications();
}

void ActionCatalog::loadBuiltinActions()
{
    for (const auto &builtin : kBuiltins) {
        ActionEntry entry;
        entry.kind = builtin.kind;
        entry.label = QString::fromLatin1(builtin.label);
        entry.category = QString::fromLatin1(builtin.category);
        entry.value = QString::fromLatin1(builtin.value);
        entry.detail = entry.value;
        entry.id = QStringLiteral("builtin:") + entry.value;
        m_actions.append(entry);
    }
}

void ActionCatalog::loadOmarchyLaunchers()
{
    // omarchy-launch-<name> maps onto o.bind's { omarchy = "<name>" } form.
    // Listing the directory keeps the catalog in step with the installed
    // version instead of a snapshot taken at build time.
    QDir binDir(Paths::omarchyPath() + QStringLiteral("/bin"));
    if (!binDir.exists())
        binDir = QDir(QStringLiteral("/usr/bin"));

    const QStringList names =
        binDir.entryList({ QStringLiteral("omarchy-launch-*") }, QDir::Files, QDir::Name);

    // Helpers, not destinations: these take arguments rather than naming an app.
    static const QSet<QString> skip{
        QStringLiteral("or-focus"), QStringLiteral("or-focus-tui"),
        QStringLiteral("or-focus-webapp"), QStringLiteral("webapp"),
        QStringLiteral("tui"), QStringLiteral("shell"),
    };

    for (const QString &file : names) {
        const QString name = file.mid(QStringLiteral("omarchy-launch-").size());
        if (name.isEmpty() || skip.contains(name))
            continue;

        ActionEntry entry;
        entry.kind = ActionKind::OmarchyLaunch;
        entry.label = titleCase(name);
        entry.category = QStringLiteral("Omarchy");
        entry.value = name;
        entry.detail = QStringLiteral("omarchy-launch-") + name;
        entry.id = QStringLiteral("omarchy:") + name;
        m_actions.append(entry);
    }
}

void ActionCatalog::loadDesktopApplications()
{
    QSet<QString> seen;

    for (const QString &directory : Paths::desktopAppDirs()) {
        QDir dir(directory);
        if (!dir.exists())
            continue;

        const QStringList files =
            dir.entryList({ QStringLiteral("*.desktop") }, QDir::Files, QDir::Name);
        for (const QString &file : files) {
            if (seen.contains(file))
                continue;

            QFile desktop(dir.filePath(file));
            if (!desktop.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;

            // A minimal, tolerant .desktop reader: only the [Desktop Entry]
            // group, only the keys a launcher needs.
            bool inEntry = false;
            QString name, exec, icon, type;
            bool noDisplay = false;
            bool terminal = false;

            QTextStream in(&desktop);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line.startsWith(QLatin1Char('['))) {
                    inEntry = line == QStringLiteral("[Desktop Entry]");
                    continue;
                }
                if (!inEntry || line.isEmpty() || line.startsWith(QLatin1Char('#')))
                    continue;

                const int equals = line.indexOf(QLatin1Char('='));
                if (equals < 0)
                    continue;
                const QString key = line.left(equals).trimmed();
                const QString value = line.mid(equals + 1).trimmed();

                if (key == QStringLiteral("Name") && name.isEmpty())
                    name = value;
                else if (key == QStringLiteral("Exec") && exec.isEmpty())
                    exec = value;
                else if (key == QStringLiteral("Icon"))
                    icon = value;
                else if (key == QStringLiteral("Type"))
                    type = value;
                else if (key == QStringLiteral("NoDisplay"))
                    noDisplay = value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
                else if (key == QStringLiteral("Terminal"))
                    terminal = value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
            }

            if (noDisplay || name.isEmpty() || exec.isEmpty())
                continue;
            if (!type.isEmpty() && type != QStringLiteral("Application"))
                continue;

            // Strip the field codes the spec defines (%f, %U, …): they are for
            // a launcher passing files, and a keybinding passes none.
            static const QStringList fieldCodes{
                QStringLiteral("%f"), QStringLiteral("%F"), QStringLiteral("%u"),
                QStringLiteral("%U"), QStringLiteral("%d"), QStringLiteral("%D"),
                QStringLiteral("%n"), QStringLiteral("%N"), QStringLiteral("%i"),
                QStringLiteral("%c"), QStringLiteral("%k"), QStringLiteral("%v"),
                QStringLiteral("%m"),
            };
            for (const QString &code : fieldCodes)
                exec.remove(code);
            exec = exec.simplified();
            if (exec.isEmpty())
                continue;

            seen.insert(file);

            ActionEntry entry;
            entry.kind = terminal ? ActionKind::Tui : ActionKind::DesktopApp;
            entry.label = name;
            entry.category = QStringLiteral("Applications");
            entry.value = exec;
            entry.detail = exec;
            entry.icon = icon;
            entry.id = QStringLiteral("desktop:") + file;
            m_actions.append(entry);
        }
    }
}

QVector<ActionEntry> ActionCatalog::byCategory(const QString &category) const
{
    QVector<ActionEntry> result;
    for (const ActionEntry &entry : m_actions)
        if (entry.category == category)
            result.append(entry);
    return result;
}

QStringList ActionCatalog::categories() const
{
    QStringList result;
    for (const ActionEntry &entry : m_actions)
        if (!result.contains(entry.category))
            result.append(entry.category);
    return result;
}

const ActionEntry *ActionCatalog::find(const QString &id) const
{
    for (const ActionEntry &entry : m_actions)
        if (entry.id == id)
            return &entry;
    return nullptr;
}

} // namespace omakeyboard
