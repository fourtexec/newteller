#include "bluetoothagent.h"
#include "notifications.h"

#include <QDBusPendingCall>
#include <QDBusServiceWatcher>
#include <QRandomGenerator>
#include <QVariant>
#include <QtGlobal>

namespace {
const QString kBluez = QStringLiteral("org.bluez");
const QString kRejected = QStringLiteral("org.bluez.Error.Rejected");
const QString kCanceled = QStringLiteral("org.bluez.Error.Canceled");
const QString kAccept = QStringLiteral("pair");
const QString kRefuse = QStringLiteral("cancel");
} // namespace

QString BluetoothAgent::objectPath()
{
    return QStringLiteral("/org/newteller/BluetoothAgent");
}

BluetoothAgent::BluetoothAgent(NotificationDaemon *notifications, const QDBusConnection &bus,
                               QObject *parent)
    : QObject(parent)
    , m_notifications(notifications)
    , m_bus(bus)
{
    if (!m_bus.isConnected() || !m_notifications)
        return;
    m_bus.registerObject(objectPath(), this, QDBusConnection::ExportScriptableSlots);
    connect(m_notifications, &NotificationDaemon::ActionInvoked, this,
            [this](uint id, const QString &key) { answered(id, key == kAccept); });
    connect(m_notifications, &NotificationDaemon::NotificationClosed, this,
            [this](uint id, uint) { answered(id, false); });
    // bluetoothd may start after the shell or restart: register every time.
    auto *watcher = new QDBusServiceWatcher(kBluez, m_bus, QDBusServiceWatcher::WatchForRegistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &BluetoothAgent::registerWithBluez);
    registerWithBluez();
}

BluetoothAgent::~BluetoothAgent()
{
    if (m_bus.isConnected())
        m_bus.unregisterObject(objectPath());
}

void BluetoothAgent::registerWithBluez()
{
    QDBusMessage add = QDBusMessage::createMethodCall(kBluez, QStringLiteral("/org/bluez"),
                                                      QStringLiteral("org.bluez.AgentManager1"),
                                                      QStringLiteral("RegisterAgent"));
    add << QVariant::fromValue(QDBusObjectPath(objectPath())) << QStringLiteral("KeyboardDisplay");
    m_bus.asyncCall(add);
    QDBusMessage makeDefault = QDBusMessage::createMethodCall(kBluez, QStringLiteral("/org/bluez"),
                                                              QStringLiteral("org.bluez.AgentManager1"),
                                                              QStringLiteral("RequestDefaultAgent"));
    makeDefault << QVariant::fromValue(QDBusObjectPath(objectPath()));
    m_bus.asyncCall(makeDefault);
}

QString BluetoothAgent::deviceName(const QDBusObjectPath &device) const
{
    QDBusMessage get = QDBusMessage::createMethodCall(kBluez, device.path(),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    get << QStringLiteral("org.bluez.Device1") << QStringLiteral("Alias");
    const QDBusMessage reply = m_bus.call(get, QDBus::Block, 500);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        const QString name = qvariant_cast<QDBusVariant>(reply.arguments().constFirst()).variant().toString();
        if (!name.isEmpty())
            return name;
    }
    // /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF -> AA:BB:CC:DD:EE:FF
    return device.path().section(QLatin1Char('/'), -1).mid(4).replace(QLatin1Char('_'), QLatin1Char(':'));
}

void BluetoothAgent::ask(const QString &summary, const QString &body)
{
    QDBusMessage call = message();
    setDelayedReply(true);
    const uint id = m_notifications->sendWithActions(QStringLiteral("Bluetooth"), summary, body,
                                                     QStringLiteral("bluetooth"),
                                                     {kAccept, tr("Pair"), kRefuse, tr("Cancel")});
    m_pending.insert(id, call);
}

void BluetoothAgent::answered(uint id, bool accept)
{
    if (!m_pending.contains(id))
        return;
    const QDBusMessage call = m_pending.take(id);
    if (accept)
        m_bus.send(call.createReply());
    else
        m_bus.send(call.createErrorReply(kRejected, tr("Pairing was refused.")));
}

void BluetoothAgent::Release()
{
}

// Legacy (PIN) pairing of a keyboard: the code is typed on the device.
QString BluetoothAgent::RequestPinCode(const QDBusObjectPath &device)
{
    const QString pin = QStringLiteral("%1").arg(QRandomGenerator::global()->bounded(1000000), 6, 10,
                                                  QLatin1Char('0'));
    DisplayPinCode(device, pin);
    return pin;
}

void BluetoothAgent::DisplayPinCode(const QDBusObjectPath &device, const QString &pincode)
{
    m_codeNotification = m_notifications->send(
        QStringLiteral("Bluetooth"), tr("Pairing with %1").arg(deviceName(device)),
        tr("Type %1 on the device and press Enter.").arg(pincode), QStringLiteral("bluetooth"), 0);
}

uint BluetoothAgent::RequestPasskey(const QDBusObjectPath &device)
{
    const uint passkey = QRandomGenerator::global()->bounded(1000000u);
    DisplayPasskey(device, passkey, 0);
    return passkey;
}

// Sent again for every key typed on the device; show it once.
void BluetoothAgent::DisplayPasskey(const QDBusObjectPath &device, uint passkey, ushort entered)
{
    if (entered > 0)
        return;
    DisplayPinCode(device, QStringLiteral("%1").arg(passkey, 6, 10, QLatin1Char('0')));
}

// Numeric comparison (phones, computers): both sides show the same code.
void BluetoothAgent::RequestConfirmation(const QDBusObjectPath &device, uint passkey)
{
    ask(tr("Pair with %1?").arg(deviceName(device)),
        tr("Check that %1 shows the code %2.")
            .arg(deviceName(device), QStringLiteral("%1").arg(passkey, 6, 10, QLatin1Char('0'))));
}

// A device that started the pairing itself.
void BluetoothAgent::RequestAuthorization(const QDBusObjectPath &device)
{
    ask(tr("Pair with %1?").arg(deviceName(device)), tr("The device asks to pair with this computer."));
}

// Services of a paired device (audio, input) are allowed.
void BluetoothAgent::AuthorizeService(const QDBusObjectPath &device, const QString &uuid)
{
    Q_UNUSED(device);
    Q_UNUSED(uuid);
}

void BluetoothAgent::Cancel()
{
    // BlueZ gave up (timeout, the device cancelled): close every question.
    const QList<uint> ids = m_pending.keys();
    for (uint id : ids) {
        const QDBusMessage call = m_pending.take(id);
        m_bus.send(call.createErrorReply(kCanceled, tr("Pairing was cancelled.")));
        m_notifications->dismiss(id, 3);
    }
    if (m_codeNotification) {
        m_notifications->dismiss(m_codeNotification, 3);
        m_codeNotification = 0;
    }
}
