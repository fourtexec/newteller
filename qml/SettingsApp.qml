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
        { id: "appearance", label: qsTr("Appearance"), icon: "palette" },
        { id: "panel", label: qsTr("Panel & dock"), icon: "panel-bottom" },
        { id: "desktop", label: qsTr("Desktop"), icon: "monitor" },
        { id: "startmenu", label: qsTr("Start menu"), icon: "search" },
        { id: "apps", label: qsTr("Applications"), icon: "package" },
        { id: "system", label: qsTr("System"), icon: "cpu" },
        { id: "notifications", label: qsTr("Notifications"), icon: "bell" },
        { id: "plugins", label: qsTr("Plugins"), icon: "puzzle" },
        { id: "hotkeys", label: qsTr("Hotkeys"), icon: "keyboard" },
        { id: "animations", label: qsTr("Animations"), icon: "sparkles" },
        { id: "windows", label: qsTr("Windows"), icon: "app-window" },
        { id: "keyboard", label: qsTr("Keyboard"), icon: "keyboard" },
        { id: "mouse", label: qsTr("Mouse & touchpad"), icon: "mouse" },
        { id: "display", label: qsTr("Displays"), icon: "monitor" },
        { id: "power", label: qsTr("Power & lock"), icon: "power" },
        { id: "about", label: qsTr("About"), icon: "info" }
    ]
    property string currentPage: "appearance"

    readonly property var wallpaperFolders: [Shell.picturesPath, "/usr/share/backgrounds"]
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
        Shell.notify("NewTeller", qsTr("All settings were reset to their defaults."));
    }

    function exportSettings() {
        const stamp = Qt.formatDateTime(Shell.now, "yyyyMMdd-hhmmss");
        const path = Shell.home + "/newteller-settings-" + stamp + ".json";
        const body = JSON.stringify(Settings.values, null, 2);
        if (Shell.saveTextFile(path, body))
            Shell.notify("NewTeller", qsTr("A copy of the settings was saved to %1").arg(path));
        else
            Shell.notify("NewTeller", qsTr("The settings copy could not be written."));
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
                        text: qsTr("version %1").arg(Shell.version)
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
                text: qsTr("The settings are built into the\nnewteller binary and always\navailable.")
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
                            title: qsTr("Theme")
                            subtitle: qsTr("Pick one of the themes; custom themes live in %1").arg(Theme.userThemesPath())

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Preview")

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
                                title: qsTr("Selected theme")
                                settingKey: "theme.name"
                                subtitle: Theme.hasUserTheme(settingsWindow.themeEditorName)
                                          ? qsTr("This theme has user customisations on disk.")
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
                            title: qsTr("Day and night")
                            subtitle: Settings.values["theme.schedule"] === "times"
                                      || Settings.values["theme.schedule"] === "nightLight"
                                      ? (Theme.night ? qsTr("The night theme is shown now.")
                                                     : qsTr("The day theme is shown now."))
                                      : qsTr("Switch between two themes at set times.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Switch themes")
                                settingKey: "theme.schedule"
                                SettingsChoice {
                                    model: [
                                        { value: "off", label: qsTr("Off") },
                                        { value: "times", label: qsTr("At set times") },
                                        { value: "nightLight", label: qsTr("With the night light") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                visible: Settings.values["theme.schedule"] === "times"
                                         || Settings.values["theme.schedule"] === "nightLight"
                                title: qsTr("Day theme")
                                settingKey: "theme.dayTheme"
                                SettingsChoice {
                                    model: Theme.availableThemes().map(name => ({ value: name, label: name }))
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                visible: Settings.values["theme.schedule"] === "times"
                                         || Settings.values["theme.schedule"] === "nightLight"
                                title: qsTr("Night theme")
                                settingKey: "theme.nightTheme"
                                SettingsChoice {
                                    model: Theme.availableThemes().map(name => ({ value: name, label: name }))
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                visible: Settings.values["theme.schedule"] === "times"
                                title: qsTr("Day starts at")
                                settingKey: "theme.dayStart"
                                SettingsTextField { width: 90; placeholder: "07:00" }
                            }
                            SettingsRow {
                                width: parent.width
                                visible: Settings.values["theme.schedule"] === "times"
                                title: qsTr("Night starts at")
                                settingKey: "theme.nightStart"
                                SettingsTextField { width: 90; placeholder: "20:00" }
                            }
                            SettingsRow {
                                width: parent.width
                                visible: Settings.values["theme.schedule"] === "times"
                                         || Settings.values["theme.schedule"] === "nightLight"
                                title: qsTr("Applications follow")
                                subtitle: qsTr("Light applications by day, dark ones at night.")
                                settingKey: "theme.scheduleApps"
                                SettingsSwitch {}
                            }
                        }

                        SettingsSection {
                            title: qsTr("Language")
                            subtitle: qsTr("The language of the panel, menus and settings. Applications follow the system language.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Shell language")
                                settingKey: "shell.language"
                                SettingsChoice {
                                    settingKey: "shell.language"
                                    // Language names stay in their own language.
                                    model: [
                                        { value: "auto", label: qsTr("System language") },
                                        { value: "en", label: "English" },
                                        { value: "ru", label: "Русский" }
                                    ]
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Applications")
                            subtitle: qsTr("GTK, GNOME, Firefox and Chromium-based applications follow this choice.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Application colour scheme")
                                settingKey: "apps.colorScheme"
                                SettingsChoice {
                                    settingKey: "apps.colorScheme"
                                    model: [
                                        { value: "dark", label: qsTr("Dark") },
                                        { value: "light", label: qsTr("Light") },
                                        { value: "system", label: qsTr("Leave as is") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("GTK theme")
                                subtitle: qsTr("Installed in /usr/share/themes or ~/.themes. Overrides the colour scheme's default theme.")
                                settingKey: "apps.gtkTheme"
                                SettingsChoice {
                                    id: gtkFlow
                                    width: Math.min(420, appearanceColumn.width * 0.6)
                                    settingKey: "apps.gtkTheme"
                                    model: [{ value: "", label: qsTr("Default") }].concat(
                                        Session.gtkThemes().map(function (t) { return { value: t, label: t }; }))
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Icon theme")
                                settingKey: "apps.iconTheme"
                                SettingsChoice {
                                    id: iconFlow
                                    width: Math.min(420, appearanceColumn.width * 0.6)
                                    settingKey: "apps.iconTheme"
                                    model: [{ value: "", label: qsTr("Default") }].concat(
                                        Session.iconThemes().map(function (t) { return { value: t, label: t }; }))
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Application font")
                                subtitle: qsTr("Name and size, e.g. \"Noto Sans 11\". Empty: unchanged.")
                                settingKey: "apps.font"
                                SettingsTextField { settingKey: "apps.font"; placeholder: qsTr("(unchanged)") }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Text size in applications")
                                settingKey: "apps.textScale"
                                SettingsSlider { settingKey: "apps.textScale"; from: 0.8; to: 1.6; step: 0.05; suffix: "x" }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Theme variants")
                            subtitle: qsTr("Automatically switch themes based on system preferences.")

                        }

                        SettingsSection {
                            title: qsTr("Theme editor")
                            subtitle: qsTr("Colours and typography of %1. Saving writes a user theme file that overrides the built-in one.")
                                          .arg(settingsWindow.themeEditorName || Theme.name)

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Background")
                                SettingsColorField {
                                    id: backgroundField
                                    onEdited: (hex) => settingsWindow.saveTheme()
                                    onPaletteRequested: settingsWindow.openPalette(backgroundField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Foreground")
                                SettingsColorField {
                                    id: foregroundField
                                    onEdited: (hex) => settingsWindow.saveTheme()
                                    onPaletteRequested: settingsWindow.openPalette(foregroundField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Accent")
                                SettingsColorField {
                                    id: accentField
                                    onEdited: (hex) => settingsWindow.saveTheme()
                                    onPaletteRequested: settingsWindow.openPalette(accentField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Surface")
                                SettingsColorField {
                                    id: surfaceField
                                    onEdited: (hex) => settingsWindow.saveTheme()
                                    onPaletteRequested: settingsWindow.openPalette(surfaceField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Border")
                                SettingsColorField {
                                    id: borderField
                                    onEdited: (hex) => settingsWindow.saveTheme()
                                    onPaletteRequested: settingsWindow.openPalette(borderField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Muted text")
                                SettingsColorField {
                                    id: mutedField
                                    onEdited: (hex) => settingsWindow.saveTheme()
                                    onPaletteRequested: settingsWindow.openPalette(mutedField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Panel opacity")
                                SettingsSlider { id: opacityBox; from: 0.3; to: 1.0; step: 0.01; onChanged: (v) => settingsWindow.saveTheme() }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Corner radius")
                                SettingsStepper { id: radiusBox; from: 0; to: 24; onChanged: (v) => settingsWindow.saveTheme() }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Font size")
                                SettingsStepper { id: fontSizeBox; from: 6; to: 16; onChanged: (v) => settingsWindow.saveTheme() }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Font family")
                                subtitle: qsTr("Every font installed on the system.")
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
                                title: qsTr("Save changes")
                                subtitle: qsTr("The file is written to %1 and applied immediately.").arg(Theme.userThemesPath())
                                Row {
                                    spacing: 6
                                    TaskButton {
                                        iconName: "save"
                                        label: qsTr("Save theme")
                                        onClicked: settingsWindow.saveTheme()
                                    }
                                    TaskButton {
                                        iconName: "eraser"
                                        label: qsTr("Discard custom copy")
                                        visible: Theme.hasUserTheme(settingsWindow.themeEditorName)
                                        onClicked: settingsWindow.resetTheme()
                                    }
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Panel styling")
                            subtitle: qsTr("Choose panel material and optional custom colors. Frosted/liquid highlights are lightweight; real backdrop blur depends on the compositor.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Panel surface")
                                subtitle: qsTr("Theme, opaque, half-transparent, frosted or liquid glass. Shown with the Bar panel style.")
                                settingKey: "panel.material"
                                SettingsChoice {
                                    model: [
                                        { value: "theme", label: qsTr("Theme") },
                                        { value: "opaque", label: qsTr("Opaque") },
                                        { value: "translucent", label: qsTr("Half transparent") },
                                        { value: "frosted", label: qsTr("Frosted glass") },
                                        { value: "liquid", label: qsTr("Liquid glass") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Use custom panel colors")
                                settingKey: "panel.customColors"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Panel background")
                                visible: Settings.values["panel.customColors"] === true
                                settingKey: "panel.backgroundColor"
                                SettingsColorField {
                                    id: panelBgColorField
                                    onEdited: (hex) => Settings.setValue("panel.backgroundColor", hex)
                                    onPaletteRequested: settingsWindow.openPalette(panelBgColorField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Panel foreground")
                                visible: Settings.values["panel.customColors"] === true
                                settingKey: "panel.foregroundColor"
                                SettingsColorField {
                                    id: panelFgColorField
                                    onEdited: (hex) => Settings.setValue("panel.foregroundColor", hex)
                                    onPaletteRequested: settingsWindow.openPalette(panelFgColorField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Panel opacity")
                                visible: Settings.values["panel.customColors"] === true
                                         && (Settings.values["panel.material"] || "theme") === "theme"
                                settingKey: "panel.backgroundOpacity"
                                SettingsSlider { from: 0.3; to: 1.0; step: 0.01 }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Dock styling")
                            subtitle: qsTr("The floating glass dock. The material controls the pill behind the icons, not each icon separately.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Dock material")
                                subtitle: qsTr("Half transparent, non transparent, frosted glass or liquid glass.")
                                settingKey: "dock.material"
                                SettingsChoice {
                                    model: [
                                        { value: "none", label: qsTr("No glass") },
                                        { value: "opaque", label: qsTr("Non transparent") },
                                        { value: "translucent", label: qsTr("Half transparent") },
                                        { value: "frosted", label: qsTr("Frosted glass") },
                                        { value: "liquid", label: qsTr("Liquid glass") }
                                    ]
                                }
                            }
                            // The four dock looks side by side, the way the
                            // classic macOS comparison shows them: the same
                            // icon row on the same wallpaper, only the pill
                            // behind it changes. Clicking one applies it.
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Material preview")
                                subtitle: qsTr("Click a pill to apply it to the dock.")

                                Row {
                                    spacing: 6

                                    Repeater {
                                        model: [
                                            { value: "translucent", label: qsTr("Half") },
                                            { value: "opaque", label: qsTr("Solid") },
                                            { value: "frosted", label: qsTr("Frosted") },
                                            { value: "liquid", label: qsTr("Liquid") }
                                        ]

                                        delegate: Item {
                                            id: dockPreviewSwatch
                                            required property var modelData
                                            readonly property bool current: (Settings.values["dock.material"] || "none") === modelData.value
                                            width: 104
                                            height: 62
                                            opacity: current ? 1 : 0.72

                                            // Wallpaper stand-in: a dark blue
                                            // to violet wash with a diagonal
                                            // light band, so transparencies
                                            // are actually visible.
                                            Rectangle {
                                                anchors.fill: parent
                                                radius: 6
                                                gradient: Gradient {
                                                    GradientStop { position: 0.0; color: "#101010" }
                                                    GradientStop { position: 0.55; color: "#1c1c1c" }
                                                    GradientStop { position: 1.0; color: "#262626" }
                                                }
                                            }
                                            Rectangle {
                                                anchors.fill: parent
                                                radius: 6
                                                gradient: Gradient {
                                                    GradientStop { position: 0.0; color: "#00000000" }
                                                    GradientStop { position: 0.45; color: "#5a5a5aff" }
                                                    GradientStop { position: 0.62; color: "#00000000" }
                                                    GradientStop { position: 1.0; color: "#00000000" }
                                                }
                                                rotation: 18
                                            }
                                            Rectangle {
                                                anchors.fill: parent
                                                radius: 6
                                                color: "transparent"
                                                border.width: current ? 2 : 1
                                                border.color: current ? Theme.accent
                                                                      : Qt.rgba(Theme.border.r, Theme.border.g,
                                                                                Theme.border.b, 0.55)
                                            }

                                            DockSurface {
                                                anchors.centerIn: parent
                                                width: 92
                                                height: 30
                                                material: dockPreviewSwatch.modelData.value
                                                baseColor: Theme.surface
                                                accentColor: Theme.accent
                                                cornerRadius: height / 2
                                                effectStrength: 0.9
                                            }

                                            // A hint of the icon row behind
                                            // the pill.
                                            Row {
                                                anchors.centerIn: parent
                                                spacing: 5
                                                Repeater {
                                                    model: ["#5b9bfd", "#fb7185", "#4ade80", "#fbbf24", "#c084fc"]
                                                    delegate: Rectangle {
                                                        required property string modelData
                                                        width: 12
                                                        height: 12
                                                        radius: 3.5
                                                        color: modelData
                                                    }
                                                }
                                            }

                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                anchors.bottom: parent.bottom
                                                anchors.bottomMargin: 3
                                                text: dockPreviewSwatch.modelData.label
                                                color: Theme.foreground
                                                style: Text.Outline
                                                styleColor: Qt.rgba(0, 0, 0, 0.65)
                                                font.family: Theme.fontFamily
                                                font.pointSize: Theme.fontSize - 3
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: Settings.setValue("dock.material", dockPreviewSwatch.modelData.value)
                                            }
                                        }
                                    }
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Dock tint")
                                visible: Settings.values["dock.material"] !== "none"
                                settingKey: "dock.backgroundColor"
                                SettingsColorField {
                                    id: dockBgColorField
                                    onEdited: (hex) => Settings.setValue("dock.backgroundColor", hex)
                                    onPaletteRequested: settingsWindow.openPalette(dockBgColorField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Dock opacity")
                                subtitle: qsTr("Biases the material: left is clearer, right is more solid.")
                                visible: Settings.values["dock.material"] !== "none"
                                settingKey: "dock.backgroundOpacity"
                                SettingsSlider { from: 0.2; to: 1.0; step: 0.05 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Show labels")
                                settingKey: "dock.showLabels"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Label position")
                                settingKey: "dock.labelPosition"
                                SettingsChoice {
                                    model: [
                                        { value: "bottom", label: qsTr("Bottom") },
                                        { value: "right", label: qsTr("Right") },
                                        { value: "top", label: qsTr("Top") },
                                        { value: "left", label: qsTr("Left") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Icon color mode")
                                settingKey: "dock.iconColorMode"
                                SettingsChoice {
                                    model: [
                                        { value: "theme", label: qsTr("Theme color") },
                                        { value: "custom", label: qsTr("Custom color") },
                                        { value: "original", label: qsTr("Original icon colors") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Custom icon color")
                                visible: Settings.values["dock.iconColorMode"] === "custom"
                                settingKey: "dock.customIconColor"
                                SettingsColorField {
                                    id: dockCustomColorField
                                    onEdited: (hex) => Settings.setValue("dock.customIconColor", hex)
                                    onPaletteRequested: settingsWindow.openPalette(dockCustomColorField)
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Window style")
                            subtitle: qsTr("The frame of shell popups — start menu, run dialog, dock menu and clock. Radius/border -1 and opacity 0 use the selected material defaults.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Style")
                                settingKey: "windows.style"
                                SettingsChoice {
                                    model: [
                                        { value: "card", label: qsTr("Card") },
                                        { value: "glass", label: qsTr("Glass") },
                                        { value: "flat", label: qsTr("Flat") },
                                        { value: "outline", label: qsTr("Outline") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Surface material")
                                subtitle: qsTr("Opaque, half-transparent, frosted or liquid glass. Frosted and liquid styles use low-cost Qt Quick highlights.")
                                settingKey: "windows.material"
                                SettingsChoice {
                                    model: [
                                        { value: "theme", label: qsTr("Theme") },
                                        { value: "opaque", label: qsTr("Opaque") },
                                        { value: "translucent", label: qsTr("Half transparent") },
                                        { value: "frosted", label: qsTr("Frosted glass") },
                                        { value: "liquid", label: qsTr("Liquid glass") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Material preview")
                                subtitle: qsTr("Click a swatch to apply it to the shell popups.")
                                Row {
                                    spacing: 6
                                    Repeater {
                                        model: [
                                            { value: "opaque", label: qsTr("Opaque") },
                                            { value: "translucent", label: qsTr("Half") },
                                            { value: "frosted", label: qsTr("Frosted") },
                                            { value: "liquid", label: qsTr("Liquid") }
                                        ]
                                        delegate: Item {
                                            required property var modelData
                                            width: 54
                                            height: 44
                                            Rectangle {
                                                anchors.fill: parent
                                                radius: Theme.radius
                                                color: Qt.lighter(Theme.surface, 1.15)
                                            }
                                            SurfaceStyle {
                                                anchors.fill: parent
                                                material: modelData.value
                                                baseColor: Theme.background
                                                cornerRadius: Theme.radius
                                                effectStrength: 0.9
                                                opacity: (Settings.values["windows.material"] || "theme") === modelData.value
                                                         ? 1 : 0.55
                                            }
                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                anchors.bottom: parent.bottom
                                                anchors.bottomMargin: 2
                                                text: modelData.label
                                                color: Theme.foreground
                                                font.family: Theme.fontFamily
                                                font.pointSize: Theme.fontSize - 3
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: Settings.setValue("windows.material", modelData.value)
                                            }
                                        }
                                    }
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Corner radius")
                                subtitle: qsTr("-1 follows the theme radius.")
                                settingKey: "windows.radius"
                                SettingsStepper { from: -1; to: 32; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Background opacity")
                                subtitle: qsTr("0 uses the selected material's preset opacity.")
                                settingKey: "windows.opacityPercent"
                                SettingsSlider { from: 0; to: 100; step: 5; suffix: " %" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Border width")
                                subtitle: qsTr("-1 follows the theme.")
                                settingKey: "windows.borderWidth"
                                SettingsStepper { from: -1; to: 6; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Border colour")
                                subtitle: qsTr("Empty follows the theme.")

                                Row {
                                    spacing: 6

                                    SettingsColorField {
                                        id: winBorderField
                                        onEdited: (hex) => Settings.setValue("windows.borderColor", hex)
                                        onPaletteRequested: settingsWindow.openPalette(winBorderField)
                                    }
                                    TaskButton {
                                        iconName: "rotate-ccw"
                                        label: qsTr("Follow theme")
                                        onClicked: Settings.reset("windows.borderColor")
                                    }
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Shadow")
                                settingKey: "windows.shadow"
                                SettingsStepper { from: 0; to: 3 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Preview")

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
                            title: qsTr("Panel")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Position")
                                settingKey: "panel.position"
                                SettingsChoice {
                                    model: [
                                        { value: "bottom", label: qsTr("Bottom") },
                                        { value: "top", label: qsTr("Top") },
                                        { value: "left", label: qsTr("Left") },
                                        { value: "right", label: qsTr("Right") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Thickness")
                                settingKey: "panel.height"
                                SettingsSlider { from: 20; to: 100; step: 2; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Style")
                                subtitle: qsTr("Pill: only the dock is drawn. Bar: a bar along the edge in the panel surface and colours (Appearance).")
                                settingKey: "panel.style"
                                SettingsChoice {
                                    model: [
                                        { value: "pill", label: qsTr("Pill") },
                                        { value: "bar", label: qsTr("Bar") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Length")
                                subtitle: qsTr("Share of the screen edge; less than 100% makes a floating, centred panel.")
                                settingKey: "panel.length"
                                SettingsSlider { from: 30; to: 100; step: 5; suffix: " %" }
                            }
                            SettingsRow {
                                width: parent.width
                                visible: Settings.values["panel.style"] === "bar"
                                title: qsTr("Bar corner radius")
                                subtitle: qsTr("-1 rounds a floating bar and keeps a full-width one square.")
                                settingKey: "panel.cornerRadius"
                                SettingsStepper { from: -1; to: 32; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Screen edge gap")
                                settingKey: "panel.margin"
                                SettingsSlider { from: 0; to: 24; step: 1; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Auto-hide")
                                subtitle: qsTr("Slide the panel away until the pointer touches its edge.")
                                settingKey: "panel.autoHide"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Auto-hide delay")
                                settingKey: "panel.autoHideDelay"
                                SettingsStepper { from: 0; to: 5000; step: 100; suffix: qsTr(" ms") }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Running badge")
                                subtitle: qsTr("Mark running applications on their dock buttons.")
                                settingKey: "panel.showRunning"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Quick settings indicators")
                                subtitle: qsTr("The bell and the indicators button at the end of the bar: notification centre, volume, brightness, network and media.")
                                settingKey: "panel.indicators"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Show date next to the clock")
                                settingKey: "panel.showDate"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Clock format")
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
                                title: qsTr("Date format")
                                settingKey: "panel.dateFormat"
                                SettingsTextField {}
                            }
                        }

                        SettingsSection {
                            title: qsTr("Dock")
                            subtitle: qsTr("Every started application appears in the dock: real windows are tracked on X11 and Wayland, and the process list fills in the rest.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Show open windows")
                                settingKey: "dock.showWindows"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Group windows of one application")
                                subtitle: qsTr("Off: every window has its own button.")
                                settingKey: "dock.group"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Dock icon size")
                                settingKey: "dock.iconSize"
                                SettingsSlider { from: 16; to: 48; step: 2; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Background zone")
                                subtitle: qsTr("Applications whose windows are all minimised gather in a dimmed zone at the end of the dock.")
                                settingKey: "dock.separateBackground"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Shrink icons on overflow")
                                subtitle: qsTr("The dock icons shrink when many applications run at once; the strip scrolls when even that is not enough.")
                                settingKey: "dock.shrinkIcons"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Window previews")
                                subtitle: qsTr("Show a live thumbnail of every window in the dock menu. Built from X11/XWayland windows; native Wayland windows show their application icon instead.")
                                settingKey: "dock.thumbnails"
                                SettingsSwitch {}
                            }
                        }

                        SettingsSection {
                            title: qsTr("Panel layout")
                            subtitle: qsTr("Advanced panel positioning and spacing options.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Left spacing")
                                settingKey: "panel.leftSpacing"
                                SettingsSlider { from: 0; to: 64; step: 2; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Right spacing")
                                settingKey: "panel.rightSpacing"
                                SettingsSlider { from: 0; to: 64; step: 2; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Zone spacing")
                                settingKey: "panel.zoneSpacing"
                                SettingsSlider { from: 0; to: 24; step: 2; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Dock position in panel")
                                settingKey: "dock.position"
                                SettingsChoice {
                                    model: [
                                        { value: "left", label: qsTr("Left") },
                                        { value: "center", label: qsTr("Center") },
                                        { value: "right", label: qsTr("Right") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Center dock buttons")
                                settingKey: "panel.centerDock"
                                SettingsSwitch {}
                            }
                        }

                        SettingsSection {
                            title: qsTr("Dock layout")
                            subtitle: qsTr("Advanced dock configuration options.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Multi-row dock")
                                settingKey: "dock.multiRow"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Number of rows")
                                settingKey: "dock.rows"
                                SettingsStepper { from: 1; to: 3 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Scrollable dock")
                                settingKey: "dock.scrollable"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Icon spacing")
                                settingKey: "dock.iconSpacing"
                                SettingsSlider { from: 0; to: 24; step: 2; suffix: " px" }
                            }
                        }

                        SettingsSection {
                            title: qsTr("System tray")
                            subtitle: qsTr("The shell acts as the StatusNotifierWatcher of the session: applications register their tray icons with it. Left click activates, middle click triggers the secondary action, and right click opens the item's menu.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Show the system tray")
                                subtitle: qsTr("Hidden when another tray host owns the session.")
                                settingKey: "tray.enabled"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Render tray menus")
                                subtitle: qsTr("Use the application's DBusMenu for right clicks instead of asking it to open its own context menu.")
                                settingKey: "tray.menus"
                                SettingsSwitch {}
                            }
                        }

                        SettingsSection {
                            title: qsTr("Pinned applications")
                            subtitle: qsTr("Desktop file ids, as in /usr/share/applications. Right-click a dock button at runtime for per-window actions, or right-click an application in the start menu to pin or unpin it.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Pinned list")

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
                                            label: qsTr("Pin")
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
                            title: qsTr("Wallpaper")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Current wallpaper")
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
                                            id: wallpaperPreview
                                            anchors.fill: parent
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            sourceSize.width: 336
                                            sourceSize.height: 192
                                            // A wallpaper that no longer loads (deleted
                                            // file, unreadable format) shows the text
                                            // fallback instead of an empty frame.
                                            visible: status === Image.Ready
                                            source: {
                                                const wp = Settings.value("desktop.wallpaper", "");
                                                if (!wp || wp.length === 0)
                                                    return "";
                                                if (wp.indexOf("://") > 0)
                                                    return wp;
                                                return "file://" + wp.split("/").map(p => encodeURIComponent(p)).join("/");
                                            }
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            visible: !Settings.value("desktop.wallpaper", "")
                                                     || wallpaperPreview.status === Image.Error
                                            text: qsTr("Solid colour")
                                            color: Theme.muted
                                            font.family: Theme.fontFamily
                                            font.pointSize: Theme.fontSize - 1
                                        }
                                    }

                                    Column {
                                        spacing: 6
                                        TaskButton {
                                            iconName: "image"
                                            label: qsTr("Browse images")
                                            onClicked: {
                                                wallpaperOverlay.visible = true;
                                            }
                                        }
                                        TaskButton {
                                            iconName: "folder"
                                            label: qsTr("Choose from file manager")
                                            onClicked: settingsWindow.pickWallpaper(
                                                           settingsWindow.wallpaperFolder())
                                        }
                                        TaskButton {
                                            iconName: "shuffle"
                                            label: qsTr("Random picture")
                                            onClicked: wallpaperOverlay.pickRandom()
                                        }
                                        TaskButton {
                                            iconName: "x"
                                            label: qsTr("No wallpaper")
                                            onClicked: Settings.setValue("desktop.wallpaper", "")
                                        }
                                    }
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Wallpaper")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Default wallpaper")
                                subtitle: qsTr("Shown while no picture is chosen and no background colour is set.")
                                settingKey: "desktop.defaultWallpaper"
                                SettingsSwitch { settingKey: "desktop.defaultWallpaper" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Wallpaper mode")
                                settingKey: "desktop.wallpaperMode"
                                SettingsChoice {
                                    model: [
                                        { value: "fill", label: qsTr("Fill") },
                                        { value: "fit", label: qsTr("Fit") },
                                        { value: "stretch", label: qsTr("Stretch") },
                                        { value: "tile", label: qsTr("Tile") },
                                        { value: "center", label: qsTr("Center") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Wallpaper blur")
                                settingKey: "desktop.wallpaperBlur"
                                SettingsSlider { from: 0; to: 50; step: 1; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Wallpaper darken")
                                settingKey: "desktop.wallpaperDarken"
                                SettingsSlider { from: 0; to: 100; step: 5; suffix: " %" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Per-monitor wallpapers")
                                settingKey: "desktop.perMonitorWallpaper"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Wallpaper slideshow")
                                settingKey: "desktop.wallpaperSlideshow"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Slideshow interval")
                                settingKey: "desktop.wallpaperSlideshowInterval"
                                SettingsStepper { from: 10; to: 3600; step: 10; suffix: qsTr(" s") }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Desktop background")
                            subtitle: qsTr("Custom background color when no wallpaper is set.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Use gradient background")
                                settingKey: "desktop.backgroundGradient"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Background color")
                                visible: !Settings.values["desktop.backgroundGradient"]
                                settingKey: "desktop.backgroundColor"
                                SettingsColorField {
                                    id: desktopBgColorField
                                    onEdited: (hex) => Settings.setValue("desktop.backgroundColor", hex)
                                    onPaletteRequested: settingsWindow.openPalette(desktopBgColorField)
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Icons")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Show desktop icons")
                                settingKey: "desktop.showIcons"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Icon size")
                                settingKey: "desktop.iconSize"
                                SettingsSlider { from: 24; to: 96; step: 4; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Grid width")
                                settingKey: "desktop.cellWidth"
                                SettingsSlider { from: 64; to: 160; step: 8; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Grid height")
                                settingKey: "desktop.cellHeight"
                                SettingsSlider { from: 64; to: 180; step: 8; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Right-click menu")
                                settingKey: "desktop.contextMenu"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Icon color")
                                settingKey: "desktop.iconColor"
                                SettingsColorField {
                                    id: desktopIconColorField
                                    onEdited: (hex) => Settings.setValue("desktop.iconColor", hex)
                                    onPaletteRequested: settingsWindow.openPalette(desktopIconColorField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Icon shadow")
                                settingKey: "desktop.iconShadow"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Icon glow")
                                settingKey: "desktop.iconGlow"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Label colour")
                                settingKey: "desktop.iconTextColor"
                                SettingsColorField {
                                    id: desktopLabelColorField
                                    onEdited: (hex) => Settings.setValue("desktop.iconTextColor", hex)
                                    onPaletteRequested: settingsWindow.openPalette(desktopLabelColorField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Label outline")
                                subtitle: qsTr("A dark outline keeps names readable on light wallpapers.")
                                settingKey: "desktop.iconTextShadow"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Label size")
                                subtitle: qsTr("0 uses the theme font size.")
                                settingKey: "desktop.iconTextSize"
                                SettingsStepper { from: 0; to: 20; suffix: " pt" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Label lines")
                                subtitle: qsTr("Longer names end in an ellipsis.")
                                settingKey: "desktop.iconTextLines"
                                SettingsStepper { from: 1; to: 4 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Show hidden files")
                                settingKey: "desktop.showHiddenFiles"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Auto-arrange icons")
                                subtitle: qsTr("Drag an icon on the desktop to place it freely; auto-arrange then turns itself off.")
                                settingKey: "desktop.autoArrangeIcons"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Icon layout")
                                subtitle: qsTr("Forget every dragged position and return to the grid.")
                                TaskButton {
                                    iconName: "rotate-ccw"
                                    label: qsTr("Reset layout")
                                    onClicked: Settings.setValues({
                                        "desktop.gridPositions": ({}),
                                        "desktop.iconPositions": ({}),
                                        "desktop.widgetPositions": ({}),
                                        "desktop.autoArrangeIcons": true
                                    })
                                }
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
                            title: qsTr("Start menu")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Width")
                                settingKey: "startmenu.width"
                                SettingsSlider { from: 300; to: 700; step: 10; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Height")
                                settingKey: "startmenu.height"
                                SettingsSlider { from: 400; to: 900; step: 10; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Typo tolerant search")
                                settingKey: "startmenu.fuzzy"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Recently used row")
                                settingKey: "startmenu.showRecent"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Search recently used files")
                                subtitle: qsTr("Documents opened in other applications show up when their name is typed.")
                                settingKey: "startmenu.recentFiles"
                                SettingsSwitch { settingKey: "startmenu.recentFiles" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Recent list length")
                                settingKey: "apps.recentLimit"
                                SettingsStepper { from: 0; to: 30 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Search result limit")
                                settingKey: "startmenu.limit"
                                SettingsStepper { from: 10; to: 500; step: 10 }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Layout")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Applications as")
                                settingKey: "startmenu.layout"
                                SettingsChoice {
                                    model: [
                                        { value: "list", label: qsTr("List") },
                                        { value: "grid", label: qsTr("Grid") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Grid icon size")
                                visible: Settings.values["startmenu.layout"] === "grid"
                                settingKey: "startmenu.gridIconSize"
                                SettingsSlider { from: 24; to: 96; step: 4; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Search field")
                                settingKey: "startmenu.searchPosition"
                                SettingsChoice {
                                    model: [
                                        { value: "top", label: qsTr("Top") },
                                        { value: "bottom", label: qsTr("Bottom") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Power buttons")
                                subtitle: qsTr("Lock, log out, sleep, restart and shut down in the footer.")
                                settingKey: "startmenu.showPowerButtons"
                                SettingsSwitch {}
                            }
                        }

                        SettingsSection {
                            title: qsTr("Appearance")
                            subtitle: qsTr("Empty colours follow the theme.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Surface")
                                settingKey: "startmenu.material"
                                SettingsChoice {
                                    model: [
                                        { value: "default", label: qsTr("Like other popups") },
                                        { value: "opaque", label: qsTr("Opaque") },
                                        { value: "translucent", label: qsTr("Half transparent") },
                                        { value: "frosted", label: qsTr("Frosted glass") },
                                        { value: "liquid", label: qsTr("Liquid glass") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Surface opacity")
                                subtitle: qsTr("0 uses the selected material's preset opacity.")
                                settingKey: "startmenu.opacityPercent"
                                SettingsSlider { from: 0; to: 100; step: 5; suffix: " %" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Corner radius")
                                subtitle: qsTr("-1 follows the other popups.")
                                settingKey: "startmenu.cornerRadius"
                                SettingsStepper { from: -1; to: 32; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Background colour")
                                settingKey: "startmenu.backgroundColor"
                                SettingsColorField {
                                    id: menuBgColorField
                                    onEdited: (hex) => Settings.setValue("startmenu.backgroundColor", hex)
                                    onPaletteRequested: settingsWindow.openPalette(menuBgColorField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Text colour")
                                settingKey: "startmenu.foregroundColor"
                                SettingsColorField {
                                    id: menuFgColorField
                                    onEdited: (hex) => Settings.setValue("startmenu.foregroundColor", hex)
                                    onPaletteRequested: settingsWindow.openPalette(menuFgColorField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Accent colour")
                                subtitle: qsTr("The selected row and the active category.")
                                settingKey: "startmenu.accentColor"
                                SettingsColorField {
                                    id: menuAccentColorField
                                    onEdited: (hex) => Settings.setValue("startmenu.accentColor", hex)
                                    onPaletteRequested: settingsWindow.openPalette(menuAccentColorField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Search match colour")
                                subtitle: qsTr("Letters that match the search; empty uses the accent.")
                                settingKey: "startmenu.highlight"
                                SettingsColorField {
                                    id: menuHighlightColorField
                                    onEdited: (hex) => Settings.setValue("startmenu.highlight", hex)
                                    onPaletteRequested: settingsWindow.openPalette(menuHighlightColorField)
                                }
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
                            title: qsTr("Applications")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Terminal command")
                                subtitle: qsTr("Used by \"Open terminal\"; empty means auto-detect.")
                                settingKey: "apps.terminalCommand"
                                SettingsTextField { width: 260 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Application catalogue cap")
                                subtitle: qsTr("0 keeps every installed application.")
                                settingKey: "apps.maxApps"
                                SettingsStepper { from: 0; to: 2000; step: 50 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Usage history")
                                subtitle: qsTr("Clears the recent row and the ranking boost.")
                                TaskButton {
                                    iconName: "eraser"
                                    label: qsTr("Clear recent list")
                                    onClicked: {
                                        Apps.forgetRecent();
                                        Shell.notify("NewTeller", qsTr("The recent applications list was cleared."));
                                    }
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Startup applications")
                            subtitle: qsTr("Applications to launch automatically when NewTeller starts.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Startup delay (seconds)")
                                settingKey: "startup.delay"
                                SettingsStepper { from: 0; to: 60; step: 1; suffix: qsTr(" s") }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Startup applications list")

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
                                            placeholder: qsTr("Application name or .desktop file")
                                        }
                                        TaskButton {
                                            iconName: "plus"
                                            label: qsTr("Add")
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
                            title: qsTr("Started by the system")
                            subtitle: qsTr("XDG autostart entries from ~/.config/autostart and /etc/xdg/autostart. Applications add them with their own \"start at login\" option; delete the file in ~/.config/autostart to stop one.")

                            Repeater {
                                model: Session.autostartEntries()

                                delegate: SettingsRow {
                                    required property var modelData
                                    width: parent ? parent.width : 320
                                    title: modelData.name
                                    subtitle: modelData.exec

                                    Text {
                                        text: modelData.file.indexOf("/etc/") === 0 ? qsTr("System") : qsTr("Personal")
                                        color: Theme.muted
                                        font.family: Theme.fontFamily
                                        font.pointSize: Theme.fontSize - 1
                                    }
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Search")
                            subtitle: qsTr("Application search behavior.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Show category chips")
                                settingKey: "search.showCategories"
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
                            title: qsTr("Metrics")
                            subtitle: qsTr("The shared ticker behind the system monitor widgets.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Sampling interval")
                                settingKey: "system.interval"
                                SettingsStepper { from: 250; to: 60000; step: 250; suffix: qsTr(" ms") }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Per-core CPU")
                                settingKey: "system.perCore"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Battery")
                                settingKey: "system.battery"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Thermal zones")
                                settingKey: "system.thermal"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Disks")
                                subtitle: qsTr("The most expensive part of a snapshot.")
                                settingKey: "system.disks"
                                SettingsSwitch {}
                            }
                        }

                        SettingsSection {
                            title: qsTr("Caching")
                            subtitle: qsTr("Improve performance by caching frequently used data.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Icon cache")
                                settingKey: "icons.cache"
                                SettingsSwitch {}
                            }
                        }

                        SettingsSection {
                            title: qsTr("Clipboard")
                            subtitle: qsTr("Clipboard history and management.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Enable clipboard history")
                                settingKey: "clipboard.historyEnabled"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("History size")
                                visible: Settings.values["clipboard.historyEnabled"] === true
                                settingKey: "clipboard.historySize"
                                SettingsStepper { from: 10; to: 1000; step: 10 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Persist history across restarts")
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
                            title: qsTr("Popups")
                            subtitle: qsTr("The built-in daemon serves org.freedesktop.Notifications, so notify-send and every application reach the shell. newteller-ctl notify works through the private socket.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Notification popups")
                                settingKey: "notifications.enabled"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Do not disturb")
                                subtitle: qsTr("Popups stay hidden; critical notifications still come through.")
                                settingKey: "notifications.doNotDisturb"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Corner")
                                settingKey: "notifications.position"
                                SettingsChoice {
                                    model: [
                                        { value: "top-left", label: qsTr("Top left") },
                                        { value: "top-center", label: qsTr("Top center") },
                                        { value: "top-right", label: qsTr("Top right") },
                                        { value: "bottom-left", label: qsTr("Bottom left") },
                                        { value: "bottom-center", label: qsTr("Bottom center") },
                                        { value: "bottom-right", label: qsTr("Bottom right") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Duration")
                                subtitle: qsTr("0 keeps a popup until it is clicked.")
                                settingKey: "notifications.duration"
                                SettingsStepper { from: 0; to: 120; suffix: qsTr(" s") }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("History length")
                                settingKey: "notifications.historyLimit"
                                SettingsStepper { from: 5; to: 200; step: 5 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Try it")
                                TaskButton {
                                    iconName: "bell-ring"
                                    label: qsTr("Send a test notification")
                                    onClicked: Shell.notify("NewTeller",
                                                            qsTr("This is what a notification looks like."))
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Appearance")
                            subtitle: qsTr("Customize the look of notifications.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Notification theme")
                                settingKey: "notifications.theme"
                                SettingsChoice {
                                    model: [
                                        { value: "system", label: qsTr("System theme") },
                                        { value: "light", label: qsTr("Light") },
                                        { value: "dark", label: qsTr("Dark") },
                                        { value: "custom", label: qsTr("Custom colors") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Notification surface")
                                subtitle: qsTr("Used by popups and the notification centre.")
                                settingKey: "notifications.material"
                                SettingsChoice {
                                    model: [
                                        { value: "theme", label: qsTr("Theme") },
                                        { value: "opaque", label: qsTr("Opaque") },
                                        { value: "translucent", label: qsTr("Half transparent") },
                                        { value: "frosted", label: qsTr("Frosted glass") },
                                        { value: "liquid", label: qsTr("Liquid glass") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Surface opacity")
                                subtitle: qsTr("0 uses the selected material's preset opacity.")
                                settingKey: "notifications.opacityPercent"
                                SettingsSlider { from: 0; to: 100; step: 5; suffix: " %" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Corner radius")
                                settingKey: "notifications.cornerRadius"
                                SettingsStepper { from: -1; to: 32; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Custom background")
                                visible: Settings.values["notifications.theme"] === "custom"
                                settingKey: "notifications.customBackground"
                                SettingsColorField {
                                    id: notifBgColorField
                                    onEdited: (hex) => Settings.setValue("notifications.customBackground", hex)
                                    onPaletteRequested: settingsWindow.openPalette(notifBgColorField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Custom foreground")
                                visible: Settings.values["notifications.theme"] === "custom"
                                settingKey: "notifications.customForeground"
                                SettingsColorField {
                                    id: notifFgColorField
                                    onEdited: (hex) => Settings.setValue("notifications.customForeground", hex)
                                    onPaletteRequested: settingsWindow.openPalette(notifFgColorField)
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Group by application")
                                subtitle: qsTr("In the notification centre; off lists every notification by time.")
                                settingKey: "notifications.groupByApp"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Show app icon")
                                settingKey: "notifications.showAppIcon"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Announce removable drives")
                                subtitle: qsTr("A USB stick or memory card that is plugged in gets Open and Eject buttons.")
                                settingKey: "drives.notify"
                                SettingsSwitch {}
                            }
                        }

                        SettingsSection {
                            title: qsTr("Sounds")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Enable notification sounds")
                                settingKey: "notifications.soundEnabled"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Quiet while a window is full screen")
                                subtitle: qsTr("Games, videos and presentations are not interrupted; notifications wait in the history. Urgent ones still show.")
                                settingKey: "notifications.quietInFullscreen"
                                SettingsSwitch { settingKey: "notifications.quietInFullscreen" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Play the bell when an application rings it")
                                subtitle: qsTr("Terminals and other applications (not under do-not-disturb).")
                                settingKey: "sounds.bell"
                                SettingsSwitch { settingKey: "sounds.bell" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Sound file")
                                visible: Settings.values["notifications.soundEnabled"] === true
                                settingKey: "notifications.soundFile"
                                SettingsTextField { width: 260; placeholder: qsTr("Leave empty for default") }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Do Not Disturb scheduling")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Enable scheduled DND")
                                settingKey: "notifications.doNotDisturbSchedule"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Start time")
                                visible: Settings.values["notifications.doNotDisturbSchedule"] === true
                                settingKey: "notifications.dndStartTime"
                                SettingsTextField { width: 120; placeholder: qsTr("HH:mm (24h)") }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("End time")
                                visible: Settings.values["notifications.doNotDisturbSchedule"] === true
                                settingKey: "notifications.dndEndTime"
                                SettingsTextField { width: 120; placeholder: qsTr("HH:mm (24h)") }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Daemon")
                            subtitle: qsTr("When another notification daemon owns the session, NewTeller only shows the notifications that arrive on its own socket.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("D-Bus service")
                                Text {
                                    color: Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pointSize: Theme.fontSize
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: Notifications.ownsDBus
                                          ? qsTr("This shell serves org.freedesktop.Notifications")
                                          : qsTr("Another daemon owns org.freedesktop.Notifications")
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Recent notifications")
                                TaskButton {
                                    iconName: "eraser"
                                    label: qsTr("Clear history")
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
                            title: qsTr("Plugins")
                            subtitle: qsTr("QML widgets from ~/.config/newteller/plugins and /usr/share/newteller/plugins.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Rescan on change")
                                settingKey: "plugins.autoRescan"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Show plugin descriptions")
                                settingKey: "plugins.showDescriptions"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Rescan")
                                TaskButton {
                                    iconName: "rotate-cw"
                                    label: qsTr("Rescan now")
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
                            title: qsTr("Plugin marketplace")
                            subtitle: qsTr("Online plugin discovery (experimental).")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Enable online plugins")
                                settingKey: "plugins.onlineEnabled"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Browse plugins")
                                TaskButton {
                                    iconName: "globe"
                                    label: qsTr("Open plugin marketplace")
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
                            title: qsTr("Keyboard shortcuts")
                            subtitle: qsTr("Click a shortcut and press the new keys; the cross turns it off. Your own commands are added under Windows -> Your shortcuts and take priority.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Built-in shortcuts")
                                subtitle: qsTr("Off: only the shortcuts you changed below keep working.")
                                settingKey: "hotkeys.enabled"
                                SettingsSwitch {}
                            }

                            Repeater {
                                model: [
                                    { key: "hotkeys.startMenu", title: qsTr("Open start menu"), keys: qsTr("Super (tap)") },
                                    { key: "hotkeys.runDialog", title: qsTr("Run a command"), keys: "Alt+F2, Super+R" },
                                    { key: "hotkeys.terminal", title: qsTr("Open terminal"), keys: "Super+Return" },
                                    { key: "hotkeys.fileManager", title: qsTr("Open file manager"), keys: "Super+E" },
                                    { key: "hotkeys.settings", title: qsTr("Open settings"), keys: "Super+S, Super+I" },
                                    { key: "hotkeys.quickSettings", title: qsTr("Open quick settings"), keys: "Super+A" },
                                    { key: "hotkeys.notificationCenter", title: qsTr("Open notification centre"), keys: "Super+N" },
                                    { key: "hotkeys.clipboard", title: qsTr("Clipboard history"), keys: "Super+V" },
                                    { key: "hotkeys.lockScreen", title: qsTr("Lock now"), keys: "Super+L" },
                                    { key: "hotkeys.closeWindow", title: qsTr("Close the window"), keys: "Super+Q, Alt+F4" },
                                    { key: "hotkeys.maximize", title: qsTr("Maximise or restore the window"), keys: "Super+M" },
                                    { key: "hotkeys.minimize", title: qsTr("Minimise the window"), keys: qsTr("not set") },
                                    { key: "hotkeys.fullscreen", title: qsTr("Fullscreen"), keys: "Super+F" },
                                    { key: "hotkeys.alwaysOnTop", title: qsTr("Keep the window on top"), keys: qsTr("not set") },
                                    { key: "hotkeys.showDesktop", title: qsTr("Show the desktop"), keys: "Super+D" },
                                    { key: "hotkeys.overview", title: qsTr("Overview of the windows"), keys: "Super+Tab" },
                                    { key: "hotkeys.windowMenu", title: qsTr("Window menu (always on top, move to a desktop)"), keys: "Alt+Space" },
                                    { key: "hotkeys.nextLayout", title: qsTr("Next keyboard layout"), keys: "Super+Space" },
                                    { key: "hotkeys.screenshot", title: qsTr("Screenshot"), keys: "Print" },
                                    { key: "hotkeys.screenshotArea", title: qsTr("Screenshot of an area"), keys: "Shift+Print, Super+Shift+S" },
                                    { key: "hotkeys.screenshotWindow", title: qsTr("Screenshot of the window"), keys: "Alt+Print" },
                                    { key: "hotkeys.screenRecording", title: qsTr("Record the screen"), keys: "Super+Alt+R" },
                                    { key: "hotkeys.forceQuit", title: qsTr("End a frozen application"), keys: "Ctrl+Shift+Esc" },
                                    { key: "hotkeys.logout", title: qsTr("Log out"), keys: qsTr("not set") },
                                    { key: "hotkeys.sleep", title: qsTr("Sleep"), keys: qsTr("not set") },
                                    { key: "hotkeys.reboot", title: qsTr("Restart"), keys: qsTr("not set") },
                                    { key: "hotkeys.shutdown", title: qsTr("Shut down"), keys: qsTr("not set") }
                                ]

                                delegate: SettingsRow {
                                    required property var modelData
                                    width: parent ? parent.width : 320
                                    title: modelData.title
                                    settingKey: modelData.key
                                    SettingsKeyField {
                                        objectName: "hotkeyField_" + modelData.key
                                        defaultKeys: modelData.keys
                                    }
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Fixed shortcuts")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Snap to the left or right half")
                                subtitle: "Super+Left, Super+Right"
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Maximise and restore")
                                subtitle: "Super+Up, Super+Down"
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Switch windows")
                                subtitle: "Alt+Tab"
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Windows of the same application")
                                subtitle: "Alt+`"
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Snap to a quarter")
                                subtitle: qsTr("Super+Left/Right, then Super+Up/Down")
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Applications on the dock")
                                subtitle: "Super+1 … Super+9"
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Switch desktops")
                                subtitle: "Ctrl+Super+Left/Right, Ctrl+Super+1…9, Super+Page Up/Down; Super+Shift+1…9"
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Move the window to another screen")
                                subtitle: "Super+Shift+Left, Super+Shift+Right"
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Give the shortcuts back to the desktop")
                                subtitle: "Super+Escape"
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Text console")
                                subtitle: "Ctrl+Alt+F1 ... F12"
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
                            title: qsTr("Desktop effects")
                            subtitle: qsTr("Drawn by the NewTeller compositor: rounded window corners, blur behind see-through panels and menus, and window animations.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Effects")
                                subtitle: qsTr("Off: the plain, fastest drawing path.")
                                settingKey: "effects.enabled"
                                SettingsSwitch { settingKey: "effects.enabled" }
                            }
                            SettingsRow {
                                width: parent.width
                                visible: Settings.values["effects.enabled"] !== false
                                title: qsTr("Window corner radius")
                                subtitle: qsTr("0 = square corners.")
                                settingKey: "effects.cornerRadius"
                                SettingsSlider { settingKey: "effects.cornerRadius"; from: 0; to: 24; step: 1; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                visible: Settings.values["effects.enabled"] !== false
                                title: qsTr("Blur behind panels and menus")
                                subtitle: qsTr("Applies to frosted, liquid and translucent materials (Appearance).")
                                settingKey: "effects.blur"
                                SettingsSwitch { settingKey: "effects.blur" }
                            }
                            SettingsRow {
                                width: parent.width
                                visible: Settings.values["effects.enabled"] !== false
                                         && Settings.values["effects.blur"] !== false
                                title: qsTr("Blur strength")
                                settingKey: "effects.blurStrength"
                                SettingsSlider { settingKey: "effects.blurStrength"; from: 1; to: 4; step: 1 }
                            }
                            SettingsRow {
                                width: parent.width
                                visible: Settings.values["effects.enabled"] !== false
                                title: qsTr("Window animations")
                                subtitle: qsTr("Open, close, minimize and desktop switching.")
                                settingKey: "effects.animations"
                                SettingsSwitch { settingKey: "effects.animations" }
                            }
                            SettingsRow {
                                width: parent.width
                                visible: Settings.values["effects.enabled"] !== false
                                         && Settings.values["effects.animations"] !== false
                                title: qsTr("Animation speed")
                                subtitle: qsTr("1.0 = normal, 2.0 = twice as fast.")
                                settingKey: "effects.animationSpeed"
                                SettingsSlider { settingKey: "effects.animationSpeed"; from: 0.5; to: 3.0; step: 0.1 }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Animation controls")
                            subtitle: qsTr("Enable or disable animations and adjust their speed.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Enable animations globally")
                                subtitle: qsTr("Menus, cards, the dock and the panel. Window animations are above.")
                                settingKey: "animations.enabled"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Animation speed multiplier")
                                subtitle: qsTr("1.0 = normal speed, 0.5 = half speed, 2.0 = double speed")
                                settingKey: "animations.speed"
                                SettingsSlider { from: 0.1; to: 3.0; step: 0.1; onChanged: (v) => Settings.setValue("animations.speed", Math.round(v * 10) / 10) }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Preview")
                            subtitle: qsTr("Test animation speed with this demo")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Animation demo")
                                TaskButton {
                                    iconName: "play"
                                    label: qsTr("Test animation")
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

            // ---- Windows ----------------------------------------------------
            Item {
                Flickable {
                    id: windowsScroll
                    anchors.fill: parent
                    contentHeight: windowsColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: windowsColumn
                        x: 16
                        y: 12
                        width: windowsScroll.width - 32
                        spacing: 16

                        readonly property var shortcuts: {
                            const v = Settings.values["shortcuts.custom"];
                            return v && v.length !== undefined ? v : [];
                        }
                        function setShortcuts(list) {
                            Settings.setValue("shortcuts.custom", list);
                        }

                        SettingsSection {
                            title: qsTr("Title bars")
                            subtitle: qsTr("Drawn by NewTeller for applications that ask for them (Qt and KDE apps, X11 programs, terminals). GTK and GNOME apps draw their own.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Title bars for apps without a preference")
                                subtitle: qsTr("Off: such apps draw their own frame.")
                                settingKey: "decorations.server"
                                SettingsSwitch { settingKey: "decorations.server" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Buttons")
                                settingKey: "decorations.buttonsLeft"
                                SettingsChoice {
                                    settingKey: "decorations.buttonsLeft"
                                    model: [
                                        { value: false, label: qsTr("Right (Windows, GNOME)") },
                                        { value: true, label: qsTr("Left (macOS)") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Minimize button")
                                settingKey: "decorations.showMinimize"
                                SettingsSwitch { settingKey: "decorations.showMinimize" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Maximize button")
                                settingKey: "decorations.showMaximize"
                                SettingsSwitch { settingKey: "decorations.showMaximize" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Centre the title")
                                settingKey: "decorations.titleCentered"
                                SettingsSwitch { settingKey: "decorations.titleCentered" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Title bar height")
                                settingKey: "decorations.titleHeight"
                                SettingsSlider { settingKey: "decorations.titleHeight"; from: 20; to: 48; step: 1; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Border width")
                                settingKey: "decorations.border"
                                SettingsStepper { settingKey: "decorations.border"; from: 0; to: 8; step: 1; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Title font")
                                subtitle: qsTr("Pango name, e.g. \"Inter Bold 10\". Empty: the theme font.")
                                settingKey: "decorations.font"
                                SettingsTextField { settingKey: "decorations.font"; placeholder: qsTr("(theme font)") }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Colours follow the theme")
                                settingKey: "decorations.followTheme"
                                SettingsSwitch { settingKey: "decorations.followTheme" }
                            }
                            Repeater {
                                model: [
                                    { key: "decorations.activeBackground", label: qsTr("Active title bar") },
                                    { key: "decorations.activeForeground", label: qsTr("Active title text") },
                                    { key: "decorations.inactiveBackground", label: qsTr("Inactive title bar") },
                                    { key: "decorations.inactiveForeground", label: qsTr("Inactive title text") },
                                    { key: "decorations.closeHover", label: qsTr("Close button highlight") }
                                ]
                                delegate: SettingsRow {
                                    required property var modelData
                                    width: parent ? parent.width : 320
                                    visible: modelData.key === "decorations.closeHover"
                                             || Settings.values["decorations.followTheme"] === false
                                    title: modelData.label
                                    settingKey: modelData.key
                                    Row {
                                        spacing: 8
                                        Rectangle {
                                            width: 26
                                            height: 26
                                            radius: 6
                                            anchors.verticalCenter: parent.verticalCenter
                                            color: Settings.values[modelData.key] || "transparent"
                                            border.width: 1
                                            border.color: Theme.border
                                        }
                                        SettingsTextField { width: 110; settingKey: modelData.key; placeholder: "#rrggbb" }
                                    }
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Behaviour")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Focus follows the mouse")
                                subtitle: qsTr("The window under the pointer gets the keyboard, without being raised.")
                                settingKey: "windows.focusFollowsMouse"
                                SettingsSwitch { settingKey: "windows.focusFollowsMouse" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("New windows open")
                                settingKey: "windows.placement"
                                SettingsChoice {
                                    settingKey: "windows.placement"
                                    model: [
                                        { value: "center", label: qsTr("Centred") },
                                        { value: "cascade", label: qsTr("Cascaded") },
                                        { value: "mouse", label: qsTr("Under the pointer") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Snap windows dragged to a screen edge")
                                settingKey: "windows.snapping"
                                SettingsSwitch { settingKey: "windows.snapping" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Alt+Tab shows windows of every desktop")
                                settingKey: "switcher.allWorkspaces"
                                SettingsSwitch { settingKey: "switcher.allWorkspaces" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Desktop switching wraps around")
                                settingKey: "workspaces.wrap"
                                SettingsSwitch { settingKey: "workspaces.wrap" }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Hot corners")
                            subtitle: qsTr("Push the pointer into a corner of the screen to run an action.")

                            Repeater {
                                model: [
                                    { key: "hotCorners.topLeft", label: qsTr("Top left") },
                                    { key: "hotCorners.topRight", label: qsTr("Top right") },
                                    { key: "hotCorners.bottomLeft", label: qsTr("Bottom left") },
                                    { key: "hotCorners.bottomRight", label: qsTr("Bottom right") }
                                ]
                                delegate: SettingsRow {
                                    required property var modelData
                                    width: parent ? parent.width : 320
                                    title: modelData.label
                                    settingKey: modelData.key
                                    SettingsChoice {
                                        settingKey: modelData.key
                                        model: [
                                            { value: "none", label: qsTr("Nothing") },
                                            { value: "overview", label: qsTr("Overview") },
                                            { value: "desktop", label: qsTr("Show desktop") },
                                            { value: "startMenu", label: qsTr("Start menu") },
                                            { value: "notifications", label: qsTr("Notifications") },
                                            { value: "lock", label: qsTr("Lock") }
                                        ]
                                    }
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Window rules")
                            subtitle: qsTr("Application ids, separated by commas (the overview and Ctrl+Shift+Esc show them). Any window can also be put on top from its window menu: right-click the title bar or press Alt+Space.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Always on top")
                                settingKey: "windowRules.alwaysOnTop"
                                SettingsTextField {
                                    width: 240
                                    settingKey: "windowRules.alwaysOnTop"
                                    placeholder: qsTr("e.g. org.gnome.Calculator")
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Open maximized")
                                settingKey: "windowRules.maximized"
                                SettingsTextField {
                                    width: 240
                                    settingKey: "windowRules.maximized"
                                    placeholder: qsTr("e.g. firefox, code")
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Open full screen")
                                settingKey: "windowRules.fullscreen"
                                SettingsTextField {
                                    width: 240
                                    settingKey: "windowRules.fullscreen"
                                    placeholder: qsTr("e.g. mpv")
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Open minimised")
                                subtitle: qsTr("Handy for chat applications started at login.")
                                settingKey: "windowRules.minimized"
                                SettingsTextField {
                                    width: 240
                                    settingKey: "windowRules.minimized"
                                    placeholder: qsTr("e.g. telegram-desktop")
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Open on a desktop")
                                subtitle: qsTr("Application id = desktop number, e.g. firefox=2, thunderbird=3.")
                                settingKey: "windowRules.workspaces"
                                SettingsTextField {
                                    width: 240
                                    settingKey: "windowRules.workspaces"
                                    placeholder: "firefox=2"
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Your shortcuts")
                            subtitle: qsTr("Run any command from a key combination, e.g. Super+T -> konsole, Ctrl+Alt+F -> firefox. They take priority over the built-in ones.")

                            Repeater {
                                model: windowsColumn.shortcuts
                                delegate: SettingsRow {
                                    required property var modelData
                                    required property int index
                                    width: parent ? parent.width : 320
                                    title: modelData.keys
                                    subtitle: modelData.command
                                    TaskButton {
                                        width: 28
                                        iconName: "trash"
                                        onClicked: {
                                            const list = windowsColumn.shortcuts.slice();
                                            list.splice(index, 1);
                                            windowsColumn.setShortcuts(list);
                                        }
                                    }
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Add a shortcut")
                                Row {
                                    spacing: 6
                                    SettingsTextField {
                                        id: shortcutKeys
                                        width: 140
                                        placeholder: qsTr("Super+T")
                                    }
                                    SettingsTextField {
                                        id: shortcutCommand
                                        width: 200
                                        placeholder: qsTr("command")
                                    }
                                    TaskButton {
                                        iconName: "plus"
                                        label: qsTr("Add")
                                        onClicked: {
                                            const keys = shortcutKeys.text.trim();
                                            const command = shortcutCommand.text.trim();
                                            if (keys.length === 0 || command.length === 0)
                                                return;
                                            const list = windowsColumn.shortcuts.slice();
                                            list.push({ keys: keys, command: command });
                                            windowsColumn.setShortcuts(list);
                                            shortcutKeys.text = "";
                                            shortcutCommand.text = "";
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---- Keyboard ---------------------------------------------------
            Item {
                Flickable {
                    id: keyboardScroll
                    anchors.fill: parent
                    contentHeight: keyboardColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: keyboardColumn
                        x: 16
                        y: 12
                        width: keyboardScroll.width - 32
                        spacing: 16

                        // The configured layouts, in switching order.
                        readonly property var layouts: {
                            const raw = Settings.values["keyboard.layouts"];
                            return (raw === undefined || raw === null ? "" : String(raw))
                                .split(",").map(function (x) { return x.trim(); })
                                .filter(function (x) { return x.length > 0; });
                        }
                        readonly property var commonLayouts: ["us", "ru", "ua", "by", "kz", "gb",
                            "de", "fr", "es", "it", "pl", "cz", "pt", "br", "tr", "gr", "il",
                            "ara", "ge", "am", "jp", "kr", "cn", "se", "fi", "no", "dk", "nl"]

                        function setLayouts(list) {
                            Settings.setValue("keyboard.layouts", list.join(","));
                        }
                        function addLayout(code) {
                            var list = layouts.slice();
                            if (list.length === 0)
                                list.push("us");
                            if (list.indexOf(code) < 0)
                                list.push(code);
                            setLayouts(list);
                        }
                        function removeLayout(code) {
                            setLayouts(layouts.filter(function (x) { return x !== code; }));
                        }
                        function moveFirst(code) {
                            setLayouts([code].concat(layouts.filter(function (x) { return x !== code; })));
                        }

                        SettingsSection {
                            title: qsTr("Input languages")
                            subtitle: keyboardColumn.layouts.length === 0
                                      ? qsTr("Following the system keyboard settings (/etc/default/keyboard). Add a layout to choose your own.")
                                      : qsTr("Super+Space or the switch keys below cycle through them; the panel shows the current one.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Your layouts")
                                subtitle: qsTr("Click a layout to make it the first one; × removes it.")
                                settingKey: "keyboard.layouts"

                                Flow {
                                    width: Math.min(320, keyboardColumn.width * 0.55)
                                    spacing: 6
                                    Repeater {
                                        model: keyboardColumn.layouts
                                        delegate: Row {
                                            required property var modelData
                                            required property int index
                                            spacing: 2
                                            TaskButton {
                                                label: Session.layoutDisplayName(modelData)
                                                active: index === 0
                                                onClicked: keyboardColumn.moveFirst(modelData)
                                            }
                                            TaskButton {
                                                width: 28
                                                iconName: "x"
                                                onClicked: keyboardColumn.removeLayout(modelData)
                                            }
                                        }
                                    }
                                    Text {
                                        visible: keyboardColumn.layouts.length === 0
                                        text: qsTr("System default")
                                        color: Theme.muted
                                        font.family: Theme.fontFamily
                                        font.pointSize: Theme.fontSize
                                    }
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Add a layout")

                                Flow {
                                    id: addLayoutFlow
                                    width: Math.min(360, keyboardColumn.width * 0.6)
                                    spacing: 4
                                    Repeater {
                                        model: keyboardColumn.commonLayouts.filter(function (code) {
                                            return keyboardColumn.layouts.indexOf(code) < 0;
                                        })
                                        delegate: TaskButton {
                                            required property var modelData
                                            label: Session.layoutDisplayName(modelData)
                                            onClicked: keyboardColumn.addLayout(modelData)
                                        }
                                    }
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Switch layouts with")
                                subtitle: qsTr("Super+Space always works as well.")
                                settingKey: "keyboard.switchKeys"
                                SettingsChoice {
                                    settingKey: "keyboard.switchKeys"
                                    model: [
                                        { value: "grp:alt_shift_toggle", label: qsTr("Alt+Shift") },
                                        { value: "grp:ctrl_shift_toggle", label: qsTr("Ctrl+Shift") },
                                        { value: "grp:caps_toggle", label: qsTr("Caps Lock") },
                                        { value: "", label: qsTr("Super+Space only") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Show the layout on the panel")
                                settingKey: "keyboard.showIndicator"
                                SettingsSwitch { settingKey: "keyboard.showIndicator" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Remember the layout per window")
                                subtitle: qsTr("Each window keeps the layout it was used with, as on Windows; new windows start with the first one.")
                                settingKey: "keyboard.layoutPerWindow"
                                SettingsSwitch { settingKey: "keyboard.layoutPerWindow" }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Typing")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Repeat delay")
                                subtitle: qsTr("How long a key is held before it repeats.")
                                settingKey: "keyboard.repeatDelay"
                                SettingsSlider { settingKey: "keyboard.repeatDelay"; from: 150; to: 1000; step: 25; suffix: qsTr(" ms") }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Repeat rate")
                                settingKey: "keyboard.repeatRate"
                                SettingsSlider { settingKey: "keyboard.repeatRate"; from: 5; to: 60; step: 1; suffix: qsTr(" /s") }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Num Lock on at login")
                                settingKey: "keyboard.numlock"
                                SettingsSwitch { settingKey: "keyboard.numlock" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Show Caps Lock and Num Lock on screen")
                                subtitle: qsTr("For keyboards without indicator lights.")
                                settingKey: "keyboard.lockKeysOsd"
                                SettingsSwitch { settingKey: "keyboard.lockKeysOsd" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Show the keyboard layout on screen when it changes")
                                settingKey: "keyboard.layoutOsd"
                                SettingsSwitch { settingKey: "keyboard.layoutOsd" }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Advanced")
                            subtitle: qsTr("XKB names, comma separated, in the same order as the layouts.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Variants")
                                subtitle: qsTr("For example \",phonetic\" for a phonetic second layout.")
                                settingKey: "keyboard.variants"
                                SettingsTextField { settingKey: "keyboard.variants"; placeholder: qsTr("(default)") }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Extra XKB options")
                                subtitle: qsTr("For example ctrl:nocaps or compose:ralt.")
                                settingKey: "keyboard.options"
                                SettingsTextField { settingKey: "keyboard.options"; placeholder: qsTr("(none)") }
                            }
                        }
                    }
                }
            }

            // ---- Mouse & touchpad -------------------------------------------
            Item {
                Flickable {
                    id: mouseScroll
                    anchors.fill: parent
                    contentHeight: mouseColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: mouseColumn
                        x: 16
                        y: 12
                        width: mouseScroll.width - 32
                        spacing: 16

                        SettingsSection {
                            title: qsTr("Touchpad")
                            subtitle: qsTr("Applied right away to every touchpad.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Tap to click")
                                subtitle: qsTr("One finger taps click, two fingers right-click.")
                                settingKey: "touchpad.tapToClick"
                                SettingsSwitch { settingKey: "touchpad.tapToClick" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Natural scrolling")
                                subtitle: qsTr("Content follows the fingers, like on a phone.")
                                settingKey: "touchpad.naturalScroll"
                                SettingsSwitch { settingKey: "touchpad.naturalScroll" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Disable while typing")
                                settingKey: "touchpad.disableWhileTyping"
                                SettingsSwitch { settingKey: "touchpad.disableWhileTyping" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Touchpad gestures")
                                subtitle: qsTr("Three fingers sideways move the active window; four fingers sideways switch desktops.")
                                settingKey: "touchpad.gestures"
                                SettingsSwitch { settingKey: "touchpad.gestures" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Pointer speed")
                                settingKey: "touchpad.speed"
                                SettingsSlider { settingKey: "touchpad.speed"; from: -1; to: 1; step: 0.1 }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Mouse")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Pointer speed")
                                settingKey: "mouse.speed"
                                SettingsSlider { settingKey: "mouse.speed"; from: -1; to: 1; step: 0.1 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Natural scrolling")
                                settingKey: "mouse.naturalScroll"
                                SettingsSwitch { settingKey: "mouse.naturalScroll" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Left-handed buttons")
                                settingKey: "mouse.leftHanded"
                                SettingsSwitch { settingKey: "mouse.leftHanded" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Double-click speed")
                                subtitle: qsTr("Longest pause between the two clicks; for the desktop and GTK applications.")
                                settingKey: "mouse.doubleClickInterval"
                                SettingsSlider { from: 200; to: 1000; step: 50; suffix: qsTr(" ms") }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Cursor")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Cursor size")
                                settingKey: "cursor.size"
                                SettingsStepper { settingKey: "cursor.size"; from: 16; to: 96; step: 8; suffix: " px" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Cursor theme")
                                subtitle: qsTr("An installed theme name, e.g. Adwaita or breeze_cursors. Empty = default.")
                                settingKey: "cursor.theme"
                                SettingsTextField { settingKey: "cursor.theme"; placeholder: qsTr("(default)") }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Installed cursor themes")
                                SettingsChoice {
                                    id: cursorFlow
                                    width: Math.min(420, mouseColumn.width * 0.6)
                                    settingKey: "cursor.theme"
                                    model: [{ value: "", label: qsTr("Default") }].concat(
                                        Session.cursorThemes().map(function (t) { return { value: t, label: t }; }))
                                }
                            }
                        }
                    }
                }
            }

            // ---- Displays & night light -------------------------------------
            Item {
                Flickable {
                    id: displayScroll
                    anchors.fill: parent
                    contentHeight: displayColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: displayColumn
                        x: 16
                        y: 12
                        width: displayScroll.width - 32
                        spacing: 16

                        // The monitors, read from the compositor when the page opens.
                        property var screens: []
                        property var previous: null
                        property int revertIn: 0
                        property string displayError: ""

                        function reloadScreens() {
                            screens = Session.outputs();
                        }
                        function logicalWidth(o) {
                            const w = (o.transform % 2 === 1 ? o.height : o.width) / (o.scale || 1);
                            return Math.round(w);
                        }
                        function logicalHeight(o) {
                            const h = (o.transform % 2 === 1 ? o.width : o.height) / (o.scale || 1);
                            return Math.round(h);
                        }
                        // Applies one change; risky ones (mode, scale, rotation) can be
                        // undone automatically if the user cannot see the screen anymore.
                        function apply(o, change, risky) {
                            const next = Object.assign({}, o, change);
                            const error = Session.setOutput(next.name, next.enabled, next.width, next.height,
                                                            next.refresh, next.x, next.y, next.scale,
                                                            next.transform);
                            displayError = error;
                            if (error.length === 0 && risky) {
                                previous = o;
                                revertIn = 15;
                                revertTimer.restart();
                            }
                            reloadScreens();
                        }
                        function keep() {
                            previous = null;
                            revertTimer.stop();
                        }
                        function revert() {
                            const o = previous;
                            keep();
                            if (o)
                                Session.setOutput(o.name, o.enabled, o.width, o.height, o.refresh,
                                                  o.x, o.y, o.scale, o.transform);
                            reloadScreens();
                        }
                        function resolutions(o) {
                            const seen = {};
                            const list = [];
                            for (var i = 0; i < o.modes.length; ++i) {
                                const key = o.modes[i].width + "x" + o.modes[i].height;
                                if (!seen[key]) {
                                    seen[key] = true;
                                    list.push({ width: o.modes[i].width, height: o.modes[i].height });
                                }
                            }
                            list.sort(function (a, b) { return b.width * b.height - a.width * a.height; });
                            return list;
                        }
                        function refreshRates(o) {
                            return o.modes.filter(function (m) {
                                return m.width === o.width && m.height === o.height;
                            });
                        }

                        Component.onCompleted: reloadScreens()
                        onVisibleChanged: if (visible) reloadScreens()

                        Timer {
                            id: revertTimer
                            interval: 1000
                            repeat: true
                            onTriggered: {
                                displayColumn.revertIn -= 1;
                                if (displayColumn.revertIn <= 0)
                                    displayColumn.revert();
                            }
                        }

                        // "Keep these settings?" while a risky change is on trial.
                        Rectangle {
                            width: parent.width
                            height: 52
                            visible: displayColumn.previous !== null
                            radius: Theme.radius
                            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 12
                                spacing: 10
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: qsTr("Keep these display settings? Reverting in %1 s").arg(displayColumn.revertIn)
                                    color: Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pointSize: Theme.fontSize
                                }
                                TaskButton { label: qsTr("Keep"); onClicked: displayColumn.keep() }
                                TaskButton { label: qsTr("Revert"); onClicked: displayColumn.revert() }
                            }
                        }
                        Text {
                            width: parent.width
                            visible: displayColumn.displayError.length > 0
                            text: displayColumn.displayError
                            color: Theme.accent
                            wrapMode: Text.WordWrap
                            font.family: Theme.fontFamily
                            font.pointSize: Theme.fontSize
                        }
                        Text {
                            width: parent.width
                            visible: displayColumn.screens.length === 0
                            text: qsTr("The screens are managed by the NewTeller session; this page needs it running.")
                            color: Theme.muted
                            wrapMode: Text.WordWrap
                            font.family: Theme.fontFamily
                            font.pointSize: Theme.fontSize
                        }

                        Repeater {
                            model: displayColumn.screens

                            delegate: SettingsSection {
                                id: screenSection
                                required property var modelData
                                width: displayColumn.width
                                title: (modelData.description.length > 0 ? modelData.description : modelData.name)
                                subtitle: modelData.name + " - " + modelData.width + " x " + modelData.height
                                          + " at " + Math.round(modelData.refresh / 1000) + " Hz, "
                                          + Math.round(modelData.scale * 100) + "%"

                                SettingsRow {
                                    width: parent.width
                                    title: qsTr("Use this screen")
                                    SettingsSwitch {
                                        checked: screenSection.modelData.enabled
                                        onChanged: (v) => displayColumn.apply(screenSection.modelData, { enabled: v }, true)
                                    }
                                }
                                SettingsRow {
                                    width: parent.width
                                    visible: screenSection.modelData.enabled
                                    title: qsTr("Resolution")
                                    Flow {
                                        id: resolutionFlow
                                        width: Math.min(420, displayColumn.width * 0.6)
                                        spacing: 4
                                        Repeater {
                                            model: displayColumn.resolutions(screenSection.modelData)
                                            delegate: TaskButton {
                                                required property var modelData
                                                label: modelData.width + " x " + modelData.height
                                                active: modelData.width === screenSection.modelData.width
                                                        && modelData.height === screenSection.modelData.height
                                                onClicked: displayColumn.apply(screenSection.modelData,
                                                                               { width: modelData.width, height: modelData.height, refresh: 0 },
                                                                               true)
                                            }
                                        }
                                    }
                                }
                                SettingsRow {
                                    width: parent.width
                                    visible: screenSection.modelData.enabled
                                             && displayColumn.refreshRates(screenSection.modelData).length > 1
                                    title: qsTr("Refresh rate")
                                    Flow {
                                        width: Math.min(420, displayColumn.width * 0.6)
                                        spacing: 4
                                        Repeater {
                                            model: displayColumn.refreshRates(screenSection.modelData)
                                            delegate: TaskButton {
                                                required property var modelData
                                                label: (modelData.refresh / 1000).toFixed(modelData.refresh % 1000 === 0 ? 0 : 2) + " Hz"
                                                active: Math.abs(modelData.refresh - screenSection.modelData.refresh) < 500
                                                onClicked: displayColumn.apply(screenSection.modelData,
                                                                               { refresh: modelData.refresh }, true)
                                            }
                                        }
                                    }
                                }
                                SettingsRow {
                                    width: parent.width
                                    visible: screenSection.modelData.enabled
                                    title: qsTr("Scale")
                                    subtitle: qsTr("Bigger text and controls on high-density screens.")
                                    SettingsChoice {
                                        value: Math.round(screenSection.modelData.scale * 100)
                                        model: [
                                            { value: 100, label: "100%" }, { value: 125, label: "125%" },
                                            { value: 150, label: "150%" }, { value: 175, label: "175%" },
                                            { value: 200, label: "200%" }, { value: 250, label: "250%" },
                                            { value: 300, label: "300%" }
                                        ]
                                        onChanged: (v) => displayColumn.apply(screenSection.modelData, { scale: v / 100 }, true)
                                    }
                                }
                                SettingsRow {
                                    width: parent.width
                                    visible: screenSection.modelData.enabled
                                    title: qsTr("Rotation")
                                    SettingsChoice {
                                        value: screenSection.modelData.transform
                                        model: [
                                            { value: 0, label: qsTr("Normal") }, { value: 1, label: "90°" },
                                            { value: 2, label: "180°" }, { value: 3, label: "270°" }
                                        ]
                                        onChanged: (v) => displayColumn.apply(screenSection.modelData, { transform: v }, true)
                                    }
                                }
                                SettingsRow {
                                    width: parent.width
                                    visible: screenSection.modelData.enabled && displayColumn.screens.length > 1
                                    title: qsTr("Position")
                                    subtitle: qsTr("Relative to the other screen.")
                                    SettingsChoice {
                                        readonly property var other: {
                                            for (var i = 0; i < displayColumn.screens.length; ++i) {
                                                const o = displayColumn.screens[i];
                                                if (o.name !== screenSection.modelData.name && o.enabled)
                                                    return o;
                                            }
                                            return null;
                                        }
                                        model: [
                                            { value: "left", label: qsTr("Left of it") }, { value: "right", label: qsTr("Right of it") },
                                            { value: "above", label: qsTr("Above") }, { value: "below", label: qsTr("Below") }
                                        ]
                                        onChanged: (v) => {
                                            const o = other;
                                            const me = screenSection.modelData;
                                            if (!o)
                                                return;
                                            var x = o.x, y = o.y;
                                            if (v === "left")
                                                x = o.x - displayColumn.logicalWidth(me);
                                            else if (v === "right")
                                                x = o.x + displayColumn.logicalWidth(o);
                                            else if (v === "above")
                                                y = o.y - displayColumn.logicalHeight(me);
                                            else
                                                y = o.y + displayColumn.logicalHeight(o);
                                            displayColumn.apply(me, { x: x, y: y }, false);
                                        }
                                    }
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("More")
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Advanced arrangement")
                                subtitle: Session.displaySettingsTool.length > 0
                                          ? qsTr("Drag screens freely in wdisplays.")
                                          : "wlr-randr and kanshi also work with NewTeller."
                                TaskButton {
                                    iconName: "monitor"
                                    label: "wdisplays"
                                    dimmed: Session.displaySettingsTool.length === 0
                                    onClicked: Session.openDisplaySettings()
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Desktops & switching")
                            subtitle: qsTr("Ctrl+Super+Left/Right (or Super+Page Up/Down) moves between desktops; add Shift to take the window along. Alt+Tab walks through the windows of the current desktop.")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Number of desktops")
                                settingKey: "workspaces.count"
                                SettingsStepper { settingKey: "workspaces.count"; from: 1; to: 9; step: 1 }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Show desktops on the panel")
                                settingKey: "workspaces.showOnPanel"
                                SettingsSwitch { settingKey: "workspaces.showOnPanel" }
                            }
                            SettingsRow {
                                width: parent.width
                                visible: Settings.values["workspaces.showOnPanel"] !== false
                                title: qsTr("Desktop indicator")
                                settingKey: "workspaces.indicator"
                                SettingsChoice {
                                    model: [
                                        { value: "dots", label: qsTr("Dots") },
                                        { value: "numbers", label: qsTr("Numbers") },
                                        { value: "names", label: qsTr("Names") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Desktop names")
                                subtitle: qsTr("Separated by commas, e.g. Work, Web, Chat; unnamed desktops are numbered.")
                                settingKey: "workspaces.names"
                                SettingsTextField { width: 240; placeholder: qsTr("Work, Web, Chat") }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Show the desktop name when switching")
                                settingKey: "workspaces.osd"
                                SettingsSwitch {}
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Show the window list during Alt+Tab")
                                settingKey: "switcher.showOverlay"
                                SettingsSwitch { settingKey: "switcher.showOverlay" }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Night light")
                            subtitle: Session.nightLightAvailable
                                      ? qsTr("Warmer colours in the evening are easier on the eyes.")
                                      : qsTr("Needs wlsunset (sudo apt install wlsunset).")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Night light")
                                settingKey: "nightLight.enabled"
                                SettingsSwitch { settingKey: "nightLight.enabled" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Colour temperature")
                                subtitle: qsTr("Lower is warmer.")
                                settingKey: "nightLight.temperature"
                                SettingsSlider { settingKey: "nightLight.temperature"; from: 2500; to: 5500; step: 100; suffix: " K" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("When")
                                settingKey: "nightLight.schedule"
                                SettingsChoice {
                                    settingKey: "nightLight.schedule"
                                    model: [
                                        { value: "custom", label: qsTr("From - to") },
                                        { value: "always", label: qsTr("Always on") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                visible: Settings.values["nightLight.schedule"] !== "always"
                                title: qsTr("Turn on at")
                                settingKey: "nightLight.from"
                                SettingsTextField { width: 90; settingKey: "nightLight.from"; placeholder: "20:00" }
                            }
                            SettingsRow {
                                width: parent.width
                                visible: Settings.values["nightLight.schedule"] !== "always"
                                title: qsTr("Turn off at")
                                settingKey: "nightLight.to"
                                SettingsTextField { width: 90; settingKey: "nightLight.to"; placeholder: "07:00" }
                            }
                        }
                    }
                }
            }

            // ---- Power & lock -----------------------------------------------
            Item {
                Flickable {
                    id: powerScroll
                    anchors.fill: parent
                    contentHeight: powerColumn.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: powerColumn
                        x: 16
                        y: 12
                        width: powerScroll.width - 32
                        spacing: 16

                        SettingsSection {
                            title: qsTr("Screen lock")
                            subtitle: Session.lockAvailable
                                      ? qsTr("Super+L locks the screen. Your password unlocks it.")
                                      : qsTr("The screen locker is not installed: install gtklock (or swaylock)")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Built-in administrator prompt")
                                subtitle: Polkit.registered
                                          ? qsTr("Active: NewTeller asks for the administrator password itself. Applies after logging in again.")
                                          : qsTr("Off or unavailable: %1 asks instead. Applies after logging in again.")
                                                .arg(Session.polkitAgent.length > 0 ? Session.polkitAgent
                                                                                    : qsTr("no polkit agent"))
                                settingKey: "security.builtinAgent"
                                SettingsSwitch { settingKey: "security.builtinAgent" }
                            }

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Lock after inactivity")
                                subtitle: qsTr("0 = never.")
                                settingKey: "power.lockAfter"
                                SettingsStepper { settingKey: "power.lockAfter"; from: 0; to: 240; step: 1; suffix: qsTr(" min") }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Lock before suspend")
                                settingKey: "power.lockOnSleep"
                                SettingsSwitch { settingKey: "power.lockOnSleep" }
                            }
                            SettingsRow {
                                width: parent.width
                                visible: Session.powerProfiles.length > 0
                                title: qsTr("Battery saver at 20%")
                                subtitle: qsTr("Switches to the power saver profile on battery and back when plugged in.")
                                settingKey: "power.batterySaver"
                                SettingsSwitch { settingKey: "power.batterySaver" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Wallpaper on the lock screen")
                                settingKey: "lock.useWallpaper"
                                SettingsSwitch { settingKey: "lock.useWallpaper" }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Lock screen colour")
                                subtitle: qsTr("Behind the wallpaper, or the whole background without one.")
                                settingKey: "lock.color"
                                Row {
                                    spacing: 8
                                    Rectangle {
                                        width: 26
                                        height: 26
                                        radius: 6
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: Settings.values["lock.color"] || "transparent"
                                        border.width: 1
                                        border.color: Theme.border
                                    }
                                    SettingsTextField { width: 110; settingKey: "lock.color"; placeholder: "#1e1e2e" }
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Lock now")
                                TaskButton {
                                    iconName: "lock"
                                    label: qsTr("Lock")
                                    onClicked: Shell.lock()
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Screen")
                            subtitle: Session.idleAvailable
                                      ? qsTr("Video players keep the screen on while they play.")
                                      : qsTr("Idle timers need swayidle (sudo apt install swayidle).")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Turn the screen off after")
                                subtitle: qsTr("0 = never. Any key or mouse movement wakes it.")
                                settingKey: "power.screenOffAfter"
                                SettingsStepper { settingKey: "power.screenOffAfter"; from: 0; to: 240; step: 1; suffix: qsTr(" min") }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Sleep")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Sleep means")
                                subtitle: qsTr("Used by the Sleep button and the idle timer. Hibernation needs a swap partition at least as large as the memory.")
                                settingKey: "power.sleepAction"
                                SettingsChoice {
                                    model: [
                                        { value: "suspend", label: qsTr("Suspend") },
                                        { value: "hibernate", label: qsTr("Hibernate") },
                                        { value: "hybrid-sleep", label: qsTr("Hybrid sleep") },
                                        { value: "suspend-then-hibernate", label: qsTr("Suspend, then hibernate") }
                                    ]
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Sleep after inactivity")
                                subtitle: qsTr("0 = never.")
                                settingKey: "power.suspendAfter"
                                SettingsStepper { settingKey: "power.suspendAfter"; from: 0; to: 480; step: 5; suffix: qsTr(" min") }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Administrator prompts")
                            subtitle: Session.polkitAgent.length > 0
                                      ? qsTr("Authentication agent: %1").arg(Session.polkitAgent)
                                      : qsTr("No polkit agent found; install lxpolkit so programs can ask for the administrator password.")
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
                            title: qsTr("This system")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Version")
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
                                title: qsTr("Computer")
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
                                title: qsTr("Session")
                                Text {
                                    text: qsTr("%1 as %2").arg(Shell.isWayland ? "Wayland" : "X11").arg(Shell.userName)
                                    color: Theme.foreground
                                    font.family: Theme.fontFamily
                                    font.pointSize: Theme.fontSize
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Window tracking")
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
                                title: qsTr("Applications")
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
                                title: qsTr("Memory and uptime")
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
                            title: qsTr("Files")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Configuration")
                                subtitle: Settings.configPath()

                                TaskButton {
                                    iconName: "file-code"
                                    label: qsTr("Open JSON")
                                    onClicked: Shell.openConfig()
                                }
                            }
                            SettingsRow {
                                width: parent.width
                                title: qsTr("Export")
                                subtitle: qsTr("Writes a JSON copy of the current settings next to your home folder.")
                                TaskButton {
                                    iconName: "download"
                                    label: qsTr("Export a copy")
                                    onClicked: settingsWindow.exportSettings()
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTr("Configuration problems")
                            subtitle: qsTr("Reported by the same validator as newteller-ctl validate.")
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
                            title: qsTr("Danger zone")

                            SettingsRow {
                                width: parent.width
                                title: qsTr("Reset everything")
                                subtitle: qsTr("Every key of %1 returns to its default. Themes on disk are kept.").arg(Settings.configPath())
                                TaskButton {
                                    iconName: "rotate-ccw"
                                    label: qsTr("Reset all settings")
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
                text: Settings.dirty ? qsTr("Saving…") : qsTr("All changes saved")
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Settings apply live — no restart needed")
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
    // The folder the file-manager dialog should start in. Always a real
    // directory: the wallpaper may be a URL, a path in a folder that was
    // deleted since, or empty (solid colour).
    function wallpaperFolder() {
        const wp = Settings.value("desktop.wallpaper", "") || "";
        let dir = "";
        if (typeof wp === "string" && wp.length > 0) {
            let path = wp;
            if (path.indexOf("file:") === 0) {
                try { path = decodeURIComponent(path.substring(7)); } catch (error) { /* keep */ }
            }
            const slash = path.lastIndexOf("/");
            if (slash > 0)
                dir = path.substring(0, slash);
        }
        return Shell.folderPath(dir.length > 0 ? dir : (Shell.picturesPath));
    }

    // Never store anything the desktop cannot render as a wallpaper: the
    // portal and the integrated picker both can return a path the shell was
    // not told to expect (a folder, a deleted file, a non-image).
    function applyWallpaper(path) {
        if (typeof path !== "string")
            return false;
        const file = path.trim();
        if (file.length === 0)
            return false;                 // dismissed: keep the current one
        if (!Shell.isImageFile(file))
            return false;
        Settings.setValue("desktop.wallpaper", file);
        return true;
    }

    function pickWallpaper(startDir) {
        const dir = Shell.folderPath(startDir === undefined || startDir === null ? "" : startDir);
        filePortalDir = dir;
        if (FilePortal.available) {
            FilePortal.pickFile(qsTr("Choose a wallpaper"), dir, true);
        } else {
            filePicker.openFor(dir, true);
        }
    }

    property string filePortalDir: ""

    Connections {
        target: FilePortal
        function onFilePicked(path) {
            settingsWindow.applyWallpaper(path);
        }
        function onPickFailed() {
            // The portal is installed but unusable (not running, no permission,
            // no answer). Fall back to the integrated picker exactly once.
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
        onAccepted: (path) => settingsWindow.applyWallpaper(path)
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
                        label: index === 0 ? qsTr("Pictures") : qsTr("System backgrounds")
                        active: settingsWindow.wallpaperFolderIndex === index
                        onClicked: settingsWindow.wallpaperFolderIndex = index
                    }
                }
                Item { width: 12; height: 1 }
                TaskButton {
                    iconName: "shuffle"
                    label: qsTr("Random")
                    onClicked: wallpaperOverlay.pickRandom()
                }
                TaskButton {
                    iconName: "x"
                    label: qsTr("Close")
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
                            // Decode at thumbnail size: a 50-megapixel photo
                            // per cell would need gigabytes of textures and
                            // can exceed the driver's maximum texture size.
                            sourceSize.width: 400
                            sourceSize.height: 260
                            source: "file://" + filePath.split("/").map(p => encodeURIComponent(p)).join("/")
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
                    text: qsTr("No pictures in this folder")
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
                    text: qsTr("Reset all settings?")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize + 3
                    font.bold: true
                }
                Text {
                    width: parent.width
                    text: qsTr("Every key of shell.json returns to its default. This cannot be undone.")
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                    wrapMode: Text.Wrap
                }
                Row {
                    spacing: 8
                    TaskButton {
                        iconName: "check"
                        label: qsTr("Reset")
                        onClicked: settingsWindow.resetAllSettings()
                    }
                    TaskButton {
                        iconName: "x"
                        label: qsTr("Cancel")
                        onClicked: confirmBox.visible = false
                    }
                }
            }
        }
    }
}
