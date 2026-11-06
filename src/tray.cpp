#include "tray.h"
#include "settings.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QQuickImageProvider>
#include <QtDebug>

namespace {

constexpr auto kWatcherService = "org.kde.StatusNotifierWatcher";
constexpr auto kWatcherPath = "/StatusNotifierWatcher";
constexpr auto kItemInterface = "org.kde.StatusNotifierItem";
constexpr auto kPropsInterface = "org.freedesktop.DBus.Properties";
constexpr auto kMenuInterface = "com.canonical.dbusmenu";

// Object paths items commonly use; the startup scan probes them when an item
// registered before the shell appeared (items that start later call
// RegisterStatusNotifierItem themselves once the watcher name shows up).
QStringList candidatePaths()
{
    return {QStringLiteral("/StatusNotifierItem"),
            QStringLiteral("/org/kde/statusnotifieritem"),
            QStringLiteral("/org/freedesktop/StatusNotifierItem"),
            QStringLiteral("/StatusBar")};
}

// The IconPixmap property: array of structs {int width, int height, array of
// bytes}. Both Qt (QSystemTrayIcon) and libappindicator serialise the pixels
// as plain host-order ARGB32 rows, which is exactly QImage::Format_ARGB32, so
// the bytes map directly; the largest icon wins.
QImage decodePixmap(const QVariant &value)
{
    if (!value.isValid() || !value.canConvert<QDBusArgument>())
        return QImage();
    const QDBusArgument array = value.value<QDBusArgument>();
    if (array.currentType() != QDBusArgument::ArrayType)
        return QImage();
    QImage best;
    array.beginArray();
    while (!array.atEnd()) {
        const QDBusArgument entry = array;
        entry.beginStructure();
        qint32 width = 0;
        qint32 height = 0;
        entry >> width >> height;
        QByteArray bytes;
        entry >> bytes;
        entry.endStructure();
        if (width > 0 && height > 0 && bytes.size() >= qint64(width) * height * 4) {
            const QImage image(reinterpret_cast<const uchar *>(bytes.constData()), width, height,
                               width * 4, QImage::Format_ARGB32);
            if (image.width() * image.height() > best.width() * best.height())
                best = image.copy(); // deep copy: `bytes` dies with the reply
        }
    }
    array.endArray();
    return best;
}

// ToolTip type: (sa(iiay)ss) - icon name, icon pixmap, title, description.
void decodeToolTip(const QVariant &value, QString *title, QString *body)
{
    if (!value.isValid() || !value.canConvert<QDBusArgument>())
        return;
    const QDBusArgument structure = value.value<QDBusArgument>();
    if (structure.currentType() != QDBusArgument::StructureType)
        return;
    structure.beginStructure();
    QString name;
    structure >> name; // icon name, unused here
    QVariant pixmapVariant;
    structure >> pixmapVariant; // the (iiay) array, unused here
    QString tipTitle;
    structure >> tipTitle;
    QString tipBody;
    structure >> tipBody;
    structure.endStructure();
    if (title)
        *title = tipTitle;
    if (body)
        *body = tipBody;
}

// --- com.canonical.dbusmenu layout parsing --------------------------------
//
// GetLayout returns a tree of (ia{sv}av) structs: item id, a map of
// properties, and an array of child variants. The properties the panel
// understands are the ones every toolkit exports (label, enabled, visible,
// type, toggle-* and icon-name); unknown keys are ignored.

QVariantList parseMenuChildren(const QDBusArgument &children);

QVariantMap parseMenuNode(const QDBusArgument &node)
{
    QVariantMap out;
    if (node.currentType() != QDBusArgument::StructureType)
        return out;

    node.beginStructure();
    int id = 0;
    QVariantMap props;
    node >> id;
    node >> props;
    // The cursor now sits on the `av` of children.
    const QVariantList childList = parseMenuChildren(node);
    node.endStructure();

    const QString type = props.value(QStringLiteral("type")).toString();
    const QString toggle = props.value(QStringLiteral("toggle-type")).toString();

    out.insert(QStringLiteral("id"), id);
    out.insert(QStringLiteral("label"), props.value(QStringLiteral("label")).toString());
    out.insert(QStringLiteral("enabled"), props.value(QStringLiteral("enabled"), true).toBool());
    out.insert(QStringLiteral("visible"), props.value(QStringLiteral("visible"), true).toBool());
    out.insert(QStringLiteral("separator"), type == QLatin1String("separator"));
    out.insert(QStringLiteral("checkable"),
               toggle == QLatin1String("checkmark") || toggle == QLatin1String("radio"));
    out.insert(QStringLiteral("checked"),
               props.value(QStringLiteral("toggle-state")).toInt() == 1);
    out.insert(QStringLiteral("radio"), toggle == QLatin1String("radio"));
    out.insert(QStringLiteral("iconName"), props.value(QStringLiteral("icon-name")).toString());
    out.insert(QStringLiteral("hasChildren"), !childList.isEmpty()
               || props.value(QStringLiteral("children-display")).toString()
                      == QLatin1String("submenu"));
    out.insert(QStringLiteral("children"), childList);
    return out;
}

QVariantList parseMenuChildren(const QDBusArgument &children)
{
    QVariantList out;
    if (children.currentType() != QDBusArgument::ArrayType)
        return out;

    const QDBusArgument array = children;
    array.beginArray();
    while (!array.atEnd()) {
        QVariant entry;
        array >> entry;
        if (!entry.canConvert<QDBusArgument>())
            continue;
        out.append(parseMenuNode(entry.value<QDBusArgument>()));
    }
    array.endArray();
    return out;
}

QString makeId(const QString &service, const QString &path)
{
    return service + QStringLiteral("|") + path;
}

// The QML engine asks for image://tray/<service>#<path>?r=<revision>; the
// revision in the query makes every icon change reload automatically.
class TrayProvider : public QQuickImageProvider {
public:
    explicit TrayProvider(TrayWatcher *watcher)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_watcher(watcher)
    {
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        QString key = id;
        const int query = key.indexOf(QLatin1Char('?'));
        if (query >= 0)
            key.truncate(query);
        key = QUrl::fromPercentEncoding(key.toUtf8());

        QImage image = m_watcher ? m_watcher->pixmapFor(key) : QImage();
        if (image.isNull()) {
            // A neutral, transparent tile keeps the slot alive for an item
            // that reports no icon at all.
            const int px = requestedSize.width() > 0 ? requestedSize.width() : 24;
            image = QImage(px, px, QImage::Format_ARGB32);
            image.fill(Qt::transparent);
        }
        if (size)
            *size = image.size();
        return image;
    }

private:
    TrayWatcher *m_watcher;
};

} // namespace

