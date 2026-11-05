import QtQuick
import QtQuick.Window
import QtQuick.Layouts

// Clipboard history (Super+V): the texts copied in this session, newest
// first, with the pinned ones on top. Click one to put it back on the
// clipboard, then paste as usual; type to search.
DropdownWindow {
    id: popup

    property string filter: ""
    // Pinned entries first, then the history; both filtered by the search.
    readonly property var entries: {
        const needle = popup.filter.trim().toLowerCase();
        const out = [];
        const pinned = Clipboard.pinnedItems || [];
        const items = Clipboard.items || [];
        for (let i = 0; i < pinned.length; ++i) {
            if (!needle || String(pinned[i]).toLowerCase().indexOf(needle) >= 0)
                out.push({ text: String(pinned[i]), pinned: true, index: -1 });
        }
        for (let i = 0; i < items.length; ++i) {
            if (!needle || String(items[i]).toLowerCase().indexOf(needle) >= 0)
                out.push({ text: String(items[i]), pinned: false, index: i });
        }
        return out;
    }

    function opened() {
        popup.filter = "";
        searchField.text = "";
        searchField.focusInput();
    }

    function pick(entry) {
        if (entry.pinned)
            Clipboard.copyText(entry.text);
        else
            Clipboard.copy(entry.index);
        popup.hide();
    }

    cardWidth: 380
    cardHeight: 460
    padding: 12
    scopeName: "newteller-clipboard"

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        ShellTitleBar {
            Layout.fillWidth: true
            title: qsTr("Clipboard")
            iconName: "clipboard"
            onCloseClicked: popup.hide()
        }

        Text {
            Layout.fillWidth: true
            visible: !Clipboard.available
                     || (Clipboard.items.length === 0 && Clipboard.pinnedItems.length === 0)
            wrapMode: Text.WordWrap
            text: !Clipboard.available
                  ? qsTr("Clipboard history needs wl-clipboard (sudo apt install wl-clipboard).")
                  : Settings.values["clipboard.historyEnabled"] === false
                    ? qsTr("Clipboard history is off (Settings -> System).")
                    : qsTr("Nothing copied yet. Copied text shows up here.")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
        }

        SettingsTextField {
            id: searchField
            objectName: "clipboardSearch"
            Layout.fillWidth: true
            visible: Clipboard.items.length + Clipboard.pinnedItems.length > 3
            placeholder: qsTr("Search the clipboard")
            onTextChanged: popup.filter = searchField.text
            // Enter takes the first match.
            onAccepted: {
                if (popup.entries.length > 0)
                    popup.pick(popup.entries[0]);
            }
        }

        ListView {
            id: list
            objectName: "clipboardList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: popup.entries
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
                    onClicked: popup.pick(modelData)
                }
                Text {
                    id: entryText
                    anchors.left: parent.left
                    anchors.right: pinButton.left
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 8
                    text: modelData.text.replace(/\s+/g, " ").slice(0, 300)
                    maximumLineCount: 3
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                }
                TaskButton {
                    id: pinButton
                    objectName: "clipboardPin"
                    anchors.right: removeButton.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26
                    height: 26
                    iconSize: 16
                    active: modelData.pinned
                    iconName: modelData.pinned ? "pin-off" : "pin"
                    onClicked: {
                        if (modelData.pinned)
                            Clipboard.unpin(modelData.text);
                        else
                            Clipboard.pin(modelData.text);
                    }
                }
                TaskButton {
                    id: removeButton
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26
                    height: 26
                    iconSize: 16
                    iconName: "x"
                    onClicked: {
                        if (modelData.pinned)
                            Clipboard.unpin(modelData.text);
                        else
                            Clipboard.remove(modelData.index);
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            TaskButton {
                iconName: "trash"
                label: qsTr("Clear all")
                visible: Clipboard.items.length > 0
                onClicked: Clipboard.clear()
            }
        }
    }
}
