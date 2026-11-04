#include "notifications.h"
#include "settings.h"

#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusError>
#include <QtDBus/QDBusMetaType>
#include <QBuffer>
#include <QDateTime>
#include <QImage>
#include <QTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QtDebug>

namespace {
// The picture a notification carries (an avatar, an album cover, a site
// icon) as a URL QML can show, per the spec's order: image-data, then
// image-path, then the deprecated icon_data. Raw pixels become a small PNG
// data: URL, so nothing is written to disk.
QString imageDataUrl(const QVariant &value)
{
    if (!value.canConvert<QDBusArgument>())
        return {};
    const QDBusArgument arg = value.value<QDBusArgument>();
    if (arg.currentSignature() != QLatin1String("(iiibiiay)"))
        return {};
    int width = 0, height = 0, rowStride = 0, bitsPerSample = 0, channels = 0;
    bool hasAlpha = false;
    QByteArray data;
    arg.beginStructure();
    arg >> width >> height >> rowStride >> hasAlpha >> bitsPerSample >> channels >> data;
    arg.endStructure();
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || bitsPerSample != 8
        || channels != (hasAlpha ? 4 : 3) || rowStride < width * channels
        || data.size() < qint64(rowStride) * (height - 1) + qint64(width) * channels)
        return {};
    const QImage pixels(reinterpret_cast<const uchar *>(data.constData()), width, height, rowStride,
                        hasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    // The popup shows it at 38 px; 96 keeps it sharp on a 2x screen.
    const QImage image = (width > 96 || height > 96)
                             ? pixels.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                             : pixels.copy();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

// A local file as a file:// URL; anything else (an icon name) is empty.
QString localImageUrl(const QString &value)
{
    QString path = value.trimmed();
    if (path.startsWith(QLatin1String("file://")))
        path = QUrl(path).toLocalFile();
    if (!path.startsWith(QLatin1Char('/')) || !QFileInfo(path).isFile())
        return {};
    return QUrl::fromLocalFile(path).toString();
}

// org.freedesktop.Notifications close reasons.
constexpr uint kReasonExpired = 1;
constexpr uint kReasonDismissed = 2;
constexpr uint kReasonClosed = 3;

// Urgency hint values (the spec spells the type `byte`).
constexpr int kUrgencyNormal = 1;
constexpr int kUrgencyCritical = 2;

constexpr int kMaxActive = 8;

} // namespace

NotificationDaemon::NotificationDaemon(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    setupDBus();
    setupSocket();

    // Toggles apply live: `notifications.enabled` registers and releases the
    // daemon, everything else is read when a notification arrives.
    connect(settings, &Settings::changed, this, [this](const QString &key) {
        if (key.isEmpty() || key == QLatin1String("notifications.enabled"))
            setupDBus();
    });
}

NotificationDaemon::~NotificationDaemon()
{
    teardownDBus();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
}

QString NotificationDaemon::socketPath()
{
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty())
        runtime = QDir::tempPath();
    return runtime + QStringLiteral("/newteller/notify.sock");
}

void NotificationDaemon::setupDBus()
{
    const bool enabled = m_settings->value(QStringLiteral("notifications.enabled"), true).toBool();
    if (!enabled || m_dbusAvailable) {
        if (!enabled)
            teardownDBus();
        return;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        m_dbusAvailable = false;
        return;
    }
    m_dbusAvailable = true;

    if (m_ownsDBus)
        return;

    // Another daemon (GNOME, KDE, a standalone one) wins; the shell then only
    // shows notifications that arrive through its own socket.
    if (!bus.registerService(QStringLiteral("org.freedesktop.Notifications"))) {
        qInfo() << "newteller: another notification daemon owns org.freedesktop.Notifications:"
                << bus.lastError().message();
        emit dbusChanged();
        return;
    }

    m_registeredObject =
        bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"), this,
                           QDBusConnection::ExportScriptableSlots
                               | QDBusConnection::ExportScriptableSignals);
    m_ownsDBus = m_registeredObject;
    emit dbusChanged();
}

void NotificationDaemon::teardownDBus()
{
    if (!m_dbusAvailable)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (m_ownsDBus && bus.isConnected()) {
        bus.unregisterObject(QStringLiteral("/org/freedesktop/Notifications"));
        bus.unregisterService(QStringLiteral("org.freedesktop.Notifications"));
    }
    m_ownsDBus = false;
    m_registeredObject = false;
    emit dbusChanged();
}

