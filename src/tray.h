#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtDBus/QDBusContext>

class QDBusServiceWatcher;
class Settings;

// A StatusNotifierItem reported by the watcher: one system tray icon.
// All fields are plain values so they can cross into QML as one map.
struct TrayItemData {
    QString id;          // "service|path", stable across refreshes
    QString service;     // D-Bus unique name owning the item
    QString path;        // object path of the org.kde.StatusNotifierItem
    QString itemId;      // the item's own Id (often the application name)
    QString title;
    QString status;      // Active, Passive or NeedsAttention
    QString iconName;
    QImage iconImage;    // IconPixmap decoded (empty when only a name is set)
    QString tooltipTitle;
    QString tooltipBody;
    QString menuPath;    // com.canonical.dbusmenu object, empty when none
    qlonglong revision = 0; // bumped whenever the icon or text changes
};

// System tray support for the panel, the last missing top bar feature.
//
// The shell acts as a StatusNotifierWatcher (org.kde.StatusNotifierWatcher):
// applications that implement org.kde.StatusNotifierItem - Qt's
// QSystemTrayIcon, Electron apps, GTK applets, KMix, Telegram, ... - register
// with it and the panel renders their icons. The DBusMenu protocol behind the
// items is not implemented: left click activates the item, middle click
// triggers the secondary action and right click asks the item to open its own
// context menu (every libappindicator/KStatusNotifierItem application does).
//
// Everything is best effort, exactly like the notification daemon: the name is
// only claimed when no other watcher owns it (so the shell never fights the
// host desktop's own tray), and when the session bus is unreachable the tray
// simply stays hidden. Exposed to QML as the context property `Tray`.
class TrayWatcher : public QObject, protected QDBusContext {
    Q_OBJECT
    // One map per item: { id, service, path, title, name, status,
    // tooltipTitle, tooltipBody, iconUrl }.
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(int count READ count NOTIFY itemsChanged)
    // False while another StatusNotifierWatcher owns the session (or when no
    // bus exists at all); the panel hides its tray box then.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // Toggled live by the tray.enabled setting, like notifications.enabled.
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // --- the three properties the StatusNotifierItem spec requires -----------
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredItems NOTIFY itemsChanged)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ isHostRegistered NOTIFY availableChanged)
    Q_PROPERTY(int ProtocolVersion READ protocolVersion CONSTANT)

public:
    explicit TrayWatcher(Settings *settings, QObject *parent = nullptr);
    ~TrayWatcher() override;

    QVariantList items() const;
    int count() const { return int(m_items.size()); }
    bool available() const { return m_available; }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QStringList registeredItems() const { return m_registered; }
    bool isHostRegistered() const;
    int protocolVersion() const;
    // The rendered icon of one item, used by the image provider.
    QImage pixmapFor(const QString &id) const;

public slots:
    // --- org.kde.StatusNotifierWatcher --------------------------------------
    // Items register either with "com.example.App" (object path on the well
    // known /StatusNotifierItem) or with an object path, in which case the
    // sender's unique name is the service.
    void RegisterStatusNotifierItem(const QString &service);
    void RegisterStatusNotifierHost(const QString &service);

    Q_INVOKABLE void activate(const QString &id) const;
    Q_INVOKABLE void secondaryActivate(const QString &id) const;
    Q_INVOKABLE void contextMenu(const QString &id, int x, int y) const;
    Q_INVOKABLE void scroll(const QString &id, int delta, bool horizontal) const;
    // --- com.canonical.dbusmenu --------------------------------------------
    // True when the item exported a menu object (the `Menu` property); the
    // panel then renders that menu itself instead of asking the application
    // for a native popup.
    Q_INVOKABLE bool hasMenu(const QString &id) const;
    // The full menu tree, fetched live from the item's DBusMenu service:
    // [{ id, label, enabled, visible, separator, checkable, checked,
    //    iconName, hasChildren, children: [...] }, ...]
    Q_INVOKABLE QVariantList menu(const QString &id) const;
    Q_INVOKABLE void menuAboutToShow(const QString &id, int itemId) const;
    Q_INVOKABLE void menuEvent(const QString &id, int itemId, const QString &event) const;

    // Stable rendering URL for the panel: image://tray/... served by
    // TrayImageProvider. The revision in the query makes QML reload whenever
    // the underlying icon changes.
    Q_INVOKABLE QUrl iconUrl(const QString &id) const;

signals:
    void itemsChanged();
    void availableChanged();
    void enabledChanged();

    // org.kde.StatusNotifierWatcher signals, exported on the bus.
    void StatusNotifierItemRegistered(const QString &service);
    void StatusNotifierItemUnregistered(const QString &service);
    void StatusNotifierHostRegistered();

private slots:
    void handlePropertiesChanged(const QString &interface, const QVariantMap &changed,
                                 const QStringList &invalidated);

private:
    void setupDBus();
    void teardownDBus();
    void scanExistingItems();
    void addItem(const QString &service, const QString &path);
    void removeItem(const QString &service, const QString &path);
    void removeService(const QString &service);
    TrayItemData *find(const QString &id);
    void refreshItem(TrayItemData &item);
    void emitItems();
    QString calledFromService() const;

    Settings *m_settings = nullptr;
    QDBusServiceWatcher *m_serviceWatcher = nullptr;
    QVariantList m_itemsValue;
    QList<TrayItemData> m_items;
    QStringList m_registered; // RegisteredStatusNotifierItems as on the bus
    QHash<QString, QString> m_hosts;
    bool m_available = false;
    bool m_enabled = true;
    bool m_registeredObject = false;
    qlonglong m_revision = 1;
};

class QQuickImageProvider;

// Creates the image provider registered as "tray" with the QML engine (see
// main.cpp): it renders the tray icons out of the watcher's items. Defined in
// tray.cpp; the engine owns the returned provider.
QQuickImageProvider *createTrayImageProvider(TrayWatcher *watcher);
