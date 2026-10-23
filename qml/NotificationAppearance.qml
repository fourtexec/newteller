import QtQuick

// Shared colours and material settings for notification popups and the
// notification centre. `system` follows the selected shell theme; light, dark
// and custom are local overrides and do not alter the rest of the shell.
QtObject {
    readonly property string themeMode: Settings.values["notifications.theme"] || "system"
    readonly property string material: Settings.values["notifications.material"] || "theme"
    readonly property int opacityPercent: Number(Settings.values["notifications.opacityPercent"] ?? 0)
    readonly property int cornerRadius: Number(Settings.values["notifications.cornerRadius"] ?? -1)

    readonly property color background: {
        if (themeMode === "light") return Qt.color("#f5f7fb");
        if (themeMode === "dark") return Qt.color("#191c23");
        if (themeMode === "custom") {
            const value = Settings.values["notifications.customBackground"] || "";
            return value.length > 0 ? Qt.color(value) : Theme.background;
        }
        return Theme.background;
    }

    readonly property color foreground: {
        if (themeMode === "light") return Qt.color("#20232b");
        if (themeMode === "dark") return Qt.color("#f4f6fa");
        if (themeMode === "custom") {
            const value = Settings.values["notifications.customForeground"] || "";
            return value.length > 0 ? Qt.color(value) : Theme.foreground;
        }
        return Theme.foreground;
    }

    readonly property color accent: Theme.accent
    readonly property color muted: Theme.mix(foreground, background, 0.44)
    readonly property color secondary: Theme.mix(foreground, background, 0.18)
}
