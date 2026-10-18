import QtQuick

// Panel plugin: compact CPU, memory and network meter, driven by the shared
// System monitor (one sampler for the whole shell).
Rectangle {
    id: widget

    readonly property int barWidth: 54

    width: row.implicitWidth + 16
    height: 34
    radius: Theme.radius
    color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.7)

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8

        Column {
            spacing: 2

            Text {
                text: "cpu " + Math.round(System.cpuPercent) + "%"
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }
            Rectangle {
                width: barWidth
                height: 4
                radius: 2
                color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.5)
                Rectangle {
                    width: parent.width * Math.min(1, System.cpuPercent / 100)
                    height: parent.height
                    radius: 2
                    color: Theme.accent
                }
            }
        }

        Column {
            spacing: 2

            Text {
                text: "mem " + Math.round(System.memoryPercent) + "%"
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }
            Rectangle {
                width: barWidth
                height: 4
                radius: 2
                color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.5)
                Rectangle {
                    width: parent.width * Math.min(1, System.memoryPercent / 100)
                    height: parent.height
                    radius: 2
                    color: Theme.muted
                }
            }
        }

        Column {
            spacing: 2

            Text {
                text: "down " + System.networkDownText
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }
            Text {
                text: "up " + System.networkUpText
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }
        }
    }
}
