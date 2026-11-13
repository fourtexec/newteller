#include "drivemonitor.h"
#include "notifications.h"
#include "settings.h"

#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDesktopServices>
#include <QLocale>
#include <QUrl>

namespace {
const QString kUDisks = QStringLiteral("org.freedesktop.UDisks2");
const QString kRoot = QStringLiteral("/org/freedesktop/UDisks2");
const QString kBlockDevices = QStringLiteral("/org/freedesktop/UDisks2/block_devices/");
const QString kBlock = QStringLiteral("org.freedesktop.UDisks2.Block");
const QString kFilesystem = QStringLiteral("org.freedesktop.UDisks2.Filesystem");
const QString kDrive = QStringLiteral("org.freedesktop.UDisks2.Drive");
const QString kObjectManager = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString kProperties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kOpen = QStringLiteral("open");
const QString kEject = QStringLiteral("eject");
// Mounting can wait for a polkit password prompt.
constexpr int kMountTimeoutMs = 120000;

// The a{sv} of a GetAll reply (empty for an error or a malformed answer).
QVariantMap propertiesOf(const QDBusMessage &reply)
{
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()
        || !reply.arguments().constFirst().canConvert<QDBusArgument>())
        return {};
    return qdbus_cast<QVariantMap>(reply.arguments().constFirst());
}

// The first of MountPoints (aay), without the trailing NUL UDisks keeps in
// each path; empty when the filesystem is not mounted.
QString firstMountPoint(const QVariant &value)
{
    if (!value.canConvert<QDBusArgument>())
        return {};
    const QList<QByteArray> points = qdbus_cast<QList<QByteArray>>(value);
    for (QByteArray point : points) {
        while (point.endsWith('\0'))
            point.chop(1);
        if (!point.isEmpty())
            return QString::fromLocal8Bit(point);
    }
    return {};
}

// The same from a Properties.Get reply.
QString firstMountPoint(const QDBusMessage &reply)
{
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    return firstMountPoint(qvariant_cast<QDBusVariant>(reply.arguments().constFirst()).variant());
}
} // namespace

DriveMonitor::DriveMonitor(NotificationDaemon *notifications, Settings *settings,
                           const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_notifications(notifications)
    , m_settings(settings)
    , m_bus(bus)
{
    if (!m_bus.isConnected() || !m_notifications || !m_settings)
        return;
    m_bus.connect(kUDisks, kRoot, kObjectManager, QStringLiteral("InterfacesAdded"), this,
                  SLOT(interfacesAdded(QDBusMessage)));
    m_bus.connect(kUDisks, kRoot, kObjectManager, QStringLiteral("InterfacesRemoved"), this,
                  SLOT(interfacesRemoved(QDBusMessage)));
    connect(m_notifications, &NotificationDaemon::ActionInvoked, this, &DriveMonitor::answered);
    connect(m_notifications, &NotificationDaemon::NotificationClosed, this,
            [this](uint id, uint) { m_notes.remove(id); });
    // udisksd is started on demand: wake it so its signals arrive.
    m_bus.asyncCall(QDBusMessage::createMethodCall(kUDisks, kRoot, QStringLiteral("org.freedesktop.DBus.Peer"),
                                                   QStringLiteral("Ping")));
}

void DriveMonitor::interfacesAdded(const QDBusMessage &message)
{
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2 || !args.at(1).canConvert<QDBusArgument>())
        return;
    const QString path = qvariant_cast<QDBusObjectPath>(args.at(0)).path();
    if (!path.startsWith(kBlockDevices))
        return;
    // a{sa{sv}}: only the interface names matter here. A filesystem appears
    // once the new partition was probed, sometimes after the block itself.
    const QDBusArgument interfaces = args.at(1).value<QDBusArgument>();
    if (interfaces.currentSignature() != QLatin1String("a{sa{sv}}"))
        return;
    bool filesystem = false;
    interfaces.beginMap();
    while (!interfaces.atEnd()) {
        QString name;
        QVariantMap properties;
        interfaces.beginMapEntry();
        interfaces >> name >> properties;
        interfaces.endMapEntry();
        filesystem = filesystem || name == kFilesystem;
    }
    interfaces.endMap();
    if (filesystem && m_settings->value(QStringLiteral("drives.notify"), true).toBool())
        inspect(path);
}

