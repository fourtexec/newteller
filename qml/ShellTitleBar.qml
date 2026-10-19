import QtQuick
import QtQuick.Layouts

// Title bar of the shell's own windows: a small caption row with the window
// title, an optional glyph and a close button. It gives every popup the same
// recognisable NewTeller chrome (the ShellFrame card supplies the border and
// the background around it).
Rectangle {
    id: bar

    property string title: ""
    property string iconName: ""
    // Emitted by the close button; windows hide themselves.
    signal closeClicked()

    readonly property real contentHeight: 30

    implicitWidth: 200
    height: contentHeight
    radius: Theme.radius
    color: "transparent"

    RowLayout {
        anchors.fill: parent
        spacing: 8

        Item {
            visible: bar.iconName.length > 0
            width: 18
            height: 18
            Layout.alignment: Qt.AlignVCenter

            Image {
                anchors.centerIn: parent
                width: 18
                height: 18
                sourceSize.width: 36
                sourceSize.height: 36
                fillMode: Image.PreserveAspectFit
                smooth: true
                source: Shell.tintedIcon(bar.iconName, Theme.foreground)
            }
        }

        Text {
            Layout.fillWidth: true
            text: bar.title
            color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.9)
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
            font.bold: true
            elide: Text.ElideRight
        }

        TaskButton {
            width: 26
            height: 26
            iconName: "x"
            onClicked: bar.closeClicked()
        }
    }

    // A thin accent underline that visually connects the bar with the card.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.55)
    }
}
