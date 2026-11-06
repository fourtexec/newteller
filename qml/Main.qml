import QtQuick

// Root object. One desktop background and one panel per screen,
// plus a single start menu, run dialog and settings window on the primary
// screen.
//
// QtObject has no default property, so the windows are held in named
// properties. Instantiator (not Repeater, which can only create Items)
// creates the per-screen windows and recreates them when screens change.
QtObject {
    id: root

    // workspaces.names: "Work, Web, Chat"; unnamed desktops are numbered.
    function desktopName(index) {
        const names = String(Settings.values["workspaces.names"] || "").split(",");
        const name = index < names.length ? names[index].trim() : "";
        return name.length > 0 ? name : qsTr("Desktop %1").arg(index + 1);
    }

    readonly property StartMenu startMenu: StartMenu {
        screen: Qt.application.screens[0]
    }

    readonly property RunDialog runDialog: RunDialog {
        screen: Qt.application.screens[0]
    }

    // Super+V.
    readonly property ClipboardPopup clipboard: ClipboardPopup {
        screen: Qt.application.screens[0]
    }

    // Volume / brightness / media level pill for the hardware keys.
    readonly property OsdPopup osd: OsdPopup {
        screen: Qt.application.screens[0]
    }

    // Hardware keys arrive from the compositor (they work over any window
    // and on the lock screen): change the level, then show it.
    readonly property Connections mediaKeys: Connections {
        target: Session
        function onMediaKey(name) {
            if (name === "volume-up" || name === "volume-down") {
                if (!Quick.volumeAvailable)
                    return;
                const step = name === "volume-up" ? 5 : -5;
                const next = Math.max(0, Math.min(100, Math.round(Quick.volume) + step));
                if (Quick.volumeMuted && step > 0)
                    Quick.toggleMuted();
                Quick.setVolume(next);
                root.osd.show(next === 0 ? "volume-x" : "volume-2", next);
            } else if (name === "mute") {
                if (!Quick.volumeAvailable)
                    return;
                const muted = !Quick.volumeMuted;
                Quick.toggleMuted();
                root.osd.show(muted ? "volume-x" : "volume-2", muted ? 0 : Math.round(Quick.volume));
            } else if (name === "mic-mute") {
                Shell.runShell("wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle"
                               + " || pactl set-source-mute @DEFAULT_SOURCE@ toggle");
                root.osd.show("mic", -1, qsTr("Microphone mute toggled"));
            } else if (name === "brightness-up" || name === "brightness-down") {
                if (!Quick.brightnessAvailable)
                    return;
                const delta = name === "brightness-up" ? 5 : -5;
                const level = Math.max(1, Math.min(100, Math.round(Quick.brightness) + delta));
                Quick.setBrightness(level);
                root.osd.show("sun", level);
            } else if (name === "play-pause") {
                Quick.mediaPlayPause();
                root.osd.show(Quick.mediaPlaying ? "pause" : "play", -1,
                              Quick.mediaTitle.length > 0 ? Quick.mediaTitle : qsTr("Play / pause"));
            } else if (name === "next") {
                Quick.mediaNext();
                root.osd.show("skip-forward", -1, qsTr("Next track"));
            } else if (name === "previous") {
                Quick.mediaPrevious();
                root.osd.show("skip-back", -1, qsTr("Previous track"));
            } else if (name.startsWith("caps-") || name.startsWith("num-")) {
                if (Settings.values["keyboard.lockKeysOsd"] === false)
                    return;
                const on = name.endsWith("-on");
                if (name.startsWith("caps-"))
                    root.osd.show("keyboard", -1, on ? qsTr("Caps Lock on") : qsTr("Caps Lock off"));
                else
                    root.osd.show("keyboard", -1, on ? qsTr("Num Lock on") : qsTr("Num Lock off"));
            }
        }
    }

    // A tray icon that cannot be activated (Electron and AppIndicator apps:
    // Discord, Slack, Steam...): start the application behind it, which
    // brings its window back, instead of doing nothing.
    readonly property Connections trayFallback: Connections {
        target: Tray
        function onActivateFailed(id, candidates) {
            for (const candidate of candidates) {
                const app = Apps.matchWindowApp(candidate, "");
                if (app && app.id) {
                    Apps.launch(app.id);
                    return;
                }
            }
            // The application's name as the start menu knows it.
            for (const candidate of candidates) {
                const found = Apps.search(candidate, 1);
                if (found.length > 0 && found[0].id
                    && String(found[0].name).toLowerCase() === candidate.toLowerCase()) {
                    Apps.launch(found[0].id);
                    return;
                }
            }
            Shell.notify(qsTr("%1 cannot be opened from its icon")
                             .arg(candidates.length > 0 ? candidates[candidates.length - 1]
                                                        : qsTr("This application")),
                         qsTr("Right-click the icon for its menu."));
        }
    }

    // "Do not disturb for an hour" ends by itself; switching it off by hand
    // forgets the end time.
    readonly property Timer dndTimer: Timer {
        interval: 20000
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: {
            const until = Number(Settings.values["notifications.dndUntil"]) || 0;
            if (until <= 0)
                return;
            if (Settings.values["notifications.doNotDisturb"] !== true) {
                Settings.setValue("notifications.dndUntil", 0);
            } else if (Date.now() >= until) {
                Settings.setValue("notifications.doNotDisturb", false);
                Settings.setValue("notifications.dndUntil", 0);
            }
        }
    }

    // A new keyboard layout shows on screen for a moment (Super+Space,
    // Alt+Shift, the panel). Not with a layout per window, where every
    // window switch would show it.
    property int shownLayout: -1
    readonly property Connections layoutHint: Connections {
        target: Session
        function onKeyboardChanged() {
            const index = Session.keyboardLayout;
            const previous = root.shownLayout;
            root.shownLayout = index;
            if (previous < 0 || previous === index || Session.keyboardLayouts.length < 2
                || Settings.values["keyboard.layoutOsd"] === false
                || Settings.values["keyboard.layoutPerWindow"] === true)
                return;
            root.osd.show("keyboard", -1, Session.keyboardLayoutName);
        }
    }

    // workspaces.osd: the desktop's name on screen when switching.
    property int shownWorkspace: -1
    readonly property Connections workspaceHint: Connections {
        target: Session
        function onWorkspacesChanged() {
            const index = Session.workspace;
            const previous = root.shownWorkspace;
            root.shownWorkspace = index;
            if (previous < 0 || previous === index || Settings.values["workspaces.osd"] !== true)
                return;
            root.osd.show("layout-grid", -1, root.desktopName(index));
        }
    }

    // Low battery: one notification at 10% and one at 5% while discharging,
    // re-armed once the machine is plugged in.
    property int batteryWarned: 100
    // The power profile in use before the battery saver switched to
    // power-saver; restored when the machine is plugged in.
    property string profileBeforeSaver: ""
    readonly property Connections batteryWatch: Connections {
        target: System
        function onSnapshotUpdated() {
            if (!System.batteryPresent)
                return;
            if (System.batteryCharging) {
                root.batteryWarned = 100;
                if (root.profileBeforeSaver.length > 0) {
                    // Only if the user did not pick another profile since.
                    if (Session.powerProfile === "power-saver")
                        Session.setPowerProfile(root.profileBeforeSaver);
                    root.profileBeforeSaver = "";
                }
                return;
            }
            const level = Math.round(System.batteryPercent);
            if (level <= 20 && Settings.values["power.batterySaver"] !== false
                && root.profileBeforeSaver.length === 0
                && Session.powerProfiles.indexOf("power-saver") >= 0
                && Session.powerProfile !== "power-saver" && Session.powerProfile.length > 0) {
                root.profileBeforeSaver = Session.powerProfile;
                Session.setPowerProfile("power-saver");
                Shell.notify(qsTr("Battery saver on"),
                             qsTr("%1% left: the power saver profile is on until you plug in.").arg(level));
            }
            if (level <= 5 && root.batteryWarned > 5) {
                root.batteryWarned = 5;
                // Urgent: do-not-disturb must not hide it.
                Shell.notifyUrgent(qsTr("Battery critically low"),
                                   qsTr("%1% left. Plug in now or the computer will shut down.").arg(level),
                                   "battery");
            } else if (level <= 10 && root.batteryWarned > 10) {
                root.batteryWarned = 10;
                Shell.notify(qsTr("Battery low"), qsTr("%1% left. Plug in soon.").arg(level));
            }
        }
    }

    // The Alt+Tab window list; the compositor drives it through Session.
    readonly property SwitcherOverlay switcher: SwitcherOverlay {
        screen: Qt.application.screens[0]
    }

    // The notification centre and the quick settings card: one of each, on
    // the primary screen, opened from the bell and the indicators button of
    // every panel.
    readonly property NotificationCenter notificationCenter: NotificationCenter {
        screen: Qt.application.screens[0]
    }

    readonly property QuickSettingsPopup quickSettings: QuickSettingsPopup {
        screen: Qt.application.screens[0]
        notificationCenter: root.notificationCenter
        wifiPopup: root.wifiPopup
        bluetoothPopup: root.bluetoothPopup
        soundPopup: root.soundPopup
    }

    readonly property SoundPopup soundPopup: SoundPopup {
        screen: Qt.application.screens[0]
    }

    readonly property BluetoothPopup bluetoothPopup: BluetoothPopup {
        screen: Qt.application.screens[0]
    }

    // Super+Tab: every window as a thumbnail, desktops on top.
    readonly property TaskView taskView: TaskView {
        screen: Qt.application.screens[0]
    }

    // The administrator prompt (polkit): "Open with root", system changes.
    readonly property AuthDialog authDialog: AuthDialog {
        screen: Qt.application.screens[0]
    }

    // Right click on a title bar, Alt+Space: minimize, maximize, always on
    // top, move to another desktop, close.
    readonly property WindowMenu windowMenu: WindowMenu {
        screen: Qt.application.screens[0]
    }

    readonly property Connections windowMenuRequests: Connections {
        target: Session
        function onWindowMenuRequested(id, x, y, desktop, flags, title) {
            root.startMenu.hide();
            root.windowMenu.openAt(id, x, y, desktop, flags, title);
        }
    }

    // Ctrl+Shift+Esc: end a frozen application.
    readonly property ForceQuitWindow forceQuit: ForceQuitWindow {
        screen: Qt.application.screens[0]
    }

    readonly property WifiPopup wifiPopup: WifiPopup {
        screen: Qt.application.screens[0]
    }

    // The built-in settings application. Its QML is compiled into the
    // executable, so it is always there and cannot be deleted like a plugin.
    readonly property SettingsApp settingsApp: SettingsApp {
        screen: Qt.application.screens[0]
    }

    readonly property Instantiator desktops: Instantiator {
        model: Qt.application.screens

        delegate: Desktop {
            required property var modelData
            screen: modelData
        }
    }

    readonly property Instantiator panels: Instantiator {
        model: Qt.application.screens

        delegate: Panel {
            required property var modelData
            screen: modelData
            startMenu: root.startMenu
            notificationCenter: root.notificationCenter
            quickSettings: root.quickSettings
        }
    }

    // Notification popups, one window per screen; fed by the built-in
    // notification daemon (org.freedesktop.Notifications and the
    // newteller-ctl socket). The component is named NotificationPopups so the
    // `Notifications` context property (the daemon) stays unambiguous.
    readonly property Instantiator notificationPopups: Instantiator {
        model: Qt.application.screens

        delegate: NotificationPopups {
            required property var modelData
            screen: modelData
        }
    }

    // Settings -> Applications -> Startup applications, once per start after
    // the configured delay. An entry is an application id from the start
    // menu, or else a command line.
    readonly property Timer startupApps: Timer {
        interval: Math.max(0, Number(Settings.value("startup.delay", 0)) || 0) * 1000 + 500
        running: true
        repeat: false
        onTriggered: {
            const list = Settings.value("startup.apps", []) || [];
            for (var i = 0; i < list.length; ++i) {
                const entry = String(list[i]).trim();
                if (entry.length === 0)
                    continue;
                const id = entry.endsWith(".desktop") && entry.indexOf("/") < 0
                           ? entry.slice(0, -8) : entry;
                const app = Apps.byId(id);
                if (app && app.id)
                    Apps.launch(app.id);
                else
                    Shell.runShell(entry);
            }
        }
    }

    // Alt+F2 opens the run dialog anywhere in the session.
    readonly property Shortcut runShortcut: Shortcut {
        sequence: "Alt+F2"
        onActivated: {
            root.startMenu.hide();
            root.runDialog.toggle();
        }
    }

    // The panel of the screen under the pointer (multi-monitor), or null
    // outside a NewTeller session.
    function activePanel() {
        if (!Session.activeOutput)
            return null;
        for (let i = 0; i < root.panels.count; ++i) {
            const panel = root.panels.objectAt(i);
            if (panel && panel.visible && panel.screen && panel.screen.name === Session.activeOutput)
                return panel;
        }
        return null;
    }

    // A card asked for from the keyboard opens on the screen in use.
    function toggleOnActiveScreen(dropdown, kind) {
        const panel = root.activePanel();
        if (panel)
            panel.openFromKeyboard(dropdown, kind);
        else
            dropdown.toggle();
    }

    // The start menu, the desktop menu and `newteller --settings` all open the
    // same embedded settings window.
    readonly property Connections shellConnections: Connections {
        target: Shell
        function onSettingsRequested() {
            root.startMenu.hide();
            root.settingsApp.showFromShell();
        }
        function onSettingsPageRequested(page) {
            root.startMenu.hide();
            if (root.settingsApp.pages.some(p => p.id === page))
                root.settingsApp.currentPage = page;
            root.settingsApp.showFromShell();
        }
        function onStartMenuRequested() {
            if (!root.startMenu.visible)
                root.toggleOnActiveScreen(root.startMenu, "start");
        }
        function onNotificationsToggleRequested() {
            root.startMenu.hide();
            root.toggleOnActiveScreen(root.notificationCenter, "bell");
        }
        function onTaskViewToggleRequested() {
            root.startMenu.hide();
            root.taskView.toggle();
        }
        function onForceQuitRequested() {
            root.startMenu.hide();
            root.forceQuit.show();
        }
        function onClipboardToggleRequested() {
            root.startMenu.hide();
            root.toggleOnActiveScreen(root.clipboard, "");
        }
        function onQuickSettingsToggleRequested() {
            root.startMenu.hide();
            root.toggleOnActiveScreen(root.quickSettings, "indicators");
        }
        function onStartMenuToggleRequested() {
            root.runDialog.hide();
            root.toggleOnActiveScreen(root.startMenu, "start");
        }
        function onRunDialogRequested() {
            root.startMenu.hide();
            root.runDialog.show();
        }
        function onScreenRecordingToggleRequested() {
            Recorder.toggle();
        }
        function onDockItemRequested(index) {
            // The panel of the first screen: every panel shows the same dock.
            const panel = root.panels.objectAt(0);
            if (panel) {
                root.startMenu.hide();
                panel.activateDockIndex(index);
            }
        }
    }
}
