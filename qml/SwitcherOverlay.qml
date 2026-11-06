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

    // switcher.style: "thumbnails" (the windows as they look), "icons"
    // (compact tiles with the application icon) or "list" (one row per
    // window with its icon and full title).
    readonly property bool listStyle: Settings.values["switcher.style"] === "list"
    readonly property bool iconsOnly: Settings.values["switcher.style"] === "icons" || listStyle
    readonly property int tileWidth: listStyle ? Math.min(560, Screen.width - 120) : iconsOnly ? 108 : 200
    readonly property int tileHeight: listStyle ? 40 : iconsOnly ? 104 : 168
    readonly property int columns: listStyle ? 1
                                             : Math.max(1, Math.min(entries.length,
                                                                    Math.floor((Screen.width - 120) / (tileWidth + 6))))
    readonly property int rows: Math.ceil(entries.length / columns)

    width: columns * (tileWidth + 6) + 26
    height: Math.min(rows * (tileHeight + 6) + (listStyle ? 20 : 56),
                     Math.max(200, Screen.height - 80))
    visible: wanted
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus

    // No anchors: layer-shell centres the surface on the output.
    LayerShell.Window.anchors: LayerShell.Window.AnchorNone
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.scope: "newteller-switcher"

    onVisibleChanged: updateBlur()
    onWidthChanged: updateBlur()
    onHeightChanged: updateBlur()
    function updateBlur() {
        const screenName = overlay.screen ? overlay.screen.name : "";
        if (visible)
            Session.setBlur(String(overlay), "newteller-switcher", screenName, 0, 0, width, height,
                            Theme.radius + 6);
        else
            Session.clearBlur(String(overlay), "newteller-switcher", screenName);
    }

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

        clip: true

        Grid {
            id: grid
            x: 13
            // A long list scrolls so the selected row stays on screen.
            y: 13 - Math.max(0, Math.min(height - (card.height - 26),
                                         (overlay.selected + 1) * (overlay.tileHeight + 6) - (card.height - 26)))
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

                    // The window as it looks (written by the compositor when
                    // Alt+Tab started), the app icon on top of it.
                    Image {
                        id: thumb
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 10
                        width: parent.width - 20
                        height: parent.height - 46
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        source: modelData.id !== undefined && !overlay.iconsOnly
                                ? Session.cachedThumbnail(modelData.id) : ""
                    }
                    Image {
                        visible: overlay.listStyle
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 24
                        height: 24
                        sourceSize.width: 48
                        sourceSize.height: 48
                        fillMode: Image.PreserveAspectFit
                        source: overlay.listStyle ? overlay.iconFor(modelData) : ""
                    }
                    Text {
                        objectName: "switcherListTitle"
                        visible: overlay.listStyle
                        x: 44
                        width: parent.width - 54
                        anchors.verticalCenter: parent.verticalCenter
                        text: {
                            const name = overlay.nameFor(modelData);
                            const title = modelData.title || "";
                            return title.length > 0 && title !== name ? name + " \u2014 " + title : name;
                        }
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize
                        font.bold: parent.current
                        elide: Text.ElideRight
                    }
                    Image {
                        visible: !overlay.listStyle
                        x: thumb.status === Image.Ready ? 14 : (parent.width - width) / 2
                        y: thumb.status === Image.Ready ? thumb.y + thumb.height - height - 4
                                                        : thumb.y + (thumb.height - height) / 2
                        width: thumb.status === Image.Ready ? 32 : 56
                        height: width
                        sourceSize.width: 112
                        sourceSize.height: 112
                        fillMode: Image.PreserveAspectFit
                        source: overlay.iconFor(modelData)
                    }
                    Text {
                        visible: !overlay.listStyle
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

        // The full title of the selected window (the list already shows it).
        Text {
            visible: !overlay.listStyle
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
