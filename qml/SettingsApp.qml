import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import Qt.labs.folderlistmodel

// The built-in settings application of NewTeller.
//
// This file is compiled into the newteller executable (Qt resource system),
// so the settings are always available — a user cannot delete or damage them
// without replacing the binary itself. Every key of shell.json, the theme
// files, the plugins and the diagnostics are managed from here; changes apply
// live through the Settings/Theme objects.
Window {
    id: settingsWindow

    Motion {
        id: motion
    }

    // The page identifiers and their sidebar entries.
    readonly property var pages: [
        { id: "appearance", label: "Appearance", icon: "palette" },
        { id: "panel", label: "Panel & dock", icon: "panel-bottom" },
        { id: "desktop", label: "Desktop", icon: "monitor" },
        { id: "startmenu", label: "Start menu", icon: "search" },
        { id: "apps", label: "Applications", icon: "package" },
        { id: "system", label: "System", icon: "cpu" },
        { id: "notifications", label: "Notifications", icon: "bell" },
        { id: "plugins", label: "Plugins", icon: "puzzle" },
        { id: "hotkeys", label: "Hotkeys", icon: "keyboard" },
        { id: "animations", label: "Animations", icon: "sparkles" },
        { id: "about", label: "About", icon: "info" }
    ]
    property string currentPage: "appearance"

    readonly property var wallpaperFolders: [Shell.home + "/Pictures", "/usr/share/backgrounds"]
    property int wallpaperFolderIndex: 0

    // Theme editor state (values of the selected theme being edited).
    property var themeEditor: ({})
    property string themeEditorName: ""
    // Font family chosen through the font dropdown of the theme editor.
    property string editorFontFamily: "Sans"
    // The colour field the palette popup was opened from.
    property var paletteTarget: null

    function openPalette(field) {
        paletteTarget = field;
        colorPalette.openFor(field.colorValue, field, 6, 36);
    }

    function refreshWindowBorderField() {
        const c = Settings.values["windows.borderColor"];
        winBorderField.setValue((c !== undefined && c !== null && c.length > 0)
                                ? Qt.color(c) : Theme.border);
    }

    Component.onCompleted: refreshWindowBorderField()
    Connections {
        target: Settings
        function onValuesChanged() { settingsWindow.refreshWindowBorderField() }
    }

    function pageIndexOf(id) {
        for (var i = 0; i < pages.length; ++i) {
            if (pages[i].id === id)
                return i;
        }
        return 0;
    }

    function showFromShell() {
        visible = true;
        requestActivate();
    }

    function hideWindow() {
        visible = false;
    }

    function loadThemeEditor(name) {
        themeEditorName = name;
        themeEditor = Theme.themeValues(name);
        backgroundField.setValue(Qt.color(themeEditor.background || "#000000"));
        foregroundField.setValue(Qt.color(themeEditor.foreground || "#ffffff"));
        accentField.setValue(Qt.color(themeEditor.accent || "#888888"));
        surfaceField.setValue(Qt.color(themeEditor.surface || "#111111"));
        borderField.setValue(Qt.color(themeEditor.border || "#333333"));
        mutedField.setValue(Qt.color(themeEditor.muted || "#888888"));
        opacityBox.value = Number(themeEditor.opacity) || 0.92;
        radiusBox.value = Number(themeEditor.radius) || 10;
        fontSizeBox.value = Number(themeEditor.fontSize) || 10;
        editorFontFamily = themeEditor.fontFamily || "Sans";
    }

    function collectThemeEditor() {
        return {
            background: backgroundField.colorValue.toString(),
            foreground: foregroundField.colorValue.toString(),
            accent: accentField.colorValue.toString(),
            surface: surfaceField.colorValue.toString(),
            border: borderField.colorValue.toString(),
            muted: mutedField.colorValue.toString(),
            opacity: opacityBox.value,
            radius: Math.round(radiusBox.value),
            fontSize: Math.round(fontSizeBox.value),
            fontFamily: editorFontFamily
        };
    }

    function saveTheme() {
        Theme.saveUserTheme(themeEditorName || Theme.name, collectThemeEditor());
    }

    function resetTheme() {
        Theme.deleteUserTheme(themeEditorName || Theme.name);
        loadThemeEditor(themeEditorName || Theme.name);
    }

    function resetAllSettings() {
        const keys = Settings.keys();
        for (var i = 0; i < keys.length; ++i)
            Settings.reset(keys[i]);
        Settings.flush();
        confirmBox.visible = false;
        Shell.notify("NewTeller", "All settings were reset to their defaults.");
    }

    function exportSettings() {
        const stamp = Qt.formatDateTime(Shell.now, "yyyyMMdd-hhmmss");
        const path = Shell.home + "/newteller-settings-" + stamp + ".json";
        const body = JSON.stringify(Settings.values, null, 2);
        if (Shell.saveTextFile(path, body))
            Shell.notify("NewTeller", "A copy of the settings was saved to " + path);
        else
            Shell.notify("NewTeller", "The settings copy could not be written.");
    }

    function setPinned(list) {
        Settings.setValue("panel.pinned", list);
    }

    title: qsTr("NewTeller Settings")
    width: 940
    height: 660
    minimumWidth: 820
    minimumHeight: 560
    visible: false
    color: Theme.background

    onClosing: (close) => {
        close.accepted = true;
        wallpaperOverlay.visible = false;
        confirmBox.visible = false;
    }

    onVisibleChanged: {
        if (visible) {
            loadThemeEditor(Theme.name);
            // The content settles in with a short fade over the window's own
            // background; hiding stays instant.
            contentItem.opacity = 0;
            openFade.restart();
        }
    }

    NumberAnimation {
        id: openFade
        target: settingsWindow.contentItem
        property: "opacity"
        from: 0
        to: 1
        duration: motion.fast
        easing.type: motion.easeEnter
    }
    // The page that just became current fades in; StackLayout manages the
    // geometry of its pages, so opacity is the one property safe to animate.
    NumberAnimation {
        id: pageFade
        target: null
        property: "opacity"
        from: 0
        to: 1
        duration: motion.fast
        easing.type: motion.easeEnter
    }

    // ------------------------------------------------------------------
    // Sidebar
    // ------------------------------------------------------------------
    Rectangle {
        id: sidebar
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: footer.top
        width: 216
        color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.45)

        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 4

            Item {
                width: parent.width
                height: 46

                Rectangle {
                    width: 30
                    height: 30
                    radius: 8
                    color: Theme.accent
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        anchors.centerIn: parent
                        text: "N"
                        color: "#ffffff"
                        font.bold: true
                        font.pointSize: Theme.fontSize + 4
                        font.family: Theme.fontFamily
                    }
                }
                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 46
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: "NewTeller"
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize + 2
                        font.bold: true
                    }
                    Text {
                        text: "version " + Shell.version
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 2
                    }
                }
            }

            Repeater {
                model: settingsWindow.pages

                delegate: TaskButton {
                    required property var modelData
                    width: sidebar.width - 20
                    height: 36
                    fillWidth: true
                    iconName: modelData.icon
                    label: modelData.label
                    active: settingsWindow.currentPage === modelData.id
                    onClicked: settingsWindow.currentPage = modelData.id
                }
            }

            Item { height: 4; width: 1 }
            Text {
                leftPadding: 10
                text: "The settings are built into the\nnewteller binary and always\navailable."
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 2
            }
        }
    }

    // ------------------------------------------------------------------
    // Pages
    // ------------------------------------------------------------------
    Rectangle {
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: footer.top
        color: "transparent"

        StackLayout {
            id: pageStack
            anchors.fill: parent
            currentIndex: settingsWindow.pageIndexOf(settingsWindow.currentPage)

            onCurrentIndexChanged: {
                const page = pageStack.children[pageStack.currentIndex];
                if (!page)
                    return;
                pageFade.target = page;
                pageFade.restart();
            }

            // ---- Appearance -------------------------------------------------
            Item {
                Flickable {
                    id: appearanceScroll
                    anchors.fill: parent
                    contentHeight: appearanceColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: appearanceColumn
                        x: 16
                        y: 12
                        width: appearanceScroll.width - 32
                        spacing: 16

                        SettingsSection {
                            title: "Theme"
                            subtitle: "Pick one of the themes; custom themes live in " + Theme.userThemesPath()

                            SettingsRow {
                                width: parent.width
                                title: "Preview"

                                Rectangle {
                                    width: 260
                                    height: 84
                                    radius: Theme.radius
                                    color: Theme.background
                                    border.width: 1
                                    border.color: Theme.border

                                    Rectangle {
                                        x: 8; y: 8
                                        width: 110; height: 68
                                        radius: 6
                                        color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.9)
                                        Rectangle {
                                            x: 8; y: 10
                                            width: 60; height: 10; radius: 5
                                            color: Theme.accent
                                        }
                                        Rectangle {
                                            x: 8; y: 28
                                            width: 90; height: 6; radius: 3
                                            color: Theme.muted
                                        }
                                        Rectangle {
                                            x: 8; y: 42
                                            width: 70; height: 6; radius: 3
                                            color: Theme.muted
                                        }
                                    }
                                    Rectangle {
                                        anchors.right: parent.right
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 70; height: 68
                                        radius: 6
                                        color: Theme.background
                                        border.width: 1
                                        border.color: Theme.border
                                        Column {
                                            anchors.centerIn: parent
                                            spacing: 6
                                            Rectangle { width: 44; height: 12; radius: 6; color: Theme.accent; anchors.horizontalCenter: parent.horizontalCenter }
                                            Rectangle { width: 44; height: 12; radius: 6; color: Theme.surface; border.width: 1; border.color: Theme.border; anchors.horizontalCenter: parent.horizontalCenter }
                                            Rectangle { width: 44; height: 12; radius: 6; color: Theme.border; anchors.horizontalCenter: parent.horizontalCenter }
                                        }
                                    }
                                }
                            }

                            SettingsRow {
                                width: parent.width
                                title: "Selected theme"
                                settingKey: "theme.name"
                                subtitle: Theme.hasUserTheme(settingsWindow.themeEditorName)
                                          ? "This theme has user customisations on disk."
                                          : ""

                                Flow {
                                    width: 420
                                    spacing: 6

                                    Repeater {
                                        model: Theme.availableThemes()

                                        delegate: TaskButton {
                                            required property var modelData
                                            label: modelData + (Theme.hasUserTheme(modelData) ? "  •" : "")
                                            active: Theme.name === modelData
                                            onClicked: {
                                                Settings.setValue("theme.name", modelData);
                                                settingsWindow.loadThemeEditor(modelData);
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        SettingsSection {
                            title: "Theme variants"
                            subtitle: "Automatically switch themes based on system preferences."

                            SettingsRow {
                                width: parent.width
                                title: "Follow system theme"
                                settingKey: "theme.followSystem"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "System theme preference"
                                visible: Settings.values["theme.followSystem"] === true
                                settingKey: "theme.systemTheme"
                                SettingsChoice {
                                    model: [
                                        { value: "auto", label: "Auto-detect" },
                                        { value: "light", label: "Light" },
                                        { value: "dark", label: "Dark" }
                                    ]
                                }
                            }
                        }

                        SettingsSection {
                            title: "Theme editor"
                            subtitle: "Colours and typography of " + (settingsWindow.themeEditorName || Theme.name)
                                      + ". Saving writes a user theme file that overrides the built-in one."

                            SettingsRow {
                                width: parent.width
                                title: "Background"
                                SettingsColorField {
                                    id: backgroundField
                                    onEdited: (hex) => settingsWindow.saveTheme()
                                    onPaletteRequested: settingsWindow.openPalette(backgroundField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Foreground"
                                SettingsColorField {
                                    id: foregroundField
                                    onEdited: (hex) => settingsWindow.saveTheme()
                                    onPaletteRequested: settingsWindow.openPalette(foregroundField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Accent"
                                SettingsColorField {
                                    id: accentField
                                    onEdited: (hex) => settingsWindow.saveTheme()
                                    onPaletteRequested: settingsWindow.openPalette(accentField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Surface"
                                SettingsColorField {
                                    id: surfaceField
                                    onEdited: (hex) => settingsWindow.saveTheme()
                                    onPaletteRequested: settingsWindow.openPalette(surfaceField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Border"
                                SettingsColorField {
                                    id: borderField
                                    onEdited: (hex) => settingsWindow.saveTheme()
                                    onPaletteRequested: settingsWindow.openPalette(borderField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Muted text"
                                SettingsColorField {
                                    id: mutedField
                                    onEdited: (hex) => settingsWindow.saveTheme()
                                    onPaletteRequested: settingsWindow.openPalette(mutedField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Panel opacity"
                                SettingsSlider { id: opacityBox; from: 0.3; to: 1.0; step: 0.01; onChanged: (v) => settingsWindow.saveTheme() }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Corner radius"
                                SettingsStepper { id: radiusBox; from: 0; to: 24; onChanged: (v) => settingsWindow.saveTheme() }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Font size"
                                SettingsStepper { id: fontSizeBox; from: 6; to: 16; onChanged: (v) => settingsWindow.saveTheme() }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Font family"
                                subtitle: "Every font installed on the system."
                                TaskButton {
                                    id: fontFamilyButton
                                    iconName: "type"
                                    label: settingsWindow.editorFontFamily
                                    onClicked: fontPicker.openFor(settingsWindow.editorFontFamily,
                                                                  fontFamilyButton)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Save changes"
                                subtitle: "The file is written to " + Theme.userThemesPath()
                                          + " and applied immediately."
                                Row {
                                    spacing: 6
                                    TaskButton {
                                        iconName: "save"
                                        label: "Save theme"
                                        onClicked: settingsWindow.saveTheme()
                                    }
                                    TaskButton {
                                        iconName: "eraser"
                                        label: "Discard custom copy"
                                        visible: Theme.hasUserTheme(settingsWindow.themeEditorName)
                                        onClicked: settingsWindow.resetTheme()
                                    }
                                }
                            }
                        }

                        SettingsSection {
                            title: "Panel styling"
                            subtitle: "Custom colors for the panel, independent from the theme."

                            SettingsRow {
                                width: parent.width
                                title: "Use custom panel colors"
                                settingKey: "panel.customColors"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Panel background"
                                visible: Settings.values["panel.customColors"] === true
                                SettingsColorField {
                                    onEdited: (hex) => Settings.setValue("panel.backgroundColor", hex)
                                    onPaletteRequested: settingsWindow.openPalette(this)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Panel foreground"
                                visible: Settings.values["panel.customColors"] === true
                                SettingsColorField {
                                    onEdited: (hex) => Settings.setValue("panel.foregroundColor", hex)
                                    onPaletteRequested: settingsWindow.openPalette(this)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Panel opacity"
                                visible: Settings.values["panel.customColors"] === true
                                settingKey: "panel.backgroundOpacity"
                                SettingsSlider { from: 0.3; to: 1.0; step: 0.01 }
                            }
                        }

                        SettingsSection {
                            title: "Dock styling"
                            subtitle: "Custom appearance for dock buttons."

                            SettingsRow {
                                width: parent.width
                                title: "Show labels"
                                settingKey: "dock.showLabels"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Label position"
                                settingKey: "dock.labelPosition"
                                SettingsChoice {
                                    model: [
                                        { value: "bottom", label: "Bottom" },
                                        { value: "right", label: "Right" },
                                        { value: "top", label: "Top" },
                                        { value: "left", label: "Left" }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Icon color mode"
                                settingKey: "dock.iconColorMode"
                                SettingsChoice {
                                    model: [
                                        { value: "theme", label: "Theme color" },
                                        { value: "custom", label: "Custom color" },
                                        { value: "original", label: "Original icon colors" }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Custom icon color"
                                visible: Settings.values["dock.iconColorMode"] === "custom"
                                settingKey: "dock.customIconColor"
                                SettingsColorField {
                                    onEdited: (hex) => Settings.setValue("dock.customIconColor", hex)
                                    onPaletteRequested: settingsWindow.openPalette(this)
                                }
                            }
                        }

                        SettingsSection {
                            title: "Window style"
                            subtitle: "The frame of the shell windows — start menu, run dialog, "
                                      + "dock menu, clock and notifications. Values of -1 or 0 follow the theme."

                            SettingsRow {
                                width: parent.width
                                title: "Style"
                                settingKey: "windows.style"
                                SettingsChoice {
                                    model: [
                                        { value: "card", label: "Card" },
                                        { value: "glass", label: "Glass" },
                                        { value: "flat", label: "Flat" },
                                        { value: "outline", label: "Outline" }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Corner radius"
                                subtitle: "-1 follows the theme radius."
                                settingKey: "windows.radius"
                                SettingsStepper { from: -1; to: 32; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Background opacity"
                                subtitle: "0 follows the theme."
                                settingKey: "windows.opacityPercent"
                                SettingsSlider { from: 0; to: 100; step: 5; suffix: " %" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Border width"
                                subtitle: "-1 follows the theme."
                                settingKey: "windows.borderWidth"
                                SettingsStepper { from: -1; to: 6; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Border colour"
                                subtitle: "Empty follows the theme."

                                Row {
                                    spacing: 6

                                    SettingsColorField {
                                        id: winBorderField
                                        onEdited: (hex) => Settings.setValue("windows.borderColor", hex)
                                        onPaletteRequested: settingsWindow.openPalette(winBorderField)
                                    }
                                    TaskButton {
                                        iconName: "rotate-ccw"
                                        label: "Follow theme"
                                        onClicked: Settings.reset("windows.borderColor")
                                    }
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Shadow"
                                settingKey: "windows.shadow"
                                SettingsStepper { from: 0; to: 3 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Preview"

                                Item {
                                    width: 280
                                    height: 96

                                    ShellFrame {
                                        anchors.fill: parent

                                        Row {
                                            anchors.centerIn: parent
                                            spacing: 8
                                            Rectangle { width: 46; height: 12; radius: 6; color: Theme.accent }
                                            Rectangle { width: 32; height: 12; radius: 6; color: Theme.muted }
                                            Rectangle { width: 24; height: 12; radius: 6; color: Theme.border }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---- Panel & dock ----------------------------------------------
            Item {
                Flickable {
                    id: panelScroll
                    anchors.fill: parent
                    contentHeight: panelColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: panelColumn
                        x: 16
                        y: 12
                        width: panelScroll.width - 32
                        spacing: 16

                        SettingsSection {
                            title: "Panel"

                            SettingsRow {
                                width: parent.width
                                title: "Position"
                                settingKey: "panel.position"
                                SettingsChoice {
                                    model: [
                                        { value: "bottom", label: "Bottom" },
                                        { value: "top", label: "Top" },
                                        { value: "left", label: "Left" },
                                        { value: "right", label: "Right" }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Thickness"
                                settingKey: "panel.height"
                                SettingsSlider { from: 20; to: 100; step: 2; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Screen edge gap"
                                settingKey: "panel.margin"
                                SettingsSlider { from: 0; to: 24; step: 1; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Auto-hide"
                                subtitle: "Slide the panel away until the pointer touches its edge."
                                settingKey: "panel.autoHide"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Auto-hide delay"
                                settingKey: "panel.autoHideDelay"
                                SettingsStepper { from: 0; to: 5000; step: 100; suffix: " ms" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Running badge"
                                subtitle: "Mark running applications on their dock buttons."
                                settingKey: "panel.showRunning"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Quick settings indicators"
                                subtitle: "The bell and the indicators button at the end of the bar: "
                                          + "notification centre, volume, brightness, network and media."
                                settingKey: "panel.indicators"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Show date next to the clock"
                                settingKey: "panel.showDate"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Clock format"
                                settingKey: "panel.clockFormat"
                                SettingsChoice {
                                    model: [
                                        { value: "HH:mm", label: "24 h" },
                                        { value: "HH:mm:ss", label: "24 h + s" },
                                        { value: "h:mm AP", label: "12 h" }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Date format"
                                settingKey: "panel.dateFormat"
                                SettingsTextField {}
                            }
                        }

                        SettingsSection {
                            title: "Dock"
                            subtitle: "Every started application appears in the dock: real windows "
                                      + "are tracked on X11 and Wayland, and the process list fills in the rest."

                            SettingsRow {
                                width: parent.width
                                title: "Show running applications"
                                settingKey: "dock.showWindows"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Group windows of one application"
                                settingKey: "dock.group"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Dock icon size"
                                settingKey: "dock.iconSize"
                                SettingsSlider { from: 16; to: 48; step: 2; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Background zone"
                                subtitle: "Applications whose windows are all minimised gather in a "
                                          + "dimmed zone at the end of the dock."
                                settingKey: "dock.separateBackground"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Shrink icons on overflow"
                                subtitle: "The dock icons shrink when many applications run at once; "
                                          + "the strip scrolls when even that is not enough."
                                settingKey: "dock.shrinkIcons"
                                SettingsSwitch {}
                            }
                        }

                        SettingsSection {
                            title: "Panel layout"
                            subtitle: "Advanced panel positioning and spacing options."

                            SettingsRow {
                                width: parent.width
                                title: "Left spacing"
                                settingKey: "panel.leftSpacing"
                                SettingsSlider { from: 0; to: 64; step: 2; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Right spacing"
                                settingKey: "panel.rightSpacing"
                                SettingsSlider { from: 0; to: 64; step: 2; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Zone spacing"
                                settingKey: "panel.zoneSpacing"
                                SettingsSlider { from: 0; to: 24; step: 2; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Dock position in panel"
                                settingKey: "dock.position"
                                SettingsChoice {
                                    model: [
                                        { value: "left", label: "Left" },
                                        { value: "center", label: "Center" },
                                        { value: "right", label: "Right" }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Center dock buttons"
                                settingKey: "panel.centerDock"
                                SettingsSwitch {}
                            }
                        }

                        SettingsSection {
                            title: "Dock layout"
                            subtitle: "Advanced dock configuration options."

                            SettingsRow {
                                width: parent.width
                                title: "Multi-row dock"
                                settingKey: "dock.multiRow"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Number of rows"
                                settingKey: "dock.rows"
                                SettingsStepper { from: 1; to: 3 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Scrollable dock"
                                settingKey: "dock.scrollable"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Icon spacing"
                                settingKey: "dock.iconSpacing"
                                SettingsSlider { from: 0; to: 24; step: 2; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Shrink icons on overflow"
                                settingKey: "dock.shrinkIcons"
                                SettingsSwitch {}
                            }
                        }

                        SettingsSection {
                            title: "System tray"
                            subtitle: "The shell acts as the StatusNotifierWatcher of the session: "
                                      + "applications register their tray icons with it. Left click "
                                      + "activates, middle click triggers the secondary action, and "
                                      + "right click asks the application to open its own menu."

                            SettingsRow {
                                width: parent.width
                                title: "Show the system tray"
                                subtitle: "Hidden when another tray host owns the session."
                                settingKey: "tray.enabled"
                                SettingsSwitch {}
                            }
                        }

                        SettingsSection {
                            title: "Pinned applications"
                            subtitle: "Desktop file ids, as in /usr/share/applications. Right-click a dock "
                                      + "button at runtime for per-window actions, or right-click an "
                                      + "application in the start menu to pin or unpin it."

                            SettingsRow {
                                width: parent.width
                                title: "Pinned list"

                                Column {
                                    width: 420
                                    spacing: 6

                                    Repeater {
                                        model: Settings.value("panel.pinned", []) || []

                                        delegate: Row {
                                            required property var modelData
                                            required property int index
                                            spacing: 6

                                            Text {
                                                width: 330
                                                text: modelData
                                                color: Theme.foreground
                                                font.family: Theme.fontFamily
                                                font.pointSize: Theme.fontSize - 1
                                                elide: Text.ElideMiddle
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            TaskButton {
                                                width: 28
                                                height: 28
                                                iconName: "x"
                                                onClicked: {
                                                    const list = (Settings.value("panel.pinned", []) || []).slice();
                                                    list.splice(index, 1);
                                                    settingsWindow.setPinned(list);
                                                }
                                            }
                                        }
                                    }

                                    Row {
                                        spacing: 6
                                        SettingsTextField {
                                            id: pinField
                                            width: 300
                                        }
                                        TaskButton {
                                            iconName: "plus"
                                            label: "Pin"
                                            onClicked: {
                                                const id = pinField.text.trim();
                                                if (id.length === 0)
                                                    return;
                                                const list = (Settings.value("panel.pinned", []) || []).slice();
                                                if (list.indexOf(id) < 0)
                                                    list.push(id);
                                                settingsWindow.setPinned(list);
                                                pinField.text = "";
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---- Desktop ----------------------------------------------------
            Item {
                Flickable {
                    id: desktopScroll
                    anchors.fill: parent
                    contentHeight: desktopColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: desktopColumn
                        x: 16
                        y: 12
                        width: desktopScroll.width - 32
                        spacing: 16

                        SettingsSection {
                            title: "Wallpaper"

                            SettingsRow {
                                width: parent.width
                                title: "Current wallpaper"
                                settingKey: "desktop.wallpaper"

                                Row {
                                    spacing: 10

                                    Rectangle {
                                        width: 168
                                        height: 96
                                        radius: Theme.radius
                                        color: Theme.surface
                                        border.width: 1
                                        border.color: Theme.border
                                        clip: true

                                        Image {
                                            anchors.fill: parent
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            source: {
                                                const wp = Settings.value("desktop.wallpaper", "");
                                                return wp && wp.length > 0
                                                        ? (wp.indexOf("://") > 0 ? wp : "file://" + wp)
                                                        : "";
                                            }
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            visible: !Settings.value("desktop.wallpaper", "")
                                            text: "Solid colour"
                                            color: Theme.muted
                                            font.family: Theme.fontFamily
                                            font.pointSize: Theme.fontSize - 1
                                        }
                                    }

                                    Column {
                                        spacing: 6
                                        TaskButton {
                                            iconName: "image"
                                            label: "Browse images"
                                            onClicked: {
                                                wallpaperOverlay.visible = true;
                                            }
                                        }
                                        TaskButton {
                                            iconName: "folder"
                                            label: "Choose from file manager"
                                            onClicked: {
                                                const wp = Settings.value("desktop.wallpaper", "") || "";
                                                const dir = wp.length > 0 && wp.indexOf("/") >= 0
                                                        ? wp.substring(0, wp.lastIndexOf("/"))
                                                        : Shell.home + "/Pictures";
                                                settingsWindow.pickWallpaper(dir);
                                            }
                                        }
                                        TaskButton {
                                            iconName: "shuffle"
                                            label: "Random picture"
                                            onClicked: wallpaperOverlay.pickRandom()
                                        }
                                        TaskButton {
                                            iconName: "x"
                                            label: "No wallpaper"
                                            onClicked: Settings.setValue("desktop.wallpaper", "")
                                        }
                                    }
                                }
                            }
                        }

                        SettingsSection {
                            title: "Wallpaper"

                            SettingsRow {
                                width: parent.width
                                title: "Wallpaper mode"
                                settingKey: "desktop.wallpaperMode"
                                SettingsChoice {
                                    model: [
                                        { value: "fill", label: "Fill" },
                                        { value: "fit", label: "Fit" },
                                        { value: "stretch", label: "Stretch" },
                                        { value: "tile", label: "Tile" },
                                        { value: "center", label: "Center" }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Wallpaper blur"
                                settingKey: "desktop.wallpaperBlur"
                                SettingsSlider { from: 0; to: 50; step: 1; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Wallpaper darken"
                                settingKey: "desktop.wallpaperDarken"
                                SettingsSlider { from: 0; to: 100; step: 5; suffix: " %" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Per-monitor wallpapers"
                                settingKey: "desktop.perMonitorWallpaper"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Wallpaper slideshow"
                                settingKey: "desktop.wallpaperSlideshow"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Slideshow interval"
                                settingKey: "desktop.wallpaperSlideshowInterval"
                                SettingsStepper { from: 10; to: 3600; step: 10; suffix: " s" }
                            }
                        }

                        SettingsSection {
                            title: "Desktop background"
                            subtitle: "Custom background color when no wallpaper is set."

                            SettingsRow {
                                width: parent.width
                                title: "Use gradient background"
                                settingKey: "desktop.backgroundGradient"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Background color"
                                visible: !Settings.values["desktop.backgroundGradient"]
                                settingKey: "desktop.backgroundColor"
                                SettingsColorField {
                                    onEdited: (hex) => Settings.setValue("desktop.backgroundColor", hex)
                                    onPaletteRequested: settingsWindow.openPalette(this)
                                }
                            }
                        }

                        SettingsSection {
                            title: "Icons"

                            SettingsRow {
                                width: parent.width
                                title: "Show desktop icons"
                                settingKey: "desktop.showIcons"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Icon size"
                                settingKey: "desktop.iconSize"
                                SettingsSlider { from: 24; to: 96; step: 4; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Grid width"
                                settingKey: "desktop.cellWidth"
                                SettingsSlider { from: 64; to: 160; step: 8; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Grid height"
                                settingKey: "desktop.cellHeight"
                                SettingsSlider { from: 64; to: 180; step: 8; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Right-click menu"
                                settingKey: "desktop.contextMenu"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Icon color"
                                settingKey: "desktop.iconColor"
                                SettingsColorField {
                                    onEdited: (hex) => Settings.setValue("desktop.iconColor", hex)
                                    onPaletteRequested: settingsWindow.openPalette(this)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Icon shadow"
                                settingKey: "desktop.iconShadow"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Icon glow"
                                settingKey: "desktop.iconGlow"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Show hidden files"
                                settingKey: "desktop.showHiddenFiles"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Auto-arrange icons"
                                settingKey: "desktop.autoArrangeIcons"
                                SettingsSwitch {}
                            }
                        }
                    }
                }
            }

            // ---- Start menu -------------------------------------------------
            Item {
                Flickable {
                    id: menuScroll
                    anchors.fill: parent
                    contentHeight: menuColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: menuColumn
                        x: 16
                        y: 12
                        width: menuScroll.width - 32
                        spacing: 16

                        SettingsSection {
                            title: "Start menu"

                            SettingsRow {
                                width: parent.width
                                title: "Width"
                                settingKey: "startmenu.width"
                                SettingsSlider { from: 300; to: 700; step: 10; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Height"
                                settingKey: "startmenu.height"
                                SettingsSlider { from: 400; to: 900; step: 10; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Typo tolerant search"
                                settingKey: "startmenu.fuzzy"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Recently used row"
                                settingKey: "startmenu.showRecent"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Recent list length"
                                settingKey: "apps.recentLimit"
                                SettingsStepper { from: 0; to: 30 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Search result limit"
                                settingKey: "startmenu.limit"
                                SettingsStepper { from: 10; to: 500; step: 10 }
                            }
                        }
                    }
                }
            }

            // ---- Applications -----------------------------------------------
            Item {
                Flickable {
                    id: appsScroll
                    anchors.fill: parent
                    contentHeight: appsColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: appsColumn
                        x: 16
                        y: 12
                        width: appsScroll.width - 32
                        spacing: 16

                        SettingsSection {
                            title: "Applications"

                            SettingsRow {
                                width: parent.width
                                title: "Terminal command"
                                subtitle: "Used by \"Open terminal\"; empty means auto-detect."
                                settingKey: "apps.terminalCommand"
                                SettingsTextField { width: 260 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Application catalogue cap"
                                subtitle: "0 keeps every installed application."
                                settingKey: "apps.maxApps"
                                SettingsStepper { from: 0; to: 2000; step: 50 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Usage history"
                                subtitle: "Clears the recent row and the ranking boost."
                                TaskButton {
                                    iconName: "eraser"
                                    label: "Clear recent list"
                                    onClicked: {
                                        Apps.forgetRecent();
                                        Shell.notify("NewTeller", "The recent applications list was cleared.");
                                    }
                                }
                            }
                        }

                        SettingsSection {
                            title: "Startup applications"
                            subtitle: "Applications to launch automatically when NewTeller starts."

                            SettingsRow {
                                width: parent.width
                                title: "Startup delay (seconds)"
                                settingKey: "startup.delay"
                                SettingsStepper { from: 0; to: 60; step: 1; suffix: " s" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Startup applications list"

                                Column {
                                    width: 420
                                    spacing: 6

                                    Repeater {
                                        model: Settings.value("startup.apps", []) || []

                                        delegate: Row {
                                            required property var modelData
                                            required property int index
                                            spacing: 6

                                            Text {
                                                width: 330
                                                text: modelData
                                                color: Theme.foreground
                                                font.family: Theme.fontFamily
                                                font.pointSize: Theme.fontSize - 1
                                                elide: Text.ElideMiddle
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            TaskButton {
                                                width: 28
                                                height: 28
                                                iconName: "x"
                                                onClicked: {
                                                    const list = (Settings.value("startup.apps", []) || []).slice();
                                                    list.splice(index, 1);
                                                    Settings.setValue("startup.apps", list);
                                                }
                                            }
                                        }
                                    }

                                    Row {
                                        spacing: 6
                                        SettingsTextField {
                                            id: startupField
                                            width: 300
                                            placeholder: "Application name or .desktop file"
                                        }
                                        TaskButton {
                                            iconName: "plus"
                                            label: "Add"
                                            onClicked: {
                                                const id = startupField.text.trim();
                                                if (id.length === 0)
                                                    return;
                                                const list = (Settings.value("startup.apps", []) || []).slice();
                                                if (list.indexOf(id) < 0)
                                                    list.push(id);
                                                Settings.setValue("startup.apps", list);
                                                startupField.text = "";
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        SettingsSection {
                            title: "Search"
                            subtitle: "Application search behavior."

                            SettingsRow {
                                width: parent.width
                                title: "Max search results"
                                settingKey: "search.maxResults"
                                SettingsStepper { from: 10; to: 500; step: 10 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Show category chips"
                                settingKey: "search.showCategories"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Show category icons"
                                settingKey: "search.categoryIcons"
                                SettingsSwitch {}
                            }
                        }
                    }
                }
            }

            // ---- System -----------------------------------------------------
            Item {
                Flickable {
                    id: systemScroll
                    anchors.fill: parent
                    contentHeight: systemColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: systemColumn
                        x: 16
                        y: 12
                        width: systemScroll.width - 32
                        spacing: 16

                        SettingsSection {
                            title: "Metrics"
                            subtitle: "The shared ticker behind the system monitor widgets."

                            SettingsRow {
                                width: parent.width
                                title: "Sampling interval"
                                settingKey: "system.interval"
                                SettingsStepper { from: 250; to: 60000; step: 250; suffix: " ms" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Per-core CPU"
                                settingKey: "system.perCore"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Show CPU temperature"
                                settingKey: "system.showCpuTemp"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Show GPU info"
                                settingKey: "system.showGpu"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Show fan speed"
                                settingKey: "system.showFanSpeed"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Battery"
                                settingKey: "system.battery"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Thermal zones"
                                settingKey: "system.thermal"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Disks"
                                subtitle: "The most expensive part of a snapshot."
                                settingKey: "system.disks"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Config file watching"
                                settingKey: "watch.debounce"
                                SettingsStepper { from: 0; to: 10000; step: 50; suffix: " ms" }
                            }
                        }

                        SettingsSection {
                            title: "Caching"
                            subtitle: "Improve performance by caching frequently used data."

                            SettingsRow {
                                width: parent.width
                                title: "Icon cache"
                                settingKey: "cache.iconCache"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Max icon cache size"
                                visible: Settings.values["cache.iconCache"] === true
                                settingKey: "cache.maxIconCacheSize"
                                SettingsStepper { from: 100; to: 5000; step: 100 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Search cache"
                                settingKey: "cache.searchCache"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Max search cache size"
                                visible: Settings.values["cache.searchCache"] === true
                                settingKey: "cache.maxSearchCacheSize"
                                SettingsStepper { from: 100; to: 2000; step: 100 }
                            }
                        }

                        SettingsSection {
                            title: "Clipboard"
                            subtitle: "Clipboard history and management."

                            SettingsRow {
                                width: parent.width
                                title: "Enable clipboard history"
                                settingKey: "clipboard.historyEnabled"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "History size"
                                visible: Settings.values["clipboard.historyEnabled"] === true
                                settingKey: "clipboard.historySize"
                                SettingsStepper { from: 10; to: 1000; step: 10 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Persist history across restarts"
                                visible: Settings.values["clipboard.historyEnabled"] === true
                                settingKey: "clipboard.persistHistory"
                                SettingsSwitch {}
                            }
                        }
                    }
                }
            }

            // ---- Notifications ----------------------------------------------
            Item {
                Flickable {
                    id: notifScroll
                    anchors.fill: parent
                    contentHeight: notifColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: notifColumn
                        x: 16
                        y: 12
                        width: notifScroll.width - 32
                        spacing: 16

                        SettingsSection {
                            title: "Popups"
                            subtitle: "The built-in daemon serves org.freedesktop.Notifications, "
                                      + "so notify-send and every application reach the shell. "
                                      + "newteller-ctl notify works through the private socket."

                            SettingsRow {
                                width: parent.width
                                title: "Notification popups"
                                settingKey: "notifications.enabled"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Do not disturb"
                                subtitle: "Popups stay hidden; critical notifications still come through."
                                settingKey: "notifications.doNotDisturb"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Corner"
                                settingKey: "notifications.position"
                                SettingsChoice {
                                    model: [
                                        { value: "top-left", label: "Top left" },
                                        { value: "top-center", label: "Top center" },
                                        { value: "top-right", label: "Top right" },
                                        { value: "bottom-left", label: "Bottom left" },
                                        { value: "bottom-center", label: "Bottom center" },
                                        { value: "bottom-right", label: "Bottom right" }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Duration"
                                subtitle: "0 keeps a popup until it is clicked."
                                settingKey: "notifications.duration"
                                SettingsStepper { from: 0; to: 120; suffix: " s" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "History length"
                                settingKey: "notifications.historyLimit"
                                SettingsStepper { from: 5; to: 200; step: 5 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Try it"
                                TaskButton {
                                    iconName: "bell-ring"
                                    label: "Send a test notification"
                                    onClicked: Shell.notify("NewTeller",
                                                            "This is what a notification looks like.")
                                }
                            }
                        }

                        SettingsSection {
                            title: "Appearance"
                            subtitle: "Customize the look of notifications."

                            SettingsRow {
                                width: parent.width
                                title: "Notification theme"
                                settingKey: "notifications.theme"
                                SettingsChoice {
                                    model: [
                                        { value: "system", label: "System theme" },
                                        { value: "light", label: "Light" },
                                        { value: "dark", label: "Dark" },
                                        { value: "custom", label: "Custom colors" }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Custom background"
                                visible: Settings.values["notifications.theme"] === "custom"
                                settingKey: "notifications.customBackground"
                                SettingsColorField {
                                    onEdited: (hex) => Settings.setValue("notifications.customBackground", hex)
                                    onPaletteRequested: settingsWindow.openPalette(this)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Custom foreground"
                                visible: Settings.values["notifications.theme"] === "custom"
                                settingKey: "notifications.customForeground"
                                SettingsColorField {
                                    onEdited: (hex) => Settings.setValue("notifications.customForeground", hex)
                                    onPaletteRequested: settingsWindow.openPalette(this)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Group notifications by app"
                                settingKey: "notifications.groupByApp"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Show app icon"
                                settingKey: "notifications.showAppIcon"
                                SettingsSwitch {}
                            }
                        }

                        SettingsSection {
                            title: "Sounds"

                            SettingsRow {
                                width: parent.width
                                title: "Enable notification sounds"
                                settingKey: "notifications.soundEnabled"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Sound file"
                                visible: Settings.values["notifications.soundEnabled"] === true
                                settingKey: "notifications.soundFile"
                                SettingsTextField { width: 260; placeholder: "Leave empty for default" }
                            }
                        }

                        SettingsSection {
                            title: "Do Not Disturb scheduling"

                            SettingsRow {
                                width: parent.width
                                title: "Enable scheduled DND"
                                settingKey: "notifications.doNotDisturbSchedule"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Start time"
                                visible: Settings.values["notifications.doNotDisturbSchedule"] === true
                                settingKey: "notifications.dndStartTime"
                                SettingsTextField { width: 120; placeholder: "HH:mm (24h)" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "End time"
                                visible: Settings.values["notifications.doNotDisturbSchedule"] === true
                                settingKey: "notifications.dndEndTime"
                                SettingsTextField { width: 120; placeholder: "HH:mm (24h)" }
                            }
                        }

                        SettingsSection {
                            title: "Daemon"
                            subtitle: "When another notification daemon owns the session, NewTeller "
                                      + "only shows the notifications that arrive on its own socket."

                            SettingsRow {
                                width: parent.width
                                title: "D-Bus service"
                                Text {
                                    color: Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pointSize: Theme.fontSize
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: Notifications.ownsDBus
                                          ? "This shell serves org.freedesktop.Notifications"
                                          : "Another daemon owns org.freedesktop.Notifications"
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Recent notifications"
                                TaskButton {
                                    iconName: "eraser"
                                    label: "Clear history"
                                    onClicked: Notifications.clearHistory()
                                }
                            }

                            Repeater {
                                model: (Notifications.history || []).slice().reverse()

                                delegate: SettingsRow {
                                    required property var modelData
                                    width: parent.width
                                    title: (modelData.summary || "").length > 0
                                           ? modelData.summary : "(untitled)"
                                    subtitle: (modelData.app || "") + "  ·  "
                                              + Qt.formatDateTime(new Date(modelData.time || 0),
                                                                  "ddd d MMM HH:mm")

                                    TaskButton {
                                        iconName: "x"
                                        onClicked: Notifications.dismiss(modelData.id, 3)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---- Plugins ----------------------------------------------------
            Item {
                Flickable {
                    id: pluginsScroll
                    anchors.fill: parent
                    contentHeight: pluginsColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: pluginsColumn
                        x: 16
                        y: 12
                        width: pluginsScroll.width - 32
                        spacing: 16

                        SettingsSection {
                            title: "Plugins"
                            subtitle: "QML widgets from ~/.config/newteller/plugins and /usr/share/newteller/plugins."

                            SettingsRow {
                                width: parent.width
                                title: "Rescan on change"
                                settingKey: "plugins.autoRescan"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Show plugin descriptions"
                                settingKey: "plugins.showDescriptions"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Rescan"
                                TaskButton {
                                    iconName: "rotate-cw"
                                    label: "Rescan now"
                                    onClicked: Plugins.rescan()
                                }
                            }

                            Repeater {
                                model: Plugins.plugins

                                delegate: SettingsRow {
                                    required property var modelData
                                    width: parent.width
                                    title: modelData.name
                                    subtitle: (Settings.values["plugins.showDescriptions"] === true && modelData.description 
                                              ? modelData.description + "  ·  " : "") +
                                             modelData.id + "  ·  " + modelData.type
                                              + "  ·  version "
                                              + (modelData.version || "1.0.0")

                                    SettingsSwitch {
                                        checked: modelData.enabled
                                        onChanged: (on) => Plugins.setEnabled(modelData.id, on)
                                    }
                                }
                            }
                        }

                        SettingsSection {
                            title: "Plugin marketplace"
                            subtitle: "Online plugin discovery (experimental)."

                            SettingsRow {
                                width: parent.width
                                title: "Enable online plugins"
                                settingKey: "plugins.onlineEnabled"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Auto-update plugins"
                                settingKey: "plugins.autoUpdate"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Browse plugins"
                                TaskButton {
                                    iconName: "globe"
                                    label: "Open plugin marketplace"
                                    visible: Settings.values["plugins.onlineEnabled"] === true
                                    onClicked: Shell.openUrl("https://github.com/fourtexec/newteller-plugins")
                                }
                            }
                        }
                    }
                }
            }

            // ---- Hotkeys ----------------------------------------------------
            Item {
                Flickable {
                    id: hotkeysScroll
                    anchors.fill: parent
                    contentHeight: hotkeysColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: hotkeysColumn
                        x: 16
                        y: 12
                        width: hotkeysScroll.width - 32
                        spacing: 16

                        SettingsSection {
                            title: "Keyboard shortcuts"
                            subtitle: "Global hotkeys for shell actions. Multiple keys can be separated by commas."

                            SettingsRow {
                                width: parent.width
                                title: "Enable hotkeys"
                                settingKey: "hotkeys.enabled"
                                SettingsSwitch {}
                            }
                        }

                        SettingsSection {
                            title: "Shell actions"

                            SettingsRow {
                                width: parent.width
                                title: "Open start menu"
                                settingKey: "hotkeys.startMenu"
                                SettingsTextField { width: 180; placeholder: "Meta (Windows key)" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Run dialog"
                                settingKey: "hotkeys.runDialog"
                                SettingsTextField { width: 180; placeholder: "Alt+F2" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Open settings"
                                settingKey: "hotkeys.settings"
                                SettingsTextField { width: 180; placeholder: "Meta+, or Meta+S" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Open notification centre"
                                settingKey: "hotkeys.notificationCenter"
                                SettingsTextField { width: 180; placeholder: "Meta+N" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Open quick settings"
                                settingKey: "hotkeys.quickSettings"
                                SettingsTextField { width: 180; placeholder: "Meta+Q" }
                            }
                        }

                        SettingsSection {
                            title: "System actions"

                            SettingsRow {
                                width: parent.width
                                title: "Lock screen"
                                settingKey: "hotkeys.lockScreen"
                                SettingsTextField { width: 180; placeholder: "Meta+L" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Log out"
                                settingKey: "hotkeys.logout"
                                SettingsTextField { width: 180; placeholder: "Meta+Shift+Q" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Sleep"
                                settingKey: "hotkeys.sleep"
                                SettingsTextField { width: 180; placeholder: "Meta+Shift+S" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Reboot"
                                settingKey: "hotkeys.reboot"
                                SettingsTextField { width: 180; placeholder: "Meta+Shift+R" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Shut down"
                                settingKey: "hotkeys.shutdown"
                                SettingsTextField { width: 180; placeholder: "Meta+Shift+P" }
                            }
                        }

                        SettingsSection {
                            title: "Application launchers"

                            SettingsRow {
                                width: parent.width
                                title: "Open terminal"
                                settingKey: "hotkeys.terminal"
                                SettingsTextField { width: 180; placeholder: "Meta+T" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Open file manager"
                                settingKey: "hotkeys.fileManager"
                                SettingsTextField { width: 180; placeholder: "Meta+E" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Application switcher"
                                settingKey: "hotkeys.switcher"
                                SettingsTextField { width: 180; placeholder: "Alt+Tab" }
                            }
                        }

                        SettingsSection {
                            title: "Hotkey reference"
                            subtitle: "Modifier keys: Meta (Windows), Ctrl, Alt, Shift. Combine with +. Example: Ctrl+Alt+T"

                            SettingsRow {
                                width: parent.width
                                title: "Available keys"
                                subtitle: "Letters (A-Z), numbers (0-9), F1-F12, arrow keys, Enter, Space, Tab, Escape, Backspace, Delete, Insert, Home, End, PageUp, PageDown"
                            }
                        }
                    }
                }
            }

            // ---- Animations ------------------------------------------------
            Item {
                Flickable {
                    id: animationsScroll
                    anchors.fill: parent
                    contentHeight: animationsColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: animationsColumn
                        x: 16
                        y: 12
                        width: animationsScroll.width - 32
                        spacing: 16

                        SettingsSection {
                            title: "Animation controls"
                            subtitle: "Enable or disable animations and adjust their speed."

                            SettingsRow {
                                width: parent.width
                                title: "Enable animations globally"
                                settingKey: "animations.enabled"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Animation speed multiplier"
                                subtitle: "1.0 = normal speed, 0.5 = half speed, 2.0 = double speed"
                                settingKey: "animations.speed"
                                SettingsSlider { from: 0.1; to: 3.0; step: 0.1; onChanged: (v) => Settings.setValue("animations.speed", Math.round(v * 10) / 10) }
                            }
                        }

                        SettingsSection {
                            title: "Panel animations"

                            SettingsRow {
                                width: parent.width
                                title: "Panel hide/show animation"
                                settingKey: "animations.panelHide"
                                SettingsSwitch {}
                            }
                        }

                        SettingsSection {
                            title: "Window animations"

                            SettingsRow {
                                width: parent.width
                                title: "Window open/close animations"
                                settingKey: "windows.enableAnimations"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Window animation speed"
                                settingKey: "windows.animationSpeed"
                                SettingsSlider { from: 0.1; to: 3.0; step: 0.1; onChanged: (v) => Settings.setValue("windows.animationSpeed", Math.round(v * 10) / 10) }
                            }
                        }

                        SettingsSection {
                            title: "Preview"
                            subtitle: "Test animation speed with this demo"

                            SettingsRow {
                                width: parent.width
                                title: "Animation demo"
                                TaskButton {
                                    iconName: "play"
                                    label: "Test animation"
                                    onClicked: {
                                        // Create a simple animation demo
                                        demoBox.color = Theme.accent
                                        demoBox.opacity = 0
                                        demoBox.visible = true
                                        demoFade.restart()
                                    }
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                Item {
                                    width: 200
                                    height: 60
                                    Rectangle {
                                        id: demoBox
                                        visible: false
                                        width: 40
                                        height: 40
                                        radius: 8
                                        color: Theme.surface
                                        anchors.centerIn: parent
                                        
                                        NumberAnimation {
                                            id: demoFade
                                            target: demoBox
                                            property: "opacity"
                                            from: 0
                                            to: 1
                                            duration: 500 * (Settings.values["animations.speed"] || 1.0)
                                            easing.type: Easing.OutQuad
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---- About ------------------------------------------------------
            Item {
                Flickable {
                    id: aboutScroll
                    anchors.fill: parent
                    contentHeight: aboutColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: aboutColumn
                        x: 16
                        y: 12
                        width: aboutScroll.width - 32
                        spacing: 16

                        SettingsSection {
                            title: "This system"

                            SettingsRow {
                                width: parent.width
                                title: "Version"
                                Text {
                                    text: "NewTeller " + Shell.version
                                    color: Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pointSize: Theme.fontSize
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Computer"
                                Text {
                                    text: Shell.hostname + " · " + Shell.distro + " · kernel " + Shell.kernelVersion
                                    color: Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pointSize: Theme.fontSize
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Session"
                                Text {
                                    text: (Shell.isWayland ? "Wayland" : "X11") + " as " + Shell.userName
                                    color: Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pointSize: Theme.fontSize
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Window tracking"
                                Text {
                                    text: Windows.backend === "x11" ? "EWMH (X11)"
                                          : Windows.backend === "wayland" ? "wlr-foreign-toplevel (Wayland)"
                                          : "unavailable — the dock uses the process list"
                                    color: Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pointSize: Theme.fontSize
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Applications"
                                Text {
                                    text: Apps.count + " in the catalogue"
                                    color: Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pointSize: Theme.fontSize
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Memory and uptime"
                                Text {
                                    text: System.memoryText + " · up " + System.uptimeText
                                    color: Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pointSize: Theme.fontSize
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }

                        SettingsSection {
                            title: "Files"

                            SettingsRow {
                                width: parent.width
                                title: "Configuration"
                                subtitle: Settings.configPath()

                                TaskButton {
                                    iconName: "file-code"
                                    label: "Open JSON"
                                    onClicked: Shell.openConfig()
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: "Export"
                                subtitle: "Writes a JSON copy of the current settings next to your home folder."
                                TaskButton {
                                    iconName: "download"
                                    label: "Export a copy"
                                    onClicked: settingsWindow.exportSettings()
                                }
                            }
                        }

                        SettingsSection {
                            title: "Configuration problems"
                            subtitle: "Reported by the same validator as newteller-ctl validate."
                            visible: Settings.validate().length > 0

                            Repeater {
                                model: Settings.validate()

                                delegate: SettingsRow {
                                    required property var modelData
                                    width: parent.width
                                    title: modelData.key + "  (" + modelData.level + ")"
                                    subtitle: modelData.message
                                }
                            }
                        }

                        SettingsSection {
                            title: "Danger zone"

                            SettingsRow {
                                width: parent.width
                                title: "Reset everything"
                                subtitle: "Every key of " + Settings.configPath()
                                          + " returns to its default. Themes on disk are kept."
                                TaskButton {
                                    iconName: "rotate-ccw"
                                    label: "Reset all settings"
                                    onClicked: confirmBox.visible = true
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Footer: save state
    // ------------------------------------------------------------------
    Rectangle {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 34
        color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.6)

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Rectangle {
                width: 8
                height: 8
                radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: Settings.dirty ? Theme.accent : "#6cc070"
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Settings.dirty ? "Saving…" : "All changes saved"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: "Settings apply live — no restart needed"
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 1
        }
    }

    // ------------------------------------------------------------------
    // Popups: colour palette, font dropdown, file manager
    // ------------------------------------------------------------------
    // "Choose from file manager" opens the file chooser that is already
    // installed in the system (the freedesktop FileChooser portal; on GNOME
    // that is the GTK file dialog). The integrated browser is only a fallback
    // for systems without the portal service.
    function pickWallpaper(startDir) {
        if (FilePortal.available) {
            filePortalDir = startDir;
            FilePortal.pickFile("Choose a wallpaper", startDir, true);
        } else {
            filePicker.openFor(startDir, true);
        }
    }

    property string filePortalDir: ""

    Connections {
        target: FilePortal
        function onFilePicked(path) {
            if (path.length > 0)
                Settings.setValue("desktop.wallpaper", path);
        }
        function onPickFailed() {
            filePicker.openFor(settingsWindow.filePortalDir.length > 0
                               ? settingsWindow.filePortalDir : Shell.home, true);
        }
    }

    ColorPaletteWindow {
        id: colorPalette
        onApplied: (chosen) => {
            if (settingsWindow.paletteTarget) {
                settingsWindow.paletteTarget.setValue(chosen);
                settingsWindow.paletteTarget.edited(chosen.toString());
            }
        }
    }
    SettingsFontPicker {
        id: fontPicker
        onPicked: (family) => {
            settingsWindow.editorFontFamily = family;
            settingsWindow.saveTheme();
        }
    }
    SettingsFilePicker {
        id: filePicker
        onAccepted: (path) => Settings.setValue("desktop.wallpaper", path)
    }

    // ------------------------------------------------------------------
    // Wallpaper browser overlay
    // ------------------------------------------------------------------
    Rectangle {
        id: wallpaperOverlay
        anchors.fill: parent
        visible: false
        color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.96)
        z: 50

        function pickRandom() {
            const model = wallpaperFolderIndex === 0 ? picturesModel : backgroundsModel;
            if (model.count > 0) {
                const next = Math.floor(Math.random() * model.count);
                Settings.setValue("desktop.wallpaper", model.get(next, "filePath"));
            } else {
                Shell.openPath(wallpaperFolders[wallpaperFolderIndex]);
            }
            visible = false;
        }

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            Row {
                spacing: 6

                Repeater {
                    model: settingsWindow.wallpaperFolders

                    delegate: TaskButton {
                        required property var modelData
                        required property int index
                        label: index === 0 ? "Pictures" : "System backgrounds"
                        active: settingsWindow.wallpaperFolderIndex === index
                        onClicked: settingsWindow.wallpaperFolderIndex = index
                    }
                }
                Item { width: 12; height: 1 }
                TaskButton {
                    iconName: "shuffle"
                    label: "Random"
                    onClicked: wallpaperOverlay.pickRandom()
                }
                TaskButton {
                    iconName: "x"
                    label: "Close"
                    onClicked: wallpaperOverlay.visible = false
                }
            }

            GridView {
                id: wallpaperGrid
                width: parent.width
                height: parent.height - 46
                clip: true
                cellWidth: 200
                cellHeight: 130
                model: settingsWindow.wallpaperFolderIndex === 0 ? picturesModel : backgroundsModel

                delegate: Item {
                    width: wallpaperGrid.cellWidth - 10
                    height: wallpaperGrid.cellHeight - 10

                    required property string filePath

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radius
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.border

                        Image {
                            anchors.fill: parent
                            anchors.margins: 3
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            source: "file://" + filePath
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Settings.setValue("desktop.wallpaper", filePath);
                                wallpaperOverlay.visible = false;
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: wallpaperGrid.count === 0
                    text: "No pictures in this folder"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                }
            }
        }

        FolderListModel {
            id: picturesModel
            folder: settingsWindow.wallpaperFolders[0]
            nameFilters: ["*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp"]
            showDirs: false
            showDotAndDotDot: false
        }
        FolderListModel {
            id: backgroundsModel
            folder: settingsWindow.wallpaperFolders[1]
            nameFilters: ["*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp"]
            showDirs: false
            showDotAndDotDot: false
        }
    }

    // ------------------------------------------------------------------
    // Reset confirmation overlay
    // ------------------------------------------------------------------
    Rectangle {
        id: confirmBox
        anchors.fill: parent
        visible: false
        color: Qt.rgba(0, 0, 0, 0.55)
        z: 60

        Rectangle {
            width: 380
            height: 160
            radius: Theme.radius + 4
            anchors.centerIn: parent
            color: Theme.background
            border.width: 1
            border.color: Theme.border

            Column {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                Text {
                    text: "Reset all settings?"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize + 3
                    font.bold: true
                }
                Text {
                    width: parent.width
                    text: "Every key of shell.json returns to its default. This cannot be undone."
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                    wrapMode: Text.Wrap
                }
                Row {
                    spacing: 8
                    TaskButton {
                        iconName: "check"
                        label: "Reset"
                        onClicked: settingsWindow.resetAllSettings()
                    }
                    TaskButton {
                        iconName: "x"
                        label: "Cancel"
                        onClicked: confirmBox.visible = false
                    }
                }
            }
        }
    }
}
