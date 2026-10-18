import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import org.kde.layershell 1.0 as LayerShell
import NewTeller 1.0

// Panel: start button, pinned apps, plugin slots, clock. The position is
// configurable (bottom by default, also top/left/right), the bar can be
// narrow and centred, and auto-hide slides it away after a short delay.
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

    component PinnedApps: Repeater {
        model: Settings.values["panel.pinned"] || []

        delegate: TaskButton {
            required property var modelData
            readonly property var app: Apps.byId(modelData)

            Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
            visible: app.id !== undefined
            iconName: app.icon || ""
            running: Settings.values["panel.showRunning"] !== false && Apps.isRunning(modelData)
            onClicked: Apps.launch(modelData)
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
                PinnedApps {}
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
                PinnedApps {}
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
