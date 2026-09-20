#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace omakeyboard {

struct ReloadReport {
    bool reloaded = false;
    bool clean = false;        // configerrors came back empty
    QStringList errors;        // what configerrors reported
    QString message;
};

// The only component that talks to a running Hyprland. Kept narrow on purpose:
// everything it does is observable with hyprctl by hand, so a user can always
// verify or undo the app's effect without the app.
class HyprlandReloadService : public QObject {
    Q_OBJECT

public:
    explicit HyprlandReloadService(QObject *parent = nullptr);

    static bool isHyprlandRunning();
    static QString version();

    // `hyprctl reload` followed by `hyprctl configerrors`. Hyprland also
    // auto-reloads on save, so this mostly makes the timing deterministic and
    // gives the pipeline something to check.
    ReloadReport reload();

    // Read back a live option so an apply can prove it took effect rather than
    // trusting that the write succeeded.
    QString option(const QString &key) const;

    // Count of registered binds, used as a coarse before/after check.
    int bindCount() const;

    // Parse one `hyprctl getoption` reply. Its own function because the shape
    // is easy to get wrong: an unset string option answers
    //
    //     str:
    //     set: true
    //
    // and a pattern using \s to skip the space after "str:" will happily skip
    // the newline as well and return "set: true" as the value. An empty value
    // has to stay empty, or it ends up pasted into an XKB variant.
    //
    // Numbers and booleans are reported under their own prefixes ("int: 40",
    // "bool: true", "float: 1.0"), so all of them are accepted and the value
    // is returned as text.
    static QString parseOptionValue(const QString &reply);

signals:
    void reloadFinished(bool ok);

private:
    static QStringList readConfigErrors();
};

} // namespace omakeyboard
