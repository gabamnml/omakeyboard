#pragma once

#include <QString>
#include <QStringList>

namespace omakeyboard {

struct ProcessResult {
    bool started = false;
    int exitCode = -1;
    QString stdOut;
    QString stdErr;

    bool ok() const { return started && exitCode == 0; }
};

// Synchronous command runner for the short-lived helpers this app shells out to
// (hyprctl, xkbcli, lua, luac). Always argv-based: no string is ever handed to
// a shell, so a device name or a keysym cannot turn into a command.
class Process {
public:
    static ProcessResult run(const QString &program, const QStringList &arguments,
                             int timeoutMs = 5000, const QByteArray &stdIn = {});

    static bool isAvailable(const QString &program);
};

} // namespace omakeyboard
