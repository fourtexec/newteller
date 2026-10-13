import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import org.kde.layershell 1.0 as LayerShell

// Bottom panel: start button, pinned apps, plugin slots, clock.
Window {
    id: panel

    property var startMenu
    readonly property int barHeight: Settings.values["panel.height"] || 48
    readonly property var panelPlugins: Plugins.plugins.filter(p => p.type === "panel" && p.enabled)
    property date now: new Date()

    visible: true
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool
    width: screen.geometry.width
    height: barHeight
    // Positioning used only when layer-shell is unavailable (e.g. Openbox on X11).
    x: screen.geometry.x
    y: screen.geometry.y + screen.geometry.height - barHeight

    LayerShell.Window.anchors: LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.exclusionZone: barHeight
    LayerShell.Window.scope: "newteller-panel"

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: panel.now = new Date()
    }

    Rectangle {
        id: bar
        anchors.fill: parent
        anchors.margins: 4
        radius: Theme.radius
        color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.opacity)
        border.width: 1
        border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)

        RowLayout {
            anchors.fill: parent
            anchors.margins: 4
            spacing: 4

            TaskButton {
                Layout.alignment: Qt.AlignVCenter
                iconName: "layout-grid"
                label: "Start"
                onClicked: panel.startMenu.toggle()
            }

            Repeater {
                model: Settings.values["panel.pinned"] || []

                delegate: TaskButton {
                    required property var modelData
                    readonly property var app: Apps.byId(modelData)

                    Layout.alignment: Qt.AlignVCenter
                    visible: app.id !== undefined
                    iconName: app.icon || ""
                    onClicked: Apps.launch(modelData)
                }
            }

            // Space for future window list / taskbar items.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            Repeater {
                model: panel.panelPlugins

                delegate: Loader {
                    required property var modelData
                    Layout.alignment: Qt.AlignVCenter
                    source: modelData.entry
                }
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                Layout.rightMargin: 6
                Layout.leftMargin: 6
                text: Qt.formatDateTime(panel.now, Settings.values["panel.clockFormat"] || "HH:mm")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize
                horizontalAlignment: Text.AlignRight
            }
        }
    }
}
