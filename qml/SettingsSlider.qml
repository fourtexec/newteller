import QtQuick

// Horizontal slider with its value printed to the right of the track (never
// under the handle). With `settingKey` set it reads and writes the shell.json
// key directly; `step` snaps the values.
Item {
    id: control

    property string settingKey: ""
    property real value: 0
    property real from: 0
    property real to: 100
    property real step: 1
    property string suffix: ""
    property int decimals: step < 1 ? 2 : 0
    signal changed(real value)

    width: 260
    height: 26
    implicitWidth: width
    implicitHeight: height

    readonly property real ratio: Math.max(0, Math.min(1, (value - from) / Math.max(0.000001, to - from)))
    readonly property int labelWidth: 54
    readonly property real trackWidth: Math.max(60, width - labelWidth - 8)

    function refresh() {
        if (settingKey.length > 0) {
            const v = Settings.value(settingKey, from);
            const n = Number(v);
            value = isNaN(n) ? from : n;
        }
    }

    function commit(raw) {
        var snapped = Math.round((raw - from) / step) * step + from;
        snapped = Math.max(from, Math.min(to, snapped));
        // Rounding noise of the step arithmetic (0.30000000000000004).
        snapped = Number(snapped.toFixed(Math.max(0, decimals + 2)));
        value = snapped;
        if (settingKey.length > 0)
            Settings.setValue(settingKey, snapped);
        changed(snapped);
    }

    function valueAt(x) {
        const usable = Math.max(1, control.trackWidth - handle.width);
        return from + Math.max(0, Math.min(1, (x - handle.width / 2) / usable)) * (to - from);
    }

    Component.onCompleted: refresh()
    Connections {
        target: Settings
        function onValuesChanged() { control.refresh() }
    }

    Rectangle {
        id: track
        x: 0
        width: control.trackWidth
        height: 6
        radius: 3
        anchors.verticalCenter: parent.verticalCenter
        color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.14)

        Rectangle {
            width: handle.x + handle.width / 2
            height: parent.height
            radius: 3
            visible: control.ratio > 0
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.75)
        }
    }

    Rectangle {
        id: handle
        width: 18
        height: 18
        radius: 9
        x: control.ratio * (control.trackWidth - width)
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.foreground
        border.width: 1
        border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.9)
    }

    Text {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: control.labelWidth
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
        text: control.value.toFixed(control.decimals) + control.suffix
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pointSize: Theme.fontSize - 1
    }

    MouseArea {
        x: 0
        width: control.trackWidth
        height: parent.height
        cursorShape: Qt.PointingHandCursor
        preventStealing: true
        onPressed: (mouse) => control.commit(control.valueAt(mouse.x))
        onPositionChanged: (mouse) => {
            if (pressed)
                control.commit(control.valueAt(mouse.x));
        }
    }
}
