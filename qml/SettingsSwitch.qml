import QtQuick

// Toggle switch. With `settingKey` set it reads and writes the shell.json
// key directly; otherwise it is a plain controlled switch.
Rectangle {
    id: control

    Motion {
        id: motion
    }

    property string settingKey: ""
    property bool checked: false
    signal changed(bool value)

    width: 46
    height: 26
    radius: 13
    color: checked ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.9)
                   : Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.15)
    border.width: 1
    border.color: checked ? Theme.accent
                          : Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.9)
    Behavior on color {
        ColorAnimation { duration: motion.fast }
    }

    function refresh() {
        if (settingKey.length > 0)
            checked = Settings.value(settingKey, false) === true;
    }

    function set(next) {
        checked = next;
        if (settingKey.length > 0)
            Settings.setValue(settingKey, next);
        changed(next);
    }

    Component.onCompleted: refresh()
    Connections {
        target: Settings
        function onValuesChanged() { control.refresh() }
    }

    Rectangle {
        width: 20
        height: 20
        radius: 10
        x: control.checked ? control.width - width - 3 : 3
        anchors.verticalCenter: parent.verticalCenter
        color: "#ffffff"
        Behavior on x {
            NumberAnimation { duration: motion.fast; easing.type: motion.easeEnter }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: control.set(!control.checked)
    }
}
