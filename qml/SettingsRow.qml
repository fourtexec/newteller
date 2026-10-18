import QtQuick

// One setting row: title, optional description, the control on the right and
// a small reset button that restores the default of `settingKey`.
Item {
    id: row

    default property alias content: controlSlot.data
    property string title: ""
    property string subtitle: ""
    // The shell.json key this row edits; enables the reset-to-default button.
    property string settingKey: ""

    width: parent ? parent.width : 320
    height: Math.max(44, controlSlot.implicitHeight + 12)

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 1
        color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.25)
        visible: row.y > 0
    }

    Column {
        anchors.left: parent.left
        anchors.right: controlSlot.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Text {
            text: row.title
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
            width: parent.width
            elide: Text.ElideRight
        }
        Text {
            visible: row.subtitle.length > 0
            text: row.subtitle
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 2
            width: parent.width
            elide: Text.ElideRight
            wrapMode: Text.Wrap
        }
    }

    Row {
        id: controlSlot
        anchors.right: resetButton.left
        anchors.rightMargin: resetButton.visible ? 6 : 0
        anchors.verticalCenter: parent.verticalCenter
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
        anchors.verticalCenter: parent.verticalCenter
        width: 28
        height: 28
        iconName: "rotate-ccw"
        visible: row.settingKey.length > 0
                 && JSON.stringify(Settings.value(row.settingKey, null))
                    !== JSON.stringify(Settings.defaults()[row.settingKey])
        onClicked: Settings.reset(row.settingKey)
    }
}
