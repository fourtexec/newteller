import QtQuick
import QtQuick.Window
import org.kde.layershell 1.0 as LayerShell

// Start menu: searchable application list plus session actions.
Window {
    id: menu

    property int panelHeight: Settings.values["panel.height"] || 48
    property var results: []

    width: Settings.values["startmenu.width"] || 440
    height: Settings.values["startmenu.height"] || 580
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool
    // Positioning used only without layer-shell (X11, including XWayland on GNOME).
    x: Screen.virtualX + 8
    y: Screen.virtualY + Screen.height - height - panelHeight - 12

    LayerShell.Window.anchors: LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorLeft
    LayerShell.Window.margins { left: 8; bottom: menu.panelHeight + 12 }
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityExclusive
    LayerShell.Window.scope: "newteller-menu"

    function show() {
        visible = true;
        // Layer-shell's exclusive keyboard mode focuses the menu on Wayland;
        // on X11 the window manager has to be asked.
        requestActivate();
        search.text = "";
        results = Apps.search("", 500);
        search.forceActiveFocus();
    }

    function hide() {
        visible = false;
    }

    function toggle() {
        if (visible)
            hide();
        else
            show();
    }

    function launch(app) {
        Apps.launch(app.id);
        hide();
    }

    // Hiding is delayed so that clicking the Start button while the menu
    // is open can toggle it closed instead of reopening it.
    onActiveChanged: {
        if (!active)
            closeTimer.restart();
    }

    Timer {
        id: closeTimer
        interval: 200
        onTriggered: {
            if (!menu.active)
                menu.hide();
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        radius: Theme.radius + 4
        color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.opacity)
        border.width: 1
        border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)

        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

            Rectangle {
                id: searchBox
                width: parent.width
                height: 38
                radius: Theme.radius
                color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.95)

                TextInput {
                    id: search
                    anchors.fill: parent
                    anchors.margins: 10
                    focus: true
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize + 1
                    verticalAlignment: TextInput.AlignVCenter
                    onTextChanged: menu.results = Apps.search(text, 200)
                    Keys.onEscapePressed: menu.hide()
                    Keys.onReturnPressed: {
                        if (menu.results.length > 0)
                            menu.launch(menu.results[0]);
                    }

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: search.text.length === 0
                        text: "Type to search apps..."
                        color: Theme.muted
                        font: search.font
                    }
                }
            }

            ListView {
                id: list
                width: parent.width
                height: parent.height - searchBox.height - footer.height - 20
                clip: true
                spacing: 2
                model: menu.results

                delegate: TaskButton {
                    required property var modelData
                    width: ListView.view.width
                    fillWidth: true
                    iconName: modelData.icon || ""
                    label: modelData.name
                    onClicked: menu.launch(modelData)
                }
            }

            Flow {
                id: footer
                width: parent.width
                spacing: 6

                TaskButton {
                    iconName: "lock"
                    label: "Lock"
                    onClicked: { Shell.lock(); menu.hide(); }
                }
                TaskButton {
                    iconName: "log-out"
                    label: "Log out"
                    onClicked: { Shell.logout(); menu.hide(); }
                }
                TaskButton {
                    iconName: "moon"
                    label: "Sleep"
                    onClicked: { Shell.suspend(); menu.hide(); }
                }
                TaskButton {
                    iconName: "rotate-cw"
                    label: "Restart"
                    onClicked: { Shell.reboot(); menu.hide(); }
                }
                TaskButton {
                    iconName: "power"
                    label: "Shut down"
                    onClicked: { Shell.powerOff(); menu.hide(); }
                }
                TaskButton {
                    iconName: "settings"
                    label: "Settings"
                    onClicked: { Shell.openConfig(); menu.hide(); }
                }
            }
        }
    }
}
