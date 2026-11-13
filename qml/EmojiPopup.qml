import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import "EmojiData.js" as EmojiData

// Emoji picker (Super+.): type a word in English or Russian, click an emoji
// (or press Enter for the first one) and it is on the clipboard, ready to
// paste. The ones picked last come first.
DropdownWindow {
    id: popup

    property string filter: ""
    readonly property var recent: {
        const saved = Settings.values["emoji.recent"] || [];
        const out = [];
        for (let i = 0; i < saved.length; ++i)
            out.push(String(saved[i]));
        return out;
    }
    // What the grid shows: the matches of the search, or the recently used
    // ones followed by everything else.
    readonly property var entries: {
        const needle = popup.filter.trim().toLowerCase();
        const all = EmojiData.list;
        const out = [];
        if (needle.length > 0) {
            for (let i = 0; i < all.length; ++i) {
                if (all[i].k.indexOf(needle) >= 0 || all[i].e === needle)
                    out.push(all[i].e);
            }
            return out;
        }
        for (let i = 0; i < popup.recent.length; ++i)
            out.push(popup.recent[i]);
        for (let i = 0; i < all.length; ++i) {
            if (popup.recent.indexOf(all[i].e) < 0)
                out.push(all[i].e);
        }
        return out;
    }

    function opened() {
        popup.filter = "";
        searchField.text = "";
        searchField.focusInput();
    }

    function pick(emoji) {
        Shell.copyToClipboard(emoji);
        const next = [emoji];
        for (let i = 0; i < popup.recent.length && next.length < 24; ++i) {
            if (popup.recent[i] !== emoji)
                next.push(popup.recent[i]);
        }
        Settings.setValue("emoji.recent", next);
        popup.hide();
    }

    cardWidth: 380
    cardHeight: 420
    padding: 12
    scopeName: "newteller-emoji"

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        ShellTitleBar {
            Layout.fillWidth: true
            title: qsTr("Emoji")
            iconName: "sparkles"
            onCloseClicked: popup.hide()
        }

        SettingsTextField {
            id: searchField
            objectName: "emojiSearch"
            Layout.fillWidth: true
            placeholder: qsTr("Search emoji")
            onTextChanged: popup.filter = searchField.text
            // Enter takes the first match.
            onAccepted: {
                if (popup.entries.length > 0)
                    popup.pick(popup.entries[0]);
            }
        }

        Text {
            Layout.fillWidth: true
            visible: popup.entries.length === 0
            wrapMode: Text.WordWrap
            text: qsTr("No emoji for this word.")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
        }

        GridView {
            id: grid
            objectName: "emojiGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.floor(width / 8)
            cellHeight: cellWidth
            model: popup.entries
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: cell
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight
                radius: Theme.radius
                color: cellHover.hovered
                       ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
                       : "transparent"

                HoverHandler {
                    id: cellHover
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: popup.pick(cell.modelData)
                }
                Text {
                    anchors.centerIn: parent
                    text: cell.modelData
                    color: Theme.foreground
                    font.pixelSize: Math.round(cell.width * 0.56)
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("A click copies the emoji; paste it with Ctrl+V.")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 2
            elide: Text.ElideRight
        }
    }
}