QQuickImageProvider *createTrayImageProvider(TrayWatcher *watcher)
{
    return new TrayProvider(watcher);
}

// ---------------------------------------------------------------------------

TrayWatcher::TrayWatcher(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_enabled(settings ? settings->value(QStringLiteral("tray.enabled"), true).toBool() : true)
{
    setupDBus();
    if (settings) {
        connect(settings, &Settings::changed, this, [this](const QString &key) {
            if (key.isEmpty() || key == QLatin1String("tray.enabled"))
                setEnabled(m_settings->value(QStringLiteral("tray.enabled"), true).toBool());
        });
    }
}

TrayWatcher::~TrayWatcher()
{
    teardownDBus();
}

void TrayWatcher::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    if (m_enabled)
        setupDBus();
    else
        teardownDBus();
    emit enabledChanged();
}

int TrayWatcher::protocolVersion() const
{
    return 0;
}

bool TrayWatcher::isHostRegistered() const
{
    // The shell is the host once it runs.
    return m_available;
}

void TrayWatcher::setupDBus()
{
    if (!m_enabled || m_available)
        return;

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;

    if (!m_serviceWatcher) {
        // Every service leaving the bus takes its tray item with it.
        m_serviceWatcher = new QDBusServiceWatcher(this);
        m_serviceWatcher->setConnection(bus);
        m_serviceWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
        connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this,
                &TrayWatcher::removeService);
    }

    // A dedicated tray host (stalonetray, trayer, the host desktop) wins; the
    // shell hides its own tray box then, exactly like the notification daemon.
    if (!bus.registerService(QString::fromLatin1(kWatcherService))) {
        qInfo() << "newteller: another StatusNotifierWatcher owns" << kWatcherService
                << ":" << bus.lastError().message();
        emit availableChanged();
        return;
    }

    m_registeredObject = bus.registerObject(QString::fromLatin1(kWatcherPath), this,
                                            QDBusConnection::ExportScriptableSlots
                                                | QDBusConnection::ExportScriptableSignals
                                                | QDBusConnection::ExportAllProperties);
    if (!m_registeredObject) {
        bus.unregisterService(QString::fromLatin1(kWatcherService));
        emit availableChanged();
        return;
    }

    m_available = true;
    emit availableChanged();

    // Items that started before the shell: probe every name on the bus.
    scanExistingItems();

    // Some toolkits announce a new item on the bus instead of calling the
    // method; our own emissions are filtered out by the "|" guard below.
    bus.connect(QString(), QString(), QString::fromLatin1(kWatcherService),
                QStringLiteral("StatusNotifierItemRegistered"), this,
                SLOT(RegisterStatusNotifierItem(QString)));
}

