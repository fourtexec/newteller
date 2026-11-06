#include "shellcontroller.h"
#include "notifications.h"
#include "settings.h"

#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QProcess>
#include <QThread>
#include <QPointer>
#include <QMimeData>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>
#include <QXmlStreamReader>
#include <algorithm>

namespace {
// "name.txt" -> "name 2.txt" (then 3, 4...) while the name is taken.
QString freeName(const QDir &dir, const QString &name, bool isDir)
{
    if (!dir.exists(name))
        return name;
    const QFileInfo info(name);
    const QString stem = isDir || info.completeSuffix().isEmpty() || info.baseName().isEmpty()
                             ? name : info.baseName();
    const QString suffix = stem == name ? QString() : QLatin1Char('.') + info.completeSuffix();
    for (int n = 2; n < 10000; ++n) {
        const QString candidate = QStringLiteral("%1 %2%3").arg(stem).arg(n).arg(suffix);
        if (!dir.exists(candidate))
            return candidate;
    }
    return QString();
}

// Copies a file or a whole folder; false at the first failure.
bool copyTree(const QString &source, const QString &target)
{
    const QFileInfo info(source);
    if (!info.isDir() || info.isSymLink())
        return info.isSymLink() ? QFile::link(info.symLinkTarget(), target) : QFile::copy(source, target);
    if (!QDir().mkpath(target))
        return false;
    const QDir dir(source);
    const QStringList entries = dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        if (!copyTree(dir.absoluteFilePath(entry), target + QLatin1Char('/') + entry))
            return false;
    }
    return true;
}
} // namespace

