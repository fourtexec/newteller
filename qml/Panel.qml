import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import org.kde.layershell 1.0 as LayerShell
import NewTeller 1.0

// Panel: start button, the dock (pinned applications + every running window),
// plugin slots and the clock. The position is configurable (bottom by default,
// also top/left/right), the bar can be narrow and centred, and auto-hide
// slides it away after a short delay.
//
// The dock adapts to the situation: applications whose windows are all
// minimised move into a separate, dimmed zone at the end of the strip, the
// icons shrink when many applications run at once, and the strip becomes
// scrollable when even the smallest icons do not fit.
Window {
    id: panel

    property var startMenu
    // The single notification centre and quick settings card on the primary
    // screen; assigned by Main.qml. The bell and the indicators button live
    // in every panel, so they talk to these shared windows.
    property var notificationCenter: null
    property var quickSettings: null
    // "bottom", "top", "left" or "right".
    readonly property string panelPosition: Settings.values["panel.position"] || "bottom"
    readonly property bool vertical: panelPosition === "left" || panelPosition === "right"
    readonly property bool autoHide: Settings.values["panel.autoHide"] === true
    readonly property int barThickness: Settings.values["panel.height"] || 48
    readonly property int edgeMargin: Settings.values["panel.margin"] || 0
    readonly property bool revealed: !autoHide || hoverArea.containsMouse || menuWanted
    // Set while the start menu is open, so the panel stays up next to it.
    property bool menuWanted: false
    readonly property var panelPlugins: Plugins.plugins.filter(p => p.type === "panel" && p.enabled)

    Motion {
        id: motion
    }

    // --- dock configuration ---------------------------------------------
    // Every started application is shown: windows come from the `Windows`
    // object (EWMH on X11, wlr-foreign-toplevel on Wayland), with the /proc
    // based application list of `Apps` as the fallback on other compositors.
    readonly property bool dockShowWindows: Settings.values["dock.showWindows"] !== false
    readonly property bool dockGroup: Settings.values["dock.group"] !== false
    readonly property bool separateBackground: Settings.values["dock.separateBackground"] !== false
    readonly property bool shrinkIcons: Settings.values["dock.shrinkIcons"] !== false
    readonly property int dockIconSize: Settings.values["dock.iconSize"] || 28
    // The quick settings cluster at the end of the bar (bell + indicators).
    readonly property bool showIndicators: Settings.values["panel.indicators"] !== false
    // The tray shows only when the shell won the StatusNotifierWatcher name
    // and at least one item registered.
    readonly property bool showTray: Tray.available
                                     && Settings.values["tray.enabled"] !== false
                                     && Tray.count > 0
    // Icon size after the adaptive shrink pass (recomputed in rebuildDock).
    property int effectiveIconSize: dockIconSize
    readonly property bool showDockLabels: Settings.values["dock.showLabels"] === true
    readonly property string dockLabelPosition: Settings.values["dock.labelPosition"] || "bottom"
    readonly property string dockIconColorMode: Settings.values["dock.iconColorMode"] || "theme"
    readonly property string dockCustomIconColor: Settings.values["dock.customIconColor"] || "#ffffff"
    readonly property bool useCustomPanelColors: Settings.values["panel.customColors"] === true
    readonly property string panelBackgroundColor: Settings.values["panel.backgroundColor"] || ""
    readonly property real panelBackgroundOpacity: Settings.values["panel.backgroundOpacity"] || 0.92
    readonly property string panelForegroundColor: Settings.values["panel.foregroundColor"] || ""
    readonly property int panelSpacing: Settings.values["panel.spacing"] || 8
    readonly property int panelLeftSpacing: Settings.values["panel.leftSpacing"] || 8
    readonly property int panelRightSpacing: Settings.values["panel.rightSpacing"] || 8
    readonly property int panelZoneSpacing: Settings.values["panel.zoneSpacing"] || 8
    readonly property string dockPositionInPanel: Settings.values["dock.position"] || "left"
    readonly property bool centerDock: Settings.values["panel.centerDock"] === true
    readonly property bool dockMultiRow: Settings.values["dock.multiRow"] === true
    readonly property int dockRows: Settings.values["dock.rows"] || 1
    readonly property bool dockScrollable: Settings.values["dock.scrollable"] === true
    readonly property int dockIconSpacing: Settings.values["dock.iconSpacing"] || 4
    // One entry per dock button:
    //   { appId, app, windows: [], running, active, badge, title, pinned, background }
    // `dockEntries` is the combined list (main zone first), so the dock menu
    // and external users always see everything.
    property var dockEntries: []
    property var dockMainEntries: []
    property var dockBackgroundEntries: []

    visible: true
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool
    // The hover strip of an auto-hidden panel is a few pixels at the edge.
    readonly property int stripSize: 4
    readonly property int span: revealed ? barThickness : stripSize

    width: vertical ? span + (revealed ? edgeMargin : 0) : Screen.width
    height: vertical ? Screen.height : span + (revealed ? edgeMargin : 0)
    x: {
        if (panelPosition === "left")
            return Screen.virtualX + (revealed ? edgeMargin : 0);
        if (panelPosition === "right")
            return Screen.virtualX + Screen.width - width;
        return Screen.virtualX;
    }
    y: {
        if (panelPosition === "top")
            return Screen.virtualY + (revealed ? edgeMargin : 0);
        if (panelPosition === "bottom")
            return Screen.virtualY + Screen.height - height;
        return Screen.virtualY;
    }

    // Wayland: a layer surface along the chosen edge that reserves its space.
    LayerShell.Window.anchors: {
        if (panelPosition === "top")
            return LayerShell.Window.AnchorTop | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight;
        if (panelPosition === "left")
            return LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom;
        if (panelPosition === "right")
            return LayerShell.Window.AnchorRight | LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom;
        return LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight;
    }
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.exclusionZone: revealed ? barThickness : stripSize
    LayerShell.Window.scope: "newteller-panel"
    LayerShell.Window.margins {
        left: panel.panelPosition === "left" && panel.revealed ? panel.edgeMargin : 0
        right: panel.panelPosition === "right" && panel.revealed ? panel.edgeMargin : 0
        top: panel.panelPosition === "top" && panel.revealed ? panel.edgeMargin : 0
        bottom: panel.panelPosition === "bottom" && panel.revealed ? panel.edgeMargin : 0
    }

    // X11: a dock window that reserves the chosen edge.
    ShellWindow.role: ShellWindow.Dock

    // The clock popup, opened by tapping the clock at the end of the bar.
    ClockPopup {
        id: clockPopup
        panelPosition: panel.panelPosition
        panelThickness: panel.barThickness
        edgeMargin: panel.edgeMargin
    }

    // One tooltip window per panel; hovering a panel button points it at the
    // button (showToolTip below).
    PanelToolTip {
        id: panelToolTip
        position: panel.panelPosition
        panelThickness: panel.barThickness
    }

    function showToolTip(item, text) {
        if (!text || text.length === 0)
            return;
        const centre = item.mapToItem(null, item.width / 2, item.height / 2);
        panelToolTip.showAt(text, centre.x, centre.y);
    }

    function hideToolTip() {
        panelToolTip.hide();
    }

    // Kept in sync with the start menu so the panel does not slide away
    // underneath it.
    Connections {
        target: panel.startMenu
        function onVisibleChanged() {
            panel.menuWanted = panel.startMenu.visible;
        }
    }

    // --- dock model -------------------------------------------------------

    function rebuildDock() {
        var result = [];
        var seen = {};

        // Pinned applications first, in the pinned order. They keep their slot
        // even when all of their windows are minimised.
        const pinned = Settings.values["panel.pinned"] || [];
        for (const id of pinned) {
            const app = Apps.byId(id);
            if (app.id === undefined)
                continue;
            seen[id] = true;
            result.push({ appId: id, app: app, windows: [], running: false, active: false,
                          badge: 0, pinned: true, background: false });
        }

        // Running top-level windows, grouped into one button per application.
        if (dockShowWindows) {
            var loose = [];
            var groups = {};
            var order = [];
            for (const w of Windows.windows) {
                const app = Apps.matchWindowApp(w.appId || "", w.title || "");
                const appId = app.id || "";
                if (appId.length > 0) {
                    if (!groups[appId]) {
                        groups[appId] = [];
                        order.push(appId);
                    }
                    groups[appId].push(w);
                } else {
                    loose.push({ appId: "", app: {}, windows: [w], running: true, active: !!w.active,
                                 badge: 1, title: w.title || "", pinned: false, background: false });
                }
            }
            for (const appId of order) {
                const wins = groups[appId];
                var target = null;
                for (const e of result) {
                    if (e.appId === appId) {
                        target = e;
                        break;
                    }
                }
                if (target) {
                    target.windows = wins;
                } else {
                    seen[appId] = true;
                    result.push({ appId: appId, app: Apps.byId(appId), windows: wins,
                                  running: true, active: false, badge: wins.length,
                                  pinned: false, background: false });
                }
            }
            for (const entry of loose)
                result.push(entry);
        }

        // Compositors without a window list: the /proc based running ids.
        for (const id of Apps.runningIds) {
            if (seen[id])
                continue;
            seen[id] = true;
            result.push({ appId: id, app: Apps.byId(id), windows: [], running: true,
                          active: false, badge: 0, pinned: false, background: false });
        }

        for (const e of result) {
            for (const w of e.windows) {
                if (w.active) {
                    e.active = true;
                    e.running = true;
                }
            }
            if (e.windows.length > 0) {
                e.running = true;
                e.badge = e.windows.length;
            }
            if (e.appId.length > 0 && Apps.isRunning(e.appId))
                e.running = true;
            // Everything that only has minimised windows went to the
            // background: parked at the end of the strip, dimmed.
            const allMinimized = e.windows.length > 0 && e.windows.every(w => w.minimized);
            e.background = separateBackground && allMinimized && !e.pinned;
        }

        var main = [];
        var background = [];
        for (const e of result)
            (e.background ? background : main).push(e);
        dockMainEntries = main;
        dockBackgroundEntries = background;
        dockEntries = main.concat(background);
        updateAdaptiveDock(main.length + background.length);
    }

    // Shrinks the dock icons while the strip would overflow, and hides the
    // window-title labels first. The strip itself scrolls when even the
    // smallest icons do not fit.
    function updateAdaptiveDock(count) {
        const base = dockIconSize;
        const span = vertical ? Screen.height : Screen.width;
        const reserved = 64 /* start */ + 14 /* separators */
                       + (vertical ? 26 : 88) /* clock */
                       + panelPlugins.length * 30 + 24;
        const available = Math.max(120, span - reserved);
        var size = base;
        var labels = !dockGroup && !vertical;
        const widthFor = (s, withLabels) => count * (s + 14 + (withLabels ? 108 : 0));
        if (widthFor(size, labels) > available)
            labels = false;
        if (shrinkIcons) {
            while (size > 16 && widthFor(size, labels) > available)
                size -= 2;
        }
        effectiveIconSize = Math.max(16, size);
        showDockLabels = labels;
    }

    function activateEntry(entry) {
        if (entry.windows.length > 0) {
            var target = null;
            for (const w of entry.windows) {
                if (w.active) {
                    target = w;
                    break;
                }
            }
            target = target || entry.windows[0];
            if (target.readOnly) {
                // A read-only toplevel (ext_foreign_toplevel_list, e.g. GNOME
                // Wayland) cannot be focused from here: launching the matched
                // application is the best effort - single-instance apps raise
                // their existing window.
                if (entry.appId.length > 0)
                    Apps.launch(entry.appId);
                return;
            }
            Windows.activate(target.id);
        } else if (entry.appId.length > 0) {
            Apps.launch(entry.appId);
        }
    }

    // A dock click is a toggle, the way modern docks behave: an already
    // focused single window is sent to the background, everything else is
    // activated (which also restores a minimised window).
    function dockClicked(entry) {
        if (entry.windows.length === 1 && entry.windows[0].active && !entry.windows[0].minimized) {
            Windows.minimize(entry.windows[0].id);
            return;
        }
        activateEntry(entry);
    }

    function closeEntry(entry) {
        for (const w of entry.windows)
            Windows.closeWindow(w.id);
    }

    function openDockMenu(entry, button) {
        dockMenu.openFor(entry, button.x + button.width / 2, button.y + button.height / 2);
    }

    Component.onCompleted: rebuildDock()
    Connections {
        target: Windows
        function onWindowsChanged() { panel.rebuildDock(); dockMenu.refreshEntry(); }
    }
    Connections {
        target: Apps
        function onRunningChanged() { panel.rebuildDock(); }
        function onCountChanged() { panel.rebuildDock(); }
    }
    Connections {
        target: Settings
        function onValuesChanged() { panel.rebuildDock(); }
    }

    DockMenu {
        id: dockMenu
        screen: panel.screen
        entriesProvider: panel
        panelThickness: panel.barThickness
        position: panel.panelPosition
    }

    // Shared pieces of the bar, used by both layouts below.
    component StartButton: TaskButton {
        objectName: "startButton"
        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
        iconName: "layout-grid"
        label: "Start"
        onClicked: {
            panel.menuWanted = true;
            panel.startMenu.toggle();
        }
    }

    component DockApps: Repeater {
        property bool backgroundZone: false
        model: backgroundZone ? panel.dockBackgroundEntries : panel.dockMainEntries

        delegate: TaskButton {
            id: dockButton
            required property var modelData
            readonly property var entry: modelData
            readonly property string appName: entry.app && entry.app.name ? entry.app.name
                                                                          : (entry.title || "Application")

            height: dockStrip.height
            iconName: entry.app && entry.app.icon ? entry.app.icon
                    : (entry.appId.length > 0 ? "square" : "app-window")
            label: panel.showDockLabels && entry.title && entry.title.length > 0
                   ? entry.title : ""
            dockLabel: panel.showDockLabels && entry.title && entry.title.length > 0
                      ? entry.title : ""
            iconSize: panel.effectiveIconSize
            showDockLabels: panel.showDockLabels
            dockIconColorMode: panel.dockIconColorMode
            dockCustomIconColor: panel.dockCustomIconColor
            running: entry.running
            active: entry.active
            dimmed: entry.background === true
            badge: entry.badge
            onClicked: panel.dockClicked(entry)
            onMiddleClicked: panel.closeEntry(entry)
            HoverHandler {
                onHoveredChanged: {
                    if (hovered)
                        panel.showToolTip(dockButton,
                                          dockButton.entry.windows.length > 1
                                              ? dockButton.appName + " ("
                                                + dockButton.entry.windows.length + " windows)"
                                              : (dockButton.entry.title && dockButton.entry.title.length > 0
                                                 ? dockButton.entry.title : dockButton.appName));
                    else
                        panel.hideToolTip();
                }
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: panel.openDockMenu(dockButton.entry, dockButton)
            }
        }
    }

    // Divider between the running zone and the minimised/background zone.
    component BackgroundSeparator: Rectangle {
        visible: panel.separateBackground && panel.dockBackgroundEntries.length > 0
        Layout.preferredWidth: panel.vertical ? 24 : 1
        Layout.preferredHeight: panel.vertical ? 1 : 24
        Layout.alignment: Qt.AlignVCenter
        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.55)
    }

    component PluginSlot: Repeater {
        model: panel.panelPlugins

        delegate: Loader {
            required property var modelData
            Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
            source: modelData.entry
        }
    }

    component ClockText: Text {
        text: (Settings.values["panel.showDate"]
               ? Qt.formatDateTime(Shell.now, Settings.values["panel.dateFormat"] || "ddd d MMM") + "  "
               : "")
              + Qt.formatDateTime(Shell.now, Settings.values["panel.clockFormat"] || "HH:mm")
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pointSize: Theme.fontSize
    }

    // The clock is a button: tapping it opens the clock popup with the
    // analogue dial and the month calendar.
    component ClockButton: Rectangle {
        id: clockButton
        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
        implicitWidth: clockLabel.implicitWidth + 18
        implicitHeight: Math.min(panel.barThickness - 10, clockLabel.implicitHeight + 10)
        radius: Theme.radius
        color: clockHover.hovered
               ? Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.08)
               : "transparent"
        Behavior on color {
            ColorAnimation {
                duration: motion.micro
            }
        }

        HoverHandler {
            id: clockHover
            onHoveredChanged: {
                if (hovered)
                    panel.showToolTip(clockButton,
                                      Qt.formatDateTime(Shell.now, "dddd, d MMMM yyyy  HH:mm"));
                else
                    panel.hideToolTip();
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: clockPopup.toggle()
        }
        ClockText {
            id: clockLabel
            anchors.centerIn: parent
            horizontalAlignment: Text.AlignHCenter
        }
    }

    component Separator: Rectangle {
        Layout.preferredWidth: panel.vertical ? 24 : 1
        Layout.preferredHeight: panel.vertical ? 1 : 24
        Layout.alignment: Qt.AlignVCenter
        color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.8)
    }

    // --- system tray -------------------------------------------------------
    // Icons of applications that registered with our StatusNotifierWatcher:
    // left click activates, middle click secondary-activates, right click
    // asks the item to open its own context menu, and the wheel scrolls.
    component TrayBox: Row {
        spacing: 2
        visible: panel.showTray
        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter

        Repeater {
            model: Tray.items

            delegate: Item {
                id: trayIcon
                required property var modelData
                readonly property string entryId: modelData.id
                objectName: "trayButton"

                width: 26
                height: 26
                opacity: modelData.status === "Passive" ? 0.55 : 1.0
                Behavior on opacity {
                    NumberAnimation { duration: motion.fast }
                }

                Image {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    sourceSize.width: 40
                    sourceSize.height: 40
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    asynchronous: true
                    source: Tray.iconUrl(trayIcon.entryId)
                    // Growing in when the icon first renders, so a tray
                    // refresh does not pop.
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity {
                        NumberAnimation { duration: motion.micro }
                    }
                }

                // Attention dot for items that ask for it.
                Rectangle {
                    visible: trayIcon.modelData.status === "NeedsAttention"
                    width: 7
                    height: 7
                    radius: 3.5
                    color: Theme.accent
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: 1
                    anchors.rightMargin: 1
                }

                HoverHandler {
                    onHoveredChanged: {
                        if (hovered)
                            panel.showToolTip(trayIcon,
                                              trayIcon.modelData.tooltipTitle
                                              || trayIcon.modelData.title
                                              || trayIcon.modelData.name || "");
                        else
                            panel.hideToolTip();
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: (mouse) => {
                        panel.hideToolTip();
                        if (mouse.button === Qt.RightButton)
                            Tray.contextMenu(trayIcon.entryId,
                                             Math.round(mouse.globalX || 0),
                                             Math.round(mouse.globalY || 0));
                        else if (mouse.button === Qt.MiddleButton)
                            Tray.secondaryActivate(trayIcon.entryId);
                        else
                            Tray.activate(trayIcon.entryId);
                    }
                    onWheel: (wheel) => {
                        Tray.scroll(trayIcon.entryId,
                                    wheel.angleDelta.y >= 0 ? 1 : -1, false);
                    }
                }
            }
        }
    }

    // --- bell: the notification centre --------------------------------------
    component BellButton: Rectangle {
        id: bellButton
        readonly property bool dnd: Settings.values["notifications.doNotDisturb"] === true
        readonly property int unread: panel.notificationCenter
                                      ? panel.notificationCenter.unreadCount : 0

        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
        width: 34
        height: Math.min(panel.barThickness - 10, 34)
        radius: Theme.radius
        color: bellHover.hovered
               ? Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.08)
               : "transparent"
        Behavior on color {
            ColorAnimation { duration: motion.micro }
        }

        HoverHandler {
            id: bellHover
            onHoveredChanged: {
                if (hovered) {
                    const unread = bellButton.unread;
                    panel.showToolTip(bellButton,
                                      unread > 0 ? "Notifications (" + unread + " new)"
                                                 : "Notifications");
                } else {
                    panel.hideToolTip();
                }
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                panel.hideToolTip();
                if (panel.notificationCenter)
                    panel.notificationCenter.toggle();
            }
        }

        Image {
            anchors.centerIn: parent
            width: 18
            height: 18
            sourceSize.width: 36
            sourceSize.height: 36
            fillMode: Image.PreserveAspectFit
            source: Shell.tintedIcon(bellButton.dnd ? "bell-off" : "bell", Theme.foreground)
        }

        // Unread badge: a count up to nine, a dot beyond that.
        Rectangle {
            visible: bellButton.unread > 0 && !bellButton.dnd
            width: bellButton.unread > 9 ? 14 : Math.max(14, unreadLabel.implicitWidth + 8)
            height: 14
            radius: 7
            color: Theme.accent
            border.width: 1
            border.color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.9)
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: 1
            anchors.rightMargin: 1

            Text {
                id: unreadLabel
                anchors.centerIn: parent
                visible: bellButton.unread <= 9
                text: String(bellButton.unread)
                color: "#ffffff"
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 3
                font.bold: true
            }
        }
    }

    // --- indicators: the quick settings button ------------------------------
    // One button that summarises the machine: the battery level when there is
    // one, otherwise the volume or network state. Clicking it opens the
    // quick settings card.
    component IndicatorsButton: Rectangle {
        id: indicatorsButton
        readonly property bool playing: Quick.mediaAvailable && Quick.mediaPlaying
        readonly property string glyph: Quick.volumeMuted ? "volume-x"
                                        : System.batteryCharging ? "battery-charging"
                                        : System.batteryPresent ? "battery"
                                        : Quick.networkAvailable && !Quick.wifiEnabled ? "wifi-off"
                                        : Quick.networkAvailable ? "wifi"
                                        : "sliders-horizontal"

        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
        implicitWidth: indicatorsRow.implicitWidth + 14
        height: Math.min(panel.barThickness - 10, 34)
        radius: Theme.radius
        color: indicatorsHover.hovered
               ? Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.08)
               : "transparent"
        Behavior on color {
            ColorAnimation { duration: motion.micro }
        }

        HoverHandler {
            id: indicatorsHover
            onHoveredChanged: {
                if (hovered) {
                    var tip = Quick.networkStatus;
                    if (System.batteryPresent)
                        tip = Math.round(System.batteryPercent) + "% "
                              + (System.batteryTimeText.length > 0
                                 ? System.batteryTimeText : System.batteryStatus);
                    else if (Quick.volumeAvailable)
                        tip = (Quick.volumeMuted ? "Muted" : "Volume " + Quick.volume + "%")
                              + (tip.length > 0 ? " - " + tip : "");
                    panel.showToolTip(indicatorsButton, tip.length > 0 ? tip : "Quick settings");
                } else {
                    panel.hideToolTip();
                }
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                panel.hideToolTip();
                if (panel.quickSettings)
                    panel.quickSettings.toggle();
            }
        }

        Row {
            id: indicatorsRow
            anchors.centerIn: parent
            spacing: 5

            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                sourceSize.width: 36
                sourceSize.height: 36
                fillMode: Image.PreserveAspectFit
                source: Shell.tintedIcon(indicatorsButton.glyph,
                                         System.batteryCritical ? Theme.accent : Theme.foreground)
            }
            Text {
                visible: System.batteryPresent
                anchors.verticalCenter: parent.verticalCenter
                text: Math.round(System.batteryPercent) + "%"
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }
        }
    }

    Loader {
        id: barLoader
        anchors.fill: parent
        anchors.margins: 4
        sourceComponent: panel.vertical ? verticalBar : horizontalBar
        // Slide the bar content in and out of the reserved strip.
        opacity: panel.revealed ? 1.0 : 0.0
        // While fading, the bar also sinks back towards its own edge, so
        // auto-hide reads as the bar sliding away rather than a blink.
        transform: Translate {
            x: (panel.vertical ? (panel.panelPosition === "left" ? -1 : 1) : 0)
               * (1 - barLoader.opacity) * 14
            y: (panel.vertical ? 0 : (panel.panelPosition === "top" ? -1 : 1))
               * (1 - barLoader.opacity) * 14
        }
        Behavior on opacity {
            NumberAnimation {
                duration: motion.fast
                easing.type: motion.easeEnter
            }
        }
    }

    // Scrollable dock strip: it only becomes interactive when the buttons do
    // not fit, so nothing steals wheel events on a quiet desktop. The strip
    // takes all the leftover width of the bar (like a spacer would), so the
    // start button keeps its place at the very left. It always fills the bar
    // height: a Flickable has no implicit height, and without fillHeight the
    // whole dock once collapsed to zero-height, invisible buttons.
    component DockStrip: Flickable {
        id: dockStrip
        Layout.fillWidth: !panel.vertical
        Layout.fillHeight: true
        Layout.alignment: Qt.AlignVCenter
        clip: true
        contentWidth: stripRow.width
        contentHeight: stripRow.height
        interactive: panel.vertical ? contentHeight > height : contentWidth > width

        WheelHandler {
            enabled: dockStrip.interactive
            orientation: panel.vertical ? Qt.Vertical : Qt.Horizontal
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        }

        Row {
            id: stripRow
            spacing: 2
            y: (dockStrip.height - height) / 2
            x: 0

            DockApps {}
            BackgroundSeparator {}
            DockApps { backgroundZone: true }
        }
    }

    Component {
        id: horizontalBar

        Rectangle {
            radius: Theme.radius
            color: panel.useCustomPanelColors && panel.panelBackgroundColor.length > 0
                   ? Qt.rgba(Qt.color(panel.panelBackgroundColor).r, 
                             Qt.color(panel.panelBackgroundColor).g, 
                             Qt.color(panel.panelBackgroundColor).b, 
                             panel.panelBackgroundOpacity)
                   : Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.opacity)
            border.width: 1
            border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)

            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4

                StartButton {}
                Separator {}
                DockStrip {}
                PluginSlot {}

                TrayBox {}
                BellButton {
                    visible: panel.showIndicators
                }
                IndicatorsButton {
                    visible: panel.showIndicators
                }
                Separator {
                    visible: panel.showTray || panel.showIndicators
                }

                ClockButton {
                    Layout.rightMargin: 6
                    Layout.leftMargin: 4
                }
            }
        }
    }

    Component {
        id: verticalBar

        Rectangle {
            radius: Theme.radius
            color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.opacity)
            border.width: 1
            border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4

                StartButton {}
                Separator {}
                DockStrip {}
                PluginSlot {}

                TrayBox {}
                BellButton {
                    visible: panel.showIndicators
                }
                IndicatorsButton {
                    visible: panel.showIndicators
                }
                Separator {
                    visible: panel.showTray || panel.showIndicators
                }

                ClockButton {
                    Layout.bottomMargin: 6
                    Layout.topMargin: 4
                }
            }
        }
    }

    // Invisible hover surface over the whole panel; it keeps auto-hide from
    // flickering while the pointer travels across the bar.
    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }
}
