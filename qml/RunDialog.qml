import QtQuick
import QtQuick.Window

// Alt+F2 style launcher: type a command or search the applications.
Window {
    id: dialog

    property var results: []

    width: 520
    height: 360
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool
    // Centered on the primary screen; without layer-shell (X11) this is the
    // real position, with layer-shell the margins do the same job.
    x: Screen.virtualX + Math.max(0, (Screen.width - width) / 2)
    y: Screen.virtualY + Math.max(0, (Screen.height - height) / 3)

    function show() {
        visible = true;
        input.text = "";
        refresh();
        input.forceActiveFocus();
        requestActivate();
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

    function refresh() {
        const q = input.text.trim();
        var hits = Apps.searchDetailed(q, 30);
        if (hits.length === 0)
            hits = Apps.search(q, 30);
        results = hits;
    }

    function runCurrent() {
        const q = input.text.trim();
        if (q.length === 0)
            return;
        if (results.length > 0) {
            Apps.launch(results[0].id);
        } else {
            // A bare command: run it through the shell.
            Shell.runShell(q);
        }
        hide();
    }

    onActiveChanged: {
        if (!active)
            hide();
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
                width: parent.width
                height: 38
                radius: Theme.radius
                color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.95)

                TextInput {
                    id: input
                    objectName: "runQuery"
                    anchors.fill: parent
                    anchors.margins: 10
                    focus: true
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize + 1
                    verticalAlignment: TextInput.AlignVCenter
                    onTextChanged: dialog.refresh()
                    Keys.onEscapePressed: dialog.hide()
                    Keys.onReturnPressed: dialog.runCurrent()

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: input.text.length === 0
                        text: "Run a command or search applications..."
                        color: Theme.muted
                        font: input.font
                    }
                }
            }

            ListView {
                width: parent.width
                height: parent.height - 48
                clip: true
                spacing: 2
                model: dialog.results

                delegate: TaskButton {
                    required property var modelData
                    width: ListView.view.width
                    fillWidth: true
                    iconName: modelData.icon || ""
                    label: modelData.highlight && modelData.highlight.length > 0
                           ? modelData.highlight : (modelData.name || modelData.id)
                    onClicked: {
                        Apps.launch(modelData.id);
                        dialog.hide();
                    }
                }
            }
        }
    }
}