void NotificationDaemon::setupSocket()
{
    const QString path = socketPath();
    QDir().mkpath(QFileInfo(path).path());
    QLocalServer::removeServer(path);
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(path)) {
        qWarning() << "newteller: notification socket unavailable:" << m_server->errorString();
        m_server->deleteLater();
        m_server = nullptr;
        return;
    }
    connect(m_server, &QLocalServer::newConnection, this, &NotificationDaemon::readSocket);
}

void NotificationDaemon::readSocket()
{
    if (!m_server)
        return;
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_buffered.remove(socket);
        });
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            m_buffered[socket].append(socket->readAll());
            // One JSON object per line; an incomplete trailing line waits.
            while (true) {
                const int newline = m_buffered[socket].indexOf('\n');
                if (newline < 0)
                    break;
                const QByteArray line = m_buffered[socket].left(newline).trimmed();
                m_buffered[socket].remove(0, newline + 1);
                if (line.isEmpty())
                    continue;
                QJsonParseError error;
                const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
                if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                    qWarning() << "newteller: bad notification payload:" << error.errorString();
                    continue;
                }
                const QJsonObject object = doc.object();
                send(object.value("app").toString(QStringLiteral("newteller-ctl")),
                     object.value("title").toString(),
                     object.value("body").toString(),
                     object.value("icon").toString(),
                     object.value("timeout").toInt(-1));
            }
            // Abandoned senders cannot stall the queue: cap the per-socket buffer.
            if (m_buffered[socket].size() > 64 * 1024)
                m_buffered[socket].clear();
        });
    }
}

uint NotificationDaemon::Notify(const QString &appName, uint replacesId, const QString &appIcon,
                                const QString &summary, const QString &body, const QStringList &actions,
                                const QVariantMap &hints, int expireTimeout)
{
    return publish(replacesId, appName, appIcon, summary, body, actions, hints, expireTimeout);
}

void NotificationDaemon::CloseNotification(uint id)
{
    for (const QVariant &entry : m_active) {
        if (entry.toMap().value(QStringLiteral("id")).toUInt() == id) {
            dismiss(id, kReasonClosed);
            return;
        }
    }
}

QStringList NotificationDaemon::GetCapabilities() const
{
    // Body text is rendered plain; actions are supported through the popups.
    return {QStringLiteral("actions"), QStringLiteral("body")};
}

QString NotificationDaemon::GetServerInformation(QString &vendor, QString &version,
                                                 QString &specVersion) const
{
    vendor = QStringLiteral("fourtexec");
    version = QStringLiteral(NEWTELLER_VERSION);
    specVersion = QStringLiteral("1.2");
    return QStringLiteral("NewTeller");
}

uint NotificationDaemon::sendWithActions(const QString &appName, const QString &summary,
                                         const QString &body, const QString &icon,
                                         const QStringList &actions, bool urgent, int expireTimeoutMs)
{
    // Critical: it gets through do-not-disturb and stays until answered.
    if (urgent)
        return publish(0, appName, icon, summary, body, actions,
                       {{QStringLiteral("urgency"), QVariant::fromValue<uchar>(kUrgencyCritical)}}, 0);
    return publish(0, appName, icon, summary, body, actions, {}, expireTimeoutMs);
}

uint NotificationDaemon::send(const QString &appName, const QString &summary, const QString &body,
                              const QString &icon, int expireTimeoutMs)
{
    return publish(0, appName, icon, summary, body, {}, {}, expireTimeoutMs);
}