void TrayWatcher::teardownDBus()
{
    if (!m_available && !m_registeredObject)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (m_registeredObject && bus.isConnected()) {
        bus.unregisterObject(QString::fromLatin1(kWatcherPath));
        bus.unregisterService(QString::fromLatin1(kWatcherService));
    }
    m_registeredObject = false;
    m_available = false;
    m_registered.clear();
    m_items.clear();
    m_hosts.clear();
    emitItems();
    emit availableChanged();
}

void TrayWatcher::scanExistingItems()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.interface())
        return;
    // Asynchronous probes only: a bus easily has dozens of names, and one
    // blocking call per name and path froze the shell for minutes. Items also
    // re-register by themselves when they see the watcher appear.
    auto *namesCall = new QDBusPendingCallWatcher(bus.interface()->asyncCall(
                                                      QStringLiteral("ListNames")), this);
    connect(namesCall, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QStringList> reply = *w;
        if (reply.isError() || !m_available)
            return;
        for (const QString &name : reply.value()) {
            if (name.startsWith(QLatin1Char(':')) || name.startsWith(QLatin1String("org.freedesktop"))
                || name == QLatin1String(kWatcherService)
                || name.startsWith(QLatin1String("org.kde.StatusNotifierHost")))
                continue;
            for (const QString &path : candidatePaths()) {
                QDBusMessage call = QDBusMessage::createMethodCall(
                    name, path, QString::fromLatin1(kPropsInterface), QStringLiteral("Get"));
                call << QString::fromLatin1(kItemInterface) << QStringLiteral("Id");
                auto *probe = new QDBusPendingCallWatcher(
                    QDBusConnection::sessionBus().asyncCall(call, 2000), this);
                connect(probe, &QDBusPendingCallWatcher::finished, this,
                        [this, name, path](QDBusPendingCallWatcher *p) {
                            p->deleteLater();
                            if (p->isError() || !m_available)
                                return;
                            // One item per service: the first path that answers.
                            for (const TrayItemData &item : std::as_const(m_items)) {
                                if (item.service == name)
                                    return;
                            }
                            addItem(name, path);
                        });
            }
        }
    });
}

void TrayWatcher::RegisterStatusNotifierItem(const QString &service)
{
    if (!m_available)
        return;
    // Our own StatusNotifierItemRegistered emission carries the merged id.
    if (service.contains(QLatin1Char('|')))
        return;
    QString name = service;
    QString path = QStringLiteral("/StatusNotifierItem");
    // A few items register with the object path instead of a bus name; the
    // sender's unique name is the service then.
    if (service.startsWith(QLatin1Char('/'))) {
        path = service;
        name = calledFromService();
    }
    if (find(makeId(name, path)))
        return;
    addItem(name, path);
}

void TrayWatcher::RegisterStatusNotifierHost(const QString &service)
{
    if (!m_hosts.contains(service)) {
        m_hosts.insert(service, service);
        emit StatusNotifierHostRegistered();
    }
}

QString TrayWatcher::calledFromService() const
{
    return message().service();
}