ShellController::ShellController(Settings *settings, NotificationDaemon *notifications, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_notifications(notifications)
{
    // Undo on a "moved to the trash" notification puts the file back.
    if (m_notifications) {
        connect(m_notifications, &NotificationDaemon::ActionInvoked, this, [this](uint id, const QString &key) {
            if (!m_trashUndo.contains(id) || key != QLatin1String("undo"))
                return;
            const auto [trashed, original] = m_trashUndo.take(id);
            if (QFileInfo::exists(original) || !QFile::rename(trashed, original)) {
                notify(tr("Could not restore %1").arg(QFileInfo(original).fileName()),
                       tr("Open the trash in the file manager to restore it."));
                return;
            }
            // The trash's record of it: Trash/info/<name>.trashinfo.
            const QFileInfo inTrash(trashed);
            QFile::remove(inTrash.dir().absoluteFilePath(QStringLiteral("../info/%1.trashinfo")
                                                             .arg(inTrash.fileName())));
        });
        connect(m_notifications, &NotificationDaemon::NotificationClosed, this,
                [this](uint id, uint) { m_trashUndo.remove(id); });
    }

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

    // A stable session endpoint for the compositor and for scripts. Failure is
    // normal when a second shell runs in a nested test session.
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        if (!bus.registerObject(QStringLiteral("/org/newteller/Shell"), this,
                                QDBusConnection::ExportAllContents)) {
            qWarning("NewTeller: could not export the shell D-Bus object: %s",
                     qUtf8Printable(bus.lastError().message()));
        }
        if (!bus.registerService(QStringLiteral("org.newteller.Shell"))) {
            qWarning("NewTeller: could not own org.newteller.Shell: %s",
                     qUtf8Printable(bus.lastError().message()));
        }
    }

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

QString ShellController::folderPath(const QString &path) const
{
    QString p = path.trimmed();
    if (p.isEmpty())
        return QDir::homePath();
    // A URL (file:///..., or already percent-encoded) is decoded first; other
    // schemes have no local path to browse.
    if (p.startsWith(QLatin1String("file:"))) {
        const QString local = QUrl(p).toLocalFile();
        p = local.isEmpty() ? p.mid(7) : local;
    } else if (p.contains(QLatin1String("://"))) {
        return QDir::homePath();
    }
    if (p == QLatin1String("~"))
        p = QDir::homePath();
    else if (p.startsWith(QLatin1String("~/")))
        p = QDir::homePath() + p.mid(1);
    if (!QDir::isAbsolutePath(p))
        p = QDir::homePath() + QLatin1Char('/') + p;
    p = QDir::cleanPath(p);
    const QFileInfo info(p);
    if (info.isDir())
        return info.absoluteFilePath();
    if (info.isFile())
        return info.absolutePath();   // a file: browse the folder it is in
    // The folder is gone (unmounted stick, deleted pictures): start at home
    // instead of handing an unreadable URL to the folder model.
    return QDir::homePath();
}

bool ShellController::isImageFile(const QString &path) const
{
    const QString p = path.trimmed();
    if (p.isEmpty() || !QFileInfo(p).isFile())
        return false;
    QImageReader reader(p);
    // canRead() only parses the header, so even a huge photo is cheap here.
    return reader.canRead();
}

QString ShellController::pathKind(const QString &path) const
{
    QString local = path.trimmed();
    if (local == QLatin1String("~"))
        local = QDir::homePath();
    local = expandHome(local);
    if (local.isEmpty() || !QDir::isAbsolutePath(local))
        return QString();
    const QFileInfo info(local);
    if (info.isDir())
        return QStringLiteral("dir");
    if (info.isFile())
        return info.isExecutable() ? QStringLiteral("exec") : QStringLiteral("file");
    return QString();
}

QVariantList ShellController::recentFiles(int limit) const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                         + QStringLiteral("/recently-used.xbel");
    const QFileInfo info(path);
    if (!info.exists())
        return {};
    if (info.lastModified() != m_recentStamp) {
        m_recentStamp = info.lastModified();
        struct Entry {
            QString path;
            QString stamp;
        };
        QList<Entry> entries;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly) && file.size() < 16 * 1024 * 1024) {
            QXmlStreamReader xml(&file);
            while (!xml.atEnd()) {
                if (xml.readNext() != QXmlStreamReader::StartElement
                    || xml.name() != QLatin1String("bookmark"))
                    continue;
                const QXmlStreamAttributes attributes = xml.attributes();
                const QUrl url(attributes.value(QLatin1String("href")).toString());
                if (!url.isLocalFile())
                    continue;
                // ISO timestamps sort as text; the newest use wins.
                QString stamp = attributes.value(QLatin1String("visited")).toString();
                const QString modified = attributes.value(QLatin1String("modified")).toString();
                if (modified > stamp)
                    stamp = modified;
                entries.append({url.toLocalFile(), stamp});
            }
        }
        std::sort(entries.begin(), entries.end(),
                  [](const Entry &a, const Entry &b) { return a.stamp > b.stamp; });
        m_recent.clear();
        for (const Entry &entry : std::as_const(entries)) {
            if (m_recent.size() >= 500)
                break;
            const QFileInfo target(entry.path);
            if (!target.exists())
                continue;
            QVariantMap item;
            item.insert(QStringLiteral("path"), entry.path);
            item.insert(QStringLiteral("name"), target.fileName());
            item.insert(QStringLiteral("folder"), target.isDir());
            item.insert(QStringLiteral("image"), isImageFile(entry.path));
            m_recent.append(item);
        }
    }
    return limit > 0 ? m_recent.mid(0, limit) : m_recent;
}

bool ShellController::fileExists(const QString &path) const
{
    const QString p = path.trimmed();
    return !p.isEmpty() && QFileInfo(p).isFile();
}

QString ShellController::desktopUrl() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    return QUrl::fromLocalFile(dir).toString();
}

QString ShellController::picturesPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
}

QString ShellController::documentsPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

QString ShellController::downloadsPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
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
    // QML often has a file:// URL at hand (Shell.desktopUrl); its path may be
    // percent-encoded, so let QUrl decode it instead of cutting the scheme off.
    if (path.startsWith(QLatin1String("file:")))
        return QUrl(path).toLocalFile();
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
    const QUrl target(url);
    // A launcher (.desktop file) on the desktop starts its program instead of
    // opening in a text editor - but only when it is marked executable, as
    // KDE does, so a downloaded file cannot run a command by a double-click.
    if (target.isLocalFile()) {
        const QFileInfo info(target.toLocalFile());
        if (info.isFile() && info.suffix().compare(QLatin1String("desktop"), Qt::CaseInsensitive) == 0
            && info.isExecutable()) {
            const QString gio = QStandardPaths::findExecutable(QStringLiteral("gio"));
            if (!gio.isEmpty()
                && QProcess::startDetached(gio, {QStringLiteral("launch"), info.absoluteFilePath()}))
                return;
            const QString dex = QStandardPaths::findExecutable(QStringLiteral("dex"));
            if (!dex.isEmpty() && QProcess::startDetached(dex, {info.absoluteFilePath()}))
                return;
        }
    }
    openOrSay(target);
}

