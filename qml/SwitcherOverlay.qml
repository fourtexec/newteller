import QtQuick
import QtQuick.Window
import org.kde.layershell 1.0 as LayerShell

// The Alt+Tab overlay. The compositor owns the keys (Alt held, Tab steps,
// Shift+Tab goes back, Escape cancels, releasing Alt switches) and sends the
// window list through the Session object; this window only draws it, centred
// on the screen, without ever taking the keyboard.
Window {
    id: overlay

    readonly property var entries: Session.switcherEntries
    readonly property int selected: Session.switcherIndex
    readonly property bool wanted: selected >= 0 && entries.length > 1
                                   && Settings.values["switcher.showOverlay"] !== false

    readonly property int tileWidth: 104
    readonly property int tileHeight: 96
    readonly property int columns: Math.max(1, Math.min(entries.length,
                                                        Math.floor((Screen.width - 120) / (tileWidth + 6))))
    readonly property int rows: Math.ceil(entries.length / columns)

    width: columns * (tileWidth + 6) + 26
    height: rows * (tileHeight + 6) + 56
    visible: wanted
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus

    // No anchors: layer-shell centres the surface on the output.
    LayerShell.Window.anchors: LayerShell.Window.AnchorNone
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.scope: "newteller-switcher"

    function iconFor(entry) {
        const app = Apps.matchWindowApp(entry.appId || "", entry.title || "");
        return Shell.iconUrl(app && app.icon ? app.icon : "app-window");
    }

    function nameFor(entry) {
        const app = Apps.matchWindowApp(entry.appId || "", entry.title || "");
        if (app && app.name)
            return app.name;
        return entry.appId && entry.appId.length > 0 ? entry.appId : entry.title;
    }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: Theme.radius + 6
        color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.94)
        border.width: 1
        border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.9)

        Grid {
            id: grid
            x: 13
            y: 13
            columns: overlay.columns
            spacing: 6

            Repeater {
                model: overlay.entries

                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool current: index === overlay.selected

                    width: overlay.tileWidth
                    height: overlay.tileHeight
                    radius: Theme.radius
                    color: current ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.28)
                                   : "transparent"
                    border.width: current ? 2 : 0
                    border.color: Theme.accent

                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 12
                        width: 48
                        height: 48
                        sourceSize.width: 96
                        sourceSize.height: 96
                        fillMode: Image.PreserveAspectFit
                        source: overlay.iconFor(modelData)
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 6
                        horizontalAlignment: Text.AlignHCenter
                        text: overlay.nameFor(modelData)
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 1
                        elide: Text.ElideRight
                    }
                }
            }
        }

        // The full title of the selected window.
        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 14
            horizontalAlignment: Text.AlignHCenter
            text: overlay.selected >= 0 && overlay.selected < overlay.entries.length
                  ? (overlay.entries[overlay.selected].title || "") : ""
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
            font.bold: true
            elide: Text.ElideMiddle
        }
    }
}
