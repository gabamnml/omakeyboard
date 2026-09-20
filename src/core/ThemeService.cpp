#include "ThemeService.h"

#include "HyprlandReloadService.h"
#include "Paths.h"
#include "Process.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QTextStream>

namespace omakeyboard {

ThemeService::ThemeService(QObject *parent)
    : QObject(parent)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(250);
    connect(&m_settle, &QTimer::timeout, this, [this]() {
        refresh();
        watchOmarchyTheme();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle,
            qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle,
            qOverload<>(&QTimer::start));

    refresh();
    watchOmarchyTheme();
    listenToHyprland();

    // The portal signals when the desktop's appearance changes.
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Settings"),
        QStringLiteral("SettingChanged"), this, SLOT(refresh()));
}

void ThemeService::listenToHyprland()
{
    const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (signature.isEmpty() || runtime.isEmpty())
        return;

    connect(&m_hyprEvents, &QLocalSocket::readyRead, this, [this]() {
        m_hyprBuffer += m_hyprEvents.readAll();
        bool reloaded = false;
        int newline;
        while ((newline = m_hyprBuffer.indexOf('\n')) >= 0) {
            if (m_hyprBuffer.startsWith("configreloaded>>"))
                reloaded = true;
            m_hyprBuffer.remove(0, newline + 1);
        }
        if (reloaded)
            m_settle.start();
    });
    m_hyprEvents.connectToServer(
        runtime + QStringLiteral("/hypr/") + signature + QStringLiteral("/.socket2.sock"),
        QIODevice::ReadOnly);
}

void ThemeService::refresh()
{
    loadPortalSettings();
    loadOmarchyTheme();
    loadShellStructure();
    loadHyprlandGeometry();
    emit themeChanged();
}

void ThemeService::loadShellStructure()
{
    // shell.toml has sections, unlike colors.toml, so the parser has to know
    // which one it is in: `base-size` means something different under [font]
    // than it would anywhere else.
    m_fontBaseSize = 12;
    m_spacingScale = 1.0;

    bool read = false;
    const QByteArray raw = Paths::read(
        Paths::omarchyCurrentDir() + QStringLiteral("/theme/shell.toml"), &read);
    if (!read)
        return;

    QString section;
    const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2);
            continue;
        }

        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1).trimmed();

        if (section == QStringLiteral("font") && key == QStringLiteral("base-size")) {
            bool ok = false;
            const int parsed = value.toInt(&ok);
            if (ok && parsed > 0)
                m_fontBaseSize = parsed;
        } else if (section == QStringLiteral("spacing") && key == QStringLiteral("scale")) {
            bool ok = false;
            const double parsed = value.toDouble(&ok);
            if (ok && parsed > 0.1 && parsed < 10.0)
                m_spacingScale = parsed;
        }
    }
}

void ThemeService::loadHyprlandGeometry()
{
    const QString rounding =
        HyprlandReloadService::parseOptionValue(
            Process::run(QStringLiteral("hyprctl"),
                         { QStringLiteral("getoption"), QStringLiteral("decoration:rounding") })
                .stdOut);
    bool ok = false;
    const int parsedRounding = rounding.toInt(&ok);
    if (ok && parsedRounding >= 0)
        m_cornerRadius = parsedRounding;

    // gaps_out answers as "css gap data: 10 10 10 10" rather than a typed
    // value, so take the first number in the reply.
    const QString gaps =
        Process::run(QStringLiteral("hyprctl"),
                     { QStringLiteral("getoption"), QStringLiteral("general:gaps_out") })
            .stdOut;
    static const QRegularExpression firstNumber(QStringLiteral("(\\d+)"));
    const auto match = firstNumber.match(gaps);
    if (match.hasMatch()) {
        const int parsedGaps = match.captured(1).toInt();
        if (parsedGaps >= 0)
            m_gapsOut = qMax(1, parsedGaps / 2);
    }
}

QVariant ThemeService::portalSetting(const QString &nameSpace, const QString &key)
{
    // Short timeout: a stalled portal must not hold up the first paint.
    QDBusInterface interface(QStringLiteral("org.freedesktop.portal.Desktop"),
                             QStringLiteral("/org/freedesktop/portal/desktop"),
                             QStringLiteral("org.freedesktop.portal.Settings"),
                             QDBusConnection::sessionBus());
    if (!interface.isValid())
        return {};
    interface.setTimeout(500);

    const QDBusReply<QDBusVariant> reply =
        interface.call(QStringLiteral("ReadOne"), nameSpace, key);
    if (reply.isValid())
        return reply.value().variant();

    // Older portals only expose Read, which wraps the value twice.
    const QDBusReply<QDBusVariant> legacy =
        interface.call(QStringLiteral("Read"), nameSpace, key);
    if (legacy.isValid()) {
        const QVariant outer = legacy.value().variant();
        if (outer.canConvert<QDBusVariant>())
            return outer.value<QDBusVariant>().variant();
        return outer;
    }

    return {};
}

