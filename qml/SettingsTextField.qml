import QtQuick

// One-line text input. With `settingKey` set it loads the key and writes the
// value when editing finishes (Enter or losing focus).
Rectangle {
    id: control

    property string settingKey: ""
    property string placeholder: ""
    property alias text: input.text
    signal textEdited(string text)

    width: 220
    height: 30
    radius: Theme.radius
    color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.10)
    border.width: input.activeFocus ? 1 : 0
    border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.7)

    function refresh() {
        if (settingKey.length > 0) {
            const v = Settings.value(settingKey, "");
            input.text = v === undefined || v === null ? "" : v.toString();
        }
    }

    Component.onCompleted: refresh()
    Connections {
        target: Settings
        function onValuesChanged() { control.refresh() }
    }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pointSize: Theme.fontSize
        verticalAlignment: TextInput.AlignVCenter
        selectByMouse: true
        onEditingFinished: {
            if (control.settingKey.length > 0)
                Settings.setValue(control.settingKey, text);
            control.textEdited(text);
        }

        Text {
            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter
            visible: input.text.length === 0 && control.placeholder.length > 0
            text: control.placeholder
            color: Theme.muted
            font: input.font
            elide: Text.ElideRight
        }
    }
}