void TrayWatcher::addItem(const QString &service, const QString &path)
{
    if (!m_available)
        return;
    TrayItemData item;
    item.id = makeId(service, path);
    item.service = service;
    item.path = path;
    item.itemId = service;
    item.title = service;
    item.status = QStringLiteral("Active");

    m_items.append(item);
    m_registered.append(item.id);
    requestRefresh(item.id);

    // The StatusNotifierItem change signals (most items use these rather
    // than PropertiesChanged).
    for (const char *signal : {"NewIcon", "NewTitle", "NewStatus", "NewToolTip",
                               "NewAttentionIcon", "NewMenu"}) {
        QDBusConnection::sessionBus().connect(service, path, QString::fromLatin1(kItemInterface),
                                              QString::fromLatin1(signal), this,
                                              SLOT(handleItemSignal(QDBusMessage)));
    }

    // Watch the item's properties so icon, title and status changes reach the
    // panel without polling.
    QDBusConnection::sessionBus().connect(
        service, path, QString::fromLatin1(kPropsInterface), QStringLiteral("PropertiesChanged"),
        this, SLOT(handlePropertiesChanged(QString, QVariantMap, QStringList)));

    emitItems();
    emit StatusNotifierItemRegistered(item.id);
}

void TrayWatcher::removeItem(const QString &service, const QString &path)
{
    const QString id = makeId(service, path);
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id) {
            QDBusConnection::sessionBus().disconnect(
                service, path, QString::fromLatin1(kPropsInterface),
                QStringLiteral("PropertiesChanged"), this,
                SLOT(handlePropertiesChanged(QString, QVariantMap, QStringList)));
            for (const char *signal : {"NewIcon", "NewTitle", "NewStatus", "NewToolTip",
                                       "NewAttentionIcon", "NewMenu"}) {
                QDBusConnection::sessionBus().disconnect(
                    service, path, QString::fromLatin1(kItemInterface), QString::fromLatin1(signal),
                    this, SLOT(handleItemSignal(QDBusMessage)));
            }
            if (id == m_menuId) {
                QDBusConnection::sessionBus().disconnect(
                    m_menuService, m_menuPath, QString::fromLatin1(kMenuInterface),
                    QStringLiteral("LayoutUpdated"), this, SLOT(handleMenuLayoutUpdated()));
                m_menuId.clear();
                m_menuService.clear();
                m_menuPath.clear();
            }
            m_items.removeAt(i);
            m_registered.removeAll(id);
            emitItems();
            emit StatusNotifierItemUnregistered(id);
            return;
        }
    }
}

void TrayWatcher::removeService(const QString &service)
{
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items.at(i).service == service)
            removeItem(service, m_items.at(i).path);
    }
}

TrayItemData *TrayWatcher::find(const QString &id)
{
    for (TrayItemData &item : m_items) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

// Reads every property the panel shows. One round trip each; items are few and
// this only runs when an item appears or reports a change.
void TrayWatcher::requestRefresh(const QString &id)
{
    TrayItemData *item = find(id);
    if (!item)
        return;
    if (m_refreshing.contains(id)) {
        m_refreshAgain.insert(id);
        return;
    }
    m_refreshing.insert(id);
    QDBusMessage call = QDBusMessage::createMethodCall(item->service, item->path,
                                                       QString::fromLatin1(kPropsInterface),
                                                       QStringLiteral("GetAll"));
    call << QString::fromLatin1(kItemInterface);
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(call, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, id](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        m_refreshing.remove(id);
        QDBusPendingReply<QVariantMap> reply = *w;
        TrayItemData *item = find(id);
        if (item && !reply.isError()) {
            applyProperties(*item, reply.value());
            emitItems();
        }
        if (m_refreshAgain.remove(id))
            requestRefresh(id);
    });
}

void TrayWatcher::handleItemSignal(const QDBusMessage &message)
{
    // Items registered under a well-known name emit from their unique name,
    // so match the exact id first and fall back to the object path.
    const QString exact = makeId(message.service(), message.path());
    if (find(exact)) {
        requestRefresh(exact);
        return;
    }
    for (const TrayItemData &item : std::as_const(m_items)) {
        if (item.path == message.path())
            requestRefresh(item.id);
    }
}