void ThemeService::loadPortalSettings()
{
    const QVariant scheme = portalSetting(QStringLiteral("org.freedesktop.appearance"),
                                          QStringLiteral("color-scheme"));
    if (scheme.isValid()) {
        // 1 = prefer dark, 2 = prefer light, 0 = no preference.
        const uint value = scheme.toUInt();
        if (value == 1)
            m_darkMode = true;
        else if (value == 2)
            m_darkMode = false;
    }

    const QVariant scale = portalSetting(QStringLiteral("org.gnome.desktop.interface"),
                                         QStringLiteral("text-scaling-factor"));
    if (scale.isValid()) {
        const double factor = scale.toDouble();
        if (factor > 0.5 && factor < 4.0 && !qFuzzyCompare(factor, m_textScale)) {
            m_textScale = factor;
            emit textScaleChanged();
        }
    }
}

void ThemeService::loadOmarchyTheme()
{
    // Defaults that look right even with no Omarchy theme present.
    QHash<QString, QString> values;

    bool read = false;
    const QByteArray raw = Paths::read(Paths::omarchyThemeColors(), &read);
    if (read) {
        // colors.toml is flat `key = "value"`; a full TOML parser would be
        // more machinery than the file needs.
        QTextStream in(raw);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0)
                continue;
            const QString key = line.left(equals).trimmed();
            QString value = line.mid(equals + 1).trimmed();
            if (value.size() >= 2
                && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
                    || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))) {
                value = value.mid(1, value.size() - 2);
            }
            values.insert(key, value);
        }
    }

    bool themeRead = false;
    const QByteArray name =
        Paths::read(Paths::omarchyCurrentDir() + QStringLiteral("/theme.name"), &themeRead);
    m_themeName = themeRead ? QString::fromUtf8(name).trimmed() : QString();

    const QString mode = values.value(QStringLiteral("mode"));
    if (mode == QStringLiteral("dark"))
        m_darkMode = true;
    else if (mode == QStringLiteral("light"))
        m_darkMode = false;

    const auto colorOr = [&values](const QString &key, const QColor &fallback) {
        const QColor color(values.value(key));
        return color.isValid() ? color : fallback;
    };

    const QColor defaultBackground = m_darkMode ? QColor(QStringLiteral("#16161d"))
                                                : QColor(QStringLiteral("#fbfbfa"));
    m_background = colorOr(QStringLiteral("background"), defaultBackground);

    // If no mode was declared, decide from the background's luminance, the way
    // Omawrite does.
    if (mode.isEmpty() && m_background.isValid()) {
        const double luminance = 0.299 * m_background.redF() + 0.587 * m_background.greenF()
            + 0.114 * m_background.blueF();
        m_darkMode = luminance < 0.5;
    }

    m_foreground = colorOr(QStringLiteral("foreground"),
                           m_darkMode ? QColor(QStringLiteral("#e6e6e6"))
                                      : QColor(QStringLiteral("#1f1f1f")));
    m_accent = colorOr(QStringLiteral("accent"), QColor(QStringLiteral("#7aa2f7")));
    m_selection = colorOr(QStringLiteral("selection"),
                          m_darkMode ? QColor(QStringLiteral("#2a2a35"))
                                     : QColor(QStringLiteral("#e4e4e0")));
    m_surface = colorOr(QStringLiteral("lighter_background"),
                        m_background.lighter(m_darkMode ? 130 : 98));
    m_elevated = colorOr(QStringLiteral("dark_background"),
                         m_background.darker(m_darkMode ? 115 : 100));
    m_mutedForeground = colorOr(QStringLiteral("dark_foreground"),
                                m_darkMode ? QColor(QStringLiteral("#8a8a98"))
                                           : QColor(QStringLiteral("#6b6b6b")));
    m_border = colorOr(QStringLiteral("muted"),
                       m_darkMode ? QColor(QStringLiteral("#33333f"))
                                  : QColor(QStringLiteral("#dcdcd8")));

    // Semantic colours come from the theme's own palette, so a conflict banner
    // matches the rest of the desktop.
    m_error = colorOr(QStringLiteral("red"), QColor(QStringLiteral("#f7768e")));
    m_warning = colorOr(QStringLiteral("yellow"), QColor(QStringLiteral("#e0af68")));
    m_success = colorOr(QStringLiteral("green"), QColor(QStringLiteral("#9ece6a")));
}

void ThemeService::watchOmarchyTheme()
{
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);

    // Three paths, because `theme` is a symlink that gets replaced when the
    // theme changes: watching only the file would miss the switch.
    const QString currentDir = Paths::omarchyCurrentDir();
    const QString themeDir = currentDir + QStringLiteral("/theme");
    const QString colors = Paths::omarchyThemeColors();
    const QString shell = themeDir + QStringLiteral("/shell.toml");

    if (QDir(currentDir).exists())
        m_watcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_watcher.addPath(themeDir);
    for (const QString &file : { colors, shell })
        if (QFile::exists(file))
            m_watcher.addPath(file);
}

} // namespace omakeyboard
