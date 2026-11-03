import QtQuick
import QtQuick.Window
import org.kde.layershell 1.0 as LayerShell

// Force quit (Ctrl+Shift+Esc or Ctrl+Alt+Esc), like macOS' "Force Quit
// Applications" or GNOME's "not responding" dialog: every open window with
// its state, "End task" kills the process behind it, "Close" asks it
// politely. It also opens on its own when the focused window stops answering
// the compositor (`Session.windowHung`). Escape or a click outside closes it.
Window {
    id: dialog
    objectName: "forceQuit"

    property var windowsList: []
    property int selected: 0
    property string message: ""
    // The window that triggered the dialog by hanging, shown first.
    property int hungId: -1

    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint

    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityExclusive
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.scope: "newteller-forcequit"

    x: Screen.virtualX
    y: Screen.virtualY
    width: Screen.width
    height: Screen.height

    function reload() {
        const all = Session.windows();
        const hung = Session.hungWindows;
        const list = [];
        for (var i = 0; i < all.length; ++i) {
            const w = all[i];
            list.push({
                id: w.id,
                appId: w.appId,
                title: w.title,
                hung: w.hung === true || hung.indexOf(w.id) >= 0
            });
        }
        // Not-responding windows first.
        list.sort(function (a, b) { return (b.hung ? 1 : 0) - (a.hung ? 1 : 0); });
        windowsList = list;
        if (selected >= list.length)
            selected = Math.max(0, list.length - 1);
    }

    function show() {
        message = "";
        selected = 0;
        reload();
        visible = true;
        requestActivate();
        keys.forceActiveFocus();
    }

    function hide() {
        visible = false;
        hungId = -1;
    }

    function current() {
        return selected >= 0 && selected < windowsList.length ? windowsList[selected] : null;
    }

    function endTask() {
        const w = current();
        if (!w)
            return;
        const error = Session.killWindow(w.id);
        message = error === "" ? qsTr("Ended %1").arg(w.title || w.appId) : error;
        refresh.restart();
    }

    function closeSelected() {
        const w = current();
        if (!w)
            return;
        Session.closeWindow(w.id);
        refresh.restart();
    }

    Timer {
        id: refresh
        interval: 400
        onTriggered: dialog.reload()
    }

    Connections {
        target: Session
        function onHungWindowsChanged() {
            if (dialog.visible)
                dialog.reload();
        }
        function onWindowHung(id) {
            // Only the window the user is working with: a background window
            // that hangs must not pop a dialog over everything.
            const all = Session.windows();
            for (var i = 0; i < all.length; ++i) {
                if (all[i].id === id && all[i].focused) {
                    dialog.hungId = id;
                    if (!dialog.visible)
                        dialog.show();
                    return;
                }
            }
        }
    }

    // Dimmed backdrop; a click on it closes the dialog.
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.45
        MouseArea {
            anchors.fill: parent
            onClicked: dialog.hide()
        }
    }

    Rectangle {
        id: card
        width: Math.min(520, dialog.width - 32)
        height: Math.min(column.implicitHeight + 32, dialog.height - 64)
        anchors.centerIn: parent
        radius: Theme.radius
        color: Theme.background
        border.color: Theme.border
        border.width: 1

        // Swallow clicks so they do not reach the backdrop.
        MouseArea { anchors.fill: parent }

        Column {
            id: column
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            Text {
                text: qsTr("Force Quit")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize + 3
                font.bold: true
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: dialog.hungId >= 0
                      ? qsTr("An application is not responding. You can wait for it or end it; unsaved work in it is lost.")
                      : qsTr("Choose an application to end. Unsaved work in it is lost.")
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize
            }

            Rectangle {
                width: parent.width
                height: Math.min(320, Math.max(48, list.contentHeight + 8))
                radius: Theme.radius
                color: Theme.surface
                border.color: Theme.border
                border.width: 1
                clip: true

                ListView {
                    id: list
                    objectName: "forceQuitList"
                    anchors.fill: parent
                    anchors.margins: 4
                    model: dialog.windowsList
                    currentIndex: dialog.selected
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: list.width
                        height: 40
                        radius: Theme.radius
                        color: index === dialog.selected ? Theme.border : "transparent"

                        Image {
                            id: icon
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: 24
                            height: 24
                            sourceSize: Qt.size(24, 24)
                            source: Shell.iconUrl(modelData.appId || "application-x-executable")
                        }

                        Text {
                            anchors.left: icon.right
                            anchors.leftMargin: 10
                            anchors.right: statusText.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: modelData.title || modelData.appId || qsTr("Window")
                            color: Theme.foreground
                            font.family: Theme.fontFamily
                            font.pointSize: Theme.fontSize
                        }

                        Text {
                            id: statusText
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.hung ? qsTr("Not responding") : ""
                            color: "#e5534b"
                            font.family: Theme.fontFamily
                            font.pointSize: Theme.fontSize - 1
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: dialog.selected = index
                            onDoubleClicked: {
                                dialog.selected = index;
                                dialog.endTask();
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: dialog.windowsList.length === 0
                    text: qsTr("No open windows")
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                }
            }

            Text {
                width: parent.width
                visible: dialog.message !== ""
                wrapMode: Text.WordWrap
                text: dialog.message
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }

            Row {
                anchors.right: parent.right
                spacing: 8

                Repeater {
                    model: [
                        { label: dialog.hungId >= 0 ? qsTr("Wait") : qsTr("Cancel"), action: "cancel" },
                        { label: qsTr("Close"), action: "close" },
                        { label: qsTr("End task"), action: "kill" }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        width: Math.max(88, label.implicitWidth + 28)
                        height: 32
                        radius: Theme.radius
                        color: modelData.action === "kill"
                               ? (buttonArea.containsMouse ? "#c9423b" : "#b3362f")
                               : (buttonArea.containsMouse ? Theme.border : Theme.surface)
                        border.color: Theme.border
                        border.width: modelData.action === "kill" ? 0 : 1
                        opacity: modelData.action !== "cancel" && !dialog.current() ? 0.5 : 1

                        Text {
                            id: label
                            anchors.centerIn: parent
                            text: modelData.label
                            color: modelData.action === "kill" ? "#ffffff" : Theme.foreground
                            font.family: Theme.fontFamily
                            font.pointSize: Theme.fontSize
                        }

                        MouseArea {
                            id: buttonArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if (modelData.action === "kill")
                                    dialog.endTask();
                                else if (modelData.action === "close")
                                    dialog.closeSelected();
                                else
                                    dialog.hide();
                            }
                        }
                    }
                }
            }
        }
    }

    Item {
        id: keys
        focus: true
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Escape) {
                dialog.hide();
            } else if (event.key === Qt.Key_Down) {
                dialog.selected = Math.min(dialog.windowsList.length - 1, dialog.selected + 1);
            } else if (event.key === Qt.Key_Up) {
                dialog.selected = Math.max(0, dialog.selected - 1);
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                       || event.key === Qt.Key_Delete) {
                dialog.endTask();
            } else {
                return;
            }
            event.accepted = true;
        }
    }
}
