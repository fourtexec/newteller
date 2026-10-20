import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import org.kde.layershell 1.0 as LayerShell

// The notification centre: one window on the primary screen, opened from the
// bell at the end of the panel. It lists the daemon's history newest first,
// offers per-row removal, a clear-all button and the do-not-disturb switch.
Window {
    id: center

    property int panelHeight: Settings.values["panel.height"] || 48
    property int panelMargin: Settings.values["panel.margin"] || 0

    readonly property var entries: {
        var list = (Notifications.history || []).slice();
        list.reverse(); // newest first
        return list;
    }
    // Notifications that arrived while the centre was closed: the badge on
    // the bell. Opening the centre marks everything seen.
    readonly property int unreadCount: visible ? 0 : countUnread()

    function countUnread() {
        var n = 0;
        const seen = lastSeen;
        for (const entry of Notifications.history) {
            if ((entry.time || 0) > seen)
                n++;
        }
        return n;
    }

    // Timestamp of the newest notification the user has looked at.
    property real lastSeen: 0

    function show() {
        centerCard.prepareOpen();
        visible = true;
        requestActivate();
        markSeen();
        centerCard.playOpen();
    }

    function hide() {
        if (!visible || centerCard.closing)
            return;
        markSeen();
        centerCard.playClose(function() { visible = false; });
    }

    function toggle() {
        if (visible)
            hide();
        else
            show();
    }

    function markSeen() {
        const history = Notifications.history || [];
        if (history.length > 0)
            lastSeen = history[history.length - 1].time || 0;
        else
            lastSeen = Shell.now.getTime();
    }

    width: 400
    height: 520
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
    // Positioning without layer-shell (X11, XWayland): bottom-right, above
    // the panel.
    x: Screen.virtualX + Screen.width - width - 8
    y: Screen.virtualY + Screen.height - height - panelHeight - panelMargin - 12

    LayerShell.Window.anchors: LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorRight
    LayerShell.Window.margins { right: 8; bottom: center.panelHeight + center.panelMargin + 12 }
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityOnDemand
    LayerShell.Window.scope: "newteller-notifications"

    onActiveChanged: {
        if (!active)
            closeTimer.restart();
    }

    Timer {
        id: closeTimer
        interval: 200
        onTriggered: {
            if (!center.active)
                center.hide();
        }
    }

    ShellFrame {
        id: centerCard
        anchors.fill: parent
        // The card grows out of the bell at the panel's end.
        openOrigin: Item.BottomRight

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            ShellTitleBar {
                Layout.fillWidth: true
                title: "Notifications"
                iconName: "bell"
                onCloseClicked: center.hide()
            }

            // Do not disturb: the same key the notifications settings page
            // and the quick settings popup write.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Image {
                    width: 16
                    height: 16
                    sourceSize.width: 32
                    sourceSize.height: 32
                    fillMode: Image.PreserveAspectFit
                    source: Shell.tintedIcon(
                                Settings.values["notifications.doNotDisturb"] ? "bell-off" : "bell",
                                Theme.foreground)
                }
                Text {
                    Layout.fillWidth: true
                    text: "Do not disturb"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                }
                SettingsSwitch {
                    settingKey: "notifications.doNotDisturb"
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)
            }

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: center.entries
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    id: row
                    required property var modelData
                    width: list.width
                    height: rowBody.implicitHeight + 18
                    radius: Theme.radius
                    color: rowHover.hovered
                           ? Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.06)
                           : Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.6)
                    border.width: 1
                    border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.5)

                    readonly property bool isActive: {
                        const id = modelData.id;
                        for (const a of Notifications.active) {
                            if (a.id === id)
                                return true;
                        }
                        return false;
                    }

                    HoverHandler { id: rowHover }

                    Rectangle {
                        // Accent bar: still-on-screen entries read as "new".
                        visible: row.isActive
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.margins: 4
                        width: 3
                        radius: 1.5
                        color: Theme.accent
                    }

                    ColumnLayout {
                        id: rowBody
                        anchors.left: parent.left
                        anchors.right: rowControls.left
                        anchors.top: parent.top
                        anchors.margins: 9
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: row.modelData.app || ""
                                color: Theme.accent
                                font.family: Theme.fontFamily
                                font.pointSize: Theme.fontSize - 1
                                elide: Text.ElideRight
                                Layout.maximumWidth: rowBody.width * 0.55
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: row.modelData.time > 0
                                      ? Qt.formatDateTime(new Date(row.modelData.time), "HH:mm")
                                      : ""
                                color: Theme.muted
                                font.family: Theme.fontFamily
                                font.pointSize: Theme.fontSize - 1
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.summary || ""
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pointSize: Theme.fontSize + 1
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: (row.modelData.body || "").length > 0
                            text: row.modelData.body || ""
                            color: Theme.muted
                            font.family: Theme.fontFamily
                            font.pointSize: Theme.fontSize
                            wrapMode: Text.Wrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                        }
                    }

                    // Per-row controls: tap to dismiss a popup that is still
                    // on screen, X to forget the entry.
                    Row {
                        id: rowControls
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        TaskButton {
                            width: 26
                            height: 26
                            visible: row.isActive
                            iconName: "check"
                            tooltipText: "Dismiss"
                            onClicked: Notifications.dismiss(row.modelData.id)
                        }
                        TaskButton {
                            width: 26
                            height: 26
                            iconName: "x"
                            tooltipText: "Remove"
                            onClicked: Notifications.removeHistory(row.modelData.id)
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: {
                            if (row.isActive)
                                Notifications.dismiss(row.modelData.id);
                        }
                    }
                }

                // Empty state.
                Column {
                    anchors.centerIn: parent
                    width: list.width - 40
                    spacing: 8
                    visible: center.entries.length === 0

                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 28
                        height: 28
                        sourceSize.width: 56
                        sourceSize.height: 56
                        fillMode: Image.PreserveAspectFit
                        source: Shell.tintedIcon("bell", Theme.muted)
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "No notifications"
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: "Application notifications appear here\nand stay in the history."
                        color: Qt.rgba(Theme.muted.r, Theme.muted.g, Theme.muted.b, 0.7)
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 1
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                TaskButton {
                    iconName: "trash"
                    label: "Clear all"
                    enabled: center.entries.length > 0
                    onClicked: Notifications.clearHistory()
                }
                Item { Layout.fillWidth: true }
                TaskButton {
                    iconName: "settings"
                    label: "Settings"
                    onClicked: { Shell.openSettings(); center.hide(); }
                }
            }
        }
    }

    Connections {
        target: Notifications
        function onHistoryChanged() {
            if (center.visible)
                center.markSeen();
        }
    }

    Component.onCompleted: markSeen()
}
