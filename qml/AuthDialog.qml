import QtQuick
import QtQuick.Window
import org.kde.layershell 1.0 as LayerShell

// The administrator prompt ("UAC"): shown whenever a program asks polkit for
// administrator rights - "Open with root", installing software, changing a
// system setting. The screen dims, the card says who asks for what, and only
// the password (of the account polkit allows) lets it through. Escape or
// Cancel refuses. Driven entirely by `Polkit` (src/polkitagent.h).
Window {
    id: dialog
    objectName: "authDialog"

    readonly property bool wanted: Polkit.active
    // Unfolds the technical details (action id, full command line).
    property bool showDetails: false

    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint

    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityExclusive
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.scope: "newteller-auth"

    x: Screen.virtualX
    y: Screen.virtualY
    width: Screen.width
    height: Screen.height

    onWantedChanged: {
        if (wanted) {
            showDetails = false;
            password.text = "";
            visible = true;
            requestActivate();
            password.forceActiveFocus();
        } else {
            password.text = "";
            visible = false;
        }
    }

    // A wrong password clears the field for the next try.
    Connections {
        target: Polkit
        function onChanged() {
            if (Polkit.active && !Polkit.busy && Polkit.error.length > 0) {
                password.text = "";
                password.forceActiveFocus();
            }
        }
    }

    function allow() {
        if (!Polkit.busy)
            Polkit.respond(password.text);
    }

    // Who is asking, in words: the program name of a pkexec command line, or
    // polkit's own message.
    readonly property string requester: {
        const command = Polkit.program;
        if (command.length === 0)
            return "";
        // "/usr/bin/env WAYLAND_DISPLAY=... /usr/bin/gedit file" -> "gedit"
        const parts = command.split(" ").filter(p => p.length > 0 && p.indexOf("=") < 0);
        var program = parts.length > 0 ? parts[0] : command;
        if (program.endsWith("/env") && parts.length > 1)
            program = parts[1];
        const slash = program.lastIndexOf("/");
        return slash >= 0 ? program.substring(slash + 1) : program;
    }

    // Dimmed screen: nothing else can be clicked while the prompt is up.
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.55
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }
    }

    Rectangle {
        id: card
        width: Math.min(460, dialog.width - 32)
        height: content.implicitHeight + 40
        anchors.centerIn: parent
        radius: Theme.radius + 4
        color: Theme.background
        border.color: Theme.border
        border.width: 1

        Column {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 20
            spacing: 14

            Row {
                spacing: 12

                Rectangle {
                    width: 44
                    height: 44
                    radius: 22
                    color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.08)
                    Image {
                        anchors.centerIn: parent
                        width: 24
                        height: 24
                        sourceSize: Qt.size(48, 48)
                        source: Shell.tintedIcon("shield", Theme.foreground)
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        text: "Administrator access"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize + 3
                        font.bold: true
                    }
                    Text {
                        text: dialog.requester.length > 0
                              ? "“" + dialog.requester + "” wants to make changes to the system"
                              : "A program wants to make changes to the system"
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize
                        width: content.width - 56
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Text {
                width: parent.width
                visible: Polkit.message.length > 0
                text: Polkit.message
                wrapMode: Text.WordWrap
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize
            }

            // Details, folded by default.
            Text {
                text: (dialog.showDetails ? "▾ " : "▸ ") + "Details"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: dialog.showDetails = !dialog.showDetails
                }
            }
            Rectangle {
                width: parent.width
                visible: dialog.showDetails
                height: detailText.implicitHeight + 16
                radius: Theme.radius
                color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.05)
                Text {
                    id: detailText
                    anchors.fill: parent
                    anchors.margins: 8
                    wrapMode: Text.WrapAnywhere
                    color: Theme.muted
                    font.family: "monospace"
                    font.pointSize: Theme.fontSize - 2
                    text: "Action: " + Polkit.actionId
                          + (Polkit.program.length > 0 ? "\nCommand: " + Polkit.program : "")
                }
            }

            // Which account authorizes (when polkit allows several).
            Row {
                visible: Polkit.identities.length > 1
                spacing: 6
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Account:"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                }
                Repeater {
                    model: Polkit.identities
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        width: accountName.implicitWidth + 20
                        height: 28
                        radius: Theme.radius
                        color: index === Polkit.identityIndex
                               ? Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.16)
                               : "transparent"
                        border.color: Theme.border
                        border.width: 1
                        Text {
                            id: accountName
                            anchors.centerIn: parent
                            text: modelData
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pointSize: Theme.fontSize
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: Polkit.selectIdentity(index)
                        }
                    }
                }
            }

            // The password field.
            Rectangle {
                width: parent.width
                height: 40
                radius: Theme.radius
                color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.06)
                border.width: 1
                border.color: Polkit.error.length > 0 ? "#e5534b"
                              : password.activeFocus ? Theme.foreground : Theme.border

                TextInput {
                    id: password
                    objectName: "authPassword"
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    verticalAlignment: TextInput.AlignVCenter
                    echoMode: Polkit.echo ? TextInput.Normal : TextInput.Password
                    enabled: !Polkit.busy
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize + 1
                    clip: true
                    onAccepted: dialog.allow()
                    Keys.onEscapePressed: Polkit.cancel()

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: password.text.length === 0
                        text: Polkit.busy ? "Checking..."
                              : "Password for " + (Polkit.identities[Polkit.identityIndex] || "the administrator")
                        color: Theme.muted
                        font: password.font
                        elide: Text.ElideRight
                    }
                }
            }

            Text {
                width: parent.width
                visible: Polkit.error.length > 0 || Polkit.info.length > 0
                text: Polkit.error.length > 0 ? Polkit.error : Polkit.info
                wrapMode: Text.WordWrap
                color: Polkit.error.length > 0 ? "#e5534b" : Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }

            Row {
                anchors.right: parent.right
                spacing: 8

                Rectangle {
                    width: 96
                    height: 34
                    radius: Theme.radius
                    color: cancelArea.containsMouse ? Theme.border : Theme.surface
                    border.color: Theme.border
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: "Cancel"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize
                    }
                    MouseArea {
                        id: cancelArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: Polkit.cancel()
                    }
                }

                Rectangle {
                    width: 96
                    height: 34
                    radius: Theme.radius
                    opacity: Polkit.busy ? 0.6 : 1
                    color: allowArea.containsMouse ? Qt.darker(Theme.foreground, 1.15) : Theme.foreground
                    Text {
                        anchors.centerIn: parent
                        text: Polkit.busy ? "Checking" : "Allow"
                        color: Theme.background
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize
                        font.bold: true
                    }
                    MouseArea {
                        id: allowArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: dialog.allow()
                    }
                }
            }
        }
    }
}
