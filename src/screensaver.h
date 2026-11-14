#pragma once

#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QObject>
#include <QString>

// org.freedesktop.ScreenSaver: video players, browsers and XWayland
// applications (which cannot use the Wayland idle-inhibit protocol) ask here
// to keep the screen awake while they play. Every Inhibit() holds until
// UnInhibit() or until the caller leaves the bus, so a crashed player never
// keeps the screen on. main.cpp relays inhibitedChanged() to the compositor
// ("idle-inhibit on|off" through SessionServices).
class ScreenSaverService : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.ScreenSaver")

public:
    explicit ScreenSaverService(bool registerOnBus, QObject *parent = nullptr);
    ~ScreenSaverService() override;

    bool registered() const { return m_registered; }
    bool inhibited() const { return !m_inhibitors.isEmpty(); }
    // The session's lock state, which is what "active" means here: browsers
    // and chat applications ask GetActive or listen to ActiveChanged to
    // tell that the user is away.
    void setLocked(bool locked);

public slots:
    Q_SCRIPTABLE uint Inhibit(const QString &application, const QString &reason);
    Q_SCRIPTABLE void UnInhibit(uint cookie);
    Q_SCRIPTABLE void Lock();
    Q_SCRIPTABLE void SimulateUserActivity();
    Q_SCRIPTABLE bool GetActive();
    Q_SCRIPTABLE bool SetActive(bool active);
    Q_SCRIPTABLE uint GetActiveTime();
    Q_SCRIPTABLE uint GetSessionIdleTime();

signals:
    void inhibitedChanged(bool inhibited);
    void lockRequested();
    Q_SCRIPTABLE void ActiveChanged(bool active);

private:
    struct Inhibitor {
        QString owner; // unique bus name of the caller
        QString application;
        QString reason;
    };

    void release(const QString &owner);
    void removeCookie(uint cookie);

    QHash<uint, Inhibitor> m_inhibitors;
    uint m_nextCookie = 1;
    bool m_active = false;
    qint64 m_activeSince = 0;
    QDBusServiceWatcher m_watcher;
    bool m_registered = false;
};
