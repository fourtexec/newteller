#include "notifications.h"
#include "settings.h"

#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusError>
#include <QtDBus/QDBusMetaType>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QtDebug>

namespace {

// org.freedesktop.Notifications close reasons.
constexpr uint kReasonExpired = 1;
constexpr uint kReasonDismissed = 2;
constexpr uint kReasonClosed = 3;

// Urgency hint values (the spec spells the type `byte`).
constexpr int kUrgencyNormal = 1;
constexpr int kUrgencyCritical = 2;

constexpr int kMaxActive = 8;

} // namespace

QDBusArgument &operator<<(QDBusArgument &argument, const NotificationDaemon::ServerInformation &info)
{
    argument.beginStructure();
    argument << info.name << info.vendor << info.version << info.specVersion;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, NotificationDaemon::ServerInformation &info)
{
    argument.beginStructure();
    argument >> info.name >> info.vendor >> info.version >> info.specVersion;
    argument.endStructure();
    return argument;
}

NotificationDaemon::NotificationDaemon(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    qRegisterMetaType<ServerInformation>("ServerInformation");
    qDBusRegisterMetaType<ServerInformation>();

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
                           QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
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

uint NotificationDaemon::Notify(uint replacesId, const QString &appName, const QString &appIcon,
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

NotificationDaemon::ServerInformation NotificationDaemon::GetServerInformation() const
{
    return {QStringLiteral("NewTeller"), QStringLiteral("fourtexec"),
            QStringLiteral(NEWTELLER_VERSION), QStringLiteral("1.2")};
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
    if (replacesId > 0)
        dismiss(replacesId, kReasonClosed);

    const bool critical = hints.value(QStringLiteral("urgency")).toInt() >= kUrgencyCritical;
    const bool dnd = m_settings->value(QStringLiteral("notifications.doNotDisturb"), false).toBool()
                     && !critical;

    QVariantMap entry;
    const uint id = m_nextId++;
    entry.insert(QStringLiteral("id"), id);
    entry.insert(QStringLiteral("app"), appName.isEmpty() ? QStringLiteral("NewTeller") : appName);
    entry.insert(QStringLiteral("summary"), summary);
    entry.insert(QStringLiteral("body"), body);
    entry.insert(QStringLiteral("icon"), appIcon);
    entry.insert(QStringLiteral("actions"), actions);
    entry.insert(QStringLiteral("urgent"), critical);
    entry.insert(QStringLiteral("time"), QDateTime::currentDateTime().toMSecsSinceEpoch());

    // -1 means "daemon default", 0 means sticky; both come from the settings.
    int timeout = expireTimeout;
    if (timeout < 0)
        timeout = m_settings->value(QStringLiteral("notifications.duration"), 6).toInt() * 1000;
    if (critical && timeout <= 0)
        timeout = 0; // critical notifications never expire on their own
    entry.insert(QStringLiteral("timeout"), timeout);

    m_history.append(entry);
    trimHistory();
    emit historyChanged();

    if (!dnd && m_settings->value(QStringLiteral("notifications.enabled"), true).toBool()) {
        if (m_active.size() >= kMaxActive)
            dismiss(m_active.first().toMap().value(QStringLiteral("id")).toUInt(), kReasonExpired);
        m_active.append(entry);
        emit activeChanged();
    }
    return id;
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
    dismiss(id, kReasonDismissed);
}

void NotificationDaemon::clearHistory()
{
    m_history.clear();
    emit historyChanged();
}

void NotificationDaemon::trimHistory()
{
    const int limit = qBound(5, m_settings->value(QStringLiteral("notifications.historyLimit"), 20).toInt(), 200);
    while (m_history.size() > limit)
        m_history.removeFirst();
}
