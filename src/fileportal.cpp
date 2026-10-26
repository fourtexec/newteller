#include "fileportal.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

namespace {

const char kPortalService[] = "org.freedesktop.portal.Desktop";
const char kPortalPath[] = "/org/freedesktop/portal/desktop";
const char kFileChooser[] = "org.freedesktop.portal.FileChooser";
const char kRequest[] = "org.freedesktop.portal.Request";
// How long the portal may take before the shell stops waiting and falls back
// to its integrated picker. Generous: the user may be browsing for a while.
const int kResponseTimeoutMs = 120000;

// One file-name pattern of a portal filter: 0 = glob pattern, 1 = MIME type.
struct FilterPattern {
    uint type = 0;
    QString pattern;
};
// One filter: a display name and its patterns, serialised as (sa(us)).
struct FilterSet {
    QString name;
    QList<FilterPattern> patterns;
};

} // namespace

Q_DECLARE_METATYPE(FilterPattern)
Q_DECLARE_METATYPE(FilterSet)
Q_DECLARE_METATYPE(QList<FilterPattern>)
Q_DECLARE_METATYPE(QList<FilterSet>)

QDBusArgument &operator<<(QDBusArgument &arg, const FilterPattern &p)
{
    arg.beginStructure();
    arg << p.type << p.pattern;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, FilterPattern &p)
{
    arg.beginStructure();
    arg >> p.type >> p.pattern;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const FilterSet &fs)
{
    arg.beginStructure();
    arg << fs.name << fs.patterns;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, FilterSet &fs)
{
    arg.beginStructure();
    arg >> fs.name >> fs.patterns;
    arg.endStructure();
    return arg;
}

FilePortal::FilePortal(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<FilterPattern>("FilterPattern");
    qDBusRegisterMetaType<FilterPattern>();
    qRegisterMetaType<FilterSet>("FilterSet");
    qDBusRegisterMetaType<FilterSet>();
    qRegisterMetaType<QList<FilterPattern>>("QList<FilterPattern>");
    qDBusRegisterMetaType<QList<FilterPattern>>();
    qRegisterMetaType<QList<FilterSet>>("QList<FilterSet>");
    qDBusRegisterMetaType<QList<FilterSet>>();

    m_timeout.setSingleShot(true);
    m_timeout.setInterval(kResponseTimeoutMs);
    connect(&m_timeout, &QTimer::timeout, this, &FilePortal::responseTimedOut);
}

FilePortal::~FilePortal()
{
    // The request subscription belongs to the (global) session bus connection,
    // so it has to be removed explicitly: it points at this object.
    resetRequest();
}

bool FilePortal::serviceFileInstalled()
{
    // A portal that is installed but not running yet is fine: the async call
    // below starts it through D-Bus service activation. Checking the service
    // files avoids a blocking StartService() on the GUI thread.
    const QString fileName = QStringLiteral("org.freedesktop.portal.Desktop.service");
    QStringList dirs;
    dirs << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                + QStringLiteral("/dbus-1/services");
    const QStringList dataDirs =
        QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &dir : dataDirs)
        dirs << dir + QStringLiteral("/dbus-1/services");
    dirs << QStringLiteral("/usr/share/dbus-1/services")
         << QStringLiteral("/usr/local/share/dbus-1/services");
    for (const QString &dir : dirs) {
        if (QFileInfo::exists(dir + QLatin1Char('/') + fileName))
            return true;
    }
    return false;
}

bool FilePortal::available() const
{
    // Escape hatch for sessions with a broken/partial portal installation and
    // for the UI test suite, which needs the integrated picker deterministically.
    if (qEnvironmentVariableIsSet("NEWTELLER_NO_PORTAL"))
        return false;

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface())
        return false;
    if (bus.interface()->isServiceRegistered(QLatin1String(kPortalService)))
        return true;
    return serviceFileInstalled();
}

