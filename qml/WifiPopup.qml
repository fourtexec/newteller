import QtQuick
import QtQuick.Window
import QtQuick.Layouts

// Wi-Fi networks in range, opened from quick settings. Click a network to
// join it; secured networks that are not saved yet ask for the password
// inline. NetworkManager (over D-Bus) stores what it learns. When Wi-Fi cannot
// work, Session.wifiStatus says why (no NetworkManager, no adapter, missing
// firmware, switched off) instead of an empty list.
DropdownWindow {
    id: popup

    // The network waiting for a password, if any.
    property string pendingSsid: ""
    property string statusText: ""

    cardWidth: 360
    cardHeight: 440
    padding: 12
    scopeName: "newteller-wifi"
    exclusiveKeyboard: true

    function opened() {
        pendingSsid = "";
        statusText = "";
        Session.scanWifi();
    }

    function join(network) {
        statusText = "";
        if (network.active)
            return;
        if (network.secure) {
            // Try a saved connection first; without one the password field
            // opens right away.
            pendingSsid = network.ssid;
            passwordField.text = "";
            Session.connectWifi(network.ssid, "");
        } else {
            pendingSsid = "";
            Session.connectWifi(network.ssid, "");
        }
    }

    Connections {
        target: Session
        function onWifiConnectFinished(ok, ssid, message) {
            if (ok) {
                popup.pendingSsid = "";
                popup.statusText = "Connected to " + ssid;
            } else if (ssid === popup.pendingSsid) {
                popup.statusText = message.length > 0 ? message : "Enter the password for " + ssid;
                passwordField.forceActiveFocus();
            } else {
                popup.statusText = message;
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        ShellTitleBar {
            Layout.fillWidth: true
            title: "Wi-Fi networks"
            iconName: "wifi"
            onCloseClicked: popup.hide()
        }

        Text {
            Layout.fillWidth: true
            visible: popup.statusText.length > 0 || Session.wifiBusy
            text: Session.wifiBusy ? "Working..." : popup.statusText
            wrapMode: Text.WordWrap
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 1
        }

        // Password entry for the selected secured network.
        RowLayout {
            Layout.fillWidth: true
            visible: popup.pendingSsid.length > 0
            spacing: 6

            Rectangle {
                Layout.fillWidth: true
                height: 32
                radius: Theme.radius
                color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.10)
                border.width: passwordField.activeFocus ? 1 : 0
                border.color: Theme.accent

                TextInput {
                    id: passwordField
                    anchors.fill: parent
                    anchors.margins: 8
                    echoMode: TextInput.Password
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                    clip: true
                    onAccepted: Session.connectWifi(popup.pendingSsid, text)

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: passwordField.text.length === 0
                        text: "Password for " + popup.pendingSsid
                        color: Theme.muted
                        font: passwordField.font
                        elide: Text.ElideRight
                    }
                }
            }
            TaskButton {
                label: "Join"
                onClicked: Session.connectWifi(popup.pendingSsid, passwordField.text)
            }
        }

        // Why there is nothing to list.
        Rectangle {
            Layout.fillWidth: true
            visible: Session.wifiStatus.length > 0
            implicitHeight: diagnosis.implicitHeight + 20
            radius: Theme.radius
            color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.06)
            border.color: Theme.border
            border.width: 1

            Text {
                id: diagnosis
                objectName: "wifiDiagnosis"
                anchors.fill: parent
                anchors.margins: 10
                text: Session.wifiStatus
                wrapMode: Text.WordWrap
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }
        }

        ListView {
            id: networks
            objectName: "wifiList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: Session.wifiNetworks
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                required property var modelData
                width: networks.width
                height: 40
                radius: Theme.radius
                color: modelData.active
                       ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
                       : rowHover.hovered
                         ? Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.08)
                         : "transparent"

                HoverHandler {
                    id: rowHover
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: popup.join(modelData)
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8

                    Image {
                        width: 18
                        height: 18
                        sourceSize.width: 36
                        sourceSize.height: 36
                        fillMode: Image.PreserveAspectFit
                        opacity: 0.4 + 0.6 * modelData.signal / 100
                        source: Shell.tintedIcon("wifi", Theme.foreground)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.ssid
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize
                        font.bold: modelData.active
                        elide: Text.ElideRight
                    }
                    Image {
                        visible: modelData.secure
                        width: 14
                        height: 14
                        sourceSize.width: 28
                        sourceSize.height: 28
                        source: Shell.tintedIcon("lock", Theme.muted)
                    }
                    Text {
                        text: modelData.signal + "%"
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 2
                    }
                }
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            visible: networks.count === 0 && Session.wifiStatus.length === 0
            text: Session.wifiBusy ? "Searching for networks..." : "No networks found"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
        }

        RowLayout {
            Layout.fillWidth: true
            TaskButton {
                iconName: "rotate-cw"
                label: "Rescan"
                onClicked: Session.scanWifi()
            }
            Item { Layout.fillWidth: true }
            TaskButton {
                iconName: "x"
                label: "Disconnect"
                visible: {
                    for (var i = 0; i < Session.wifiNetworks.length; ++i) {
                        if (Session.wifiNetworks[i].active)
                            return true;
                    }
                    return false;
                }
                onClicked: Session.disconnectWifi()
            }
        }
    }
}