void TrayWatcher::applyProperties(TrayItemData &item, const QVariantMap &properties)
{
    const QVariant id = properties.value(QStringLiteral("Id"));
    const QVariant title = properties.value(QStringLiteral("Title"));
    const QVariant status = properties.value(QStringLiteral("Status"));
    const QVariant iconName = properties.value(QStringLiteral("IconName"));
    const QVariant pixmap = properties.value(QStringLiteral("IconPixmap"));
    const QVariant tooltip = properties.value(QStringLiteral("ToolTip"));
    const QVariant menu = properties.value(QStringLiteral("Menu"));

    QString newId = id.toString();
    if (newId.isEmpty())
        newId = item.service;
    QString newTitle = title.toString();
    if (newTitle.isEmpty())
        newTitle = newId;
    QString newStatus = status.toString();
    if (newStatus.isEmpty())
        newStatus = QStringLiteral("Active");
    const QString newIconName = iconName.toString();
    const QImage newImage = decodePixmap(pixmap);

    if (item.itemId != newId || item.title != newTitle || item.status != newStatus
        || item.iconName != newIconName || item.iconImage != newImage) {
        item.itemId = newId;
        item.title = newTitle;
        item.status = newStatus;
        item.iconName = newIconName;
        item.iconImage = newImage;
        item.revision = ++m_revision;
    }

    QString tipTitle;
    QString tipBody;
    decodeToolTip(tooltip, &tipTitle, &tipBody);
    if (item.tooltipTitle != tipTitle || item.tooltipBody != tipBody) {
        item.tooltipTitle = tipTitle;
        item.tooltipBody = tipBody;
        item.revision = ++m_revision;
    }

    QString newMenuPath;
    if (menu.isValid()) {
        if (menu.metaType().id() == qMetaTypeId<QDBusObjectPath>())
            newMenuPath = menu.value<QDBusObjectPath>().path();
        else
            newMenuPath = menu.toString();
    }
    // Some items use the well-known "no menu" sentinel.
    if (newMenuPath == QLatin1String("/NO_DBUSMENU"))
        newMenuPath.clear();
    if (item.menuPath != newMenuPath) {
        item.menuPath = newMenuPath;
        item.revision = ++m_revision;
    }
}

// Property changes arrive as (interface, changed, invalidated).
void TrayWatcher::handlePropertiesChanged(const QString &interface, const QVariantMap &changed,
                                          const QStringList &invalidated)
{
    if (interface != QString::fromLatin1(kItemInterface))
        return;
    const QString service = calledFromService();
    for (TrayItemData &item : m_items) {
        if (item.service != service)
            continue;
        const bool relevant = changed.contains(QStringLiteral("Id"))
                              || changed.contains(QStringLiteral("Title"))
                              || changed.contains(QStringLiteral("Status"))
                              || changed.contains(QStringLiteral("IconName"))
                              || changed.contains(QStringLiteral("IconPixmap"))
                              || changed.contains(QStringLiteral("ToolTip"))
                              || changed.contains(QStringLiteral("Menu"))
                              || !invalidated.isEmpty();
        if (relevant)
            requestRefresh(item.id);
        return;
    }
}

void TrayWatcher::activate(const QString &id) const
{
    TrayItemData *item = const_cast<TrayWatcher *>(this)->find(id);
    if (!item)
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(item->service, item->path,
                                                       QString::fromLatin1(kItemInterface),
                                                       QStringLiteral("Activate"));
    call << 0 << 0;
    // A click on an item that cannot be activated used to do nothing at
    // all; the reply says whether it worked.
    auto *self = const_cast<TrayWatcher *>(this);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 5000),
                                                self);
    const QString service = item->service;
    const QStringList names{item->itemId, item->title};
    connect(watcher, &QDBusPendingCallWatcher::finished, self,
            [self, id, service, names](QDBusPendingCallWatcher *pending) {
                pending->deleteLater();
                if (!pending->isError())
                    return;
                // A busy application that answers late is not started a
                // second time.
                const QDBusError::ErrorType type = pending->error().type();
                if (type == QDBusError::NoReply || type == QDBusError::Timeout)
                    return;
                self->activateFallback(id, service, names);
            });
}

void TrayWatcher::activateFallback(const QString &id, const QString &service,
                                   const QStringList &names)
{
    QDBusMessage pidCall = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetConnectionUnixProcessID"));
    pidCall << service;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(pidCall, 2000),
                                                this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, id, names](QDBusPendingCallWatcher *pending) {
                pending->deleteLater();
                QStringList candidates;
                const QDBusPendingReply<uint> reply = *pending;
                if (reply.isValid())
                    candidates += processNames(reply.value());
                candidates += names;
                for (QString &candidate : candidates)
                    candidate = candidate.trimmed();
                candidates.removeAll(QString());
                candidates.removeDuplicates();
                emit activateFailed(id, candidates);
            });
}

