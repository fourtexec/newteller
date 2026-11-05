#include "settings.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJSValue>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDebug>

namespace {
// Lists and objects set from QML arrive as a QJSValue, which QJsonDocument
// writes as null: the dock pins, the run history and the sticky notes were
// lost on the next start. Keep plain QVariantList / QVariantMap instead.
QVariant plainValue(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QJSValue>())
        return value.value<QJSValue>().toVariant();
    return value;
}
} // namespace

QString Settings::configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/newteller");
}

QString Settings::configFile()
{
    return configDir() + QStringLiteral("/shell.json");
}

QString Settings::terminalCommand() const
{
    const QString configured = value(QStringLiteral("apps.terminalCommand")).toString().simplified();
    const QString program = configured.section(QLatin1Char(' '), 0, 0);
    if (!program.isEmpty() && !QStandardPaths::findExecutable(program).isEmpty())
        return configured;
    return QStringLiteral("newteller-terminal");
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
    // false: one dock button per window, not per application.
    m_defaults.insert(QStringLiteral("dock.group"), false);
    m_defaults.insert(QStringLiteral("dock.iconSize"), 28);
    m_defaults.insert(QStringLiteral("dock.separateBackground"), true);
    m_defaults.insert(QStringLiteral("dock.shrinkIcons"), true);
    m_defaults.insert(QStringLiteral("panel.indicators"), true);
    m_defaults.insert(QStringLiteral("tray.enabled"), true);
    m_defaults.insert(QStringLiteral("tray.menus"), true);
    m_defaults.insert(QStringLiteral("windows.style"), QStringLiteral("card"));
    m_defaults.insert(QStringLiteral("windows.material"), QStringLiteral("theme"));
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
    // Without a chosen wallpaper the desktop shows the bundled one (false:
    // the plain themed gradient).
    m_defaults.insert(QStringLiteral("desktop.defaultWallpaper"), true);
    // Where the user put icons and widgets on the desktop grid:
    // "icon:<file>" / "widget:<plugin id>" -> {col, row}.
    m_defaults.insert(QStringLiteral("desktop.gridPositions"), QVariantMap());
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
    // newteller-terminal picks the installed terminal (x-terminal-emulator
    // exists only on Debian and Ubuntu).
    m_defaults.insert(QStringLiteral("apps.terminalCommand"), QStringLiteral("newteller-terminal"));
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
    m_defaults.insert(QStringLiteral("panel.material"), QStringLiteral("theme"));
    m_defaults.insert(QStringLiteral("panel.foregroundColor"), QString());
    m_defaults.insert(QStringLiteral("panel.customColors"), false);
    // Dock styling
    m_defaults.insert(QStringLiteral("dock.backgroundColor"), QString());
    m_defaults.insert(QStringLiteral("dock.backgroundOpacity"), 0.5);
    m_defaults.insert(QStringLiteral("dock.material"), QStringLiteral("translucent"));
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
    m_defaults.insert(QStringLiteral("dock.thumbnails"), true);
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
    // Pixel positions from before the desktop grid (desktop.gridPositions):
    // { "notes.txt": { "x": 120, "y": 48 }, ... } and plugin id -> {x, y}.
    // Read once to keep an old layout; the next drag or reset clears them.
    m_defaults.insert(QStringLiteral("desktop.iconPositions"), QVariantMap());
    m_defaults.insert(QStringLiteral("desktop.widgetPositions"), QVariantMap());

    // ===== Notifications =====
    m_defaults.insert(QStringLiteral("notifications.theme"), QStringLiteral("system"));
    m_defaults.insert(QStringLiteral("notifications.material"), QStringLiteral("theme"));
    m_defaults.insert(QStringLiteral("notifications.opacityPercent"), 0);
    m_defaults.insert(QStringLiteral("notifications.cornerRadius"), -1);
    m_defaults.insert(QStringLiteral("notifications.customBackground"), QString());
    m_defaults.insert(QStringLiteral("notifications.customForeground"), QString());
    m_defaults.insert(QStringLiteral("notifications.groupByApp"), false);
    m_defaults.insert(QStringLiteral("notifications.showAppIcon"), true);
    m_defaults.insert(QStringLiteral("drives.notify"), true);
    // The weather widget: nothing is fetched until a city is set.
    m_defaults.insert(QStringLiteral("weather.location"), QString());
    m_defaults.insert(QStringLiteral("weather.units"), QStringLiteral("metric"));
    m_defaults.insert(QStringLiteral("weather.showForecast"), true);
    // The sticky notes widget: [{id, text, x, y, width, height}, ...].
    m_defaults.insert(QStringLiteral("stickyNotes.notes"), QVariantList());
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
    // Hot corners: none | overview | desktop | startMenu | notifications | lock.
    m_defaults.insert(QStringLiteral("hotCorners.topLeft"), QStringLiteral("none"));
    m_defaults.insert(QStringLiteral("hotCorners.topRight"), QStringLiteral("none"));
    m_defaults.insert(QStringLiteral("hotCorners.bottomLeft"), QStringLiteral("none"));
    m_defaults.insert(QStringLiteral("hotCorners.bottomRight"), QStringLiteral("none"));
    // Screen recordings (wf-recorder) also record the default microphone.
    m_defaults.insert(QStringLiteral("recorder.audio"), false);
    // Power management
    m_defaults.insert(QStringLiteral("power.sleepAction"), QStringLiteral("suspend"));
    m_defaults.insert(QStringLiteral("power.lockOnSleep"), true);
    // Minutes of inactivity before the screen locks / turns off (0 = never).
    m_defaults.insert(QStringLiteral("power.lockAfter"), 10);
    m_defaults.insert(QStringLiteral("power.screenOffAfter"), 10);
    // Night light (wlsunset): warmer colours in the evening.
    m_defaults.insert(QStringLiteral("nightLight.enabled"), false);
    m_defaults.insert(QStringLiteral("nightLight.temperature"), 4000);
    m_defaults.insert(QStringLiteral("nightLight.schedule"), QStringLiteral("custom"));
    m_defaults.insert(QStringLiteral("nightLight.from"), QStringLiteral("20:00"));
    m_defaults.insert(QStringLiteral("nightLight.to"), QStringLiteral("07:00"));
    // Keyboard (mirrored into compositor.conf). Empty layouts = the system
    // keyboard configuration (/etc/default/keyboard).
    m_defaults.insert(QStringLiteral("keyboard.layouts"), QString());
    m_defaults.insert(QStringLiteral("keyboard.variants"), QString());
    m_defaults.insert(QStringLiteral("keyboard.switchKeys"), QStringLiteral("grp:alt_shift_toggle"));
    m_defaults.insert(QStringLiteral("keyboard.options"), QString());
    m_defaults.insert(QStringLiteral("keyboard.repeatRate"), 25);
    m_defaults.insert(QStringLiteral("keyboard.repeatDelay"), 600);
    m_defaults.insert(QStringLiteral("keyboard.numlock"), false);
    // Every window remembers its keyboard layout (as on Windows).
    m_defaults.insert(QStringLiteral("keyboard.layoutPerWindow"), false);
    // Show Caps Lock / Num Lock changes on screen.
    m_defaults.insert(QStringLiteral("keyboard.lockKeysOsd"), true);
    m_defaults.insert(QStringLiteral("keyboard.showIndicator"), true);
    // Pointer devices (libinput, applied by the compositor).
    m_defaults.insert(QStringLiteral("touchpad.tapToClick"), true);
    m_defaults.insert(QStringLiteral("touchpad.naturalScroll"), true);
    m_defaults.insert(QStringLiteral("touchpad.disableWhileTyping"), true);
    m_defaults.insert(QStringLiteral("touchpad.gestures"), true);
    // NewTeller's own administrator prompt instead of an external polkit agent.
    m_defaults.insert(QStringLiteral("security.builtinAgent"), true);
    m_defaults.insert(QStringLiteral("touchpad.speed"), 0.0);
    m_defaults.insert(QStringLiteral("mouse.naturalScroll"), false);
    m_defaults.insert(QStringLiteral("mouse.speed"), 0.0);
    m_defaults.insert(QStringLiteral("mouse.leftHanded"), false);
    m_defaults.insert(QStringLiteral("cursor.theme"), QString());
    m_defaults.insert(QStringLiteral("cursor.size"), 24);
    // Colour scheme applications follow: "system" (leave it), "dark", "light".
    m_defaults.insert(QStringLiteral("apps.colorScheme"), QStringLiteral("dark"));
    // Language of the shell: "auto" follows the system locale; "en", "ru".
    m_defaults.insert(QStringLiteral("shell.language"), QStringLiteral("auto"));
    // Virtual desktops (Ctrl+Super+Left/Right) and their panel indicator.
    m_defaults.insert(QStringLiteral("workspaces.count"), 4);
    m_defaults.insert(QStringLiteral("workspaces.showOnPanel"), true);
    // The Alt+Tab overlay with the windows and their titles.
    m_defaults.insert(QStringLiteral("switcher.showOverlay"), true);
    m_defaults.insert(QStringLiteral("switcher.allWorkspaces"), false);
    m_defaults.insert(QStringLiteral("workspaces.wrap"), false);
    // Title bars drawn by the compositor for windows that want them.
    m_defaults.insert(QStringLiteral("decorations.server"), true);
    m_defaults.insert(QStringLiteral("decorations.followTheme"), true);
    m_defaults.insert(QStringLiteral("decorations.titleHeight"), 30);
    m_defaults.insert(QStringLiteral("decorations.border"), 1);
    m_defaults.insert(QStringLiteral("decorations.buttonsLeft"), false);
    m_defaults.insert(QStringLiteral("decorations.titleCentered"), true);
    m_defaults.insert(QStringLiteral("decorations.showMinimize"), true);
    m_defaults.insert(QStringLiteral("decorations.showMaximize"), true);
    m_defaults.insert(QStringLiteral("decorations.font"), QString());
    m_defaults.insert(QStringLiteral("decorations.activeBackground"), QStringLiteral("#262626"));
    m_defaults.insert(QStringLiteral("decorations.activeForeground"), QStringLiteral("#f2f2f5"));
    m_defaults.insert(QStringLiteral("decorations.inactiveBackground"), QStringLiteral("#1a1a1a"));
    m_defaults.insert(QStringLiteral("decorations.inactiveForeground"), QStringLiteral("#8e8e93"));
    m_defaults.insert(QStringLiteral("decorations.closeHover"), QStringLiteral("#e5484d"));
    // Window behaviour (compositor).
    m_defaults.insert(QStringLiteral("windows.focusFollowsMouse"), false);
    m_defaults.insert(QStringLiteral("windows.placement"), QStringLiteral("cascade"));
    m_defaults.insert(QStringLiteral("windows.snapping"), true);
    // The user's own shortcuts: [{ "keys": "Super+T", "command": "..." }].
    m_defaults.insert(QStringLiteral("shortcuts.custom"), QVariantList());
    // Applications' look (GTK/GNOME settings).
    m_defaults.insert(QStringLiteral("apps.gtkTheme"), QString());
    m_defaults.insert(QStringLiteral("apps.iconTheme"), QString());
    m_defaults.insert(QStringLiteral("apps.font"), QString());
    m_defaults.insert(QStringLiteral("apps.textScale"), 1.0);
    // Desktop effects drawn by the compositor.
    m_defaults.insert(QStringLiteral("effects.enabled"), true);
    m_defaults.insert(QStringLiteral("effects.cornerRadius"), 10);
    m_defaults.insert(QStringLiteral("effects.blur"), true);
    m_defaults.insert(QStringLiteral("effects.blurStrength"), 2);
    m_defaults.insert(QStringLiteral("effects.animations"), true);
    m_defaults.insert(QStringLiteral("effects.animationSpeed"), 1.0);
    // Lock screen.
    m_defaults.insert(QStringLiteral("lock.color"), QStringLiteral("#1e1e2e"));
    m_defaults.insert(QStringLiteral("lock.useWallpaper"), true);
    // Clipboard
    m_defaults.insert(QStringLiteral("clipboard.historyEnabled"), true);
    m_defaults.insert(QStringLiteral("clipboard.historySize"), 100);
    // Off by default: copied passwords should not end up on disk.
    m_defaults.insert(QStringLiteral("clipboard.persistHistory"), false);

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
        for (auto it = value.constBegin(); it != value.constEnd(); ++it) {
            // null: a list or object an older version failed to save; the
            // default is better than nothing.
            if (!it.value().isNull())
                merged.insert(it.key(), it.value());
        }
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
                for (auto it = user.cbegin(); it != user.cend(); ++it) {
                    if (!it.value().isNull())
                        merged.insert(it.key(), it.value());
                }
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

void Settings::setValue(const QString &key, const QVariant &input)
{
    const QVariant value = plainValue(input);
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
        const QVariant value = plainValue(it.value());
        if (m_values.value(it.key()) != value) {
            m_values.insert(it.key(), value);
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