uint NotificationDaemon::publish(uint replacesId, const QString &appName, const QString &appIcon,
                                 const QString &summary, const QString &body, const QStringList &actions,
                                 const QVariantMap &hints, int expireTimeout)
{
    // Scripts tag their volume or brightness popups instead of keeping an
    // id (notify-send -h string:x-canonical-private-synchronous:volume, or
    // dunst's x-dunst-stack-tag): a new one replaces the one on screen with
    // the same tag and app.
    QString stackTag = hints.value(QStringLiteral("x-canonical-private-synchronous")).toString();
    if (stackTag.isEmpty())
        stackTag = hints.value(QStringLiteral("x-dunst-stack-tag")).toString();
    if (replacesId == 0 && !stackTag.isEmpty()) {
        for (const QVariant &active : std::as_const(m_active)) {
            const QVariantMap map = active.toMap();
            if (map.value(QStringLiteral("stackTag")).toString() == stackTag
                && map.value(QStringLiteral("app")).toString()
                       == (appName.isEmpty() ? QStringLiteral("NewTeller") : appName))
                replacesId = map.value(QStringLiteral("id")).toUInt();
        }
    }

    // replaces_id updates a notification in place and keeps its id
    // (progress, volume, "now playing"). It is not closed: the application
    // would take NotificationClosed for the user dismissing it.
    int replaceAt = -1;
    bool known = false;
    if (replacesId > 0) {
        for (int i = 0; i < m_active.size(); ++i) {
            if (m_active.at(i).toMap().value(QStringLiteral("id")).toUInt() == replacesId) {
                replaceAt = i;
                known = true;
                break;
            }
        }
        for (int i = m_history.size() - 1; i >= 0; --i) {
            if (m_history.at(i).toMap().value(QStringLiteral("id")).toUInt() == replacesId) {
                m_history.removeAt(i); // the update takes its place
                known = true;
            }
        }
    }

    const bool critical = hints.value(QStringLiteral("urgency")).toInt() >= kUrgencyCritical;
    const bool dnd = (m_settings->value(QStringLiteral("notifications.doNotDisturb"), false).toBool()
                      || scheduledQuiet())
                     && !critical;

    QVariantMap entry;
    const uint id = known ? replacesId : m_nextId++;
    entry.insert(QStringLiteral("revision"), ++m_revision);
    entry.insert(QStringLiteral("id"), id);
    entry.insert(QStringLiteral("app"), appName.isEmpty() ? QStringLiteral("NewTeller") : appName);
    entry.insert(QStringLiteral("summary"), summary);
    entry.insert(QStringLiteral("body"), body);
    // app_icon may be a file:// URI; the icon lookup takes plain paths.
    QString icon = appIcon.startsWith(QLatin1String("file://")) ? QUrl(appIcon).toLocalFile() : appIcon;
    QString image;
    for (const char *key : {"image-data", "image_data"}) {
        if (image.isEmpty())
            image = imageDataUrl(hints.value(QLatin1String(key)));
    }
    for (const char *key : {"image-path", "image_path"}) {
        const QString path = hints.value(QLatin1String(key)).toString();
        if (!image.isEmpty() || path.isEmpty())
            continue;
        image = localImageUrl(path);
        // Not a file: an icon name, which then replaces the app icon.
        if (image.isEmpty() && !path.contains(QLatin1Char('/')))
            icon = path;
    }
    if (image.isEmpty())
        image = imageDataUrl(hints.value(QStringLiteral("icon_data")));
    entry.insert(QStringLiteral("icon"), icon);
    entry.insert(QStringLiteral("image"), image);
    entry.insert(QStringLiteral("actions"), actions);
    entry.insert(QStringLiteral("urgent"), critical);
    entry.insert(QStringLiteral("time"), QDateTime::currentDateTime().toMSecsSinceEpoch());

    // -1 means "daemon default", 0 means sticky; both come from the settings.
    int timeout = expireTimeout;
    if (timeout < 0)
        timeout = m_settings->value(QStringLiteral("notifications.duration"), 6).toInt() * 1000;
    // Critical notifications stay until the user closes them, unless the
    // application asked for a timeout itself.
    if (critical && expireTimeout < 0)
        timeout = 0;
    entry.insert(QStringLiteral("timeout"), timeout);
    // resident: stays on screen after one of its actions was clicked.
    entry.insert(QStringLiteral("resident"), hints.value(QStringLiteral("resident")).toBool());
    entry.insert(QStringLiteral("stackTag"), stackTag);
    // value: a 0-100 progress or level the popup draws as a bar; -1 = none.
    bool hasValue = false;
    const int value = hints.value(QStringLiteral("value")).toInt(&hasValue);
    entry.insert(QStringLiteral("value"), hasValue ? qBound(0, value, 100) : -1);

    // transient: shown, but not kept in the history (volume, progress).
    if (!hints.value(QStringLiteral("transient")).toBool()) {
        m_history.append(entry);
        trimHistory();
    }
    emit historyChanged();

    if (replaceAt >= 0) {
        // Shown already: update the popup where it is, even under DND.
        m_active[replaceAt] = entry;
        emit activeChanged();
    } else if (!dnd && m_settings->value(QStringLiteral("notifications.enabled"), true).toBool()) {
        if (m_active.size() >= kMaxActive)
            dismiss(m_active.first().toMap().value(QStringLiteral("id")).toUInt(), kReasonExpired);
        m_active.append(entry);
        emit activeChanged();
        playSound(hints);
    }
    return id;
}

