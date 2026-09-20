#include "XkbOptionCatalog.h"

#include "Paths.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace omakeyboard {

XkbOptionCatalog &XkbOptionCatalog::instance()
{
    static XkbOptionCatalog catalog;
    return catalog;
}

QStringList XkbOptionCatalog::remappingGroups()
{
    // The groups that change what a key does, as opposed to layout switching
    // or typographic niceties. "grp" is included because layout switching is
    // still a key remap from the user's point of view.
    return { QStringLiteral("caps"), QStringLiteral("ctrl"), QStringLiteral("altwin"),
             QStringLiteral("compose"), QStringLiteral("lv3"), QStringLiteral("lv5"),
             QStringLiteral("shift"), QStringLiteral("esperanto"),
             QStringLiteral("keypad"), QStringLiteral("grp"), QStringLiteral("terminate") };
}

QString XkbOptionCatalog::groupLabel(const QString &group)
{
    static const QHash<QString, QString> labels{
        { QStringLiteral("caps"), QStringLiteral("Caps Lock") },
        { QStringLiteral("ctrl"), QStringLiteral("Control") },
        { QStringLiteral("altwin"), QStringLiteral("Alt and Super") },
        { QStringLiteral("compose"), QStringLiteral("Compose key") },
        { QStringLiteral("lv3"), QStringLiteral("Layer 3 chooser") },
        { QStringLiteral("lv5"), QStringLiteral("Layer 5 chooser") },
        { QStringLiteral("shift"), QStringLiteral("Shift behaviour") },
        { QStringLiteral("grp"), QStringLiteral("Layout switching") },
        { QStringLiteral("keypad"), QStringLiteral("Keypad") },
        { QStringLiteral("terminate"), QStringLiteral("Terminate server") },
    };
    return labels.value(group, group);
}

QStringList XkbOptionCatalog::claimedKeysFor(const QString &group, const QString &name)
{
    // Map an option to the physical keys it repurposes. This is what makes
    // "Caps Lock as Ctrl" and Omarchy's default "compose:caps" detectably
    // incompatible: both claim CAPS.
    QStringList keys;
    const QString suffix = name.section(QLatin1Char(':'), 1);

    const auto claims = [&keys](const QString &key) {
        if (!keys.contains(key))
            keys.append(key);
    };

    if (group == QStringLiteral("caps"))
        claims(QStringLiteral("CAPS"));

    if (suffix.contains(QStringLiteral("caps")))
        claims(QStringLiteral("CAPS"));
    if (suffix.contains(QStringLiteral("nocaps")) || suffix.contains(QStringLiteral("swapcaps")))
        claims(QStringLiteral("CAPS"));
    if (suffix.contains(QStringLiteral("lalt")))
        claims(QStringLiteral("LALT"));
    if (suffix.contains(QStringLiteral("ralt")))
        claims(QStringLiteral("RALT"));
    if (suffix.contains(QStringLiteral("lwin")))
        claims(QStringLiteral("LWIN"));
    if (suffix.contains(QStringLiteral("rwin")))
        claims(QStringLiteral("RWIN"));
    if (suffix.contains(QStringLiteral("lctrl")) || suffix.contains(QStringLiteral("lctl")))
        claims(QStringLiteral("LCTL"));
    if (suffix.contains(QStringLiteral("rctrl")) || suffix.contains(QStringLiteral("rctl")))
        claims(QStringLiteral("RCTL"));
    if (suffix.contains(QStringLiteral("menu")))
        claims(QStringLiteral("MENU"));
    if (suffix.contains(QStringLiteral("esc")))
        claims(QStringLiteral("ESC"));
    if (suffix == QStringLiteral("ralt") && group == QStringLiteral("compose"))
        claims(QStringLiteral("RALT"));

    return keys;
}

void XkbOptionCatalog::load()
{
    if (m_loaded)
        return;

    QFile file(Paths::xkbRulesLst());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_loaded = true;
        return;
    }

    // base.lst is a sequence of "! <section>" blocks; only the option block is
    // of interest and each of its lines is "  name<spaces>description".
    static const QRegularExpression entry(
        QStringLiteral("^\\s+([a-z0-9_]+:[a-z0-9_]+)\\s+(.*)$"));

    bool inOptions = false;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.startsWith(QLatin1Char('!'))) {
            inOptions = line.startsWith(QStringLiteral("! option"));
            continue;
        }
        if (!inOptions)
            continue;

        const auto match = entry.match(line);
        if (!match.hasMatch())
            continue;

        XkbOption option;
        option.name = match.captured(1);
        option.group = option.name.section(QLatin1Char(':'), 0, 0);
        // base.lst escapes angle brackets for the "< >" key as HTML entities.
        option.description = match.captured(2).trimmed()
                                 .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
                                 .replace(QStringLiteral("&gt;"), QStringLiteral(">"));
        option.claimedKeys = claimedKeysFor(option.group, option.name);

        if (m_index.contains(option.name))
            continue;
        m_index.insert(option.name, m_options.size());
        m_options.append(option);
    }

    m_loaded = true;
}

bool XkbOptionCatalog::contains(const QString &name) const
{
    return m_index.contains(name);
}

const XkbOption *XkbOptionCatalog::find(const QString &name) const
{
    const auto it = m_index.constFind(name);
    if (it == m_index.constEnd())
        return nullptr;
    return &m_options.at(*it);
}

QVector<XkbOption> XkbOptionCatalog::remappingOptions() const
{
    const QStringList groups = remappingGroups();
    QVector<XkbOption> result;
    for (const QString &group : groups) {
        for (const XkbOption &option : m_options) {
            if (option.group == group)
                result.append(option);
        }
    }
    return result;
}

} // namespace omakeyboard
