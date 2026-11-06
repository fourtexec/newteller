import QtQuick

// A built-in shortcut that the user can move or turn off. The key holds ""
// (the built-in default, shown greyed), "none" (off) or a key combination in
// the compositor's notation ("Super+Shift+L"). Click the field and press the
// new keys; Escape cancels, Backspace returns to the default.
//
// The compositor still sees the keys first while recording, so a combination
// that is already a shortcut (Super+L) runs it; type it into the field
// instead, or turn the old shortcut off first.
Row {
    id: control

    property string settingKey: ""
    // What the compositor does without a setting, for the placeholder.
    property string defaultKeys: ""
    property bool recording: false
    readonly property string current: settingKey.length > 0
                                      ? String(Settings.values[settingKey] || "") : ""

    spacing: 4

    // Qt key -> xkb keysym name (what the compositor's parser understands).
    function keyName(key, text) {
        if (key >= Qt.Key_A && key <= Qt.Key_Z)
            return String.fromCharCode(key).toUpperCase();
        if (key >= Qt.Key_0 && key <= Qt.Key_9)
            return String.fromCharCode(key);
        if (key >= Qt.Key_F1 && key <= Qt.Key_F24)
            return "F" + (key - Qt.Key_F1 + 1);
        const names = {};
        names[Qt.Key_Return] = "Return"; names[Qt.Key_Enter] = "Return";
        names[Qt.Key_Space] = "space"; names[Qt.Key_Tab] = "Tab";
        names[Qt.Key_Print] = "Print"; names[Qt.Key_Left] = "Left";
        names[Qt.Key_Right] = "Right"; names[Qt.Key_Up] = "Up"; names[Qt.Key_Down] = "Down";
        names[Qt.Key_PageUp] = "Page_Up"; names[Qt.Key_PageDown] = "Page_Down";
        names[Qt.Key_Home] = "Home"; names[Qt.Key_End] = "End";
        names[Qt.Key_Insert] = "Insert"; names[Qt.Key_Delete] = "Delete";
        names[Qt.Key_Pause] = "Pause"; names[Qt.Key_Minus] = "minus";
        names[Qt.Key_Equal] = "equal"; names[Qt.Key_Comma] = "comma";
        names[Qt.Key_Period] = "period"; names[Qt.Key_Slash] = "slash";
        names[Qt.Key_Semicolon] = "semicolon"; names[Qt.Key_Apostrophe] = "apostrophe";
        names[Qt.Key_BracketLeft] = "bracketleft"; names[Qt.Key_BracketRight] = "bracketright";
        names[Qt.Key_Backslash] = "backslash"; names[Qt.Key_QuoteLeft] = "grave";
        if (names[key] !== undefined)
            return names[key];
        return "";
    }

    function chord(event) {
        const name = keyName(event.key, event.text);
        if (name.length === 0)
            return "";
        const parts = [];
        if (event.modifiers & Qt.MetaModifier) parts.push("Super");
        if (event.modifiers & Qt.ControlModifier) parts.push("Ctrl");
        if (event.modifiers & Qt.AltModifier) parts.push("Alt");
        if (event.modifiers & Qt.ShiftModifier) parts.push("Shift");
        parts.push(name);
        return parts.join("+");
    }

    function store(value) {
        if (settingKey.length > 0)
            Settings.setValue(settingKey, value);
        recording = false;
    }

    Rectangle {
        id: field
        objectName: "keyField"
        width: 180
        height: 30
        radius: Theme.radius
        color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b,
                       control.recording ? 0.16 : 0.10)
        border.width: control.recording || keyCatcher.activeFocus ? 1 : 0
        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.8)

        Text {
            anchors.fill: parent
            anchors.margins: 8
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: control.recording ? qsTr("Press the keys...")
                  : control.current === "none" ? qsTr("Off")
                  : control.current.length > 0 ? control.current
                  : control.defaultKeys
            color: control.recording || control.current.length > 0 && control.current !== "none"
                   ? Theme.foreground : Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
        }

        Item {
            id: keyCatcher
            anchors.fill: parent
            focus: control.recording
            Keys.onPressed: (event) => {
                event.accepted = true;
                if (event.key === Qt.Key_Escape) {
                    control.recording = false;
                    return;
                }
                if (event.key === Qt.Key_Backspace) {
                    control.store("");
                    return;
                }
                const keys = control.chord(event);
                if (keys.length > 0)
                    control.store(keys);
            }
            onActiveFocusChanged: {
                if (!activeFocus)
                    control.recording = false;
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                control.recording = !control.recording;
                if (control.recording)
                    keyCatcher.forceActiveFocus();
            }
        }
    }

    // Off / back on with the default.
    TaskButton {
        width: 30
        height: 30
        iconName: control.current === "none" ? "rotate-ccw" : "x"
        tooltipText: control.current === "none" ? qsTr("Turn back on") : qsTr("Turn off")
        onClicked: control.store(control.current === "none" ? "" : "none")
    }
}
