#include "shellcontroller.h"
#include "notifications.h"
#include "settings.h"

#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

ShellController::ShellController(Settings *settings, NotificationDaemon *notifications, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_notifications(notifications)
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
        if (key.isEmpty() || key.startsWith(QLatin1String("icons."))) {
            m_iconCache.clear();
            m_tintCache.clear();
        }
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
            return themeIconUrl(name);
        return QUrl(*cached);
    }

    QString resolved;

    // 1. The core set compiled into the binary (IconKit's lucide icons): the
    //    shell chrome always renders, even on a system without IconKit on
    //    disk and without these names in the system icon theme.
    const QString qrc = QStringLiteral("qrc:/icons/") + name + QStringLiteral(".svg");
    if (QFile::exists(qrc))
        resolved = qrc;

    // 2. A user-managed IconKit checkout (settings: icons.path + icons.set)
    //    overrides everything the binary and the installation carry.
    if (resolved.isEmpty()) {
        const QString overridePath = expandHome(
            m_settings->value(QStringLiteral("icons.path")).toString());
        if (!overridePath.isEmpty()) {
            const QString set = m_settings->value(QStringLiteral("icons.set"),
                                                  QStringLiteral("lucide")).toString();
            const QString file = overridePath + QLatin1Char('/') + set + QLatin1Char('/')
                                 + name + QStringLiteral(".svg");
            if (QFile::exists(file))
                resolved = QUrl::fromLocalFile(file).toString();
        }
    }

    // 3. The IconKit bundled with the release (the install ships
    //    github.com/fourtexec/iconkit under share/newteller/icons).
    if (resolved.isEmpty()) {
        const QString set = m_settings->value(QStringLiteral("icons.set"),
                                              QStringLiteral("lucide")).toString();
        const QString relative = set + QLatin1Char('/') + name + QStringLiteral(".svg");
        const QString appShare = QCoreApplication::applicationDirPath()
                                 + QStringLiteral("/../share/newteller/icons/");
        for (const QString &base : { appShare, QStringLiteral("/usr/share/newteller/icons/") }) {
            const QString file = base + relative;
            if (QFile::exists(file)) {
                resolved = QUrl::fromLocalFile(QFileInfo(file).absoluteFilePath()).toString();
                break;
            }
        }
    }

    if (cache)
        m_iconCache.insert(name, resolved);

    if (!resolved.isEmpty())
        return QUrl(resolved);

    // 4. Served by ThemeIconProvider (main.cpp) from the system icon theme.
    return themeIconUrl(name);
}

QUrl ShellController::themeIconUrl(const QString &name)
{
    return QUrl(QStringLiteral("image://theme/")
                + QString::fromUtf8(QUrl::toPercentEncoding(name)));
}

QUrl ShellController::tintedIcon(const QString &name, const QColor &color) const
{
    if (name.isEmpty())
        return {};

    // The IconKit glyph set (lucide) draws with stroke="currentColor", which
    // the SVG renderer resolves to black. On a dark theme those glyphs would
    // be nearly invisible, so they are recoloured to the theme foreground.
    // Application icons from the system theme (colourful logos) are returned
    // unchanged - tintedIcon only rewrites SVGs it finds in the IconKit sets.
    QString source;
    const QString qrc = QStringLiteral("qrc:/icons/") + name + QStringLiteral(".svg");
    if (QFile::exists(qrc)) {
        source = qrc;
    } else {
        const QString set = m_settings->value(QStringLiteral("icons.set"),
                                              QStringLiteral("lucide")).toString();
        const QString overridePath = expandHome(
            m_settings->value(QStringLiteral("icons.path")).toString());
        if (!overridePath.isEmpty()) {
            const QString file = overridePath + QLatin1Char('/') + set + QLatin1Char('/')
                                 + name + QStringLiteral(".svg");
            if (QFile::exists(file))
                source = file;
        }
        if (source.isEmpty()) {
            const QString relative = set + QLatin1Char('/') + name + QStringLiteral(".svg");
            const QString appShare = QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/../share/newteller/icons/");
            for (const QString &base : { appShare, QStringLiteral("/usr/share/newteller/icons/") }) {
                const QString file = base + relative;
                if (QFile::exists(file)) {
                    source = QFileInfo(file).absoluteFilePath();
                    break;
                }
            }
        }
    }
    if (source.isEmpty())
        return iconUrl(name); // not an IconKit glyph: the system theme decides

    const QUrl original = source.startsWith(QLatin1String("qrc:"))
                          ? QUrl(source) : QUrl::fromLocalFile(source);
    const QString cacheKey = name + QLatin1Char('@') + color.name();
    if (m_tintCache.contains(cacheKey))
        return QUrl(m_tintCache.value(cacheKey));

    QFile in(source);
    if (!in.open(QIODevice::ReadOnly))
        return original;
    const QString svg = QString::fromUtf8(in.readAll());
    in.close();
    if (!svg.contains(QLatin1String("currentColor")))
        return original; // nothing to recolour

    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                             + QStringLiteral("/tinted-icons");
    if (!QDir().mkpath(cacheDir))
        return original;
    QString safeName = name;
    safeName.replace(QLatin1Char('/'), QLatin1Char('_'));
    const QString outPath = cacheDir + QLatin1Char('/') + safeName + QLatin1Char('-')
                            + color.name().mid(1) + QStringLiteral(".svg");
    {
        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return original;
        out.write(QString(svg).replace(QLatin1String("currentColor"), color.name()).toUtf8());
    }
    const QUrl outUrl = QUrl::fromLocalFile(outPath);
    m_tintCache.insert(cacheKey, outUrl.toString());
    return outUrl;
}

void ShellController::openUrl(const QString &url) const
{
    QDesktopServices::openUrl(QUrl(url));
}

void ShellController::openSettings()
{
    emit settingsRequested();
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
    // The built-in daemon shows the popup itself when it owns the session; if
    // it is disabled (or another daemon owns org.freedesktop.Notifications and
    // ours is off), notify-send hands the message to whoever is responsible.
    if (m_notifications
        && m_settings->value(QStringLiteral("notifications.enabled"), true).toBool()) {
        m_notifications->send(QStringLiteral("NewTeller"), title, body);
        return;
    }
    // A missing binary is not an error, the shell keeps working without it.
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