void DriveMonitor::interfacesRemoved(const QDBusMessage &message)
{
    const QList<QVariant> args = message.arguments();
    if (args.isEmpty())
        return;
    // The stick was pulled out: its question has no answer any more.
    const QString path = qvariant_cast<QDBusObjectPath>(args.at(0)).path();
    const QList<uint> ids = m_notes.keys();
    for (uint id : ids) {
        if (m_notes.value(id).block == path) {
            m_notes.remove(id);
            m_notifications->CloseNotification(id);
        }
    }
}

void DriveMonitor::inspect(const QString &block)
{
    QDBusMessage get = QDBusMessage::createMethodCall(kUDisks, block, kProperties, QStringLiteral("GetAll"));
    get << kBlock;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(get), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, block](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QVariantMap props = propertiesOf(w->reply());
        const QString drive = qvariant_cast<QDBusObjectPath>(props.value(QStringLiteral("Drive"))).path();
        // Internal disks are "system" devices; loop devices have no drive.
        if (props.isEmpty() || props.value(QStringLiteral("HintIgnore")).toBool()
            || props.value(QStringLiteral("HintSystem")).toBool() || drive.isEmpty() || drive == QLatin1String("/"))
            return;
        const QString label = props.value(QStringLiteral("IdLabel")).toString().trimmed();
        const qulonglong size = props.value(QStringLiteral("Size")).toULongLong();

        QDBusMessage driveGet = QDBusMessage::createMethodCall(kUDisks, drive, kProperties,
                                                               QStringLiteral("GetAll"));
        driveGet << kDrive;
        auto *driveWatcher = new QDBusPendingCallWatcher(m_bus.asyncCall(driveGet), this);
        connect(driveWatcher, &QDBusPendingCallWatcher::finished, this,
                [this, block, drive, label, size](QDBusPendingCallWatcher *d) {
                    d->deleteLater();
                    const QVariantMap driveProps = propertiesOf(d->reply());
                    if (!driveProps.value(QStringLiteral("Removable")).toBool())
                        return;
                    Volume volume{block, drive, label};
                    if (volume.label.isEmpty()) {
                        const QString model = QStringLiteral("%1 %2")
                                                  .arg(driveProps.value(QStringLiteral("Vendor")).toString(),
                                                       driveProps.value(QStringLiteral("Model")).toString())
                                                  .simplified();
                        volume.label = model.isEmpty() ? tr("%1 drive").arg(QLocale().formattedDataSize(qint64(size)))
                                                       : model;
                    }
                    announce(volume);
                });
    });
}

void DriveMonitor::announce(const Volume &volume)
{
    const uint id = m_notifications->sendWithActions(
        tr("Drives"), tr("%1 connected").arg(volume.label), tr("Open it or eject it safely."),
        QStringLiteral("usb"), {kOpen, tr("Open"), kEject, tr("Eject")}, false, 15000);
    m_notes.insert(id, volume);
}

void DriveMonitor::answered(uint id, const QString &key)
{
    if (!m_notes.contains(id))
        return;
    const Volume volume = m_notes.take(id);
    if (key == kOpen)
        open(volume);
    else if (key == kEject)
        eject(volume);
}

void DriveMonitor::open(const Volume &volume)
{
    QDBusMessage get = QDBusMessage::createMethodCall(kUDisks, volume.block, kProperties, QStringLiteral("Get"));
    get << kFilesystem << QStringLiteral("MountPoints");
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(get), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, volume](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QString mounted = firstMountPoint(w->reply());
        if (!mounted.isEmpty()) {
            openFolder(mounted);
            return;
        }
        QDBusMessage mount = QDBusMessage::createMethodCall(kUDisks, volume.block, kFilesystem,
                                                            QStringLiteral("Mount"));
        mount << QVariantMap();
        auto *mountWatcher = new QDBusPendingCallWatcher(m_bus.asyncCall(mount, kMountTimeoutMs), this);
        connect(mountWatcher, &QDBusPendingCallWatcher::finished, this, [this, volume](QDBusPendingCallWatcher *m) {
            m->deleteLater();
            const QDBusMessage reply = m->reply();
            const QString path = reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()
                                     ? reply.arguments().constFirst().toString()
                                     : QString();
            if (path.isEmpty())
                failed(tr("Could not open %1").arg(volume.label), reply);
            else
                openFolder(path);
        });
    });
}

