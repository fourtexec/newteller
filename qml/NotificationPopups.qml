import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import org.kde.layershell 1.0 as LayerShell

// Notification popups, one window per screen. The daemon (src/notifications)
// owns the queue; this window mirrors `Notifications.active` as a stack of
// cards in the configured corner (notifications.position) and dismisses them
// when their timeout elapses or the user clicks.
Window {
    id: notif

    readonly property string corner: Settings.values["notifications.position"] || "top-right"
    readonly property bool topEdge: corner.indexOf("top") === 0
    readonly property bool rightEdge: corner.indexOf("right") > 0
    readonly property bool centered: corner.indexOf("center") > 0
    readonly property int screenMargin: 10
    readonly property int cardWidth: 344
    // Newest at the bottom, at most four cards on screen.
    readonly property var shown: (Notifications.active || []).slice(-4)
    readonly property int framePad: 8

    property bool initialized: false

    width: cardWidth + 2 * framePad
    height: Math.min(Screen.height - 2 * screenMargin, column.implicitHeight + 2 * framePad)
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint

    // Positioning without layer-shell (X11, XWayland).
    x: {
        if (centered)
            return Screen.virtualX + (Screen.width - width) / 2;
        return rightEdge ? Screen.virtualX + Screen.width - width - screenMargin
                         : Screen.virtualX + screenMargin;
    }
    y: topEdge ? Screen.virtualY + screenMargin
               : Screen.virtualY + Screen.height - height - screenMargin

    LayerShell.Window.anchors: {
        var a = topEdge ? LayerShell.Window.AnchorTop : LayerShell.Window.AnchorBottom;
        if (centered)
            a |= LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight;
        else
            a |= rightEdge ? LayerShell.Window.AnchorRight : LayerShell.Window.AnchorLeft;
        return a;
    }
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.exclusionZone: 0
    LayerShell.Window.scope: "newteller-notifications"
    LayerShell.Window.margins {
        left: centered ? 0 : notif.screenMargin
        right: centered ? 0 : notif.screenMargin
        top: topEdge ? notif.screenMargin : 0
        bottom: topEdge ? 0 : notif.screenMargin
    }

    function sync() {
        const enabled = Settings.values["notifications.enabled"] !== false;
        popupsModel.model = notif.shown;
        visible = enabled && notif.shown.length > 0;
    }

    Connections {
        target: Notifications
        function onActiveChanged() { notif.sync(); }
        function onHistoryChanged() { notif.sync(); }
    }
    Connections {
        target: Settings
        function onValuesChanged() { notif.sync(); }
    }
    Component.onCompleted: notif.sync()

    // --- cards ---------------------------------------------------------------
    Column {
        id: column
        anchors.fill: parent
        anchors.margins: notif.framePad
        spacing: 8

        Repeater {
            id: popupsModel
            model: []

            delegate: ShellFrame {
                id: card
                required property var modelData
                width: column.width
                height: cardColumn.implicitHeight + 26

                readonly property var entry: modelData || {}
                readonly property var actions: (entry.actions || []).filter(a => a && a.length > 0)
                // 1.0 -> 0.0 while the timeout runs; drives the progress line.
                property real progress: (entry.timeout || 0) > 0 ? 1.0 : 0.0

                // Expire on the daemon-side timeout (0 = sticky).
                Timer {
                    id: lifeTimer
                    running: card.entry.timeout > 0 && card.visible
                    interval: card.entry.timeout || 0
                    onTriggered: Notifications.dismiss(card.entry.id, 1)
                }
                NumberAnimation {
                    target: card
                    property: "progress"
                    from: 1.0
                    to: 0.0
                    duration: Math.max(1, card.entry.timeout || 1)
                    running: lifeTimer.running
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: card.actions.length === 0
                    onClicked: Notifications.dismiss(card.entry.id, 2)
                }

                RowLayout {
                    id: cardColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 11

                    // The notification's own accent: a rounded vertical bar in
                    // the theme accent colour, echoing the shell chrome.
                    Rectangle {
                        Layout.preferredWidth: 3
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignLeft
                        radius: 1.5
                        color: Theme.accent
                    }

                    Item {
                        Layout.preferredWidth: 38
                        Layout.preferredHeight: 38
                        Layout.alignment: Qt.AlignTop
                        Rectangle {
                            anchors.fill: parent
                            radius: 12
                            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                            border.width: 1
                            border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)
                            Image {
                                anchors.centerIn: parent
                                width: 22
                                height: 22
                                sourceSize.width: 44
                                sourceSize.height: 44
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                source: Shell.tintedIcon(card.entry.icon && card.entry.icon.length > 0
                                                         ? card.entry.icon : "bell", Theme.foreground)
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            visible: (card.entry.app || "").length > 0
                            text: (card.entry.app || "").toUpperCase()
                            color: Theme.muted
                            font.family: Theme.fontFamily
                            font.pointSize: Theme.fontSize - 3
                            font.letterSpacing: 0.8
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: (card.entry.summary || "").length > 0
                            text: card.entry.summary || ""
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pointSize: Theme.fontSize + 1
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: (card.entry.body || "").length > 0
                            text: card.entry.body || ""
                            color: Qt.rgba(Theme.foreground.r, Theme.foreground.g,
                                           Theme.foreground.b, 0.85)
                            font.family: Theme.fontFamily
                            font.pointSize: Theme.fontSize
                            wrapMode: Text.Wrap
                            maximumLineCount: 4
                            elide: Text.ElideRight
                        }

                        Row {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            spacing: 6
                            visible: card.actions.length > 0

                            Repeater {
                                // The spec sends key/label pairs.
                                model: Math.floor(card.actions.length / 2)

                                delegate: TaskButton {
                                    required property int index
                                    readonly property int at: index * 2
                                    label: card.actions.length > at + 1 ? card.actions[at + 1]
                                                                        : card.actions[at]
                                    onClicked: {
                                        Notifications.invokeAction(card.entry.id, card.actions[at]);
                                    }
                                }
                            }
                        }
                    }

                    TaskButton {
                        id: closeButton
                        Layout.alignment: Qt.AlignTop
                        width: 26
                        height: 26
                        iconName: "x"
                        // Quiet until the pointer reaches the card.
                        opacity: cardHover.hovered ? 1.0 : 0.35
                        Behavior on opacity {
                            NumberAnimation { duration: 120 }
                        }
                        onClicked: Notifications.dismiss(card.entry.id, 2)
                    }
                }

                // Remaining-time line along the bottom edge.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 3
                    radius: 1.5
                    visible: (card.entry.timeout || 0) > 0
                    clip: true
                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * card.progress
                        radius: 1.5
                        color: Theme.accent
                    }
                }

                HoverHandler {
                    id: cardHover
                }
            }
        }
    }
}
