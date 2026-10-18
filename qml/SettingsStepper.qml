import QtQuick

// Integer stepper: minus / value / plus. With `settingKey` set it reads and
// writes the shell.json key directly.
Row {
    id: control

    property string settingKey: ""
    property int value: 0
    property int from: 0
    property int to: 999
    property int step: 1
    property string suffix: ""
    signal changed(int value)

    spacing: 6

    function refresh() {
        if (settingKey.length > 0) {
            const n = Number(Settings.value(settingKey, from));
            value = isNaN(n) ? from : Math.round(n);
        }
    }

    function commit(next) {
        next = Math.max(from, Math.min(to, next));
        if (next === value)
            return;
        value = next;
        if (settingKey.length > 0)
            Settings.setValue(settingKey, next);
        changed(next);
    }

    Component.onCompleted: refresh()
    Connections {
        target: Settings
        function onValuesChanged() { control.refresh() }
    }

    TaskButton {
        width: 30
        height: 28
        iconName: "minus"
        onClicked: control.commit(control.value - control.step)
    }

    Rectangle {
        width: 58
        height: 28
        radius: Theme.radius
        color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.10)

        Text {
            anchors.centerIn: parent
            text: control.value + control.suffix
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
            horizontalAlignment: Text.AlignHCenter
        }
    }

    TaskButton {
        width: 30
        height: 28
        iconName: "plus"
        onClicked: control.commit(control.value + control.step)
    }
}
