#include "settings.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDebug>

QString Settings::configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/newteller");
}

QString Settings::configFile()
{
    return configDir() + QStringLiteral("/shell.json");
}

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_saveTimer(this)
    , m_debounceTimer(this)
{
    // Built-in defaults keep the shell working with no Rust core and no file.
    m_defaults.insert(QStringLiteral("panel.height"), 48);
    m_defaults.insert(QStringLiteral("panel.position"), QStringLiteral("bottom"));
    m_defaults.insert(QStringLiteral("panel.margin"), 0);
    m_defaults.insert(QStringLiteral("panel.centered"), false);
    m_defaults.insert(QStringLiteral("panel.width"), 0);
    m_defaults.insert(QStringLiteral("panel.autoHide"), false);
    m_defaults.insert(QStringLiteral("panel.autoHideDelay"), 400);
    m_defaults.insert(QStringLiteral("panel.clockFormat"), QStringLiteral("HH:mm"));
    m_defaults.insert(QStringLiteral("panel.dateFormat"), QStringLiteral("ddd d MMM"));
    m_defaults.insert(QStringLiteral("panel.showDate"), false);
    m_defaults.insert(QStringLiteral("panel.showRunning"), true);
    m_defaults.insert(QStringLiteral("panel.pinned"),
                      QStringList{QStringLiteral("org.gnome.Terminal.desktop"),
                                  QStringLiteral("firefox-esr.desktop"),
                                  QStringLiteral("org.gnome.Nautilus.desktop")});
    m_defaults.insert(QStringLiteral("dock.showWindows"), true);
    m_defaults.insert(QStringLiteral("dock.group"), true);
    m_defaults.insert(QStringLiteral("dock.iconSize"), 28);
    m_defaults.insert(QStringLiteral("dock.separateBackground"), true);
    m_defaults.insert(QStringLiteral("dock.shrinkIcons"), true);
    m_defaults.insert(QStringLiteral("panel.indicators"), true);
    m_defaults.insert(QStringLiteral("tray.enabled"), true);
    m_defaults.insert(QStringLiteral("windows.style"), QStringLiteral("card"));
    m_defaults.insert(QStringLiteral("windows.radius"), -1);
    m_defaults.insert(QStringLiteral("windows.opacityPercent"), 0);
    m_defaults.insert(QStringLiteral("windows.borderWidth"), -1);
    m_defaults.insert(QStringLiteral("windows.borderColor"), QString());
    m_defaults.insert(QStringLiteral("windows.shadow"), 1);
    m_defaults.insert(QStringLiteral("notifications.enabled"), true);
    m_defaults.insert(QStringLiteral("notifications.doNotDisturb"), false);
    m_defaults.insert(QStringLiteral("notifications.position"), QStringLiteral("top-right"));
    m_defaults.insert(QStringLiteral("notifications.duration"), 6);
    m_defaults.insert(QStringLiteral("notifications.historyLimit"), 20);
    m_defaults.insert(QStringLiteral("startmenu.width"), 440);
    m_defaults.insert(QStringLiteral("startmenu.height"), 580);
    m_defaults.insert(QStringLiteral("startmenu.limit"), 200);
    m_defaults.insert(QStringLiteral("startmenu.fuzzy"), true);
    m_defaults.insert(QStringLiteral("startmenu.highlight"), QStringLiteral("#ffffff"));
    m_defaults.insert(QStringLiteral("startmenu.showRecent"), true);
    m_defaults.insert(QStringLiteral("theme.name"), QStringLiteral("default"));
    m_defaults.insert(QStringLiteral("theme.autoReload"), true);
    m_defaults.insert(QStringLiteral("desktop.wallpaper"), QString());
    m_defaults.insert(QStringLiteral("desktop.showIcons"), true);
    m_defaults.insert(QStringLiteral("desktop.iconSize"), 48);
    m_defaults.insert(QStringLiteral("desktop.cellWidth"), 96);
    m_defaults.insert(QStringLiteral("desktop.cellHeight"), 110);
    m_defaults.insert(QStringLiteral("desktop.contextMenu"), true);
    // Empty: use the icons bundled with the shell (IconKit compiled into the
    // binary and, for everything else, the kit installed with the release).
    // A non-empty path (say ~/iconkit/icons) overrides the bundled sets.
    m_defaults.insert(QStringLiteral("icons.path"), QString());
    m_defaults.insert(QStringLiteral("icons.set"), QStringLiteral("lucide"));
    m_defaults.insert(QStringLiteral("icons.cache"), true);
    m_defaults.insert(QStringLiteral("plugins.disabled"), QStringList());
    m_defaults.insert(QStringLiteral("plugins.autoRescan"), true);
    m_defaults.insert(QStringLiteral("apps.recent"), QStringList());
    m_defaults.insert(QStringLiteral("apps.recentLimit"), 8);
    m_defaults.insert(QStringLiteral("apps.usage"), QVariantMap());
    m_defaults.insert(QStringLiteral("apps.terminalCommand"),
                      QStringLiteral("x-terminal-emulator -e"));
    m_defaults.insert(QStringLiteral("apps.maxApps"), 0);
    m_defaults.insert(QStringLiteral("system.interval"), 2000);
    m_defaults.insert(QStringLiteral("system.perCore"), false);
    m_defaults.insert(QStringLiteral("system.battery"), true);
    m_defaults.insert(QStringLiteral("system.thermal"), true);
    m_defaults.insert(QStringLiteral("system.disks"), true);
    m_defaults.insert(QStringLiteral("watch.debounce"), 250);
    m_defaults.insert(QStringLiteral("run.history"), QStringList());
    m_defaults.insert(QStringLiteral("run.historyLimit"), 25);

    // ===== Appearance & Theming =====
    // Panel styling
    m_defaults.insert(QStringLiteral("panel.backgroundColor"), QString());
    m_defaults.insert(QStringLiteral("panel.backgroundOpacity"), 0.92);
    m_defaults.insert(QStringLiteral("panel.foregroundColor"), QString());
    m_defaults.insert(QStringLiteral("panel.customColors"), false);
    // Dock styling
    m_defaults.insert(QStringLiteral("dock.backgroundColor"), QString());
    m_defaults.insert(QStringLiteral("dock.backgroundOpacity"), -1);
    m_defaults.insert(QStringLiteral("dock.hoverOpacity"), 0.15);
    m_defaults.insert(QStringLiteral("dock.activeOpacity"), 0.30);
    m_defaults.insert(QStringLiteral("dock.showLabels"), false);
    m_defaults.insert(QStringLiteral("dock.labelPosition"), QStringLiteral("bottom"));
    m_defaults.insert(QStringLiteral("dock.iconColorMode"), QStringLiteral("theme"));
    m_defaults.insert(QStringLiteral("dock.customIconColor"), QStringLiteral("#ffffff"));
    // Start menu styling
    m_defaults.insert(QStringLiteral("startmenu.backgroundColor"), QString());
    m_defaults.insert(QStringLiteral("startmenu.backgroundOpacity"), -1);
    m_defaults.insert(QStringLiteral("startmenu.foregroundColor"), QString());
    m_defaults.insert(QStringLiteral("startmenu.accentColor"), QString());
    m_defaults.insert(QStringLiteral("startmenu.highlightColor"), QStringLiteral("#ffffff"));
    // Desktop styling
    m_defaults.insert(QStringLiteral("desktop.backgroundColor"), QString());
    m_defaults.insert(QStringLiteral("desktop.backgroundGradient"), true);
    m_defaults.insert(QStringLiteral("desktop.iconColor"), QStringLiteral("#ffffff"));
    m_defaults.insert(QStringLiteral("desktop.iconShadow"), true);
    m_defaults.insert(QStringLiteral("desktop.iconGlow"), false);
    // Window styling
    m_defaults.insert(QStringLiteral("windows.enableAnimations"), true);
    m_defaults.insert(QStringLiteral("windows.animationSpeed"), 1.0);
    // Theme variants
    m_defaults.insert(QStringLiteral("theme.followSystem"), false);
    m_defaults.insert(QStringLiteral("theme.systemTheme"), QStringLiteral("auto"));

    // ===== Panel & Dock Improvements =====
    m_defaults.insert(QStringLiteral("panel.spacing"), 8);
    m_defaults.insert(QStringLiteral("panel.leftSpacing"), 8);
    m_defaults.insert(QStringLiteral("panel.rightSpacing"), 8);
    m_defaults.insert(QStringLiteral("panel.centerDock"), false);
    m_defaults.insert(QStringLiteral("dock.position"), QStringLiteral("left"));
    m_defaults.insert(QStringLiteral("dock.multiRow"), false);
    m_defaults.insert(QStringLiteral("dock.rows"), 1);
    m_defaults.insert(QStringLiteral("dock.scrollable"), true);
    m_defaults.insert(QStringLiteral("dock.iconSpacing"), 4);
    m_defaults.insert(QStringLiteral("panel.zoneSpacing"), 8);

    // ===== Desktop Enhancements =====
    m_defaults.insert(QStringLiteral("desktop.wallpaperMode"), QStringLiteral("fill"));
    m_defaults.insert(QStringLiteral("desktop.wallpaperBlur"), 0);
    m_defaults.insert(QStringLiteral("desktop.wallpaperDarken"), 0);
    m_defaults.insert(QStringLiteral("desktop.perMonitorWallpaper"), false);
    m_defaults.insert(QStringLiteral("desktop.wallpaperSlideshow"), false);
    m_defaults.insert(QStringLiteral("desktop.wallpaperSlideshowInterval"), 60);
    m_defaults.insert(QStringLiteral("desktop.autoArrangeIcons"), true);
    m_defaults.insert(QStringLiteral("desktop.iconTextColor"), QStringLiteral("#ffffff"));
    m_defaults.insert(QStringLiteral("desktop.iconTextShadow"), true);
    m_defaults.insert(QStringLiteral("desktop.showHiddenFiles"), false);
    m_defaults.insert(QStringLiteral("desktop.doubleClickSpeed"), 500);

    // ===== Notifications =====
    m_defaults.insert(QStringLiteral("notifications.theme"), QStringLiteral("system"));
    m_defaults.insert(QStringLiteral("notifications.customBackground"), QString());
    m_defaults.insert(QStringLiteral("notifications.customForeground"), QString());
    m_defaults.insert(QStringLiteral("notifications.groupByApp"), false);
    m_defaults.insert(QStringLiteral("notifications.showAppIcon"), true);
    m_defaults.insert(QStringLiteral("notifications.soundEnabled"), true);
    m_defaults.insert(QStringLiteral("notifications.soundFile"), QString());
    m_defaults.insert(QStringLiteral("notifications.doNotDisturbSchedule"), false);
    m_defaults.insert(QStringLiteral("notifications.dndStartTime"), QStringLiteral("22:00"));
    m_defaults.insert(QStringLiteral("notifications.dndEndTime"), QStringLiteral("08:00"));
    m_defaults.insert(QStringLiteral("notifications.urgencyLevel"), 2);

    // ===== System & Behavior =====
    // Hotkeys
    m_defaults.insert(QStringLiteral("hotkeys.enabled"), true);
    m_defaults.insert(QStringLiteral("hotkeys.startMenu"), QStringLiteral("Meta"));
    m_defaults.insert(QStringLiteral("hotkeys.runDialog"), QStringLiteral("Alt+F2"));
    m_defaults.insert(QStringLiteral("hotkeys.settings"), QStringLiteral("Meta+,Meta+S"));
    m_defaults.insert(QStringLiteral("hotkeys.notificationCenter"), QStringLiteral("Meta+N"));
    m_defaults.insert(QStringLiteral("hotkeys.quickSettings"), QStringLiteral("Meta+Q"));
    m_defaults.insert(QStringLiteral("hotkeys.lockScreen"), QStringLiteral("Meta+L"));
    m_defaults.insert(QStringLiteral("hotkeys.logout"), QStringLiteral("Meta+Shift+Q"));
    m_defaults.insert(QStringLiteral("hotkeys.sleep"), QStringLiteral("Meta+Shift+S"));
    m_defaults.insert(QStringLiteral("hotkeys.reboot"), QStringLiteral("Meta+Shift+R"));
    m_defaults.insert(QStringLiteral("hotkeys.shutdown"), QStringLiteral("Meta+Shift+P"));
    m_defaults.insert(QStringLiteral("hotkeys.terminal"), QStringLiteral("Meta+T"));
    m_defaults.insert(QStringLiteral("hotkeys.fileManager"), QStringLiteral("Meta+E"));
    m_defaults.insert(QStringLiteral("hotkeys.switcher"), QStringLiteral("Alt+Tab"));
    // Startup apps
    m_defaults.insert(QStringLiteral("startup.apps"), QStringList());
    m_defaults.insert(QStringLiteral("startup.delay"), 0);
    // Window management
    m_defaults.insert(QStringLiteral("windowRules.alwaysOnTop"), QStringList());
    m_defaults.insert(QStringLiteral("windowRules.maximized"), QStringList());
    // Power management
    m_defaults.insert(QStringLiteral("power.sleepAction"), QStringLiteral("suspend"));
    m_defaults.insert(QStringLiteral("power.lockOnSleep"), true);
    // Clipboard
    m_defaults.insert(QStringLiteral("clipboard.historyEnabled"), true);
    m_defaults.insert(QStringLiteral("clipboard.historySize"), 100);
    m_defaults.insert(QStringLiteral("clipboard.persistHistory"), true);

    // ===== Performance & Features =====
    // Animations
    m_defaults.insert(QStringLiteral("animations.enabled"), true);
    m_defaults.insert(QStringLiteral("animations.speed"), 1.0);
    m_defaults.insert(QStringLiteral("animations.panelHide"), true);
    // Caching
    m_defaults.insert(QStringLiteral("cache.iconCache"), true);
    m_defaults.insert(QStringLiteral("cache.searchCache"), true);
    m_defaults.insert(QStringLiteral("cache.maxIconCacheSize"), 1000);
    m_defaults.insert(QStringLiteral("cache.maxSearchCacheSize"), 500);
    // Metrics
    m_defaults.insert(QStringLiteral("system.showCpuTemp"), false);
    m_defaults.insert(QStringLiteral("system.showGpu"), false);
    m_defaults.insert(QStringLiteral("system.showFanSpeed"), false);
    // Search
    m_defaults.insert(QStringLiteral("search.maxResults"), 50);
    m_defaults.insert(QStringLiteral("search.showCategories"), true);
    m_defaults.insert(QStringLiteral("search.categoryIcons"), true);

    // ===== Multi-monitor =====
    m_defaults.insert(QStringLiteral("multiMonitor.perMonitorSettings"), false);
    m_defaults.insert(QStringLiteral("multiMonitor.panelPositions"), QVariantMap());
    m_defaults.insert(QStringLiteral("multiMonitor.wallpapers"), QVariantMap());

    // ===== Plugin System Improvements =====
    m_defaults.insert(QStringLiteral("plugins.onlineEnabled"), true);
    m_defaults.insert(QStringLiteral("plugins.autoUpdate"), false);
    m_defaults.insert(QStringLiteral("plugins.showDescriptions"), true);

    // The Rust schema knows the same keys; merge its defaults on top so a new
    // key appears here without editing this table.
    if (Rust::available()) {
        const QVariantMap rustDefaults = Rust::configDefaults();
        for (auto it = rustDefaults.constBegin(); it != rustDefaults.constEnd(); ++it) {
            if (!m_defaults.contains(it.key()))
                m_defaults.insert(it.key(), it.value());
        }
    }

    load();

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(150);
    connect(&m_saveTimer, &QTimer::timeout, this, &Settings::flush);

    startWatching();
}

