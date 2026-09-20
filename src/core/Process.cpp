#include "Process.h"

#include <QProcess>
#include <QStandardPaths>

namespace omakeyboard {

ProcessResult Process::run(const QString &program, const QStringList &arguments,
                           int timeoutMs, const QByteArray &stdIn)
{
    ProcessResult result;

    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();

    if (!process.waitForStarted(timeoutMs)) {
        result.stdErr = process.errorString();
        return result;
    }
    result.started = true;

    if (!stdIn.isEmpty())
        process.write(stdIn);
    process.closeWriteChannel();

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        result.stdErr = QStringLiteral("%1 timed out after %2 ms")
                            .arg(program)
                            .arg(timeoutMs);
        return result;
    }

    result.exitCode = process.exitCode();
    result.stdOut = QString::fromUtf8(process.readAllStandardOutput());
    result.stdErr = QString::fromUtf8(process.readAllStandardError());
    return result;
}

bool Process::isAvailable(const QString &program)
{
    return !QStandardPaths::findExecutable(program).isEmpty();
}

} // namespace omakeyboard
