import QtQuick

// Reusable clickable button with an optional icon and label.
// Icons are resolved by Shell.iconUrl(): IconKit first, then the system icon theme.
Rectangle {
    id: btn

    property string iconName: ""
    property string label: ""
    property bool active: false
    property bool fillWidth: false
    // The label is Qt StyledText (used for search hit highlighting).
    property bool richLabel: false
    // Small dot in the top-right corner (an application that is running).
    property bool running: false

    signal clicked()

    radius: Theme.radius
    implicitWidth: row.implicitWidth + 16
    implicitHeight: row.implicitHeight + 10
    // HoverHandler (not MouseArea.containsMouse) so a full-panel hover
    // surface can coexist with the buttons without stealing their hover.
    HoverHandler {
        id: hoverHandler
    }
    color: area.pressed
           ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45)
           : (btn.active || hoverHandler.hovered)
             ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.25)
             : "transparent"

    Row {
        id: row
        spacing: 8
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: btn.fillWidth ? parent.left : undefined
        anchors.leftMargin: 8
        anchors.horizontalCenter: btn.fillWidth ? undefined : parent.horizontalCenter

        Image {
            visible: btn.iconName !== ""
            anchors.verticalCenter: parent.verticalCenter
            width: 22
            height: 22
            sourceSize.width: 22
            sourceSize.height: 22
            fillMode: Image.PreserveAspectFit
            smooth: true
            source: Shell.iconUrl(btn.iconName)
        }

        Text {
            visible: btn.label !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: btn.label
            textFormat: btn.richLabel ? Text.StyledText : Text.PlainText
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
        }
    }

    Rectangle {
        visible: btn.running
        width: 8
        height: 8
        radius: 4
        color: Theme.accent
        anchors.right: parent.right
        anchors.rightMargin: 3
        anchors.top: parent.top
        anchors.topMargin: 3
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: btn.clicked()
    }
}
