#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class NotificationDaemon;
class Settings;

// Removable drives through UDisks2. Without a file manager's volume monitor
// (gvfs) nothing happens when a USB stick is plugged in; this announces a
// new filesystem on a removable drive with "Open" (mounts it if needed and
// opens the folder) and "Eject" (unmounts and powers the drive off).
// Only drives plugged in while the shell runs are announced, and only when
// drives.notify is on. Mounting goes through UDisks2, so polkit decides:
// an active local session mounts without a password.
class DriveMonitor : public QObject {
    Q_OBJECT

public:
    // bus: the system bus in the session; tests pass the session bus.
    DriveMonitor(NotificationDaemon *notifications, Settings *settings, const QDBusConnection &bus,
                 QObject *parent = nullptr);

private slots:
    // org.freedesktop.DBus.ObjectManager signals of UDisks2, read from the
    // raw message so a malformed one cannot abort the shell.
    void interfacesAdded(const QDBusMessage &message);
    void interfacesRemoved(const QDBusMessage &message);

private:
    struct Volume {
        QString block;  // /org/freedesktop/UDisks2/block_devices/sdb1
        QString drive;  // /org/freedesktop/UDisks2/drives/...
        QString label;  // what the notification calls it
    };

    void inspect(const QString &block);
    void announce(const Volume &volume);
    void answered(uint id, const QString &key);
    void open(const Volume &volume);
    void eject(const Volume &volume);
    void unmountAll(const Volume &volume, QStringList blocks);
    void powerOff(const Volume &volume);
    void failed(const QString &summary, const QDBusMessage &reply);

    NotificationDaemon *m_notifications;
    Settings *m_settings;
    QDBusConnection m_bus;
    // Notifications with Open/Eject buttons: id -> the volume they are about.
    QHash<uint, Volume> m_notes;
};
