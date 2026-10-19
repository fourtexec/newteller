#include "fileportal.h"

#include <QDateTime>
#include <QDir>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QUrl>

namespace {

const char kPortalService[] = "org.freedesktop.portal.Desktop";
const char kPortalPath[] = "/org/freedesktop/portal/desktop";
const char kFileChooser[] = "org.freedesktop.portal.FileChooser";
const char kRequest[] = "org.freedesktop.portal.Request";

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
}

bool FilePortal::available() const
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.interface() && bus.interface()->isServiceRegistered(QLatin1String(kPortalService));
}

void FilePortal::pickFile(const QString &title, const QString &startFolder, bool imagesOnly)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (m_waiting || !bus.interface()
        || !bus.interface()->isServiceRegistered(QLatin1String(kPortalService))) {
        emit pickFailed();
        return;
    }

    // The Response signal arrives on a request object path that the caller
    // derives from its own bus name and a handle token it passes in the
    // options, so the path must be built before the OpenFile call.
    const QString token = QStringLiteral("newteller_%1")
                              .arg(QDateTime::currentMSecsSinceEpoch() % 1000000);
    QString sender = bus.baseService();
    if (sender.startsWith(QLatin1Char(':')))
        sender = sender.mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    m_requestPath = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
                        .arg(sender, token);

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("multiple"), false);
    if (!startFolder.isEmpty()) {
        // Option type is a byte array holding the directory path.
        options.insert(QStringLiteral("current_folder"), expandFolder(startFolder).toUtf8());
    }
    if (imagesOnly) {
        // Filters: a(sa(us)) — serialised by the registered FilterSet type.
        FilterSet images;
        images.name = QStringLiteral("Images");
        for (const char *glob : { "*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp",
                                  "*.gif", "*.svg", "*.xpm" })
            images.patterns.append(FilterPattern { 0 /* glob */, QString::fromLatin1(glob) });
        options.insert(QStringLiteral("filters"), QVariant::fromValue(QList<FilterSet> { images }));
    }

    if (!bus.connect(QLatin1String(kPortalService), m_requestPath, QLatin1String(kRequest),
                     QStringLiteral("Response"), this,
                     SLOT(portalResponse(uint, QVariantMap)))) {
        m_requestPath.clear();
        emit pickFailed();
        return;
    }

    QDBusMessage call = QDBusMessage::createMethodCall(QLatin1String(kPortalService),
                                                       QLatin1String(kPortalPath),
                                                       QLatin1String(kFileChooser),
                                                       QStringLiteral("OpenFile"));
    call << QString() /* parent window: the portal picks its own placement */
         << title << options;
    QDBusPendingCallWatcher *watcher =
        new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &FilePortal::callFinished);
    m_waiting = true;
}

QString FilePortal::expandFolder(const QString &path) const
{
    if (path.startsWith(QLatin1String("~/")))
        return QDir::homePath() + path.mid(1);
    return path;
}

void FilePortal::callFinished(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();
    QDBusPendingReply<QDBusObjectPath> reply = *watcher;
    if (reply.isError()) {
        // No portal, no permission or a broken helper: let the QML fall back.
        if (m_waiting) {
            m_waiting = false;
            if (!m_requestPath.isEmpty())
                QDBusConnection::sessionBus().disconnect(
                    QLatin1String(kPortalService), m_requestPath, QLatin1String(kRequest),
                    QStringLiteral("Response"), this,
                    SLOT(portalResponse(uint, QVariantMap)));
            m_requestPath.clear();
            emit pickFailed();
        }
    }
    // On success the Response signal carries the result.
}

void FilePortal::portalResponse(uint response, const QVariantMap &results)
{
    m_waiting = false;
    if (!m_requestPath.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(QLatin1String(kPortalService), m_requestPath,
                                                 QLatin1String(kRequest),
                                                 QStringLiteral("Response"), this,
                                                 SLOT(portalResponse(uint, QVariantMap)));
        m_requestPath.clear();
    }
    if (response != 0) {
        // 1 = dismissed, everything else = aborted; either way: nothing picked.
        emit filePicked(QString());
        return;
    }
    const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
    if (uris.isEmpty()) {
        emit filePicked(QString());
        return;
    }
    emit filePicked(QUrl(uris.first()).toLocalFile());
}
