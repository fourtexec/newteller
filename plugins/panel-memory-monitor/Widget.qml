import QtQuick
import QtQuick.Window

// Panel Memory Monitor Widget
// Shows memory usage as a compact bar graph in the panel.
Rectangle {
    id: widget

    readonly property var pluginSettings: (parent && parent.modelData && parent.modelData.settings)
                                          ? parent.modelData.settings : ({})
    property bool showPercentage: pluginSettings.showPercentage !== false
    property string barColor: pluginSettings.barColor ? String(pluginSettings.barColor) : "accent"
    property int widgetWidth: pluginSettings.width !== undefined ? Number(pluginSettings.width) : 84

    readonly property real memoryPercent: System ? System.memoryPercent : 0
    readonly property string memoryText: System ? System.memoryText : "0 / 0"
    readonly property color resolvedColor: barColor === "accent" ? Theme.accent
                                           : barColor === "foreground" ? Theme.foreground
                                           : barColor === "surface" ? Theme.surface
                                           : Qt.color(barColor)

    width: widgetWidth
    height: 34
    radius: Theme.radius
    color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.7)

    Row {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 6

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: widget.showPercentage ? Math.max(16, parent.width - 54) : Math.max(16, parent.width - 16)
            height: 8
            radius: 4
            color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.5)

            Rectangle {
                id: fillBar
                width: parent.width * Math.max(0, Math.min(1, widget.memoryPercent / 100))
                height: parent.height
                radius: 4
                color: widget.resolvedColor

                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.lighter(widget.resolvedColor, 1.2) }
                    GradientStop { position: 1.0; color: widget.resolvedColor }
                }
            }
        }

        Text {
            visible: widget.showPercentage
            width: 32
            anchors.verticalCenter: parent.verticalCenter
            text: Math.round(widget.memoryPercent) + "%"
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 1
            horizontalAlignment: Text.AlignRight
        }
    }

    HoverHandler {
        onHoveredChanged: {
            const win = widget.Window.window
            if (!win || typeof win.showToolTip !== "function")
                return
            if (hovered)
                win.showToolTip(widget,
                                "Memory " + Math.round(widget.memoryPercent) + "% ("
                                + widget.memoryText + ")")
            else if (typeof win.hideToolTip === "function")
                win.hideToolTip()
        }
    }
}
