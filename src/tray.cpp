#include "tray.h"
#include "settings.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QIcon>
#include <QQuickImageProvider>
#include <QtDebug>

namespace {

constexpr auto kWatcherService = "org.kde.StatusNotifierWatcher";
constexpr auto kWatcherPath = "/StatusNotifierWatcher";
constexpr auto kItemInterface = "org.kde.StatusNotifierItem";
constexpr auto kPropsInterface = "org.freedesktop.DBus.Properties";

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

// Reads one item property; invalid when the item is gone or lacks the property.
QVariant itemProperty(const QString &service, const QString &path, const QString &name)
{
    QDBusMessage call = QDBusMessage::createMethodCall(service, path, QString::fromLatin1(kPropsInterface),
                                                       QStringLiteral("Get"));
    call << QString::fromLatin1(kItemInterface) << name;
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 500);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    return reply.arguments().first();
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
        QDBusArgument entry = array;
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
    QDBusArgument structure = value.value<QDBusArgument>();
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
                                            QDBusConnection::ExportAllSlots
                                                | QDBusConnection::ExportAllSignals
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
    const QStringList names = bus.interface()->registeredServiceNames().value();
    for (const QString &name : names) {
        if (name.startsWith(QLatin1String("org.freedesktop"))
            || name.startsWith(QLatin1String("org.kde.StatusNotifier")))
            continue;
        for (const QString &path : candidatePaths()) {
            if (itemProperty(name, path, QStringLiteral("Id")).isValid()) {
                addItem(name, path);
                break;
            }
        }
    }
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
    refreshItem(item);

    m_items.append(item);
    m_registered.append(item.id);

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
void TrayWatcher::refreshItem(TrayItemData &item)
{
    const QVariant id = itemProperty(item.service, item.path, QStringLiteral("Id"));
    const QVariant title = itemProperty(item.service, item.path, QStringLiteral("Title"));
    const QVariant status = itemProperty(item.service, item.path, QStringLiteral("Status"));
    const QVariant iconName = itemProperty(item.service, item.path, QStringLiteral("IconName"));
    const QVariant pixmap = itemProperty(item.service, item.path, QStringLiteral("IconPixmap"));
    const QVariant tooltip = itemProperty(item.service, item.path, QStringLiteral("ToolTip"));

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
                              || !invalidated.isEmpty();
        if (relevant)
            refreshItem(item);
        emitItems();
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
    QDBusConnection::sessionBus().asyncCall(call);
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
        out.append(map);
    }
    m_itemsValue = out;
    emit itemsChanged();
}