// The names a process goes by: its Flatpak application id, the executable
// and the command name (all small /proc reads).
QStringList TrayWatcher::processNames(uint pid)
{
    QStringList names;
    if (pid == 0)
        return names;
    const QString proc = QStringLiteral("/proc/%1").arg(pid);
    QFile info(proc + QStringLiteral("/root/.flatpak-info"));
    if (info.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = info.read(16384).split('\n');
        for (const QByteArray &line : lines) {
            if (line.startsWith("name=")) {
                names << QString::fromUtf8(line.mid(5)).trimmed();
                break;
            }
        }
    }
    const QString exe = QFileInfo(proc + QStringLiteral("/exe")).symLinkTarget();
    if (!exe.isEmpty())
        names << QFileInfo(exe).fileName();
    QFile cmdline(proc + QStringLiteral("/cmdline"));
    if (cmdline.open(QIODevice::ReadOnly)) {
        const QByteArray argv0 = cmdline.read(4096).split('\0').value(0);
        if (!argv0.isEmpty())
            names << QFileInfo(QString::fromUtf8(argv0)).fileName();
    }
    QFile comm(proc + QStringLiteral("/comm"));
    if (comm.open(QIODevice::ReadOnly))
        names << QString::fromUtf8(comm.readAll()).trimmed();
    return names;
}

void TrayWatcher::secondaryActivate(const QString &id) const
{
    TrayItemData *item = const_cast<TrayWatcher *>(this)->find(id);
    if (!item)
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(item->service, item->path,
                                                       QString::fromLatin1(kItemInterface),
                                                       QStringLiteral("SecondaryActivate"));
    call << 0 << 0;
    QDBusConnection::sessionBus().asyncCall(call);
}

void TrayWatcher::contextMenu(const QString &id, int x, int y) const
{
    TrayItemData *item = const_cast<TrayWatcher *>(this)->find(id);
    if (!item)
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(item->service, item->path,
                                                       QString::fromLatin1(kItemInterface),
                                                       QStringLiteral("ContextMenu"));
    call << x << y;
    QDBusConnection::sessionBus().asyncCall(call);
}

void TrayWatcher::scroll(const QString &id, int delta, bool horizontal) const
{
    TrayItemData *item = const_cast<TrayWatcher *>(this)->find(id);
    if (!item)
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(item->service, item->path,
                                                       QString::fromLatin1(kItemInterface),
                                                       QStringLiteral("Scroll"));
    call << delta << (horizontal ? QStringLiteral("horizontal") : QStringLiteral("vertical"));
    QDBusConnection::sessionBus().asyncCall(call);
}

bool TrayWatcher::hasMenu(const QString &id) const
{
    TrayItemData *item = const_cast<TrayWatcher *>(this)->find(id);
    return item && !item->menuPath.isEmpty();
}

void TrayWatcher::requestMenu(const QString &id)
{
    TrayItemData *item = find(id);
    if (!item || item->menuPath.isEmpty()) {
        emit menuReady(id, {});
        return;
    }
    const QString service = item->service;
    const QString path = item->menuPath;
    watchMenu(id, service, path);

    // AboutToShow lets the application update dynamic entries first; the
    // layout is fetched once it answered (or failed, or timed out). Nothing
    // here waits on the application, so a hung one cannot freeze the panel.
    QDBusMessage aboutCall = QDBusMessage::createMethodCall(service, path,
                                                            QString::fromLatin1(kMenuInterface),
                                                            QStringLiteral("AboutToShow"));
    aboutCall << 0;
    auto *about = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(aboutCall, 1000), this);
    connect(about, &QDBusPendingCallWatcher::finished, this, [this, id, service, path](QDBusPendingCallWatcher *a) {
        a->deleteLater();
        QDBusMessage call = QDBusMessage::createMethodCall(service, path, QString::fromLatin1(kMenuInterface),
                                                           QStringLiteral("GetLayout"));
        // parent id 0 (the root), recursion depth -1 (the whole tree), no
        // explicit property filter (so every toolkit sends what it has).
        call << 0 << -1 << QStringList();
        auto *layout = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 1500), this);
        connect(layout, &QDBusPendingCallWatcher::finished, this, [this, id](QDBusPendingCallWatcher *w) {
            w->deleteLater();
            const QDBusMessage reply = w->reply();
            QVariantList items;
            if (reply.type() == QDBusMessage::ReplyMessage && reply.arguments().size() >= 2
                && reply.arguments().at(1).canConvert<QDBusArgument>()) {
                const QDBusArgument tree = reply.arguments().at(1).value<QDBusArgument>();
                items = parseMenuNode(tree).value(QStringLiteral("children")).toList();
            }
            emit menuReady(id, items);
        });
    });
}

