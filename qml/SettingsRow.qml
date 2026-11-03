import QtQuick

// One setting row: title, optional description, the control on the right and
// a small reset button that restores the default of `settingKey`.
//
// The row is as tall as its text or its control, whichever needs more, so a
// wrapped description never runs into the next row. When the control is too
// wide to leave the text a readable column, it moves under the text.
Item {
    id: row

    default property alias content: controlSlot.data
    property string title: ""
    property string subtitle: ""
    // The shell.json key this row edits; enables the reset-to-default button.
    property string settingKey: ""

    readonly property int resetWidth: resetButton.visible ? 34 : 0
    // Text column width when the control sits beside it.
    readonly property real besideTextWidth: width - controlSlot.implicitWidth - resetWidth - 16
    readonly property bool stacked: besideTextWidth < Math.min(220, width * 0.4)

    width: parent ? parent.width : 320
    height: stacked
            ? textColumn.implicitHeight + controlSlot.implicitHeight + 22
            : Math.max(44, textColumn.implicitHeight + 16, controlSlot.implicitHeight + 12)

    // Separator above every row but the first.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.25)
        visible: row.y > 0
    }

    Column {
        id: textColumn
        x: 0
        y: row.stacked ? 8 : Math.round((row.height - implicitHeight) / 2)
        width: row.stacked ? row.width - row.resetWidth : Math.max(60, row.besideTextWidth)
        spacing: 2

        Text {
            text: row.title
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
            width: parent.width
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            visible: row.subtitle.length > 0
            text: row.subtitle
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 2
            width: parent.width
            wrapMode: Text.Wrap
        }
    }

    Row {
        id: controlSlot
        x: row.stacked ? 0 : row.width - row.resetWidth - implicitWidth - (resetButton.visible ? 0 : 0)
        y: row.stacked ? textColumn.y + textColumn.implicitHeight + 8
                       : Math.round((row.height - implicitHeight) / 2)
        spacing: 4
    }

    // The rows declare `settingKey`; push it down into the control so the
    // controls stay dumb (a row may also host several standalone controls).
    Component.onCompleted: {
        for (var i = 0; i < controlSlot.children.length; ++i) {
            const child = controlSlot.children[i];
            if (child && child.settingKey !== undefined
                && (child.settingKey === null || child.settingKey.length === 0)) {
                child.settingKey = row.settingKey;
                if (typeof child.refresh === "function")
                    child.refresh();
            }
        }
    }

    TaskButton {
        id: resetButton
        anchors.right: parent.right
        y: row.stacked ? 6 : Math.round((row.height - height) / 2)
        width: 28
        height: 28
        iconName: "rotate-ccw"
        visible: row.settingKey.length > 0
                 && JSON.stringify(Settings.value(row.settingKey, null))
                    !== JSON.stringify(Settings.defaults()[row.settingKey])
        onClicked: Settings.reset(row.settingKey)
    }
}