Settings::~Settings()
{
    if (m_dirty)
        flush();
}

void Settings::load()
{
    QVariantMap merged = m_defaults;

    if (Rust::available()) {
        // Resilient load: falls back to the `.bak` file when the JSON is broken.
        const QVariantMap result = Rust::loadConfig(configFile());
        const QVariantMap value = result.value(QStringLiteral("value")).toMap();
        for (auto it = value.constBegin(); it != value.constEnd(); ++it)
            merged.insert(it.key(), it.value());
        const QString error = result.value(QStringLiteral("error")).toString();
        if (!error.isEmpty())
            qWarning() << "newteller: settings:" << error;
    } else {
        QFile file(configFile());
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray body = file.readAll();
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                const QVariantMap user = doc.object().toVariantMap();
                for (auto it = user.cbegin(); it != user.cend(); ++it)
                    merged.insert(it.key(), it.value());
            } else {
                qWarning() << "newteller: invalid settings file" << configFile()
                           << err.errorString();
            }
        }
    }

    // Remember the on-disk body so the watcher can tell real edits apart from
    // the echo of our own save.
    QFile file(configFile());
    if (file.open(QIODevice::ReadOnly))
        m_lastWritten = QString::fromUtf8(file.readAll());
    else
        m_lastWritten.clear();

    m_values = merged;
    m_dirty = false;
    emit valuesChanged();
    emit dirtyChanged();
}

