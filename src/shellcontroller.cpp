#include "shellcontroller.h"
#include "settings.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

ShellController::ShellController(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

QString ShellController::home() const
{
    return QDir::homePath();
}

QString ShellController::desktopUrl() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    return QUrl::fromLocalFile(dir).toString();
}

QString ShellController::userName() const
{
    return qEnvironmentVariable("USER");
}

QString ShellController::sessionType() const
{
    return qEnvironmentVariable("XDG_SESSION_TYPE");
}

QString ShellController::expandHome(const QString &path) const
{
    if (path.startsWith(QLatin1String("~/")))
        return QDir::homePath() + path.mid(1);
    return path;
}

QUrl ShellController::iconUrl(const QString &name) const
{
    if (name.isEmpty())
        return {};
    if (name.startsWith(QLatin1Char('/')))
        return QUrl::fromLocalFile(name);

    const QString set = m_settings->value(QStringLiteral("icons.set"), QStringLiteral("lucide")).toString();
    const QString kit = expandHome(m_settings->value(QStringLiteral("icons.path")).toString());
    const QString file = kit + QLatin1Char('/') + set + QLatin1Char('/') + name + QStringLiteral(".svg");
    if (QFile::exists(file))
        return QUrl::fromLocalFile(file);

    // Served by ThemeIconProvider (main.cpp) from the system icon theme.
    return QUrl(QStringLiteral("image://theme/") + QString::fromUtf8(QUrl::toPercentEncoding(name)));
}

void ShellController::openUrl(const QString &url) const
{
    QDesktopServices::openUrl(QUrl(url));
}

void ShellController::openConfig() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::configFile()));
}

void ShellController::runShell(const QString &command) const
{
    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
}

void ShellController::lock() const
{
    QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("lock-session")});
}

void ShellController::logout() const
{
    QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("terminate-user"), userName()});
}

void ShellController::suspend() const
{
    QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("suspend")});
}

void ShellController::reboot() const
{
    QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
}

void ShellController::powerOff() const
{
    QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("poweroff")});
}
