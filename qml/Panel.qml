import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import org.kde.layershell 1.0 as LayerShell
import NewTeller 1.0

// Panel: start button, the dock (pinned applications + every running window),
// plugin slots and the clock. The position is configurable (bottom by default,
// also top/left/right), the bar can be narrow and centred, and auto-hide
// slides it away after a short delay.
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
    readonly property int dockIconSize: Settings.values["dock.iconSize"] || 28
    // One entry per dock button:
    //   { appId, app, windows: [], running, active, badge, title }
    property var dockEntries: []

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

        // Pinned applications first, in the pinned order.
        const pinned = Settings.values["panel.pinned"] || [];
        for (const id of pinned) {
            const app = Apps.byId(id);
            if (app.id === undefined)
                continue;
            seen[id] = true;
            result.push({ appId: id, app: app, windows: [], running: false, active: false, badge: 0 });
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
                } else if (dockGroup) {
                    loose.push({ appId: "", app: {}, windows: [w], running: true, active: !!w.active,
                                 badge: 1, title: w.title || "" });
                } else {
                    loose.push({ appId: "", app: {}, windows: [w], running: true, active: !!w.active,
                                 badge: 1, title: w.title || "" });
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
                                  running: true, active: false, badge: wins.length });
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
                          active: false, badge: 0 });
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
        }
        dockEntries = result;
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
        model: panel.dockEntries

        delegate: TaskButton {
            id: dockButton
            required property var modelData
            readonly property var entry: modelData
            readonly property string appName: entry.app && entry.app.name ? entry.app.name : (entry.title || "Application")

            Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
            iconName: entry.app && entry.app.icon ? entry.app.icon
                    : (entry.appId.length > 0 ? "square" : "app-window")
            label: !panel.dockGroup && entry.title && entry.title.length > 0 ? entry.title : ""
            iconSize: panel.dockIconSize
            running: entry.running
            active: entry.active
            badge: entry.badge
            onClicked: panel.activateEntry(entry)
            onMiddleClicked: panel.closeEntry(entry)
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: panel.openDockMenu(dockButton.entry, dockButton)
            }
        }
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
        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
        text: (Settings.values["panel.showDate"]
               ? Qt.formatDateTime(Shell.now, Settings.values["panel.dateFormat"] || "ddd d MMM") + "  "
               : "")
              + Qt.formatDateTime(Shell.now, Settings.values["panel.clockFormat"] || "HH:mm")
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pointSize: Theme.fontSize
    }

    component Spacer: Item {
        Layout.fillWidth: !panel.vertical
        Layout.fillHeight: panel.vertical
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
                DockApps {}
                Spacer {}
                PluginSlot {}

                ClockText {
                    Layout.rightMargin: 6
                    Layout.leftMargin: 6
                    horizontalAlignment: Text.AlignRight
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
                DockApps {}
                Spacer {}
                PluginSlot {}

                ClockText {
                    Layout.bottomMargin: 6
                    Layout.topMargin: 6
                    horizontalAlignment: Text.AlignHCenter
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
