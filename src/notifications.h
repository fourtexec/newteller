#pragma once

#include <QHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class Settings;

// The notification system of the shell. Two entry points feed one queue:
//
//   * org.freedesktop.Notifications on the session D-Bus: the shell claims
//     the well-known name (when no other daemon owns it), so `notify-send`
//     and every conforming application reach the shell without configuration.
//   * A private Unix socket in $XDG_RUNTIME_DIR/newteller/notify.sock that
//     `newteller-ctl notify` writes one JSON object per line into.
//
// The popups themselves are QML (qml/Notifications.qml, one window per
// screen); this class only owns the queue, the history and the protocol.
// Exposed to QML as the context property `Notifications`.
class NotificationDaemon : public QObject {
    Q_OBJECT
    // The D-Bus interface name callers use (notify-send, libnotify, browsers).
    // Without it QtDBus publishes "local.<app>.NotificationDaemon" and every
    // call to org.freedesktop.Notifications fails with UnknownInterface.
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
    // The notifications that should be on screen right now:
    // { id, app, summary, body, icon, actions, timeout, urgent, time }
    Q_PROPERTY(QVariantList active READ active NOTIFY activeChanged)
    // The most recent notifications, oldest last, capped by notifications.historyLimit.
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    // True when the shell owns org.freedesktop.Notifications on the session bus.
    Q_PROPERTY(bool ownsDBus READ ownsDBus NOTIFY dbusChanged)
    // True when a session bus exists at all (the daemon then simply waits).
    Q_PROPERTY(bool dbusAvailable READ dbusAvailable CONSTANT)

public:
    explicit NotificationDaemon(Settings *settings, QObject *parent = nullptr);
    ~NotificationDaemon() override;

    QVariantList active() const { return m_active; }
    QVariantList history() const { return m_history; }
    bool ownsDBus() const { return m_ownsDBus; }
    bool dbusAvailable() const { return m_dbusAvailable; }

    // Pushes a notification into the queue (the built-in test button and
    // Shell.notify use this path).
    // A notification with action buttons (actions: key, label, key,
    // label...); the answer arrives as ActionInvoked or NotificationClosed.
    // Urgent ones (Bluetooth pairing) get through do-not-disturb and stay
    // until answered; others (a new drive) expire like any notification.
    uint sendWithActions(const QString &appName, const QString &summary, const QString &body,
                         const QString &icon, const QStringList &actions, bool urgent = true,
                         int expireTimeoutMs = -1);
    Q_INVOKABLE uint send(const QString &appName, const QString &summary, const QString &body,
                          const QString &icon = QString(), int expireTimeoutMs = -1);
    // Removes a popup: reason 1 = expired, 2 = dismissed by the user,
    // 3 = closed by a call, 4 = undefined (spec values).
    Q_INVOKABLE void dismiss(uint id, uint reason = 2);
    // The user clicked one of the `actions` buttons of a popup.
    Q_INVOKABLE void invokeAction(uint id, const QString &actionKey);
    Q_INVOKABLE void clearHistory();
    // Removes one entry from the history (the X button in the notification
    // centre). Notifications still on screen are dismissed first.
    Q_INVOKABLE void removeHistory(uint id);

    // The socket `newteller-ctl notify` writes into.
    static QString socketPath();

signals:
    // org.freedesktop.Notifications signals (the only ones on D-Bus).
    Q_SCRIPTABLE void NotificationClosed(uint id, uint reason);
    Q_SCRIPTABLE void ActionInvoked(uint id, const QString &actionKey);

    void activeChanged();
    void historyChanged();
    void dbusChanged();

public slots:
    // --- org.freedesktop.Notifications methods -------------------------------
    // Q_SCRIPTABLE: only these are exported on the bus.
    // Spec order: app_name first, then replaces_id (signature susssasa{sv}i).
    Q_SCRIPTABLE uint Notify(const QString &appName, uint replacesId, const QString &appIcon,
                             const QString &summary, const QString &body, const QStringList &actions,
                             const QVariantMap &hints, int expireTimeout);
    Q_SCRIPTABLE void CloseNotification(uint id);
    Q_SCRIPTABLE QStringList GetCapabilities() const;
    // Four out arguments (name, vendor, version, spec version), as the
    // specification has it - not one struct.
    Q_SCRIPTABLE QString GetServerInformation(QString &vendor, QString &version,
                                              QString &specVersion) const;

private:
    uint publish(uint replacesId, const QString &appName, const QString &appIcon,
                 const QString &summary, const QString &body, const QStringList &actions,
                 const QVariantMap &hints, int expireTimeout);
    void setupDBus();
    void teardownDBus();
    void setupSocket();
    void readSocket();
    void trimHistory();
    void playSound(const QVariantMap &hints);
    bool scheduledQuiet() const;

    Settings *m_settings;
    QVariantList m_active;
    QVariantList m_history;
    bool m_dbusAvailable = false;
    bool m_ownsDBus = false;
    bool m_registeredObject = false;
    uint m_nextId = 1;
    // Bumped on every publish, so a replaced popup restarts its timer.
    uint m_revision = 0;
    qint64 m_lastSound = 0; // ms since epoch; at most two sounds a second
    QLocalServer *m_server = nullptr;
    // Partial lines per connected sender, until a newline completes one JSON object.
    QHash<QLocalSocket *, QByteArray> m_buffered;
};