// notifications.doNotDisturbSchedule: quiet between dndStartTime and
// dndEndTime ("HH:mm"); a start after the end spans midnight (22:00-07:00).
bool NotificationDaemon::scheduledQuiet() const
{
    if (!m_settings->value(QStringLiteral("notifications.doNotDisturbSchedule"), false).toBool())
        return false;
    const QTime start = QTime::fromString(
        m_settings->value(QStringLiteral("notifications.dndStartTime")).toString().trimmed(), QStringLiteral("H:mm"));
    const QTime end = QTime::fromString(
        m_settings->value(QStringLiteral("notifications.dndEndTime")).toString().trimmed(), QStringLiteral("H:mm"));
    if (!start.isValid() || !end.isValid() || start == end)
        return false;
    const QTime now = QTime::currentTime();
    if (start < end)
        return now >= start && now < end;
    return now >= start || now < end;
}

// notifications.soundEnabled: a new popup plays the application's
// sound-file hint, the chosen notifications.soundFile, or the theme's
// message-new-instant sound. suppress-sound silences one notification.
void NotificationDaemon::playSound(const QVariantMap &hints)
{
    if (!m_settings->value(QStringLiteral("notifications.soundEnabled"), true).toBool()
        || hints.value(QStringLiteral("suppress-sound")).toBool())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastSound < 500)
        return;
    m_lastSound = now;

    QString file = hints.value(QStringLiteral("sound-file")).toString();
    if (file.startsWith(QLatin1String("file://")))
        file = QUrl(file).toLocalFile();
    if (file.isEmpty())
        file = m_settings->value(QStringLiteral("notifications.soundFile")).toString().trimmed();
    if (file.startsWith(QLatin1String("~/")))
        file = QDir::homePath() + file.mid(1);
    if (!file.isEmpty() && !QFileInfo(file).isReadable())
        file.clear();
    const QString name = hints.value(QStringLiteral("sound-name"),
                                     QStringLiteral("message-new-instant")).toString();

    // libcanberra knows the sound theme; otherwise play the freedesktop file.
    const QString canberra = QStandardPaths::findExecutable(QStringLiteral("canberra-gtk-play"));
    if (!canberra.isEmpty()) {
        QProcess::startDetached(canberra, file.isEmpty() ? QStringList{QStringLiteral("-i"), name}
                                                         : QStringList{QStringLiteral("-f"), file});
        return;
    }
    if (file.isEmpty()) {
        const QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
        for (const QString &dir : dirs) {
            for (const QString &candidate : {name, QStringLiteral("message-new-instant")}) {
                const QString path = dir + QStringLiteral("/sounds/freedesktop/stereo/") + candidate
                                     + QStringLiteral(".oga");
                if (QFileInfo::exists(path)) {
                    file = path;
                    break;
                }
            }
            if (!file.isEmpty())
                break;
        }
    }
    if (file.isEmpty())
        return;
    for (const QString &player : {QStringLiteral("pw-play"), QStringLiteral("paplay")}) {
        const QString path = QStandardPaths::findExecutable(player);
        if (!path.isEmpty() && QProcess::startDetached(path, {file}))
            return;
    }
}

void NotificationDaemon::dismiss(uint id, uint reason)
{
    for (int i = 0; i < m_active.size(); ++i) {
        if (m_active.at(i).toMap().value(QStringLiteral("id")).toUInt() == id) {
            m_active.removeAt(i);
            emit activeChanged();
            emit NotificationClosed(id, reason);
            return;
        }
    }
}

void NotificationDaemon::invokeAction(uint id, const QString &actionKey)
{
    emit ActionInvoked(id, actionKey);
    for (const QVariant &entry : std::as_const(m_active)) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("id")).toUInt() == id && map.value(QStringLiteral("resident")).toBool())
            return;
    }
    dismiss(id, kReasonDismissed);
}

void NotificationDaemon::clearHistory()
{
    m_history.clear();
    emit historyChanged();
}

void NotificationDaemon::removeHistory(uint id)
{
    // A popup that is still on screen goes away with its history entry.
    dismiss(id, kReasonDismissed);
    for (int i = m_history.size() - 1; i >= 0; --i) {
        if (m_history.at(i).toMap().value(QStringLiteral("id")).toUInt() == id) {
            m_history.removeAt(i);
            emit historyChanged();
            return;
        }
    }
}

void NotificationDaemon::trimHistory()
{
    const int limit = qBound(5, m_settings->value(QStringLiteral("notifications.historyLimit"), 20).toInt(), 200);
    while (m_history.size() > limit)
        m_history.removeFirst();
}
