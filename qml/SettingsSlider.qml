import QtQuick

// Horizontal slider with a value label. With `settingKey` set it reads and
// writes the shell.json key directly; `step` snaps the values.
Rectangle {
    id: control

    property string settingKey: ""
    property real value: 0
    property real from: 0
    property real to: 100
    property real step: 1
    property string suffix: ""
    property int decimals: step < 1 ? 2 : 0
    signal changed(real value)

    width: 220
    height: 26
    radius: 13
    color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.10)

    readonly property real ratio: (value - from) / Math.max(0.000001, to - from)

    function refresh() {
        if (settingKey.length > 0) {
            const v = Settings.value(settingKey, from);
            const n = Number(v);
            value = isNaN(n) ? from : n;
        }
    }

    function commit(raw) {
        const span = to - from;
        var snapped = Math.round((raw - from) / step) * step + from;
        snapped = Math.max(from, Math.min(to, snapped));
        value = snapped;
        if (settingKey.length > 0)
            Settings.setValue(settingKey, snapped);
        changed(snapped);
    }

    function valueAt(x) {
        return from + (Math.max(2, Math.min(width - 22, x)) - 2) / Math.max(1, width - 24)
               * (to - from);
    }

    Component.onCompleted: refresh()
    Connections {
        target: Settings
        function onValuesChanged() { control.refresh() }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        width: 2 + control.ratio * (control.width - 24) + 8
        height: 6
        radius: 3
        visible: control.ratio > 0
        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.55)
    }

    Rectangle {
        id: handle
        width: 20
        height: 20
        radius: 10
        x: 2 + control.ratio * (control.width - 24)
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.accent
        border.width: 2
        border.color: Qt.lighter(Theme.accent, 1.25)
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: control.value.toFixed(control.decimals) + control.suffix
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pointSize: Theme.fontSize - 1
    }

    MouseArea {
        anchors.fill: parent
        anchors.rightMargin: 44
        cursorShape: Qt.PointingHandCursor
        preventStealing: true
        onPressed: (mouse) => control.commit(control.valueAt(mouse.x))
        onPositionChanged: (mouse) => {
            if (pressed)
                control.commit(control.valueAt(mouse.x));
        }
    }
}
