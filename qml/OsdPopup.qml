import QtQuick
import QtQuick.Window
import org.kde.layershell 1.0 as LayerShell

// The on-screen display for volume, brightness and media keys: a small pill
// near the bottom of the screen with an icon and a level bar. It never takes
// the keyboard or the pointer and hides itself after a moment.
Window {
    id: osd

    property string iconName: "volume-2"
    property string text: ""
    // 0..100, or -1 for no bar (media keys show the track instead).
    property int level: -1

    width: 300
    height: 64
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus

    LayerShell.Window.anchors: LayerShell.Window.AnchorBottom
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.scope: "newteller-osd"
    LayerShell.Window.margins.bottom: 120

    function show(icon, newLevel, newText) {
        iconName = icon;
        level = newLevel;
        text = newText || "";
        visible = true;
        hideTimer.restart();
    }

    onVisibleChanged: {
        const screenName = osd.screen ? osd.screen.name : "";
        if (visible)
            Session.setBlur(String(osd), "newteller-osd", screenName, 0, 0, width, height, height / 2);
        else
            Session.clearBlur(String(osd), "newteller-osd", screenName);
    }

    Timer {
        id: hideTimer
        interval: 1500
        onTriggered: osd.visible = false
    }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.94)
        border.width: 1
        border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.9)

        Image {
            id: osdIcon
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            width: 24
            height: 24
            sourceSize.width: 48
            sourceSize.height: 48
            fillMode: Image.PreserveAspectFit
            source: Shell.tintedIcon(osd.iconName, Theme.foreground)
        }

        // Level bar.
        Rectangle {
            visible: osd.level >= 0
            anchors.left: osdIcon.right
            anchors.leftMargin: 14
            anchors.right: levelText.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            height: 6
            radius: 3
            color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.15)

            Rectangle {
                width: parent.width * Math.max(0, Math.min(100, osd.level)) / 100
                height: parent.height
                radius: 3
                color: Theme.accent
                Behavior on width { NumberAnimation { duration: 90 } }
            }
        }
        Text {
            id: levelText
            visible: osd.level >= 0
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            width: 34
            horizontalAlignment: Text.AlignRight
            text: osd.level + ""
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
        }

        // Media keys: the track instead of a bar.
        Text {
            visible: osd.level < 0
            anchors.left: osdIcon.right
            anchors.leftMargin: 14
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            text: osd.text
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
            elide: Text.ElideRight
        }
    }
}
