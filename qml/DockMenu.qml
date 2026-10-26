import QtQuick
import QtQuick.Window

// Right-click menu of a dock button: the windows of the application, a new
// instance and a close-all action. Anchored to the dock button that opened it,
// like every other dropdown, so it never covers the icon it belongs to.
DropdownWindow {
    id: dockMenu

    property var entry: null
    // The panel, whose `dockEntries` property keeps the fresh entry data.
    property var entriesProvider: null

    readonly property real menuWidth: 280
    cardWidth: menuWidth
    cardHeight: menuColumn.implicitHeight + 20
    padding: 10
    scopeName: "newteller-dock-menu"

    function openFor(entryData, anchorRect) {
        entry = entryData;
        anchor = anchorRect;
        // Snap first so the first frame is the small, transparent card.
        show();
    }

    function close() {
        hide();
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

    // The menu rows; the card grows to fit them.
    Column {
        id: menuColumn
        anchors.fill: parent
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
