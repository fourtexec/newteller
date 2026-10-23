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
    // The edge the menu grows from: the side of the panel it sits against.
    readonly property int openOrigin: position === "top" ? Item.Top
                                      : position === "left" ? Item.Left
                                      : position === "right" ? Item.Right
                                      : Item.Bottom
    readonly property real clampedX: Math.max(8, Math.min(anchorX - menuWidth / 2,
                                                          Screen.width - menuWidth - 8))
    readonly property real clampedY: Math.max(8, Math.min(anchorY - height / 2,
                                                          Screen.height - height - 8))

    width: menuWidth
    height: menuColumn.implicitHeight + 20
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint

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
        // Snap first so the first frame is the small, transparent card.
        menuCard.prepareOpen();
        visible = true;
        requestActivate();
        // The menu grows out of the dock button, towards the panel edge.
        menuCard.playOpen();
    }

    function close() {
        if (!visible || menuCard.closing)
            return;
        menuCard.playClose(function() { visible = false; });
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
            id: menuCard
            anchors.fill: parent
            openOrigin: dockMenu.openOrigin

        Column {
            id: menuColumn
            anchors.fill: parent
            anchors.margins: 10
            spacing: 4

            // The shell's own title bar: application name and a close button.
            ShellTitleBar {
                width: parent.width
                iconName: dockMenu.entry && dockMenu.entry.app && dockMenu.entry.app.icon
                          ? dockMenu.entry.app.icon : "app-window"
                title: {
                    if (!dockMenu.entry)
                        return "Application";
                    if (dockMenu.entry.app && dockMenu.entry.app.name)
                        return dockMenu.entry.app.name;
                    return dockMenu.entry.title || "Application";
                }
                onCloseClicked: dockMenu.close()
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)
                visible: dockMenu.entry && dockMenu.entry.windows.length > 0
            }

            Repeater {
                model: dockMenu.entry ? dockMenu.entry.windows : []

                delegate: Rectangle {
                    id: windowRow
                    required property var modelData
                    readonly property bool thumbnailsEnabled: Settings.values["dock.thumbnails"] !== false
                    readonly property bool hasThumbnail: thumbnailsEnabled
                                                         && thumb.status === Image.Ready
                    width: menuColumn.width
                    height: 72
                    radius: Theme.radius
                    color: modelData.active
                           ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
                           : windowHover.hovered
                             ? Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.07)
                             : "transparent"

                    HoverHandler {
                        id: windowHover
                    }

                    Rectangle {
                        id: previewFrame
                        anchors.left: parent.left
                        anchors.leftMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 100
                        height: 60
                        radius: 4
                        clip: true
                        color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.55)
                        border.width: 1
                        border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.8)

                        // Live preview (X11/XWayland windows). Native Wayland
                        // windows have no screenshot protocol, so the fallback
                        // below stays visible for them.
                        Image {
                            id: thumb
                            anchors.fill: parent
                            asynchronous: true
                            cache: false
                            fillMode: Image.PreserveAspectCrop
                            sourceSize.width: 200
                            sourceSize.height: 120
                            source: windowRow.thumbnailsEnabled
                                    ? Windows.thumbnailUrl(modelData.id) : ""
                        }

                        Image {
                            visible: !windowRow.hasThumbnail
                            anchors.centerIn: parent
                            width: 28
                            height: 28
                            sourceSize.width: 56
                            sourceSize.height: 56
                            fillMode: Image.PreserveAspectFit
                            source: Shell.tintedIcon(dockMenu.entry && dockMenu.entry.app
                                                     && dockMenu.entry.app.icon
                                                     ? dockMenu.entry.app.icon : "app-window",
                                                     Theme.foreground)
                        }
                    }

                    Text {
                        anchors.left: previewFrame.right
                        anchors.leftMargin: 8
                        anchors.right: windowState.left
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.title && modelData.title.length > 0
                              ? modelData.title : "Window"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.Wrap
                    }

                    MouseArea {
                        anchors.left: parent.left
                        anchors.right: windowState.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            Windows.activate(modelData.id);
                            dockMenu.close();
                        }
                    }

                    // Minimise or restore this window, matching its state.
                    // Read-only toplevels (ext_foreign_toplevel_list) can be
                    // neither minimised nor closed, so they get no buttons.
                    TaskButton {
                        id: windowState
                        anchors.right: windowClose.left
                        anchors.rightMargin: 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: 28
                        height: 28
                        visible: !modelData.readOnly
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
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 28
                        height: 28
                        visible: !modelData.readOnly
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
                iconName: "trash"
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
