import QtQuick
import QtQuick.Window
import org.kde.layershell 1.0 as LayerShell

// The overview (Super+Tab), like Windows' Task View or GNOME's Activities:
// every window of a desktop as a live thumbnail, the desktops along the top.
// Click a window to go to it, x to close it, the numbers under a hovered
// window move it to another desktop. Arrows and Enter work too; Escape or a
// click on the background closes the overview.
Window {
    id: view
    objectName: "taskView"

    property var windowsList: []
    property var thumbs: ({})
    // The desktop shown (not necessarily the current one until a click).
    property int desktop: 0
    property int selected: 0
    readonly property var shown: windowsList.filter(function (w) { return w.workspace === view.desktop; })

    readonly property int cardWidth: 340
    readonly property int cardHeight: 250
    readonly property int columns: Math.max(1, Math.min(shown.length,
                                                        Math.floor((width - 80) / (cardWidth + 24))))

    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint

    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityExclusive
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.scope: "newteller-overview"

    // Without layer-shell (X11 fallback) the window simply covers the screen.
    x: Screen.virtualX
    y: Screen.virtualY
    width: Screen.width
    height: Screen.height

    function reload() {
        windowsList = Session.windows();
        const next = {};
        for (var i = 0; i < windowsList.length; ++i)
            next[windowsList[i].id] = Session.thumbnail(windowsList[i].id, 480);
        thumbs = next;
    }

    function show() {
        desktop = Session.workspace;
        reload();
        selected = 0;
        visible = true;
        requestActivate();
        keys.forceActiveFocus();
    }

    function hide() {
        visible = false;
    }

    // The whole screen behind the overview is blurred.
    onVisibleChanged: {
        const screenName = view.screen ? view.screen.name : "";
        if (visible)
            Session.setBlur(String(view), "newteller-overview", screenName, 0, 0, width, height, 0);
        else
            Session.clearBlur(String(view), "newteller-overview", screenName);
    }

    function toggle() {
        if (visible)
            hide();
        else
            show();
    }

    function activate(w) {
        hide();
        Session.activateWindow(w.id);
    }

    // Dimmed backdrop; a click on it closes the overview.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.62)
        MouseArea {
            anchors.fill: parent
            onClicked: view.hide()
        }
    }

    Item {
        id: keys
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: view.hide()
        Keys.onLeftPressed: view.selected = Math.max(0, view.selected - 1)
        Keys.onRightPressed: view.selected = Math.min(view.shown.length - 1, view.selected + 1)
        Keys.onUpPressed: view.selected = Math.max(0, view.selected - view.columns)
        Keys.onDownPressed: view.selected = Math.min(view.shown.length - 1, view.selected + view.columns)
        Keys.onReturnPressed: {
            if (view.selected >= 0 && view.selected < view.shown.length)
                view.activate(view.shown[view.selected]);
        }
        Keys.onTabPressed: view.selected = (view.selected + 1) % Math.max(1, view.shown.length)
    }

    // --- desktops ---------------------------------------------------------
    Row {
        id: desktops
        anchors.top: parent.top
        anchors.topMargin: 28
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 12

        Repeater {
            model: Session.workspaceCount

            delegate: Rectangle {
                required property int index
                readonly property int count: view.windowsList.filter(function (w) {
                    return w.workspace === index;
                }).length
                width: 150
                height: 64
                radius: Theme.radius + 2
                color: index === view.desktop
                       ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)
                       : Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.85)
                border.width: index === Session.workspace ? 2 : 1
                border.color: index === Session.workspace ? Theme.accent
                                                         : Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.9)

                Column {
                    anchors.centerIn: parent
                    spacing: 2
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Desktop %1").arg(index + 1)
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize
                        font.bold: index === Session.workspace
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: count === 0 ? "empty" : count === 1 ? "1 window" : count + " windows"
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 2
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    // One click shows it here, a second goes there.
                    onClicked: {
                        if (view.desktop === index) {
                            Session.switchWorkspace(index);
                            view.hide();
                        } else {
                            view.desktop = index;
                            view.selected = 0;
                        }
                    }
                }
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: view.shown.length === 0
        text: qsTr("No windows on this desktop")
        color: "#ffffff"
        opacity: 0.7
        font.family: Theme.fontFamily
        font.pointSize: Theme.fontSize + 4
    }

    // --- windows ----------------------------------------------------------
    Grid {
        id: grid
        objectName: "taskViewGrid"
        anchors.top: desktops.bottom
        anchors.topMargin: 40
        anchors.horizontalCenter: parent.horizontalCenter
        columns: view.columns
        spacing: 24

        Repeater {
            id: cards
            model: view.shown

            delegate: Rectangle {
                id: card
                required property var modelData
                required property int index
                readonly property bool current: index === view.selected

                width: view.cardWidth
                height: view.cardHeight
                radius: Theme.radius + 4
                color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.92)
                border.width: current || cardHover.hovered ? 2 : 1
                border.color: current || cardHover.hovered ? Theme.accent
                                                           : Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.8)

                HoverHandler {
                    id: cardHover
                    onHoveredChanged: {
                        if (hovered)
                            view.selected = card.index;
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.MiddleButton) {
                            Session.closeWindow(card.modelData.id);
                            closeLater.start();
                        } else {
                            view.activate(card.modelData);
                        }
                    }
                }

                // Title row.
                Row {
                    id: titleRow
                    x: 10
                    y: 8
                    width: parent.width - 52
                    spacing: 8
                    Image {
                        width: 20
                        height: 20
                        sourceSize.width: 40
                        sourceSize.height: 40
                        source: {
                            const app = Apps.matchWindowApp(card.modelData.appId || "", card.modelData.title || "");
                            return Shell.iconUrl(app && app.icon ? app.icon : "app-window");
                        }
                    }
                    Text {
                        width: parent.width - 28
                        anchors.verticalCenter: parent.verticalCenter
                        text: card.modelData.title || card.modelData.appId
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize
                        elide: Text.ElideRight
                    }
                }
                TaskButton {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 6
                    width: 28
                    height: 28
                    iconName: "x"
                    tooltipText: "Close"
                    onClicked: {
                        Session.closeWindow(card.modelData.id);
                        closeLater.start();
                    }
                }

                // The live thumbnail, or the app icon when none could be read.
                Image {
                    id: shot
                    anchors.top: titleRow.bottom
                    anchors.topMargin: 8
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: moveRow.top
                    anchors.margins: 10
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    source: view.thumbs[card.modelData.id] || ""
                    opacity: card.modelData.minimized ? 0.55 : 1.0
                }
                Image {
                    anchors.centerIn: shot
                    visible: shot.status !== Image.Ready
                    width: 64
                    height: 64
                    sourceSize.width: 128
                    sourceSize.height: 128
                    source: {
                        const app = Apps.matchWindowApp(card.modelData.appId || "", card.modelData.title || "");
                        return Shell.iconUrl(app && app.icon ? app.icon : "app-window");
                    }
                }

                // Move to another desktop.
                Row {
                    id: moveRow
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 4
                    visible: Session.workspaceCount > 1
                    opacity: cardHover.hovered ? 1.0 : 0.0
                    height: 24

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Move to")
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 2
                    }
                    Repeater {
                        model: Session.workspaceCount
                        delegate: TaskButton {
                            required property int index
                            visible: index !== card.modelData.workspace
                            width: 26
                            height: 22
                            label: String(index + 1)
                            onClicked: {
                                Session.moveWindow(card.modelData.id, index);
                                view.reload();
                            }
                        }
                    }
                }
            }
        }
    }

    // A closed window disappears from the list a moment later.
    Timer {
        id: closeLater
        interval: 250
        onTriggered: view.reload()
    }
}