void ShellController::openSettings()
{
    emit settingsRequested();
}

void ShellController::showStartMenu()
{
    emit startMenuRequested();
}

void ShellController::toggleStartMenu()
{
    emit startMenuToggleRequested();
}

void ShellController::toggleNotifications()
{
    emit notificationsToggleRequested();
}

void ShellController::toggleQuickSettings()
{
    emit quickSettingsToggleRequested();
}

void ShellController::toggleClipboard()
{
    emit clipboardToggleRequested();
}

void ShellController::toggleTaskView()
{
    emit taskViewToggleRequested();
}

void ShellController::showForceQuit()
{
    emit forceQuitRequested();
}

void ShellController::toggleScreenRecording()
{
    emit screenRecordingToggleRequested();
}

void ShellController::openSettingsPage(const QString &page)
{
    emit settingsPageRequested(page);
}

void ShellController::activateDockItem(int index)
{
    if (index >= 0)
        emit dockItemRequested(index);
}

void ShellController::showRunDialog()
{
    emit runDialogRequested();
}

void ShellController::openConfig() const
{
    openOrSay(QUrl::fromLocalFile(Settings::configFile()));
}

void ShellController::openPath(const QString &path) const
{
    openOrSay(QUrl::fromLocalFile(expandHome(path)));
}

bool ShellController::addToDesktop(const QString &desktopFile) const
{
    const QFileInfo source(expandHome(desktopFile.trimmed()));
    const QDir desktop(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    if (source.isFile() && (desktop.exists() || QDir().mkpath(desktop.absolutePath()))) {
        // Already there (the same launcher): nothing to add.
        QFile existing(desktop.absoluteFilePath(source.fileName()));
        QFile original(source.absoluteFilePath());
        if (existing.exists() && existing.open(QIODevice::ReadOnly) && original.open(QIODevice::ReadOnly)
            && existing.readAll() == original.readAll())
            return true;
        const QString name = freeName(desktop, source.fileName(), false);
        const QString target = desktop.absoluteFilePath(name);
        if (!name.isEmpty() && QFile::copy(source.absoluteFilePath(), target)) {
            QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::ExeOwner
                                              | QFileDevice::ExeUser);
            return true;
        }
    }
    notify(tr("Could not add %1 to the desktop").arg(source.completeBaseName()),
           tr("You may not be allowed to change this folder, or the disk is full."));
    return false;
}

bool ShellController::renameFile(const QString &path, const QString &newName) const
{
    const QFileInfo info(expandHome(path.trimmed()));
    const QString name = newName.trimmed();
    if (name.isEmpty() || name == info.fileName())
        return false; // nothing to do
    if (name.contains(QLatin1Char('/')) || name == QLatin1String(".") || name == QLatin1String("..")) {
        notify(tr("Could not rename %1").arg(info.fileName()), tr("A name cannot contain \"/\"."));
        return false;
    }
    const QString target = info.dir().absoluteFilePath(name);
    if (QFileInfo::exists(target)) {
        notify(tr("Could not rename %1").arg(info.fileName()), tr("%1 already exists here.").arg(name));
        return false;
    }
    if (!info.exists() || !info.dir().rename(info.fileName(), name)) {
        notify(tr("Could not rename %1").arg(info.fileName()),
               tr("You may not be allowed to change this folder."));
        return false;
    }
    return true;
}

QString ShellController::createFolder(const QString &parent, const QString &name) const
{
    const QString base = expandHome(parent.trimmed());
    const QString wanted = name.trimmed().isEmpty() ? tr("New folder") : name.trimmed();
    QDir dir(base);
    if (!base.isEmpty() && dir.exists()) {
        for (int n = 1; n < 1000; ++n) {
            const QString candidate = n == 1 ? wanted : QStringLiteral("%1 %2").arg(wanted).arg(n);
            if (dir.exists(candidate))
                continue;
            if (dir.mkdir(candidate))
                return dir.absoluteFilePath(candidate);
            break;
        }
    }
    notify(tr("Could not create a folder in %1").arg(base),
           tr("You may not be allowed to change this folder."));
    return QString();
}

