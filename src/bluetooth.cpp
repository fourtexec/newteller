#include "bluetooth.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>

#include <algorithm>

namespace {

const QString kService = QStringLiteral("org.bluez");
const QString kAdapter = QStringLiteral("org.bluez.Adapter1");
const QString kDevice = QStringLiteral("org.bluez.Device1");
const QString kProperties = QStringLiteral("org.freedesktop.DBus.Properties");

} // namespace

Bluetooth::Bluetooth(QObject *parent)
    : QObject(parent)
{
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(150);
    connect(&m_refresh, &QTimer::timeout, this, &Bluetooth::refresh);

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected())
        return;
    // Any property change, device appearing or vanishing: read it all again.
    bus.connect(kService, QString(), kProperties, QStringLiteral("PropertiesChanged"),
                this, SLOT(scheduleRefresh()));
    bus.connect(kService, QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                QStringLiteral("InterfacesAdded"), this, SLOT(scheduleRefresh()));
    bus.connect(kService, QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                QStringLiteral("InterfacesRemoved"), this, SLOT(scheduleRefresh()));
    auto *watcher = new QDBusServiceWatcher(kService, bus,
                                            QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &Bluetooth::scheduleRefresh);
    refresh();
}

void Bluetooth::scheduleRefresh()
{
    m_refresh.start();
}

void Bluetooth::refresh()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected())
        return;
    QDBusMessage message = QDBusMessage::createMethodCall(
        kService, QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("GetManagedObjects"));
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        QString adapter;
        bool powered = false;
        bool discovering = false;
        QVariantList devices;
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            const QDBusArgument objects = reply.arguments().constFirst().value<QDBusArgument>();
            QList<QPair<QString, QVariantMap>> deviceObjects;
            objects.beginMap();
            while (!objects.atEnd()) {
                QDBusObjectPath path;
                objects.beginMapEntry();
                objects >> path;
                objects.beginMap();
                while (!objects.atEnd()) {
                    QString interface;
                    QVariantMap properties;
                    objects.beginMapEntry();
                    objects >> interface >> properties;
                    objects.endMapEntry();
                    if (interface == kAdapter && adapter.isEmpty()) {
                        adapter = path.path();
                        powered = properties.value(QStringLiteral("Powered")).toBool();
                        discovering = properties.value(QStringLiteral("Discovering")).toBool();
                    } else if (interface == kDevice) {
                        deviceObjects.append({path.path(), properties});
                    }
                }
                objects.endMap();
                objects.endMapEntry();
            }
            objects.endMap();

            for (const auto &object : deviceObjects) {
                const QVariantMap &p = object.second;
                if (p.value(QStringLiteral("Adapter")).value<QDBusObjectPath>().path() != adapter)
                    continue;
                const QString address = p.value(QStringLiteral("Address")).toString();
                QString name = p.value(QStringLiteral("Alias")).toString();
                if (name.isEmpty())
                    name = p.value(QStringLiteral("Name")).toString();
                const bool paired = p.value(QStringLiteral("Paired")).toBool();
                // Nameless strangers (BlueZ uses the address as alias) are noise.
                const bool named = p.contains(QStringLiteral("Name"));
                if (!paired && !named)
                    continue;
                QVariantMap device;
                device.insert(QStringLiteral("path"), object.first);
                device.insert(QStringLiteral("address"), address);
                device.insert(QStringLiteral("name"), name.isEmpty() ? address : name);
                device.insert(QStringLiteral("icon"), p.value(QStringLiteral("Icon")).toString());
                device.insert(QStringLiteral("paired"), paired);
                device.insert(QStringLiteral("connected"), p.value(QStringLiteral("Connected")).toBool());
                device.insert(QStringLiteral("trusted"), p.value(QStringLiteral("Trusted")).toBool());
                device.insert(QStringLiteral("rssi"), p.value(QStringLiteral("RSSI"), -200).toInt());
                devices.append(device);
            }
            std::sort(devices.begin(), devices.end(), [](const QVariant &a, const QVariant &b) {
                const QVariantMap x = a.toMap();
                const QVariantMap y = b.toMap();
                const auto rank = [](const QVariantMap &d) {
                    return d.value(QStringLiteral("connected")).toBool() ? 0
                           : d.value(QStringLiteral("paired")).toBool() ? 1 : 2;
                };
                if (rank(x) != rank(y))
                    return rank(x) < rank(y);
                return x.value(QStringLiteral("name")).toString().compare(
                           y.value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
            });
        }
        m_adapter = adapter;
        m_powered = powered;
        m_discovering = discovering;
        m_devices = devices;
        emit changed();
    });
}

void Bluetooth::call(const QString &path, const QString &interface, const QString &method,
                     const QVariantList &arguments, const QString &busyPath,
                     std::function<void()> then)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected() || path.isEmpty())
        return;
    QDBusMessage message = QDBusMessage::createMethodCall(kService, path, interface, method);
    message.setArguments(arguments);
    if (!busyPath.isEmpty()) {
        m_busy = busyPath;
        emit changed();
    }
    // Pairing waits for the other device; give it time.
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, 60000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, busyPath, then](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                const QDBusMessage reply = w->reply();
                const bool ok = reply.type() != QDBusMessage::ErrorMessage;
                if (!busyPath.isEmpty() && m_busy == busyPath) {
                    m_busy.clear();
                    emit changed();
                }
                if (ok && then) {
                    then();
                    return;
                }
                if (!busyPath.isEmpty())
                    emit operationFinished(busyPath, ok ? QString() : reply.errorMessage());
                scheduleRefresh();
            });
}

void Bluetooth::setPowered(bool on)
{
    call(m_adapter, kProperties, QStringLiteral("Set"),
         {kAdapter, QStringLiteral("Powered"), QVariant::fromValue(QDBusVariant(on))}, QString());
}

void Bluetooth::startDiscovery()
{
    if (m_powered && !m_discovering)
        call(m_adapter, kAdapter, QStringLiteral("StartDiscovery"), {}, QString());
}

void Bluetooth::stopDiscovery()
{
    if (m_discovering)
        call(m_adapter, kAdapter, QStringLiteral("StopDiscovery"), {}, QString());
}

void Bluetooth::connectDevice(const QString &path)
{
    call(path, kDevice, QStringLiteral("Connect"), {}, path);
}

void Bluetooth::disconnectDevice(const QString &path)
{
    call(path, kDevice, QStringLiteral("Disconnect"), {}, path);
}

void Bluetooth::pairDevice(const QString &path)
{
    // Pair, then trust (so it reconnects by itself), then connect.
    call(path, kDevice, QStringLiteral("Pair"), {}, path, [this, path] {
        call(path, kProperties, QStringLiteral("Set"),
             {kDevice, QStringLiteral("Trusted"), QVariant::fromValue(QDBusVariant(true))},
             QString());
        connectDevice(path);
    });
}

void Bluetooth::forgetDevice(const QString &path)
{
    call(m_adapter, kAdapter, QStringLiteral("RemoveDevice"),
         {QVariant::fromValue(QDBusObjectPath(path))}, QString());
}
