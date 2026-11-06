import QtQuick

// Example desktop plugin. Plugins can use the globals Theme, Settings, Apps,
// System, Shell and Plugins, and may import their own JS files next to this one.
// The clock binds to the shell-wide ticker `Shell.now`.
Rectangle {
    id: widget

    // The manifest's "settings" (format, dateFormat, fontSize), else the
    // panel clock's format, so a 12-hour panel gets a 12-hour desktop clock.
    readonly property var pluginSettings: (parent && parent.modelData && parent.modelData.settings)
                                          ? parent.modelData.settings : ({})
    readonly property string timeFormat: pluginSettings.format
                                         || Settings.values["panel.clockFormat"] || "HH:mm"
    readonly property string dateFormat: pluginSettings.dateFormat || "dddd, d MMMM"
    readonly property int timeSize: Number(pluginSettings.fontSize) > 0 ? Number(pluginSettings.fontSize) : 36

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
            text: Qt.formatDateTime(Shell.now, widget.timeFormat)
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: widget.timeSize
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: Qt.formatDateTime(Shell.now, widget.dateFormat)
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
        }
    }
}