bool ShellController::moveToTrash(const QString &path) const
{
    QString local = path.trimmed();
    if (local.startsWith(QLatin1String("file:")))
        local = QUrl(local).toLocalFile();
    local = expandHome(local);
    // QFile follows the freedesktop trash: the file manager can restore it.
    QString trashed;
    if (!local.isEmpty() && QFileInfo::exists(local) && QFile::moveToTrash(local, &trashed)) {
        if (m_notifications && !trashed.isEmpty()
            && m_settings->value(QStringLiteral("notifications.enabled"), true).toBool()) {
            const uint id = m_notifications->sendWithActions(
                QStringLiteral("NewTeller"), tr("%1 moved to the trash").arg(QFileInfo(local).fileName()),
                QString(), QStringLiteral("trash"), {QStringLiteral("undo"), tr("Undo")}, false, 8000);
            m_trashUndo.insert(id, {trashed, local});
        }
        return true;
    }
    notify(tr("Could not move %1 to the trash").arg(QFileInfo(local).fileName()),
           tr("It may be on a drive without a trash folder, or you may not be allowed to change it."));
    return false;
}

void ShellController::openOrSay(const QUrl &url) const
{
    // Qt hands the URL to xdg-open (or the OpenURI portal); without them a
    // click on a folder or file did nothing at all.
    if (QDesktopServices::openUrl(url))
        return;
    const QString name = url.isLocalFile() ? url.toLocalFile() : url.toDisplayString();
    notify(tr("Could not open %1").arg(name),
           tr("No application is set up to open it. Install xdg-utils and a file manager."));
}

void ShellController::runShell(const QString &command) const
{
    QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
}

void ShellController::terminal(const QString &command) const
{
    // Without a command the terminal opens on its own (a prefix such as
    // "kitty -e" with nothing after it would fail).
    const QString wrapper = m_settings->terminalCommand();
    runShell(command.trimmed().isEmpty() ? wrapper.section(QLatin1Char(' '), 0, 0)
                                         : wrapper + QLatin1Char(' ') + command);
}

void ShellController::copyToClipboard(const QString &text) const
{
    // On Wayland only a surface with keyboard focus may set the clipboard,
    // which the desktop and the panel usually do not have; wl-copy goes
    // through wlr-data-control and works from anywhere.
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        && !QStandardPaths::findExecutable(QStringLiteral("wl-copy")).isEmpty()
        && QProcess::startDetached(QStringLiteral("wl-copy"), {QStringLiteral("--"), text}))
        return;
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

void ShellController::notifyUrgent(const QString &title, const QString &body, const QString &icon) const
{
    if (m_notifications
        && m_settings->value(QStringLiteral("notifications.enabled"), true).toBool()) {
        m_notifications->sendWithActions(QStringLiteral("NewTeller"), title, body, icon, {});
        return;
    }
    QProcess::startDetached(QStringLiteral("notify-send"),
                            {QStringLiteral("--app-name=NewTeller"), QStringLiteral("--urgency=critical"),
                             title, body});
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
    // Wayland hands the clipboard only to the focused surface, which the
    // desktop is not: wl-paste reads it through wlr-data-control. It runs on
    // a click, bounded to a second so a hung clipboard owner cannot freeze
    // the shell.
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        && !QStandardPaths::findExecutable(QStringLiteral("wl-paste")).isEmpty()) {
        QProcess paste;
        paste.start(QStringLiteral("wl-paste"),
                    {QStringLiteral("--no-newline"), QStringLiteral("--type"), QStringLiteral("text")});
        if (paste.waitForFinished(1000) && paste.exitStatus() == QProcess::NormalExit
            && paste.exitCode() == 0)
            return QString::fromUtf8(paste.readAllStandardOutput());
        paste.kill();
        paste.waitForFinished(100);
    }
    const QClipboard *clipboard = QGuiApplication::clipboard();
    return clipboard != nullptr ? clipboard->text() : QString();
}