void DriveMonitor::openFolder(const QString &path)
{
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        return;
    m_notifications->send(tr("Drives"), tr("Could not open %1").arg(path),
                          tr("No application is set up to open it. Install xdg-utils and a file manager."),
                          QStringLiteral("usb"));
}

void DriveMonitor::eject(const Volume &volume)
{
    // Every mounted filesystem of the drive: a stick can carry several
    // partitions, and it is only safe to pull out once all are unmounted.
    QDBusMessage list = QDBusMessage::createMethodCall(kUDisks, kRoot, kObjectManager,
                                                       QStringLiteral("GetManagedObjects"));
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(list), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, volume](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        QStringList mounted;
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()
            && reply.arguments().constFirst().canConvert<QDBusArgument>()) {
            const QDBusArgument objects = reply.arguments().constFirst().value<QDBusArgument>();
            if (objects.currentSignature() == QLatin1String("a{oa{sa{sv}}}")) {
                objects.beginMap();
                while (!objects.atEnd()) {
                    QDBusObjectPath path;
                    QMap<QString, QVariantMap> interfaces;
                    objects.beginMapEntry();
                    objects >> path >> interfaces;
                    objects.endMapEntry();
                    if (!path.path().startsWith(kBlockDevices) || !interfaces.contains(kFilesystem))
                        continue;
                    const QString drive =
                        qvariant_cast<QDBusObjectPath>(interfaces.value(kBlock).value(QStringLiteral("Drive"))).path();
                    if (drive == volume.drive
                        && !firstMountPoint(interfaces.value(kFilesystem).value(QStringLiteral("MountPoints"))).isEmpty())
                        mounted << path.path();
                }
                objects.endMap();
            } else {
                mounted << volume.block;
            }
        } else {
            mounted << volume.block; // no list: at least the one we know
        }
        unmountAll(volume, mounted);
    });
}

void DriveMonitor::unmountAll(const Volume &volume, QStringList blocks)
{
    if (blocks.isEmpty()) {
        powerOff(volume);
        return;
    }
    const QString block = blocks.takeFirst();
    QDBusMessage unmount = QDBusMessage::createMethodCall(kUDisks, block, kFilesystem, QStringLiteral("Unmount"));
    unmount << QVariantMap();
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(unmount, kMountTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, volume, blocks](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        // Busy: a file is still open there, and the drive stays mounted.
        if (reply.type() == QDBusMessage::ErrorMessage
            && reply.errorName() != QLatin1String("org.freedesktop.UDisks2.Error.NotMounted")) {
            failed(tr("Could not eject %1").arg(volume.label), reply);
            return;
        }
        unmountAll(volume, blocks);
    });
}

void DriveMonitor::powerOff(const Volume &volume)
{
    // Unmounted, so already safe to pull out; powering the drive off also
    // makes its light go dark. Card readers cannot do that and only eject.
    QDBusMessage off = QDBusMessage::createMethodCall(kUDisks, volume.drive, kDrive, QStringLiteral("PowerOff"));
    off << QVariantMap();
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(off, kMountTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, volume](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->reply().type() == QDBusMessage::ErrorMessage) {
            QDBusMessage eject = QDBusMessage::createMethodCall(kUDisks, volume.drive, kDrive,
                                                                QStringLiteral("Eject"));
            eject << QVariantMap();
            m_bus.asyncCall(eject, kMountTimeoutMs);
        }
        m_notifications->send(tr("Drives"), tr("%1 can be removed").arg(volume.label), QString(),
                              QStringLiteral("usb"));
    });
}

void DriveMonitor::failed(const QString &summary, const QDBusMessage &reply)
{
    // A polkit refusal the user chose is not worth a second notification.
    if (reply.errorName() == QLatin1String("org.freedesktop.UDisks2.Error.NotAuthorizedDismissed"))
        return;
    QString detail = reply.errorMessage();
    if (reply.errorName() == QLatin1String("org.freedesktop.UDisks2.Error.DeviceBusy")
        || detail.contains(QLatin1String("target is busy")))
        detail = tr("A program is still using it. Close the files on it and try again.");
    m_notifications->send(tr("Drives"), summary, detail, QStringLiteral("usb"));
}