void FilePortal::pickFile(const QString &title, const QString &startFolder, bool imagesOnly)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface()) {
        finish(true);
        return;
    }

    // Only one dialog at a time: close the previous request instead of
    // stacking dialogs. The previous request then answers with "cancelled",
    // which is ignored because m_waiting is already false here.
    if (m_waiting) {
        if (!m_requestPath.isEmpty()) {
            QDBusMessage close = QDBusMessage::createMethodCall(
                QLatin1String(kPortalService), m_requestPath, QLatin1String(kRequest),
                QStringLiteral("Close"));
            bus.call(close, QDBus::NoBlock);
        }
        resetRequest();
    }

    // The request object path is derived from the caller's unique name and the
    // handle token. Predict it so the Response signal is subscribed *before*
    // OpenFile() is sent — the portal may answer immediately.
    const QString token = QStringLiteral("newteller_%1_%2")
                              .arg(QCoreApplication::applicationPid())
                              .arg(++m_requestSequence);
    QString sender = bus.baseService();
    if (sender.startsWith(QLatin1Char(':')))
        sender = sender.mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("multiple"), false);
    if (!startFolder.isEmpty()) {
        // The option type is a byte array holding the directory path; the
        // portal expects it to be a directory that exists.
        const QString folder = expandFolder(startFolder);
        if (QFileInfo(folder).isDir())
            options.insert(QStringLiteral("current_folder"), QDir(folder).absolutePath().toUtf8());
    }
    if (imagesOnly) {
        // Filters: a(sa(us)) — serialised by the registered FilterSet type.
        FilterSet images;
        images.name = QStringLiteral("Images");
        for (const char *glob : { "*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp",
                                  "*.gif", "*.svg", "*.xpm", "*.tif", "*.tiff",
                                  "*.avif", "*.heic", "*.jxl" })
            images.patterns.append(FilterPattern { 0 /* glob */, QString::fromLatin1(glob) });
        // A second, unrestricted entry so the dialog can still show every
        // file (the portal expects at least one filter to be selectable).
        FilterSet all;
        all.name = QStringLiteral("All files");
        all.patterns.append(FilterPattern { 0 /* glob */, QStringLiteral("*") });
        options.insert(QStringLiteral("filters"),
                       QVariant::fromValue(QList<FilterSet> { images, all }));
    }

    m_requestPath = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
                        .arg(sender, token);
    m_waiting = true;

    if (!subscribe(m_requestPath)) {
        resetRequest();
        finish(true);
        return;
    }

    QDBusMessage call = QDBusMessage::createMethodCall(QLatin1String(kPortalService),
                                                       QLatin1String(kPortalPath),
                                                       QLatin1String(kFileChooser),
                                                       QStringLiteral("OpenFile"));
    call << QString() /* parent window: the portal picks its own placement */
         << (title.isEmpty() ? QStringLiteral("Choose a file") : title)
         << options;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        // The watcher is parented to this object and deleted here; the reply is
        // copied first, so nothing dangles even if the request is reset below.
        const QDBusPendingReply<QDBusObjectPath> reply{*watcher};
        watcher->deleteLater();
        callFinished(reply);
    });
    m_timeout.start();
}

QString FilePortal::expandFolder(const QString &path) const
{
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")))
        return QDir::homePath() + path.mid(1);
    if (path.startsWith(QLatin1String("file://")))
        return QUrl(path).toLocalFile();
    if (path.contains(QLatin1String("://")))
        return QString();
    return path;
}

void FilePortal::callFinished(const QDBusPendingReply<QDBusObjectPath> &reply)
{
    if (!m_waiting)
        return; // the request was closed or already answered

    if (reply.isError()) {
        // No portal, no permission or a broken helper: let the QML fall back
        // to its integrated picker.
        finish(true);
        return;
    }

    // The portal is allowed to sanitise the handle token. Whenever it reports
    // a different request object, move the subscription there.
    const QDBusObjectPath returned = reply.value();
    const QString path = returned.path();
    if (!path.isEmpty() && path != m_requestPath) {
        const QString previous = m_requestPath;
        m_requestPath = path;
        if (!subscribe(m_requestPath)) {
            m_requestPath = previous;
        } else if (!previous.isEmpty()) {
            QDBusConnection::sessionBus().disconnect(
                QLatin1String(kPortalService), previous, QLatin1String(kRequest),
                QStringLiteral("Response"), this,
                SLOT(portalResponse(uint, QVariantMap)));
        }
    }
    // On success the Response signal carries the result.
}

bool FilePortal::subscribe(const QString &path)
{
    return QDBusConnection::sessionBus().connect(
        QLatin1String(kPortalService), path, QLatin1String(kRequest),
        QStringLiteral("Response"), this, SLOT(portalResponse(uint, QVariantMap)));
}

void FilePortal::resetRequest()
{
    m_timeout.stop();
    m_waiting = false;
    if (!m_requestPath.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(QLatin1String(kPortalService), m_requestPath,
                                                 QLatin1String(kRequest),
                                                 QStringLiteral("Response"), this,
                                                 SLOT(portalResponse(uint, QVariantMap)));
        m_requestPath.clear();
    }
}

void FilePortal::finish(bool failed, const QString &path)
{
    resetRequest();
    if (failed)
        emit pickFailed();
    else
        emit filePicked(path);
}

void FilePortal::portalResponse(uint response, const QVariantMap &results)
{
    if (!m_waiting)
        return; // a late answer to a request that was already closed

    if (response != 0) {
        // 1 = dismissed, everything else = aborted; either way: nothing picked.
        finish(false, QString());
        return;
    }
    const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
    if (uris.isEmpty()) {
        finish(false, QString());
        return;
    }
    const QString path = QUrl(uris.first()).toLocalFile();
    // Never hand the QML a path that cannot be read: a stale/deleted file or a
    // non-local URI would only break the wallpaper a moment later.
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        finish(false, QString());
        return;
    }
    finish(false, path);
}

void FilePortal::responseTimedOut()
{
    if (!m_waiting)
        return;
    // The portal did not answer (a hung backend, a lost signal). Close the
    // request so it does not linger, and let the QML fall back.
    if (!m_requestPath.isEmpty()) {
        QDBusMessage close = QDBusMessage::createMethodCall(
            QLatin1String(kPortalService), m_requestPath, QLatin1String(kRequest),
            QStringLiteral("Close"));
        QDBusConnection::sessionBus().call(close, QDBus::NoBlock);
    }
    finish(true);
}