void Settings::scheduleSave()
{
    if (!m_dirty) {
        m_dirty = true;
        emit dirtyChanged();
    }
    m_saveTimer.start();
}

bool Settings::writeBack()
{
    // Only user-visible overrides are written; defaults stay out of the file.
    QVariantMap userValues;
    for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it) {
        if (m_defaults.value(it.key()) != it.value())
            userValues.insert(it.key(), it.value());
    }
    const QByteArray body = QJsonDocument(QJsonObject::fromVariantMap(userValues))
                                .toJson(QJsonDocument::Indented);

    QDir().mkpath(configDir());
    bool ok = false;
    if (Rust::available()) {
        // Atomic: temporary file, fsync, rename, plus a `.bak` backup.
        ok = Rust::writeFileAtomic(configFile(), body, /*backup=*/true);
    }
    if (!ok) {
        if (QFile::exists(configFile()))
            QFile::copy(configFile(), configFile() + QStringLiteral(".bak"));
        QSaveFile file(configFile());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(body);
            ok = file.commit();
        }
    }
    if (ok) {
        m_lastWritten = QString::fromUtf8(body);
        m_dirty = false;
        emit dirtyChanged();
    } else {
        qWarning() << "newteller: cannot write" << configFile();
    }
    return ok;
}

void Settings::flush()
{
    if (m_dirty)
        writeBack();
}

