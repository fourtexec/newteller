import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// The font dropdown of the settings: every font family installed on the
// system (Qt.fontFamilies()), filterable, each entry previewed in its own
// face. A plain popup window that belongs to the settings window.
Window {
    id: fontPicker

    signal picked(string family)

    property string current: ""
    readonly property int framePad: 8

    width: 300
    height: 400
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint

    function openFor(family, item) {
        current = family || "";
        filter.text = "";
        if (item) {
            const g = item.mapToGlobal(0, 0);
            x = Math.max(8, Math.min(g.x, Screen.width - width - 8));
            y = Math.max(8, Math.min(g.y + item.height + 4, Screen.height - height - 8));
        }
        visible = true;
        requestActivate();
        filter.forceActiveFocus();
    }

    onActiveChanged: {
        if (!active)
            visible = false;
    }

    ShellFrame {
        anchors.fill: parent

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                radius: Theme.radius
                color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.95)
                border.width: filter.activeFocus ? 1 : 0
                border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.7)

                TextInput {
                    id: filter
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                    verticalAlignment: TextInput.AlignVCenter

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: filter.text.length === 0
                        text: "Filter fonts..."
                        color: Theme.muted
                        font: filter.font
                    }
                }
            }

            ListView {
                id: fontList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds

                model: {
                    const all = Qt.fontFamilies();
                    const q = filter.text.trim().toLowerCase();
                    return q.length === 0 ? all : all.filter(f => f.toLowerCase().indexOf(q) >= 0);
                }

                delegate: Item {
                    id: row
                    required property var modelData
                    width: fontList.width
                    height: 34

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radius
                        color: row.modelData === fontPicker.current
                               ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
                               : rowMouse.containsMouse
                                 ? Qt.rgba(Theme.foreground.r, Theme.foreground.g,
                                           Theme.foreground.b, 0.08)
                                 : "transparent"

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            spacing: 0

                            Text {
                                text: row.modelData
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pointSize: Theme.fontSize
                            }
                            Text {
                                text: "The quick brown fox  ·  " + row.modelData
                                color: Theme.muted
                                // The preview is rendered in the font itself.
                                font.family: row.modelData
                                font.pointSize: Theme.fontSize
                                elide: Text.ElideRight
                                width: Math.min(implicitWidth, fontList.width - 60)
                            }
                        }

                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                fontPicker.picked(row.modelData);
                                fontPicker.visible = false;
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: fontList.count === 0
                    text: "No font matches"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                }
            }
        }
    }
}
