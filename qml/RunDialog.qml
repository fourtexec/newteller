import QtQuick
import QtQuick.Window
import QtQuick.Layouts

// Alt+F2 style launcher: type a command or search the applications.
Window {
    id: dialog

    property var results: []

    width: 520
    height: 360
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
    // Centered on the primary screen; without layer-shell (X11) this is the
    // real position, with layer-shell the margins do the same job.
    x: Screen.virtualX + Math.max(0, (Screen.width - width) / 2)
    y: Screen.virtualY + Math.max(0, (Screen.height - height) / 3)

    function show() {
        // Snap first so the first frame is the small, transparent card.
        card.prepareOpen();
        visible = true;
        input.text = "";
        refresh();
        input.forceActiveFocus();
        requestActivate();
        card.playOpen();
    }

    function hide() {
        if (!visible || card.closing)
            return;
        card.playClose(function() { visible = false; });
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
            rememberCommand(q);
        }
        hide();
    }

    // --- command history --------------------------------------------------
    // Bare commands land in the run.history settings key; Up/Down walk it.
    property int historyIndex: -1
    property bool navigatingHistory: false

    function rememberCommand(command) {
        const limit = Settings.values["run.historyLimit"] || 25;
        var history = (Settings.values["run.history"] || []).slice();
        history = history.filter(c => c !== command);
        history.unshift(command);
        while (history.length > limit)
            history.pop();
        Settings.setValue("run.history", history);
        historyIndex = -1;
    }

    function historyStep(delta) {
        const history = Settings.values["run.history"] || [];
        if (history.length === 0)
            return;
        const next = Math.max(-1, Math.min(history.length - 1, historyIndex + delta));
        if (next === historyIndex)
            return;
        historyIndex = next;
        navigatingHistory = true;
        input.text = next >= 0 ? history[next] : "";
        navigatingHistory = false;
        input.cursorPosition = input.text.length;
    }

    onActiveChanged: {
        if (!active)
            hide();
    }

    ShellFrame {
        id: card
        anchors.fill: parent

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

            ShellTitleBar {
                Layout.fillWidth: true
                title: "Run a command"
                iconName: "app-window"
                onCloseClicked: dialog.hide()
            }

            Rectangle {
                id: inputBox
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                radius: Theme.radius
                color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.95)
                border.width: input.activeFocus ? 1 : 0
                border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.7)

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
                    onTextChanged: {
                        if (!dialog.navigatingHistory)
                            dialog.historyIndex = -1;
                        dialog.refresh();
                    }
                    Keys.onEscapePressed: dialog.hide()
                    Keys.onReturnPressed: dialog.runCurrent()
                    Keys.onUpPressed: dialog.historyStep(1)
                    Keys.onDownPressed: dialog.historyStep(-1)

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

            // History hint, only when there is history to walk.
            Text {
                Layout.fillWidth: true
                visible: (Settings.values["run.history"] || []).length > 0
                text: "Up/Down walks the command history"
                horizontalAlignment: Text.AlignHCenter
                color: Qt.rgba(Theme.muted.r, Theme.muted.g, Theme.muted.b, 0.7)
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 2
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: dialog.results

                delegate: TaskButton {
                    required property var modelData
                    width: ListView.view.width
                    fillWidth: true
                    iconSize: 24
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