int ShellController::pasteFiles(const QString &folder) const
{
    QStringList sources;
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        && !QStandardPaths::findExecutable(QStringLiteral("wl-paste")).isEmpty()) {
        QProcess types;
        types.start(QStringLiteral("wl-paste"), {QStringLiteral("--list-types")});
        const QStringList offered = types.waitForFinished(1000)
                                        ? QString::fromUtf8(types.readAllStandardOutput()).split(QLatin1Char('\n'))
                                        : QStringList();
        if (!offered.contains(QStringLiteral("text/uri-list")) && offered.contains(QStringLiteral("image/png"))) {
            // A picture (a screenshot, an image copied in a browser): saved
            // as a PNG file.
            QProcess image;
            image.start(QStringLiteral("wl-paste"), {QStringLiteral("--type"), QStringLiteral("image/png")});
            if (image.waitForFinished(3000) && image.exitCode() == 0) {
                const QByteArray png = image.readAllStandardOutput();
                const QDir dir(expandHome(folder.trimmed()));
                const QString name = freeName(dir, tr("Pasted image %1.png")
                                                       .arg(QDateTime::currentDateTime().toString(
                                                           QStringLiteral("yyyyMMdd-hhmmss"))), false);
                QFile file(dir.absoluteFilePath(name));
                if (!png.isEmpty() && !name.isEmpty() && file.open(QIODevice::WriteOnly) && file.write(png) == png.size())
                    return 1;
                notify(tr("Could not paste the image"),
                       tr("You may not be allowed to change this folder, or the disk is full."));
                return 1; // handled: do not paste it as text as well
            }
            image.kill();
            image.waitForFinished(100);
        }
        if (offered.contains(QStringLiteral("text/uri-list"))) {
            QProcess paste;
            paste.start(QStringLiteral("wl-paste"),
                        {QStringLiteral("--no-newline"), QStringLiteral("--type"), QStringLiteral("text/uri-list")});
            if (paste.waitForFinished(1000)) {
                const QStringList lines = QString::fromUtf8(paste.readAllStandardOutput()).split(QLatin1Char('\n'));
                for (QString line : lines) {
                    line = line.trimmed();
                    if (line.startsWith(QLatin1String("file:")))
                        sources << QUrl(line).toLocalFile();
                }
            }
        }
    } else if (const QClipboard *clipboard = QGuiApplication::clipboard()) {
        if (const QMimeData *data = clipboard->mimeData()) {
            for (const QUrl &url : data->urls()) {
                if (url.isLocalFile())
                    sources << url.toLocalFile();
            }
        }
    }
    sources.removeAll(QString());
    if (sources.isEmpty())
        return 0;

    const QString target = expandHome(folder.trimmed());
    // A big folder takes a while: copy in the background, report at the end.
    QPointer<const ShellController> self(this);
    QThread *worker = QThread::create([self, sources, target] {
        const QDir dir(target);
        QStringList failed;
        for (const QString &source : sources) {
            const QFileInfo from(source);
            const QString name = freeName(dir, from.fileName(), from.isDir());
            if (name.isEmpty() || !QFileInfo::exists(source) || !copyTree(source, dir.absoluteFilePath(name)))
                failed << QFileInfo(source).fileName();
        }
        if (!failed.isEmpty() && self) {
            QMetaObject::invokeMethod(const_cast<ShellController *>(self.data()), [self, failed] {
                if (self)
                    self->notify(tr("Could not paste %1").arg(failed.join(QStringLiteral(", "))),
                                 tr("You may not be allowed to change this folder, or the disk is full."));
            }, Qt::QueuedConnection);
        }
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
    return int(sources.size());
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
    // Inside a NewTeller session the compositor starts the locker (and
    // refuses a second one); elsewhere newteller-lock or logind does it.
    if (!qEnvironmentVariableIsEmpty("NEWTELLER_IPC")
        && QProcess::startDetached(QStringLiteral("newteller-compositor"),
                                   {QStringLiteral("--msg"), QStringLiteral("lock")})) {
        return;
    }
    if (QProcess::startDetached(QStringLiteral("newteller-lock"), {}))
        return;
    QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("lock-session")});
}

void ShellController::logout() const
{
    // Ending the compositor ends the session cleanly (applications get
    // their Wayland connection closed); terminate-user is the fallback.
    if (!qEnvironmentVariableIsEmpty("NEWTELLER_IPC")
        && QProcess::startDetached(QStringLiteral("newteller-compositor"),
                                   {QStringLiteral("--msg"), QStringLiteral("exit")})) {
        return;
    }
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
