#include "shellcontroller.h"
#include "settings.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

ShellController::ShellController(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    // One shared ticker: the panel, the start menu and clock widgets all bind
    // to `Shell.now` instead of running their own timers.
    m_now = QDateTime::currentDateTime();
    m_clock.setInterval(1000);
    m_clock.setTimerType(Qt::CoarseTimer);
    connect(&m_clock, &QTimer::timeout, this, [this] {
        m_now = QDateTime::currentDateTime();
        emit nowChanged();
    });
    m_clock.start();

    // The icon cache follows the icon settings.
    connect(settings, &Settings::changed, this, [this](const QString &key) {
        if (key.isEmpty() || key.startsWith(QLatin1String("icons.")))
            m_iconCache.clear();
    });
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

QString ShellController::hostname() const
{
    return QSysInfo::machineHostName();
}

QString ShellController::distro() const
{
    return QSysInfo::prettyProductName();
}

QString ShellController::kernelVersion() const
{
    return QSysInfo::kernelVersion();
}

bool ShellController::isWayland() const
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
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

    const bool cache = m_settings->value(QStringLiteral("icons.cache"), true).toBool();
    const auto cached = m_iconCache.constFind(name);
    if (cache && cached != m_iconCache.constEnd()) {
        if (cached->isEmpty())
            return QUrl(QStringLiteral("image://theme/") + QString::fromUtf8(QUrl::toPercentEncoding(name)));
        return QUrl(*cached);
    }

    QString resolved;
    const QString set = m_settings->value(QStringLiteral("icons.set"), QStringLiteral("lucide")).toString();
    const QString kit = expandHome(m_settings->value(QStringLiteral("icons.path")).toString());
    const QString file = kit + QLatin1Char('/') + set + QLatin1Char('/') + name + QStringLiteral(".svg");
    if (QFile::exists(file))
        resolved = QUrl::fromLocalFile(file).toString();

    if (cache)
        m_iconCache.insert(name, resolved);

    if (!resolved.isEmpty())
        return QUrl(resolved);

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

void ShellController::openPath(const QString &path) const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(expandHome(path)));
}

void ShellController::runShell(const QString &command) const
{
    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
}

void ShellController::terminal(const QString &command) const
{
    const QString wrapper = m_settings->value(
        QStringLiteral("apps.terminalCommand"),
        QStringLiteral("x-terminal-emulator -e")).toString();
    runShell(wrapper + QLatin1Char(' ') + command);
}

void ShellController::copyToClipboard(const QString &text) const
{
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

void ShellController::notify(const QString &title, const QString &body) const
{
    // notify-send talks to org.freedesktop.Notifications; a missing binary is
    // not an error, the shell keeps working without a notification daemon.
    QProcess::startDetached(QStringLiteral("notify-send"),
                            {QStringLiteral("--app-name=NewTeller"), title, body});
}

QString ShellController::clipboardText() const
{
    const QClipboard *clipboard = QGuiApplication::clipboard();
    return clipboard != nullptr ? clipboard->text() : QString();
}

bool ShellController::saveTextFile(const QString &path, const QString &text) const
{
    QFile file(expandHome(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(text.toUtf8());
    return true;
}

QVariantMap ShellController::diagnostics() const
{
    QVariantMap out;
    out.insert(QStringLiteral("platform"), QGuiApplication::platformName());
    out.insert(QStringLiteral("sessionType"), sessionType());
    out.insert(QStringLiteral("wayland"), isWayland());
    out.insert(QStringLiteral("hostname"), hostname());
    out.insert(QStringLiteral("distro"), distro());
    out.insert(QStringLiteral("kernel"), kernelVersion());
    out.insert(QStringLiteral("user"), userName());
    out.insert(QStringLiteral("configPath"), Settings::configFile());
    out.insert(QStringLiteral("iconPath"), expandHome(m_settings->value(QStringLiteral("icons.path")).toString()));
    out.insert(QStringLiteral("theme"), m_settings->value(QStringLiteral("theme.name")).toString());
    return out;
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
