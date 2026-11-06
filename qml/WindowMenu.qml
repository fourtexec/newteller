import QtQuick
import QtQuick.Window

// The window menu: a right click on a title bar (NewTeller's own frame or an
// application's header bar), Alt+Space, or `--msg window-menu`. The
// compositor sends where to open it and the window's state
// (Session.windowMenuRequested); the actions go back over the control socket.
DropdownWindow {
    id: menu

    // workspaces.names: "Work, Web, Chat"; unnamed desktops are numbered.
    function desktopName(index) {
        const names = String(Settings.values["workspaces.names"] || "").split(",");
        const name = index < names.length ? names[index].trim() : "";
        return name.length > 0 ? name : qsTr("Desktop %1").arg(index + 1);
    }

    property int windowId: -1
    property int windowDesktop: 0
    property string windowFlags: ""
    property string windowTitle: ""
    readonly property bool isMinimized: windowFlags.indexOf("m") >= 0
    readonly property bool isMaximized: windowFlags.indexOf("x") >= 0
    readonly property bool isFullscreen: windowFlags.indexOf("F") >= 0
    readonly property bool isOnTop: windowFlags.indexOf("t") >= 0
    readonly property bool isSticky: windowFlags.indexOf("s") >= 0

    cardWidth: 260
    cardHeight: menuColumn.implicitHeight + 2 * padding
    padding: 8
    scopeName: "newteller-window-menu"
    // The card hangs below the pointer (above it near the bottom edge).
    panelPosition: "top"
    gap: 4

    // x/y are layout coordinates; the menu moves to the screen holding them.
    function openAt(id, x, y, desktop, flags, title) {
        windowId = id;
        windowDesktop = desktop;
        windowFlags = flags;
        windowTitle = title;
        const screens = Qt.application.screens;
        for (let i = 0; i < screens.length; ++i) {
            const s = screens[i];
            if (x >= s.virtualX && x < s.virtualX + s.width
                && y >= s.virtualY && y < s.virtualY + s.height) {
                if (menu.screen !== s)
                    menu.screen = s;
                break;
            }
        }
        anchor = Qt.rect(Math.round(x) - 1, Math.round(y) - 1, 2, 2);
        show();
    }

    function close() {
        hide();
    }

    // One entry point for every row, so the smoke test can drive the menu.
    function run(action, argument) {
        const id = windowId;
        close();
        if (id < 0)
            return;
        if (action === "minimize")
            Session.minimizeWindow(id);
        else if (action === "restore")
            Session.activateWindow(id);
        else if (action === "maximize")
            Session.toggleMaximizeWindow(id);
        else if (action === "fullscreen")
            Session.toggleFullscreenWindow(id);
        else if (action === "onTop")
            Session.setWindowAlwaysOnTop(id, !menu.isOnTop);
        else if (action === "sticky")
            Session.setWindowSticky(id, !menu.isSticky);
        else if (action === "screen")
            Session.moveWindowToNextScreen(id);
        else if (action === "desktop")
            Session.moveWindow(id, argument);
        else if (action === "close")
            Session.closeWindow(id);
    }

    Column {
        id: menuColumn
        width: parent.width
        spacing: 2

        Text {
            width: parent.width
            leftPadding: 6
            rightPadding: 6
            bottomPadding: 4
            text: menu.windowTitle.length > 0 ? menu.windowTitle : qsTr("Window")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
            elide: Text.ElideRight
        }

        TaskButton {
            objectName: "windowMenuMinimize"
            width: parent.width
            height: 32
            fillWidth: true
            iconSize: 18
            iconName: "minus"
            label: menu.isMinimized ? qsTr("Restore") : qsTr("Minimize")
            onClicked: menu.run(menu.isMinimized ? "restore" : "minimize")
        }

        TaskButton {
            objectName: "windowMenuMaximize"
            width: parent.width
            height: 32
            fillWidth: true
            iconSize: 18
            visible: !menu.isFullscreen
            iconName: "square"
            label: menu.isMaximized ? qsTr("Restore size") : qsTr("Maximize")
            onClicked: menu.run("maximize")
        }

        TaskButton {
            objectName: "windowMenuFullscreen"
            width: parent.width
            height: 32
            fillWidth: true
            iconSize: 18
            iconName: "monitor"
            label: menu.isFullscreen ? qsTr("Leave full screen") : qsTr("Full screen")
            onClicked: menu.run("fullscreen")
        }

        TaskButton {
            objectName: "windowMenuOnTop"
            width: parent.width
            height: 32
            fillWidth: true
            iconSize: 18
            active: menu.isOnTop
            iconName: menu.isOnTop ? "pin-off" : "pin"
            label: qsTr("Always on top")
            onClicked: menu.run("onTop")
        }

        TaskButton {
            objectName: "windowMenuScreen"
            width: parent.width
            height: 32
            fillWidth: true
            iconSize: 18
            visible: Qt.application.screens.length > 1
            iconName: "monitor"
            label: qsTr("Move to the next screen")
            onClicked: menu.run("screen")
        }

        TaskButton {
            objectName: "windowMenuSticky"
            width: parent.width
            height: 32
            fillWidth: true
            iconSize: 18
            visible: Session.workspaceCount > 1
            active: menu.isSticky
            iconName: "layout-grid"
            label: qsTr("Show on every desktop")
            onClicked: menu.run("sticky")
        }

        // Move to another desktop: one small button per desktop.
        Column {
            width: parent.width
            spacing: 2
            visible: Session.workspaceCount > 1 && !menu.isSticky

            Text {
                leftPadding: 6
                topPadding: 4
                text: qsTr("Move to desktop")
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize
            }

            Flow {
                width: parent.width
                leftPadding: 4
                spacing: 4

                Repeater {
                    model: Session.workspaceCount

                    delegate: TaskButton {
                        required property int index
                        width: 34
                        height: 30
                        active: index === menu.windowDesktop
                        label: String(index + 1)
                        tooltipText: menu.desktopName(index)
                        onClicked: {
                            if (index !== menu.windowDesktop)
                                menu.run("desktop", index);
                        }
                    }
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)
        }

        TaskButton {
            objectName: "windowMenuClose"
            width: parent.width
            height: 32
            fillWidth: true
            iconSize: 18
            iconName: "x"
            label: qsTr("Close")
            onClicked: menu.run("close")
        }
    }
}
