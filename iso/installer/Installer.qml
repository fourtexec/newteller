import QtQuick
import QtQuick.Window

// The NewTeller installer, laid out like Ubuntu's: the steps on the left, one
// question per page, Back / Continue at the bottom. Plain QtQuick only (no
// Controls), in NewTeller's dark palette. `installer` is the Python backend.
Window {
    id: root
    width: 980
    height: 660
    minimumWidth: 820
    minimumHeight: 560
    visible: true
    title: "Install NewTeller"
    color: bg

    readonly property color bg: "#141414"
    readonly property color surface: "#1e1e1e"
    readonly property color raised: "#262628"
    readonly property color border: "#2e2e30"
    readonly property color fg: "#ececec"
    readonly property color muted: "#8e8e93"
    readonly property color danger: "#e5534b"
    readonly property color ok: "#57ab5a"
    readonly property string fontName: "Sans"

    // --- choices -------------------------------------------------------------
    property string locale: "en_US.UTF-8"
    property string layout: "us"
    property string disk: installer.disks.length > 0 ? installer.disks[0].path : ""
    property string timezone: installer.defaultTimezone
    property string fullName: ""
    property string username: ""
    property string hostname: ""
    property string password: ""
    property string password2: ""
    property bool autologin: false
    property bool usernameEdited: false
    property bool hostnameEdited: false
    property bool confirmErase: false

    // --- steps ----------------------------------------------------------------
    readonly property var steps: ["Welcome", "Keyboard", "Disk", "Location", "Account", "Summary", "Installing"]
    property int step: 0
    property bool done: false
    property bool failed: false
    property string failure: ""

    readonly property var selectedDisk: {
        for (var i = 0; i < installer.disks.length; ++i)
            if (installer.disks[i].path === root.disk)
                return installer.disks[i];
        return null;
    }

    function layoutName(code) {
        const parts = code.split(",");
        const names = [];
        for (var p = 0; p < parts.length; ++p) {
            var name = parts[p];
            for (var i = 0; i < installer.layouts.length; ++i)
                if (installer.layouts[i].code === parts[p]) {
                    name = installer.layouts[i].name;
                    break;
                }
            names.push(name);
        }
        return names.join(" + ");
    }
    function localeName(code) {
        for (var i = 0; i < installer.locales.length; ++i)
            if (installer.locales[i].code === code)
                return installer.locales[i].name;
        return code;
    }

    readonly property bool usernameValid: /^[a-z_][a-z0-9_-]{0,31}$/.test(username)
                                          && username !== "root" && username !== "live"
    readonly property bool hostnameValid: /^[A-Za-z0-9][A-Za-z0-9-]{0,62}$/.test(hostname)
    readonly property bool accountValid: fullName.trim().length > 0 && usernameValid && hostnameValid
                                         && password.length > 0 && password === password2

    readonly property bool canContinue: {
        switch (step) {
        case 2: return selectedDisk !== null;
        case 4: return accountValid;
        case 5: return confirmErase;
        default: return true;
        }
    }

    function next() {
        if (!canContinue)
            return;
        if (step === 5) {
            step = 6;
            installer.install({
                disk: disk, fullName: fullName.trim(), username: username, password: password,
                hostname: hostname, timezone: timezone, locale: locale, layout: layout,
                autologin: autologin
            });
            return;
        }
        step = Math.min(step + 1, steps.length - 1);
    }
    function back() {
        if (step > 0 && step < 6)
            step -= 1;
    }

    Connections {
        target: installer
        function onFinished(ok, message) {
            root.done = ok;
            root.failed = !ok;
            root.failure = message;
        }
    }

    // --- small widgets ---------------------------------------------------------

    component Label: Text {
        color: root.fg
        font.family: root.fontName
        font.pixelSize: 14
        wrapMode: Text.WordWrap
    }

    component Button: Rectangle {
        id: button
        property string text: ""
        property bool primary: false
        property bool enabledButton: true
        property bool destructive: false
        signal clicked()
        width: Math.max(120, buttonLabel.implicitWidth + 36)
        height: 38
        radius: 8
        opacity: enabledButton ? 1 : 0.4
        color: primary ? (destructive ? root.danger : (area.containsMouse ? "#ffffff" : root.fg))
                       : (area.containsMouse ? root.raised : root.surface)
        border.color: primary ? "transparent" : root.border
        border.width: 1
        Text {
            id: buttonLabel
            anchors.centerIn: parent
            text: button.text
            color: button.primary && !button.destructive ? root.bg : root.fg
            font.family: root.fontName
            font.pixelSize: 14
            font.bold: button.primary
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: if (button.enabledButton) button.clicked()
        }
    }

    component Field: Rectangle {
        id: field
        property alias text: input.text
        property string placeholder: ""
        property bool secret: false
        property bool invalid: false
        width: 360
        height: 38
        radius: 8
        color: root.surface
        border.width: 1
        border.color: invalid ? root.danger : (input.activeFocus ? root.fg : root.border)
        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            verticalAlignment: TextInput.AlignVCenter
            color: root.fg
            font.family: root.fontName
            font.pixelSize: 14
            echoMode: field.secret ? TextInput.Password : TextInput.Normal
            clip: true
            selectByMouse: true
            Text {
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
                visible: input.text.length === 0
                text: field.placeholder
                color: root.muted
                font: input.font
            }
        }
    }

    // A searchable list of {code, name} or plain strings.
    component Picker: Rectangle {
        id: picker
        property var model: []
        property string current: ""
        property string filter: ""
        property bool strings: false
        signal picked(string value)
        radius: 8
        color: root.surface
        border.color: root.border
        border.width: 1
        clip: true
        readonly property var shown: {
            const f = filter.toLowerCase();
            const out = [];
            for (var i = 0; i < model.length; ++i) {
                const item = model[i];
                const label = strings ? item : item.name + " " + item.code;
                if (f.length === 0 || label.toLowerCase().indexOf(f) >= 0)
                    out.push(item);
            }
            return out;
        }
        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: 4
            model: picker.shown
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            delegate: Rectangle {
                required property var modelData
                readonly property string value: picker.strings ? modelData : modelData.code
                width: list.width
                height: 34
                radius: 6
                color: value === picker.current ? root.raised
                       : (rowArea.containsMouse ? Qt.rgba(1, 1, 1, 0.04) : "transparent")
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    elide: Text.ElideRight
                    text: picker.strings ? modelData.replace(/_/g, " ") : modelData.name
                    color: root.fg
                    font.family: root.fontName
                    font.pixelSize: 14
                    font.bold: parent.value === picker.current
                }
                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: picker.picked(parent.value)
                }
            }
        }
    }

    // --- layout ------------------------------------------------------------------

    // The steps.
    Rectangle {
        id: sidebar
        width: 230
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: root.surface
        Column {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 6
            Text {
                text: "NewTeller"
                color: root.fg
                font.family: root.fontName
                font.pixelSize: 22
                font.bold: true
            }
            Text {
                text: installer.distroName
                color: root.muted
                font.family: root.fontName
                font.pixelSize: 13
                bottomPadding: 24
            }
            Repeater {
                model: root.steps
                delegate: Row {
                    required property string modelData
                    required property int index
                    spacing: 12
                    height: 34
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 22
                        height: 22
                        radius: 11
                        color: index < root.step ? root.fg : "transparent"
                        border.color: index <= root.step ? root.fg : root.border
                        border.width: 2
                        Text {
                            anchors.centerIn: parent
                            text: index < root.step ? "✓" : String(index + 1)
                            color: index < root.step ? root.bg : (index === root.step ? root.fg : root.muted)
                            font.family: root.fontName
                            font.pixelSize: 11
                            font.bold: true
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        color: index === root.step ? root.fg : root.muted
                        font.family: root.fontName
                        font.pixelSize: 14
                        font.bold: index === root.step
                    }
                }
            }
        }
    }

    Item {
        id: page
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: footer.top
        anchors.margins: 32

        // 0 - Welcome and language
        Item {
            anchors.fill: parent
            visible: root.step === 0
            Column {
                id: welcomeHead
                width: parent.width
                spacing: 8
                Label { text: "Welcome"; font.pixelSize: 28; font.bold: true }
                Label {
                    width: parent.width
                    color: root.muted
                    text: "Try NewTeller from this medium without changing your computer, or install "
                          + installer.distroName + " with the NewTeller desktop. Choose your language."
                }
            }
            Picker {
                anchors.top: welcomeHead.bottom
                anchors.topMargin: 20
                anchors.bottom: parent.bottom
                width: Math.min(420, parent.width)
                model: installer.locales
                current: root.locale
                onPicked: (value) => {
                    root.locale = value;
                    root.layout = installer.layoutForLocale(value);
                }
            }
        }

        // 1 - Keyboard
        Item {
            anchors.fill: parent
            visible: root.step === 1
            Column {
                id: keyboardHead
                width: parent.width
                spacing: 8
                Label { text: "Keyboard layout"; font.pixelSize: 28; font.bold: true }
                Label {
                    width: parent.width
                    color: root.muted
                    text: "Selected: " + root.layoutName(root.layout)
                          + (root.layout.indexOf(",") >= 0 ? " (Alt+Shift switches)" : "")
                }
                Field { id: layoutSearch; placeholder: "Search layouts"; width: Math.min(420, parent.width) }
            }
            Picker {
                anchors.top: keyboardHead.bottom
                anchors.topMargin: 12
                anchors.bottom: tryField.top
                anchors.bottomMargin: 12
                width: Math.min(420, parent.width)
                model: installer.layouts
                filter: layoutSearch.text
                current: root.layout.split(",").pop()
                onPicked: (value) => root.layout = value === "us" ? "us" : "us," + value
            }
            Field {
                id: tryField
                anchors.bottom: parent.bottom
                width: Math.min(420, parent.width)
                placeholder: "Type here to test your keyboard"
            }
        }

        // 2 - Disk
        Item {
            anchors.fill: parent
            visible: root.step === 2
            Column {
                width: parent.width
                spacing: 12
                Label { text: "Installation type"; font.pixelSize: 28; font.bold: true }
                Label {
                    width: parent.width
                    color: root.muted
                    text: "Erase disk and install NewTeller. Everything on the chosen disk - other "
                          + "systems, programs and files - will be deleted. Start-up mode: "
                          + (installer.uefi ? "UEFI." : "BIOS (legacy).")
                }
                Repeater {
                    model: installer.disks
                    delegate: Rectangle {
                        required property var modelData
                        width: Math.min(560, page.width)
                        height: 64
                        radius: 10
                        color: modelData.path === root.disk ? root.raised : root.surface
                        border.color: modelData.path === root.disk ? root.fg : root.border
                        border.width: 1
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 16
                            spacing: 4
                            Text {
                                text: modelData.model + "  —  " + modelData.sizeText
                                color: root.fg
                                font.family: root.fontName
                                font.pixelSize: 15
                                font.bold: true
                            }
                            Text {
                                text: modelData.path + (modelData.transport ? "  ·  " + modelData.transport : "")
                                      + (modelData.removable ? "  ·  removable" : "")
                                color: root.muted
                                font.family: root.fontName
                                font.pixelSize: 13
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.disk = modelData.path
                        }
                    }
                }
                Label {
                    visible: installer.disks.length === 0
                    color: root.danger
                    text: "No disk of 8 GB or more was found."
                }
                Button {
                    text: "Refresh"
                    onClicked: installer.refreshDisks()
                }
            }
        }

        // 3 - Location
        Item {
            anchors.fill: parent
            visible: root.step === 3
            Column {
                id: zoneHead
                width: parent.width
                spacing: 8
                Label { text: "Where are you?"; font.pixelSize: 28; font.bold: true }
                Label { color: root.muted; text: "Time zone: " + root.timezone.replace(/_/g, " ") }
                Field { id: zoneSearch; placeholder: "Search a city or region"; width: Math.min(420, parent.width) }
            }
            Picker {
                anchors.top: zoneHead.bottom
                anchors.topMargin: 12
                anchors.bottom: parent.bottom
                width: Math.min(420, parent.width)
                strings: true
                model: installer.timezones
                filter: zoneSearch.text.replace(/ /g, "_")
                current: root.timezone
                onPicked: (value) => root.timezone = value
            }
        }

        // 4 - Account
        Item {
            anchors.fill: parent
            visible: root.step === 4
            Column {
                width: parent.width
                spacing: 10
                Label { text: "Who are you?"; font.pixelSize: 28; font.bold: true }
                Label { color: root.muted; text: "Your name" }
                Field {
                    id: nameField
                    placeholder: "Full name"
                    onTextChanged: {
                        root.fullName = text;
                        if (!root.usernameEdited)
                            userField.text = installer.suggestUsername(text);
                    }
                }
                Label { color: root.muted; text: "Your computer's name" }
                Field {
                    id: hostField
                    invalid: text.length > 0 && !root.hostnameValid
                    onTextChanged: {
                        root.hostname = text;
                        root.hostnameEdited = text !== installer.suggestHostname(root.username);
                    }
                }
                Label { color: root.muted; text: "Pick a username" }
                Field {
                    id: userField
                    invalid: text.length > 0 && !root.usernameValid
                    onTextChanged: {
                        root.username = text;
                        root.usernameEdited = text !== installer.suggestUsername(root.fullName);
                        if (!root.hostnameEdited)
                            hostField.text = installer.suggestHostname(text);
                    }
                }
                Label { color: root.muted; text: "Choose a password" }
                Row {
                    spacing: 10
                    Field {
                        width: 175
                        secret: true
                        placeholder: "Password"
                        onTextChanged: root.password = text
                    }
                    Field {
                        width: 175
                        secret: true
                        placeholder: "Confirm password"
                        invalid: text.length > 0 && text !== root.password
                        onTextChanged: root.password2 = text
                    }
                }
                Label {
                    color: root.password.length > 0 && root.password !== root.password2 && root.password2.length > 0
                           ? root.danger : root.muted
                    font.pixelSize: 12
                    text: root.password.length > 0 && root.password2.length > 0 && root.password !== root.password2
                          ? "The passwords do not match."
                          : (root.password.length > 0 && root.password.length < 6
                             ? "A longer password is safer." : "")
                }
                Row {
                    spacing: 10
                    Rectangle {
                        width: 20
                        height: 20
                        radius: 5
                        color: root.autologin ? root.fg : "transparent"
                        border.color: root.autologin ? root.fg : root.muted
                        border.width: 2
                        Text {
                            anchors.centerIn: parent
                            visible: root.autologin
                            text: "✓"
                            color: root.bg
                            font.bold: true
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.autologin = !root.autologin
                        }
                    }
                    Label { text: "Log in automatically" }
                }
            }
        }

        // 5 - Summary
        Item {
            anchors.fill: parent
            visible: root.step === 5
            Column {
                width: parent.width
                spacing: 10
                Label { text: "Ready to install"; font.pixelSize: 28; font.bold: true }
                Repeater {
                    model: [
                        ["System", installer.distroName + " with NewTeller"],
                        ["Disk", root.selectedDisk ? root.selectedDisk.model + " (" + root.selectedDisk.path
                                                      + ", " + root.selectedDisk.sizeText + ") - erased" : ""],
                        ["Language", root.localeName(root.locale)],
                        ["Keyboard", root.layoutName(root.layout)],
                        ["Time zone", root.timezone.replace(/_/g, " ")],
                        ["Account", root.fullName + " (" + root.username + ") on " + root.hostname
                                    + (root.autologin ? ", automatic login" : "")]
                    ]
                    delegate: Row {
                        required property var modelData
                        spacing: 12
                        Text {
                            width: 110
                            text: modelData[0]
                            color: root.muted
                            font.family: root.fontName
                            font.pixelSize: 14
                        }
                        Text {
                            width: page.width - 130
                            text: modelData[1]
                            color: root.fg
                            wrapMode: Text.WordWrap
                            font.family: root.fontName
                            font.pixelSize: 14
                        }
                    }
                }
                Rectangle {
                    width: Math.min(560, page.width)
                    height: eraseText.implicitHeight + 28
                    radius: 10
                    color: Qt.rgba(0.9, 0.33, 0.29, 0.12)
                    border.color: root.danger
                    border.width: 1
                    Row {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 12
                        Rectangle {
                            width: 20
                            height: 20
                            radius: 5
                            color: root.confirmErase ? root.danger : "transparent"
                            border.color: root.danger
                            border.width: 2
                            Text {
                                anchors.centerIn: parent
                                visible: root.confirmErase
                                text: "✓"
                                color: "#ffffff"
                                font.bold: true
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.confirmErase = !root.confirmErase
                            }
                        }
                        Label {
                            id: eraseText
                            width: parent.width - 40
                            text: "I understand that everything on "
                                  + (root.selectedDisk ? root.selectedDisk.path : "the disk")
                                  + " will be erased."
                        }
                    }
                }
            }
        }

        // 6 - Installing / done
        Item {
            anchors.fill: parent
            visible: root.step === 6
            Column {
                width: parent.width
                spacing: 14
                Label {
                    font.pixelSize: 28
                    font.bold: true
                    text: root.done ? "Installation complete"
                          : root.failed ? "Installation failed" : "Installing NewTeller"
                }
                Label {
                    width: parent.width
                    color: root.failed ? root.danger : root.muted
                    text: root.done ? "Remove the installation medium and restart to use your new system."
                          : root.failed ? root.failure : installer.status
                }
                Rectangle {
                    width: parent.width
                    height: 8
                    radius: 4
                    color: root.surface
                    Rectangle {
                        width: parent.width * (root.done ? 1 : installer.progress / 100)
                        height: parent.height
                        radius: 4
                        color: root.failed ? root.danger : (root.done ? root.ok : root.fg)
                        Behavior on width { NumberAnimation { duration: 300 } }
                    }
                }
                Text {
                    text: (root.done ? 100 : installer.progress) + "%"
                    color: root.muted
                    font.family: root.fontName
                    font.pixelSize: 13
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Math.max(120, parent.height - 170)
                radius: 8
                color: "#0e0e0e"
                border.color: root.border
                border.width: 1
                clip: true
                Flickable {
                    id: logView
                    anchors.fill: parent
                    anchors.margins: 10
                    contentHeight: logText.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    Text {
                        id: logText
                        width: logView.width
                        text: installer.log
                        color: root.muted
                        font.family: "monospace"
                        font.pixelSize: 11
                        wrapMode: Text.WrapAnywhere
                        onTextChanged: logView.contentY = Math.max(0, logView.contentHeight - logView.height)
                    }
                }
            }
        }
    }

    // --- buttons ----------------------------------------------------------------------
    Rectangle {
        id: footer
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 72
        color: root.bg
        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: root.border
        }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 32
            spacing: 10
            Button {
                visible: root.step === 0
                text: "Try NewTeller"
                onClicked: installer.quit()
            }
            Button {
                visible: root.step > 0 && root.step < 6
                text: "Back"
                onClicked: root.back()
            }
        }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 32
            spacing: 10
            Button {
                visible: root.step === 6 && (root.done || root.failed)
                text: root.done ? "Continue testing" : "Close"
                onClicked: installer.quit()
            }
            Button {
                visible: root.step === 6 && root.done
                primary: true
                text: "Restart now"
                onClicked: installer.reboot()
            }
            Button {
                visible: root.step < 6
                primary: true
                destructive: root.step === 5
                enabledButton: root.canContinue
                text: root.step === 0 ? "Install NewTeller" : (root.step === 5 ? "Install now" : "Continue")
                onClicked: root.next()
            }
        }
    }
}
