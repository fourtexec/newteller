import QtQuick

// Section container used by the settings pages: an optional title and a
// bordered body that holds the setting rows.
Column {
    id: section

    default property alias content: bodyColumn.data
    property string title: ""
    property string subtitle: ""
    width: parent ? parent.width : 320

    spacing: 6

    Text {
        visible: section.title.length > 0
        text: section.title
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pointSize: Theme.fontSize + 2
        font.bold: true
    }
    Text {
        visible: section.subtitle.length > 0
        text: section.subtitle
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pointSize: Theme.fontSize - 1
        width: section.width
        wrapMode: Text.Wrap
    }

    Rectangle {
        width: section.width
        height: bodyColumn.implicitHeight + 20
        radius: Theme.radius
        color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.35)
        border.width: 1
        border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.5)

        Column {
            id: bodyColumn
            anchors.fill: parent
            anchors.margins: 10
            spacing: 0
        }
    }
}
