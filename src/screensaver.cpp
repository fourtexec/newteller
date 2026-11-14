#include "screensaver.h"

#include <QDateTime>

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QtGlobal>

namespace {
const QString kService = QStringLiteral("org.freedesktop.ScreenSaver");
// Applications call either path; KDE and GNOME answer on both.
const QString kPaths[] = {QStringLiteral("/ScreenSaver"), QStringLiteral("/org/freedesktop/ScreenSaver")};
} // namespace

ScreenSaverService::ScreenSaverService(bool registerOnBus, QObject *parent)
    : QObject(parent)
{
    if (!registerOnBus)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    if (!bus.registerService(kService)) {
        qInfo("newteller: org.freedesktop.ScreenSaver is served elsewhere: %s",
              qPrintable(bus.lastError().message()));
        return;
    }
    const auto options = QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals;
    for (const QString &path : kPaths)
        bus.registerObject(path, this, options);
    m_registered = true;

    m_watcher.setConnection(bus);
    m_watcher.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &ScreenSaverService::release);
}

ScreenSaverService::~ScreenSaverService()
{
    if (!m_registered)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    for (const QString &path : kPaths)
        bus.unregisterObject(path);
    bus.unregisterService(kService);
}

uint ScreenSaverService::Inhibit(const QString &application, const QString &reason)
{
    const QString owner = calledFromDBus() ? message().service() : QString();
    const bool wasInhibited = inhibited();
    uint cookie = m_nextCookie++;
    if (cookie == 0) // 0 means "no cookie" to some callers
        cookie = m_nextCookie++;
    m_inhibitors.insert(cookie, Inhibitor{owner, application, reason});
    if (!owner.isEmpty())
        m_watcher.addWatchedService(owner);
    qInfo("newteller: %s keeps the screen awake (%s)", qPrintable(application), qPrintable(reason));
    if (!wasInhibited)
        emit inhibitedChanged(true);
    return cookie;
}

void ScreenSaverService::UnInhibit(uint cookie)
{
    // Only the caller that took a cookie may hand it back.
    const auto it = m_inhibitors.constFind(cookie);
    if (it == m_inhibitors.constEnd())
        return;
    if (calledFromDBus() && it->owner != message().service())
        return;
    removeCookie(cookie);
}

void ScreenSaverService::removeCookie(uint cookie)
{
    const Inhibitor inhibitor = m_inhibitors.take(cookie);
    bool ownerStillInhibits = false;
    for (const Inhibitor &other : std::as_const(m_inhibitors)) {
        if (other.owner == inhibitor.owner) {
            ownerStillInhibits = true;
            break;
        }
    }
    if (!ownerStillInhibits && !inhibitor.owner.isEmpty())
        m_watcher.removeWatchedService(inhibitor.owner);
    if (m_inhibitors.isEmpty())
        emit inhibitedChanged(false);
}

void ScreenSaverService::release(const QString &owner)
{
    const QList<uint> cookies = m_inhibitors.keys();
    for (uint cookie : cookies) {
        if (m_inhibitors.value(cookie).owner == owner)
            removeCookie(cookie);
    }
}

void ScreenSaverService::Lock()
{
    emit lockRequested();
}

void ScreenSaverService::SimulateUserActivity()
{
    // swayidle restarts its timers on real input only; nothing to do.
}

void ScreenSaverService::setLocked(bool locked)
{
    if (m_active == locked)
        return;
    m_active = locked;
    m_activeSince = locked ? QDateTime::currentMSecsSinceEpoch() : 0;
    if (m_registered)
        emit ActiveChanged(locked);
}

bool ScreenSaverService::GetActive()
{
    return m_active;
}

bool ScreenSaverService::SetActive(bool active)
{
    Q_UNUSED(active);
    return false;
}

uint ScreenSaverService::GetActiveTime()
{
    // Seconds the screen has been locked, 0 when it is not.
    if (!m_active)
        return 0;
    return uint((QDateTime::currentMSecsSinceEpoch() - m_activeSince) / 1000);
}

uint ScreenSaverService::GetSessionIdleTime()
{
    return 0;
}
