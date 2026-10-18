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
    // A running application: small indicator under the icon or after the label.
    property bool running: false
    // Number of windows behind the button; > 1 widens the running indicator.
    property int badge: 0
    property int iconSize: 22
    // A dot pinned to the top-right corner (used by the category chips).
    property bool cornerDot: false

    signal clicked()
    // Middle click closes windows (dock behaviour); optional.
    signal middleClicked()

    radius: Theme.radius
    implicitWidth: row.implicitWidth + 16
    implicitHeight: Math.max(row.implicitHeight + 10, iconSize + 12)
    // HoverHandler (not MouseArea.containsMouse) so a full-panel hover
    // surface can coexist with the buttons without stealing their hover.
    HoverHandler {
        id: hoverHandler
    }
    color: area.pressed
           ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45)
           : btn.active
             ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
             : hoverHandler.hovered
               ? Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.08)
               : "transparent"

    Behavior on color {
        ColorAnimation { duration: 120 }
    }

    Row {
        id: row
        spacing: 8
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: btn.fillWidth ? parent.left : undefined
        anchors.leftMargin: 8
        anchors.horizontalCenter: btn.fillWidth ? undefined : parent.horizontalCenter

        Item {
            width: btn.iconSize
            height: btn.iconSize
            anchors.verticalCenter: parent.verticalCenter
            visible: btn.iconName !== ""

            Image {
                id: icon
                anchors.centerIn: parent
                width: btn.iconSize
                height: btn.iconSize
                sourceSize.width: btn.iconSize * 2
                sourceSize.height: btn.iconSize * 2
                fillMode: Image.PreserveAspectFit
                smooth: true
                source: Shell.iconUrl(btn.iconName)
                // Tiny press feedback: the icon dips while the button is held.
                scale: area.pressed ? 0.88 : 1.0
                Behavior on scale {
                    NumberAnimation { duration: 90 }
                }
            }
        }

        Text {
            visible: btn.label !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: btn.label
            textFormat: btn.richLabel ? Text.StyledText : Text.PlainText
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
            elide: Text.ElideRight
        }
    }

    // Running indicator: a short pill centred under the content, wider when
    // the application has several windows.
    Rectangle {
        visible: btn.running
        width: Math.min(btn.badge > 1 ? 22 : 12, btn.width - 12)
        height: 3
        radius: 1.5
        color: btn.active ? Theme.accent
                          : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.75)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 3
    }

    Rectangle {
        visible: btn.cornerDot
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
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        cursorShape: Qt.PointingHandCursor
        onClicked: (mouse) => {
            if (mouse.button === Qt.MiddleButton)
                btn.middleClicked();
            else
                btn.clicked();
        }
    }
}
