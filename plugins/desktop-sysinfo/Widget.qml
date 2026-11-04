import QtQuick

// Desktop plugin: host, uptime and load, from the shared System monitor.
Rectangle {
    id: widget

    width: 300
    height: column.implicitHeight + 24
    radius: Theme.radius + 4
    color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.85)
    border.width: 1
    border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)

    Column {
        id: column
        anchors.centerIn: parent
        width: parent.width - 24
        spacing: 4

        Text {
            width: parent.width
            text: Shell.hostname + " · " + Shell.distro
            elide: Text.ElideRight
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize + 2
        }
        Text {
            width: parent.width
            text: qsTr("Kernel %1").arg(Shell.kernelVersion)
            elide: Text.ElideRight
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 1
        }
        Text {
            width: parent.width
            text: qsTr("Up %1 · %2 processes").arg(System.uptimeText).arg(System.processes)
            elide: Text.ElideRight
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
        }
        Text {
            width: parent.width
            text: qsTr("Load %1 %2 %3 · %4 cores").arg(System.load1.toFixed(2)).arg(System.load5.toFixed(2))
                  .arg(System.load15.toFixed(2)).arg(System.cpuCount)
            elide: Text.ElideRight
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
        }
        Text {
            width: parent.width
            visible: System.batteryPresent
            text: qsTr("Battery %1% %2").arg(Math.round(System.batteryPercent)).arg(System.batteryStatus)
            elide: Text.ElideRight
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
        }
    }
}
