import QtQuick

// Example desktop plugin. Plugins can use the globals Theme, Settings, Apps,
// System, Shell and Plugins, and may import their own JS files next to this one.
// The clock binds to the shell-wide ticker `Shell.now`.
Rectangle {
    id: widget

    width: 260
    height: 124
    radius: Theme.radius + 4
    color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.85)
    border.width: 1
    border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)

    Column {
        anchors.centerIn: parent
        width: parent.width - 24
        spacing: 2

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: Qt.formatTime(Shell.now, "HH:mm")
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: 36
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: Qt.formatDate(Shell.now, "dddd, d MMMM")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
        }
    }
}
