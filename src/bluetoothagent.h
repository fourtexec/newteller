#pragma once

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QHash>
#include <QObject>
#include <QString>

class NotificationDaemon;

// The BlueZ pairing agent (org.bluez.Agent1, capability KeyboardDisplay).
// Without an agent BlueZ only pairs devices that need no confirmation
// (headphones, mice); keyboards that must type a code and phones that
// compare one were refused. Codes and questions are shell notifications:
// a code to type is shown, a code to compare gets "Pair" / "Cancel" and
// BlueZ waits for the answer (a delayed D-Bus reply).
class BluetoothAgent : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")

public:
    // bus: the system bus in the session; tests pass the session bus.
    BluetoothAgent(NotificationDaemon *notifications, const QDBusConnection &bus,
                   QObject *parent = nullptr);
    ~BluetoothAgent() override;

    static QString objectPath();

public slots:
    Q_SCRIPTABLE void Release();
    Q_SCRIPTABLE QString RequestPinCode(const QDBusObjectPath &device);
    Q_SCRIPTABLE void DisplayPinCode(const QDBusObjectPath &device, const QString &pincode);
    Q_SCRIPTABLE uint RequestPasskey(const QDBusObjectPath &device);
    Q_SCRIPTABLE void DisplayPasskey(const QDBusObjectPath &device, uint passkey, ushort entered);
    Q_SCRIPTABLE void RequestConfirmation(const QDBusObjectPath &device, uint passkey);
    Q_SCRIPTABLE void RequestAuthorization(const QDBusObjectPath &device);
    Q_SCRIPTABLE void AuthorizeService(const QDBusObjectPath &device, const QString &uuid);
    Q_SCRIPTABLE void Cancel();

private slots:
    // org.freedesktop.DBus.Properties.PropertiesChanged of the device that
    // shows a code: the code goes away once the device is paired.
    // Takes the raw message: typed arguments make Qt abort on a malformed one.
    void devicePropertiesChanged(const QDBusMessage &message);

private:
    void registerWithBluez();
    void closeCode();
    QString deviceName(const QDBusObjectPath &device) const;
    // Shows a question and keeps the D-Bus call open until it is answered.
    void ask(const QString &summary, const QString &body);
    void answered(uint id, bool accept);

    NotificationDaemon *m_notifications;
    QDBusConnection m_bus;
    // Open questions: notification id -> the BlueZ call waiting for it.
    QHash<uint, QDBusMessage> m_pending;
    uint m_codeNotification = 0;
    QString m_codeDevice;
};
