import QtQuick
import QtQuick.Window

// The menu of a system tray item, rendered from its com.canonical.dbusmenu
// export. Applications that only implement Activation/ContextMenu keep the
// old path (the panel asks them to open their own popup).
//
// The menu is fetched live when it opens: rootItems is the tree, path is the
// chain of submenu ids currently shown. Opening a submenu replaces the list
// in place and leaves a Back row at the top.
//
// Like the dock menu it is anchored to the icon that toggled it.
DropdownWindow {
    id: trayMenu

    property string itemId: ""
    property var rootItems: []
    property var path: []

    readonly property real menuWidth: 280
    readonly property var currentItems: visibleItems(findNode(rootItems, path))

    cardWidth: menuWidth
    cardHeight: menuCardColumn.implicitHeight + 24
    padding: 10
    scopeName: "newteller-tray-menu"

    function openFor(id, anchorRect) {
        itemId = id;
        path = [];
        anchor = anchorRect;
        rootItems = Tray.menu(id);
        show();
    }

    function close() {
        hide();
    }

    function findNode(items, ids) {
        var nodes = items || [];
        for (var i = 0; i < ids.length; ++i) {
            var found = null;
            for (var j = 0; j < nodes.length; ++j) {
                if (nodes[j].id === ids[i]) {
                    found = nodes[j];
                    break;
                }
            }
            if (!found)
                return [];
            nodes = found.children || [];
        }
        return nodes;
    }

    function visibleItems(items) {
        return (items || []).filter(function(item) {
            return item.visible !== false;
        });
    }

    function enterNode(node) {
        if (!node || !node.hasChildren)
            return;
        path = path.concat([node.id]);
        Tray.menuAboutToShow(itemId, node.id);
    }

    function back() {
        if (path.length > 0)
            path = path.slice(0, path.length - 1);
    }

    function activateNode(node) {
        if (!node || node.separator || !node.enabled)
            return;
        if (node.hasChildren) {
            enterNode(node);
            return;
        }
        Tray.menuEvent(itemId, node.id, "clicked");
        close();
    }

    Column {
        id: menuCardColumn
        anchors.fill: parent
        spacing: 2

        // Submenu navigation.
        Rectangle {
            visible: trayMenu.path.length > 0
            width: parent.width
            height: 30
            radius: Theme.radius
            color: backHover.hovered
                   ? Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.08)
                   : "transparent"

            HoverHandler {
                id: backHover
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: trayMenu.back()
            }
            Row {
                anchors.left: parent.left
                anchors.leftMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: 7
                Image {
                    width: 14
                    height: 14
                    sourceSize.width: 28
                    sourceSize.height: 28
                    fillMode: Image.PreserveAspectFit
                    source: Shell.tintedIcon("chevron-left", Theme.foreground)
                }
                Text {
                    text: "Back"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                }
            }
        }

        Rectangle {
            visible: trayMenu.path.length > 0
            width: parent.width
            height: 1
            color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)
        }

        Repeater {
            model: trayMenu.currentItems

            delegate: Item {
                id: menuRow
                required property var modelData
                readonly property bool hasLabel: (modelData.label || "").length > 0
                readonly property bool disabled: modelData.enabled === false
                readonly property bool separator: modelData.separator === true

                width: menuCardColumn.width
                height: separator ? 9 : 32

                Rectangle {
                    visible: menuRow.separator
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)
                }

                Rectangle {
                    visible: !menuRow.separator && rowHover.hovered && !menuRow.disabled
                    anchors.fill: parent
                    radius: Theme.radius
                    color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
                }

                HoverHandler {
                    id: rowHover
                }

                // Check/radio column.
                Image {
                    visible: !menuRow.separator && modelData.checkable === true
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 14
                    sourceSize.width: 28
                    sourceSize.height: 28
                    fillMode: Image.PreserveAspectFit
                    source: modelData.checked
                            ? Shell.tintedIcon("check", Theme.accent)
                            : ""
                }

                // Optional item icon.
                Image {
                    visible: !menuRow.separator && !modelData.checkable
                             && (modelData.iconName || "").length > 0
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    sourceSize.width: 32
                    sourceSize.height: 32
                    fillMode: Image.PreserveAspectFit
                    source: (modelData.iconName || "").length > 0
                            ? Shell.tintedIcon(modelData.iconName, Theme.foreground) : ""
                }

                Text {
                    visible: !menuRow.separator && menuRow.hasLabel
                    anchors.left: parent.left
                    anchors.leftMargin: 28
                    anchors.right: submenuArrow.visible ? submenuArrow.left : parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: (modelData.label || "").replace(/_/g, "")
                    color: menuRow.disabled ? Theme.muted : Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                    elide: Text.ElideRight
                }

                Image {
                    id: submenuArrow
                    visible: !menuRow.separator && modelData.hasChildren === true
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 13
                    height: 13
                    sourceSize.width: 26
                    sourceSize.height: 26
                    fillMode: Image.PreserveAspectFit
                    source: Shell.tintedIcon("chevron-right", Theme.foreground)
                }

                MouseArea {
                    visible: !menuRow.separator
                    anchors.fill: parent
                    cursorShape: menuRow.disabled ? Qt.ArrowCursor : Qt.PointingHandCursor
                    onClicked: trayMenu.activateNode(modelData)
                }
            }
        }

        Text {
            visible: trayMenu.currentItems.length === 0
            width: parent.width
            text: "No menu items"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
        }
    }
}