void TrayWatcher::watchMenu(const QString &id, const QString &service, const QString &path)
{
    m_menuId = id;
    if (service == m_menuService && path == m_menuPath)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!m_menuPath.isEmpty())
        bus.disconnect(m_menuService, m_menuPath, QString::fromLatin1(kMenuInterface),
                       QStringLiteral("LayoutUpdated"), this, SLOT(handleMenuLayoutUpdated()));
    m_menuService = service;
    m_menuPath = path;
    bus.connect(service, path, QString::fromLatin1(kMenuInterface), QStringLiteral("LayoutUpdated"),
                this, SLOT(handleMenuLayoutUpdated()));
}

void TrayWatcher::handleMenuLayoutUpdated()
{
    if (!m_menuId.isEmpty())
        emit menuChanged(m_menuId);
}

void TrayWatcher::menuAboutToShow(const QString &id, int itemId) const
{
    TrayItemData *item = const_cast<TrayWatcher *>(this)->find(id);
    if (!item || item->menuPath.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(item->service, item->menuPath,
                                                       QString::fromLatin1(kMenuInterface),
                                                       QStringLiteral("AboutToShow"));
    call << itemId;
    QDBusConnection::sessionBus().asyncCall(call);
}

void TrayWatcher::menuEvent(const QString &id, int itemId, const QString &event) const
{
    TrayItemData *item = const_cast<TrayWatcher *>(this)->find(id);
    if (!item || item->menuPath.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(item->service, item->menuPath,
                                                       QString::fromLatin1(kMenuInterface),
                                                       QStringLiteral("Event"));
    // Event(i id, s eventId, v data, u timestamp); the event data is unused
    // for a plain click, but the variant still has to be on the wire.
    call << itemId << event
         << QVariant::fromValue(QDBusVariant(QVariant(0)))
         << quint32(0);
    QDBusConnection::sessionBus().asyncCall(call);
}

QUrl TrayWatcher::iconUrl(const QString &id) const
{
    TrayItemData *item = const_cast<TrayWatcher *>(this)->find(id);
    if (!item)
        return QUrl();
    return QUrl(QStringLiteral("image://tray/%1?r=%2")
                    .arg(QString::fromUtf8(QUrl::toPercentEncoding(id)),
                         QString::number(item->revision)));
}

QImage TrayWatcher::pixmapFor(const QString &id) const
{
    TrayItemData *item = const_cast<TrayWatcher *>(this)->find(id);
    if (!item)
        return QImage();
    if (!item->iconImage.isNull())
        return item->iconImage;
    if (!item->iconName.isEmpty()) {
        return QIcon::fromTheme(item->iconName,
                                QIcon::fromTheme(QStringLiteral("application-x-executable")))
            .pixmap(24, 24)
            .toImage();
    }
    return QImage();
}

QVariantList TrayWatcher::items() const
{
    return m_itemsValue;
}

void TrayWatcher::emitItems()
{
    QVariantList out;
    for (const TrayItemData &item : m_items) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), item.id);
        map.insert(QStringLiteral("service"), item.service);
        map.insert(QStringLiteral("path"), item.path);
        map.insert(QStringLiteral("name"), item.itemId);
        map.insert(QStringLiteral("title"), item.title);
        map.insert(QStringLiteral("status"), item.status);
        map.insert(QStringLiteral("tooltipTitle"), item.tooltipTitle);
        map.insert(QStringLiteral("tooltipBody"), item.tooltipBody);
        map.insert(QStringLiteral("hasMenu"), !item.menuPath.isEmpty());
        map.insert(QStringLiteral("menuPath"), item.menuPath);
        out.append(map);
    }
    m_itemsValue = out;
    emit itemsChanged();
}
