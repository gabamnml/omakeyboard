#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QLocalSocket>
#include <QTimer>
#include <QObject>
#include <QString>

namespace omakeyboard {

// Visual integration with Omarchy, done the same way Omawrite does it: read
// the current theme's colors.toml, and follow the desktop portal for dark mode
// and text scaling. Re-read whenever the theme changes, which is why the
// watcher covers the symlink's directory and not just the file.
class ThemeService : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool darkMode READ darkMode NOTIFY themeChanged)
    Q_PROPERTY(qreal textScale READ textScale NOTIFY textScaleChanged)
    Q_PROPERTY(QString themeName READ themeName NOTIFY themeChanged)

    // Structure, taken from the same places Omarchy's own shell takes it:
    // the type scale from the theme's shell.toml, and the geometry from the
    // user's Hyprland configuration.
    Q_PROPERTY(int fontBaseSize READ fontBaseSize NOTIFY themeChanged)
    Q_PROPERTY(qreal spacingScale READ spacingScale NOTIFY themeChanged)
    Q_PROPERTY(int cornerRadius READ cornerRadius NOTIFY themeChanged)
    Q_PROPERTY(int gapsOut READ gapsOut NOTIFY themeChanged)

    Q_PROPERTY(QColor background READ background NOTIFY themeChanged)
    Q_PROPERTY(QColor surface READ surface NOTIFY themeChanged)
    Q_PROPERTY(QColor elevated READ elevated NOTIFY themeChanged)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY themeChanged)
    Q_PROPERTY(QColor mutedForeground READ mutedForeground NOTIFY themeChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
    Q_PROPERTY(QColor selection READ selection NOTIFY themeChanged)
    Q_PROPERTY(QColor border READ border NOTIFY themeChanged)
    Q_PROPERTY(QColor warning READ warning NOTIFY themeChanged)
    Q_PROPERTY(QColor error READ error NOTIFY themeChanged)
    Q_PROPERTY(QColor success READ success NOTIFY themeChanged)

public:
    explicit ThemeService(QObject *parent = nullptr);

    bool darkMode() const { return m_darkMode; }
    qreal textScale() const { return m_textScale; }
    QString themeName() const { return m_themeName; }
    int fontBaseSize() const { return m_fontBaseSize; }
    qreal spacingScale() const { return m_spacingScale; }
    int cornerRadius() const { return m_cornerRadius; }
    int gapsOut() const { return m_gapsOut; }

    QColor background() const { return m_background; }
    QColor surface() const { return m_surface; }
    QColor elevated() const { return m_elevated; }
    QColor foreground() const { return m_foreground; }
    QColor mutedForeground() const { return m_mutedForeground; }
    QColor accent() const { return m_accent; }
    QColor selection() const { return m_selection; }
    QColor border() const { return m_border; }
    QColor warning() const { return m_warning; }
    QColor error() const { return m_error; }
    QColor success() const { return m_success; }

public slots:
    void refresh();

signals:
    void themeChanged();
    void textScaleChanged();

private:
    void loadOmarchyTheme();
    // The type scale and spacing scale a theme declares in shell.toml.
    void loadShellStructure();
    // Corner rounding and edge gaps, mirrored from Hyprland the way the
    // Omarchy shell does it: cornerRadius is decoration:rounding, and the
    // gap to a screen edge is half of general:gaps_out.
    void loadHyprlandGeometry();
    void watchOmarchyTheme();
    // Hyprland announces every config reload on its event socket; rounding
    // and gaps are re-read then, since a theme switch can change both.
    void listenToHyprland();
    void loadPortalSettings();
    static QVariant portalSetting(const QString &nameSpace, const QString &key);

    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    QString m_themeName;
    int m_fontBaseSize = 12;
    qreal m_spacingScale = 1.0;
    int m_cornerRadius = 0;
    int m_gapsOut = 5;

    QColor m_background;
    QColor m_surface;
    QColor m_elevated;
    QColor m_foreground;
    QColor m_mutedForeground;
    QColor m_accent;
    QColor m_selection;
    QColor m_border;
    QColor m_warning;
    QColor m_error;
    QColor m_success;

    QFileSystemWatcher m_watcher;
    // `omarchy theme set` deletes the current theme and moves the next one
    // into place, so the first file event lands on a half-swapped directory.
    // Changes are gathered for a moment and read once.
    QTimer m_settle;
    QLocalSocket m_hyprEvents;
    QByteArray m_hyprBuffer;
};

} // namespace omakeyboard
