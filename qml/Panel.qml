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

    // --- dock configuration ---------------------------------------------
    // Every started application is shown: windows come from the `Windows`
    // object (EWMH on X11, wlr-foreign-toplevel on Wayland), with the /proc
    // based application list of `Apps` as the fallback on other compositors.
    readonly property bool dockShowWindows: Settings.values["dock.showWindows"] !== false
    readonly property bool dockGroup: Settings.values["dock.group"] !== false
    readonly property bool separateBackground: Settings.values["dock.separateBackground"] !== false
    readonly property bool shrinkIcons: Settings.values["dock.shrinkIcons"] !== false
    readonly property int dockIconSize: Settings.values["dock.iconSize"] || 28
    // Icon size after the adaptive shrink pass (recomputed in rebuildDock).
    property int effectiveIconSize: dockIconSize
    property bool showDockLabels: false
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
            Windows.activate((target || entry.windows[0]).id);
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
            iconSize: panel.effectiveIconSize
            running: entry.running
            active: entry.active
            dimmed: entry.background === true
            badge: entry.badge
            onClicked: panel.dockClicked(entry)
            onMiddleClicked: panel.closeEntry(entry)
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

        HoverHandler { id: clockHover }
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

    Loader {
        id: barLoader
        anchors.fill: parent
        anchors.margins: 4
        sourceComponent: panel.vertical ? verticalBar : horizontalBar
        // Slide the bar content in and out of the reserved strip.
        opacity: panel.revealed ? 1.0 : 0.0
        Behavior on opacity {
            NumberAnimation {
                duration: 150
            }
        }
    }

    // Scrollable dock strip: it only becomes interactive when the buttons do
    // not fit, so nothing steals wheel events on a quiet desktop.
    component DockStrip: Flickable {
        id: dockStrip
        Layout.fillWidth: !panel.vertical
        Layout.fillHeight: panel.vertical
        Layout.alignment: Qt.AlignVCenter
        Layout.maximumWidth: contentWidth > 0 ? contentWidth + 4 : 8
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
            x: Math.max(0, (dockStrip.width - width) / 2)

            DockApps {}
            BackgroundSeparator {}
            DockApps { backgroundZone: true }
        }
    }

    Component {
        id: horizontalBar

        Rectangle {
            radius: Theme.radius
            color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.opacity)
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
