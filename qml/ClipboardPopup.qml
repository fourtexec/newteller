import QtQuick
import QtQuick.Window
import QtQuick.Layouts

// Clipboard history (Super+V): the texts copied in this session, newest
// first. Click one to put it back on the clipboard, then paste as usual.
DropdownWindow {
    id: popup

    cardWidth: 380
    cardHeight: 460
    padding: 12
    scopeName: "newteller-clipboard"

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        ShellTitleBar {
            Layout.fillWidth: true
            title: "Clipboard"
            iconName: "clipboard"
            onCloseClicked: popup.hide()
        }

        Text {
            Layout.fillWidth: true
            visible: !Clipboard.available || Clipboard.items.length === 0
            wrapMode: Text.WordWrap
            text: !Clipboard.available
                  ? "Clipboard history needs wl-clipboard (sudo apt install wl-clipboard)."
                  : Settings.values["clipboard.historyEnabled"] === false
                    ? "Clipboard history is off (Settings -> System)."
                    : "Nothing copied yet. Copied text shows up here."
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
        }

        ListView {
            id: list
            objectName: "clipboardList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: Clipboard.items
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                required property var modelData
                required property int index
                width: list.width
                height: Math.min(76, entryText.implicitHeight + 16)
                radius: Theme.radius
                color: entryHover.hovered
                       ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                       : Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.06)

                HoverHandler {
                    id: entryHover
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        Clipboard.copy(index);
                        popup.hide();
                    }
                }
                Text {
                    id: entryText
                    anchors.left: parent.left
                    anchors.right: removeButton.left
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 8
                    text: String(modelData).replace(/\s+/g, " ").slice(0, 300)
                    maximumLineCount: 3
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                }
                TaskButton {
                    id: removeButton
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26
                    height: 26
                    iconName: "x"
                    onClicked: Clipboard.remove(index)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            TaskButton {
                iconName: "trash"
                label: "Clear all"
                visible: Clipboard.items.length > 0
                onClicked: Clipboard.clear()
            }
        }
    }
}