QVariant Settings::value(const QString &key, const QVariant &fallback) const
{
    return m_values.value(key, fallback);
}

void Settings::setValue(const QString &key, const QVariant &value)
{
    if (m_values.value(key) == value)
        return;
    m_values.insert(key, value);
    scheduleSave();
    emit valuesChanged();
    emit changed(key);
}

void Settings::setValues(const QVariantMap &values)
{
    bool touched = false;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (m_values.value(it.key()) != it.value()) {
            m_values.insert(it.key(), it.value());
            touched = true;
            emit changed(it.key());
        }
    }
    if (!touched)
        return;
    scheduleSave();
    emit valuesChanged();
}

void Settings::reset(const QString &key)
{
    if (!m_values.contains(key))
        return;
    if (m_defaults.contains(key))
        m_values.insert(key, m_defaults.value(key));
    else
        m_values.remove(key);
    scheduleSave();
    emit valuesChanged();
    emit changed(key);
}

bool Settings::has(const QString &key) const
{
    return m_values.contains(key);
}

QStringList Settings::keys() const
{
    QStringList out = m_defaults.keys();
    const QStringList userKeys = m_values.keys();
    for (const QString &key : userKeys) {
        if (!out.contains(key))
            out << key;
    }
    out.sort();
    return out;
}

