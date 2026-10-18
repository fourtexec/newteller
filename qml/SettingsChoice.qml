import QtQuick

// A row of chips that picks one value out of `model` (a list of
// { value, label }). With `settingKey` set it reads and writes the key.
Flow {
    id: control

    property string settingKey: ""
    property var model: []
    property var value: null
    signal changed(var value)

    spacing: 6

    function refresh() {
        if (settingKey.length > 0)
            value = Settings.value(settingKey, null);
    }

    function pick(v) {
        value = v;
        if (settingKey.length > 0)
            Settings.setValue(settingKey, v);
        changed(v);
    }

    Component.onCompleted: refresh()
    Connections {
        target: Settings
        function onValuesChanged() { control.refresh() }
    }

    Repeater {
        model: control.model

        delegate: TaskButton {
            required property var modelData
            label: modelData.label
            active: control.value === modelData.value
            onClicked: control.pick(modelData.value)
        }
    }
}
