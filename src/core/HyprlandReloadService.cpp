#include "HyprlandReloadService.h"

#include "Process.h"

#include <QRegularExpression>

namespace omakeyboard {

namespace {
constexpr const char *kHyprctl = "hyprctl";
} // namespace

HyprlandReloadService::HyprlandReloadService(QObject *parent)
    : QObject(parent)
{
}

bool HyprlandReloadService::isHyprlandRunning()
{
    if (!Process::isAvailable(QString::fromLatin1(kHyprctl)))
        return false;
    if (qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
        return false;
    const ProcessResult result =
        Process::run(QString::fromLatin1(kHyprctl), { QStringLiteral("version") }, 2000);
    return result.ok();
}

QString HyprlandReloadService::version()
{
    const ProcessResult result =
        Process::run(QString::fromLatin1(kHyprctl), { QStringLiteral("version") }, 2000);
    if (!result.ok())
        return {};
    return result.stdOut.split(QLatin1Char('\n')).value(0).trimmed();
}

QStringList HyprlandReloadService::readConfigErrors()
{
    const ProcessResult result = Process::run(
        QString::fromLatin1(kHyprctl), { QStringLiteral("configerrors") }, 4000);
    if (!result.ok())
        return {};

    QStringList errors;
    const QStringList lines = result.stdOut.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            errors.append(trimmed);
    }
    return errors;
}

ReloadReport HyprlandReloadService::reload()
{
    ReloadReport report;

    if (!isHyprlandRunning()) {
        report.message = QStringLiteral(
            "Hyprland is not running, so nothing was reloaded. The configuration "
            "on disk will apply at next login.");
        emit reloadFinished(false);
        return report;
    }

    const ProcessResult result =
        Process::run(QString::fromLatin1(kHyprctl), { QStringLiteral("reload") }, 8000);
    if (!result.ok()) {
        report.message = result.stdErr.trimmed().isEmpty()
            ? QStringLiteral("hyprctl reload failed")
            : result.stdErr.trimmed();
        emit reloadFinished(false);
        return report;
    }

    report.reloaded = true;
    report.errors = readConfigErrors();
    report.clean = report.errors.isEmpty();
    report.message = report.clean
        ? QStringLiteral("Hyprland reloaded with no configuration errors.")
        : (report.errors.size() == 1
               ? QStringLiteral("Hyprland reported a configuration error.")
               : QStringLiteral("Hyprland reported %1 configuration errors.")
                     .arg(report.errors.size()));

    emit reloadFinished(report.clean);
    return report;
}

QString HyprlandReloadService::parseOptionValue(const QString &reply)
{
    // [ \t] rather than \s: only horizontal space may be skipped, so an unset
    // option does not pick up the following "set: true" line as its value.
    static const QRegularExpression pattern(
        QStringLiteral("^(?:str|int|bool|float|vec2|custom):[ \\t]*(.*)$"),
        QRegularExpression::MultilineOption);
    const auto match = pattern.match(reply);
    if (!match.hasMatch())
        return {};
    return match.captured(1).trimmed();
}

QString HyprlandReloadService::option(const QString &key) const
{
    const ProcessResult result = Process::run(
        QString::fromLatin1(kHyprctl), { QStringLiteral("getoption"), key }, 4000);
    if (!result.ok())
        return {};
    return parseOptionValue(result.stdOut);
}

int HyprlandReloadService::bindCount() const
{
    const ProcessResult result =
        Process::run(QString::fromLatin1(kHyprctl), { QStringLiteral("binds") }, 6000);
    if (!result.ok())
        return -1;
    // Each record starts with a line beginning "bind".
    int count = 0;
    const QStringList lines = result.stdOut.split(QLatin1Char('\n'));
    for (const QString &line : lines)
        if (line.startsWith(QStringLiteral("bind")))
            ++count;
    return count;
}

} // namespace omakeyboard
