import QtQuick
import QtQuick.Window
import org.kde.layershell 1.0 as LayerShell

// Right-click menu of a dock button: the windows of the application, a new
// instance and a close-all action. Positioned next to the button with the
// same layer-shell anchoring the start menu uses.
Window {
    id: dockMenu

    property var entry: null
    // The panel, whose `dockEntries` property keeps the fresh entry data.
    property var entriesProvider: null
    // "bottom", "top", "left" or "right" — the edge the panel sits on.
    property string position: "bottom"
    property int panelThickness: 48
    // Centre of the dock button, in panel coordinates.
    property real anchorX: 0
    property real anchorY: 0

    readonly property real menuWidth: 280
    readonly property real clampedX: Math.max(8, Math.min(anchorX - menuWidth / 2,
                                                          Screen.width - menuWidth - 8))
    readonly property real clampedY: Math.max(8, Math.min(anchorY - height / 2,
                                                          Screen.height - height - 8))

    width: menuWidth
    height: menuColumn.implicitHeight + 20
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool

    // Positioning without layer-shell (X11, XWayland).
    x: {
        if (position === "left")
            return Screen.virtualX + panelThickness + 6;
        if (position === "right")
            return Screen.virtualX + Screen.width - width - panelThickness - 6;
        return Screen.virtualX + clampedX;
    }
    y: {
        if (position === "bottom")
            return Screen.virtualY + Screen.height - height - panelThickness - 6;
        if (position === "top")
            return Screen.virtualY + panelThickness + 6;
        return Screen.virtualY + clampedY;
    }

    LayerShell.Window.anchors: {
        if (position === "top")
            return LayerShell.Window.AnchorTop | LayerShell.Window.AnchorLeft;
        if (position === "left")
            return LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorTop;
        if (position === "right")
            return LayerShell.Window.AnchorRight | LayerShell.Window.AnchorTop;
        return LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorLeft;
    }
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.scope: "newteller-dock-menu"
    LayerShell.Window.margins {
        left: dockMenu.position === "left" ? panelThickness + 6
              : dockMenu.position === "right" ? 0 : dockMenu.clampedX
        right: dockMenu.position === "right" ? panelThickness + 6 : 0
        top: dockMenu.position === "top" ? panelThickness + 6
              : dockMenu.position === "bottom" ? 0 : dockMenu.clampedY
        bottom: dockMenu.position === "bottom" ? panelThickness + 6 : 0
    }

    function openFor(entryData, centerX, centerY) {
        entry = entryData;
        anchorX = centerX;
        anchorY = centerY;
        visible = true;
        requestActivate();
    }

    function close() {
        visible = false;
    }

    // The window list changed while the menu is open (a window closed, the
    // title changed): swap in the fresh entry so the rows stay current.
    function refreshEntry() {
        if (!visible || !entry || !entriesProvider)
            return;
        for (const e of entriesProvider.dockEntries) {
            if (e.appId.length > 0 && e.appId === entry.appId && e.windows.length > 0) {
                entry = e;
                return;
            }
        }
        close();
    }

    onActiveChanged: {
        if (!active)
            closeTimer.restart();
    }

    Timer {
        id: closeTimer
        interval: 200
        onTriggered: {
            if (!dockMenu.active)
                dockMenu.close();
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        radius: Theme.radius + 2
        color: "transparent"

        ShellFrame {
            anchors.fill: parent

        Column {
            id: menuColumn
            anchors.fill: parent
            anchors.margins: 10
            spacing: 4

            Text {
                width: parent.width
                text: {
                    if (!dockMenu.entry)
                        return "";
                    if (dockMenu.entry.app && dockMenu.entry.app.name)
                        return dockMenu.entry.app.name;
                    return dockMenu.entry.title || "Application";
                }
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
                elide: Text.ElideRight
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)
                visible: dockMenu.entry && dockMenu.entry.windows.length > 0
            }

            Repeater {
                model: dockMenu.entry ? dockMenu.entry.windows : []

                delegate: Item {
                    id: windowRow
                    required property var modelData
                    width: menuColumn.width
                    height: 34

                    TaskButton {
                        id: windowButton
                        anchors.left: parent.left
                        anchors.right: windowState.left
                        anchors.rightMargin: 4
                        height: parent.height
                        fillWidth: true
                        iconName: dockMenu.entry && dockMenu.entry.app && dockMenu.entry.app.icon
                                  ? dockMenu.entry.app.icon : "app-window"
                        label: modelData.title && modelData.title.length > 0 ? modelData.title : "Window"
                        active: !!modelData.active
                        onClicked: {
                            Windows.activate(modelData.id);
                            dockMenu.close();
                        }
                    }
                    // Minimise or restore this window, matching its state.
                    TaskButton {
                        id: windowState
                        anchors.right: windowClose.left
                        anchors.rightMargin: 2
                        width: 30
                        height: parent.height
                        iconName: modelData.minimized ? "maximize-2" : "minimize-2"
                        onClicked: {
                            if (modelData.minimized)
                                Windows.activate(modelData.id);
                            else
                                Windows.minimize(modelData.id);
                            dockMenu.close();
                        }
                    }
                    TaskButton {
                        id: windowClose
                        anchors.right: parent.right
                        width: 30
                        height: parent.height
                        iconName: "x"
                        onClicked: {
                            Windows.closeWindow(modelData.id);
                            dockMenu.close();
                        }
                    }
                }
            }

            TaskButton {
                width: menuColumn.width
                height: 34
                fillWidth: true
                visible: dockMenu.entry && dockMenu.entry.appId.length > 0
                         && dockMenu.entry.app.id !== undefined
                iconName: "plus"
                label: "New window"
                onClicked: {
                    Apps.launch(dockMenu.entry.appId);
                    dockMenu.close();
                }
            }

            TaskButton {
                width: menuColumn.width
                height: 34
                fillWidth: true
                visible: dockMenu.entry && dockMenu.entry.windows.length > 0
                iconName: "trash-2"
                label: "Close all windows"
                onClicked: {
                    for (const w of dockMenu.entry.windows)
                        Windows.closeWindow(w.id);
                    dockMenu.close();
                }
            }

            Text {
                width: menuColumn.width
                visible: dockMenu.entry === null
                text: "No windows"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize
            }
        }
        }
    }
}