QVariantList Settings::schema() const
{
    return Rust::available() ? Rust::configSchema() : QVariantList();
}

QVariantList Settings::validate() const
{
    return Rust::available() ? Rust::validateConfig(m_values) : QVariantList();
}

void Settings::reload()
{
    load();
    emit changed(QString());
}

void Settings::startWatching()
{
    if (Rust::available()) {
        const int debounce = m_values.value(QStringLiteral("watch.debounce"), 250).toInt();
        m_watcher = std::make_unique<Rust::Watcher>(debounce);
        if (m_watcher->isValid() && m_watcher->add(configDir(), /*recursive=*/true)) {
            m_notifier = new QSocketNotifier(m_watcher->descriptor(), QSocketNotifier::Read, this);
            connect(m_notifier, &QSocketNotifier::activated, this, &Settings::handleWatchEvents);
            m_debounceTimer.setSingleShot(true);
            connect(&m_debounceTimer, &QTimer::timeout, this, &Settings::handleWatchEvents);
            return;
        }
        m_watcher.reset();
    }

    // Fallback: watch the file itself (editors often replace it, so re-add).
    if (QFile::exists(configFile()))
        m_fsWatcher.addPath(configFile());
    connect(&m_fsWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        if (QFile::exists(configFile()) && !m_fsWatcher.files().contains(configFile()))
            m_fsWatcher.addPath(configFile());
        QFile file(configFile());
        if (file.open(QIODevice::ReadOnly)) {
            const QString body = QString::fromUtf8(file.readAll());
            if (body != m_lastWritten)
                reload();
        }
        emit externalChange(path);
    });
}

void Settings::handleWatchEvents()
{
    if (!m_watcher)
        return;
    const QVariantList events = m_watcher->poll();
    bool shellChanged = false;
    for (const QVariant &event : events) {
        const QVariantMap map = event.toMap();
        const QString path = map.value(QStringLiteral("path")).toString();
        emit externalChange(path);
        if (path == configFile() || path.endsWith(QLatin1String("/shell.json")))
            shellChanged = true;
    }
    if (shellChanged) {
        // Ignore the echo of our own save; reload on real external edits.
        QFile file(configFile());
        if (file.open(QIODevice::ReadOnly)) {
            const QString body = QString::fromUtf8(file.readAll());
            if (body != m_lastWritten)
                reload();
        }
    }
    // A debounced event may still be pending inside the watcher.
    const qint64 pending = m_watcher->pendingInMs();
    if (pending >= 0)
        m_debounceTimer.start(int(qMin<qint64>(pending, 60000)));
}
