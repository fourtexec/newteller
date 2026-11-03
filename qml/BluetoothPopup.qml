import QtQuick
import QtQuick.Window
import QtQuick.Layouts

// Bluetooth devices, opened from quick settings: connect and disconnect
// paired devices, pair new ones while the card is open (it scans then).
DropdownWindow {
    id: popup

    property string statusText: ""

    cardWidth: 360
    cardHeight: 440
    padding: 12
    scopeName: "newteller-bluetooth"

    function opened() {
        statusText = "";
        Bluetooth.startDiscovery();
    }
    function closed() {
        Bluetooth.stopDiscovery();
    }

    Connections {
        target: Bluetooth
        function onOperationFinished(path, error) {
            popup.statusText = error.length > 0 ? error : "";
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        ShellTitleBar {
            Layout.fillWidth: true
            title: "Bluetooth"
            iconName: "bluetooth"
            onCloseClicked: popup.hide()
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: !Bluetooth.available ? "No Bluetooth adapter"
                      : !Bluetooth.powered ? "Bluetooth is off"
                      : Bluetooth.discovering ? "Looking for devices..." : "Bluetooth is on"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }
            SettingsSwitch {
                checked: Bluetooth.powered
                onChanged: (v) => {
                    if (v !== Bluetooth.powered)
                        Bluetooth.setPowered(v);
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: popup.statusText.length > 0
            text: popup.statusText
            wrapMode: Text.WordWrap
            color: Theme.accent
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 1
        }

        ListView {
            id: list
            objectName: "bluetoothList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: Bluetooth.powered ? Bluetooth.devices : []
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                required property var modelData
                width: list.width
                height: 46
                radius: Theme.radius
                color: modelData.connected
                       ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
                       : rowHover.hovered
                         ? Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.08)
                         : "transparent"

                HoverHandler {
                    id: rowHover
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: modelData.name
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pointSize: Theme.fontSize
                            font.bold: modelData.connected
                            elide: Text.ElideRight
                        }
                        Text {
                            text: Bluetooth.busyDevice === modelData.path ? "Working..."
                                  : modelData.connected ? "Connected"
                                  : modelData.paired ? "Paired" : "Available"
                            color: Theme.muted
                            font.family: Theme.fontFamily
                            font.pointSize: Theme.fontSize - 2
                        }
                    }
                    TaskButton {
                        enabled: Bluetooth.busyDevice.length === 0
                        label: modelData.connected ? "Disconnect"
                               : modelData.paired ? "Connect" : "Pair"
                        onClicked: {
                            popup.statusText = "";
                            if (modelData.connected)
                                Bluetooth.disconnectDevice(modelData.path);
                            else if (modelData.paired)
                                Bluetooth.connectDevice(modelData.path);
                            else
                                Bluetooth.pairDevice(modelData.path);
                        }
                    }
                    TaskButton {
                        visible: modelData.paired
                        width: 28
                        iconName: "trash"
                        tooltipText: "Forget this device"
                        onClicked: Bluetooth.forgetDevice(modelData.path)
                    }
                }
            }
        }
    }
}
