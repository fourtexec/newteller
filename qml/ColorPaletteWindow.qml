import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import org.kde.layershell 1.0 as LayerShell

// The colour palette of the settings: preset swatches, a hue bar and a
// saturation/value square, opened from any colour field.
//
// It opens as a panel on the LEFT EDGE of the screen - a full-height sidebar
// next to the content - not as a floating window. On Wayland it is a layer
// surface anchored to the left edge; on X11/XWayland it is a frameless,
// always-on-top window pinned to the left edge.
Window {
    id: picker

    signal applied(color chosen)

    property color current: "#000000"
    // HSV of `current`, kept in sync while picking.
    property real hue: 0
    property real sat: 0
    property real val: 0

    readonly property int panelWidth: 340
    readonly property int screenMargin: 8

    width: panelWidth
    height: Screen.height
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint

    // Pinned to the left edge without layer-shell (X11, XWayland).
    x: Screen.virtualX + screenMargin
    y: Screen.virtualY

    // Wayland: a layer surface along the left edge, above the windows.
    LayerShell.Window.anchors: LayerShell.Window.AnchorLeft
                               | LayerShell.Window.AnchorTop
                               | LayerShell.Window.AnchorBottom
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.exclusionZone: 0
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityOnDemand
    LayerShell.Window.scope: "newteller-palette"
    LayerShell.Window.margins {
        left: picker.screenMargin
        top: 0
        right: 0
        bottom: 0
    }

    function openFor(colorValue) {
        current = colorValue;
        const parts = rgbToHsv(colorValue.r, colorValue.g, colorValue.b);
        hue = parts[0];
        sat = parts[1];
        val = parts[2];
        visible = true;
        requestActivate();
    }

    // Note: existing callers pass an anchor item and offsets from the old
    // floating layout; JavaScript ignores surplus arguments, so openFor
    // keeps a single parameter.

    function rgbToHsv(r, g, b) {
        const max = Math.max(r, g, b), min = Math.min(r, g, b);
        const d = max - min;
        let h = 0;
        if (d > 0) {
            if (max === r)
                h = 60 * (((g - b) / d) % 6);
            else if (max === g)
                h = 60 * ((b - r) / d + 2);
            else
                h = 60 * ((r - g) / d + 4);
        }
        if (h < 0)
            h += 360;
        return [h, max <= 0 ? 0 : d / max, max];
    }

    function hsvToHex(h, s, v) {
        const c = v * s;
        const hp = (h % 360) / 60;
        const x = c * (1 - Math.abs(hp % 2 - 1));
        var r = 0, g = 0, b = 0;
        if (hp < 1) { r = c; g = x; }
        else if (hp < 2) { r = x; g = c; }
        else if (hp < 3) { g = c; b = x; }
        else if (hp < 4) { g = x; b = c; }
        else if (hp < 5) { r = x; b = c; }
        else { r = c; b = x; }
        const m = v - c;
        const hex = f => ("0" + Math.round((f + m) * 255).toString(16)).slice(-2);
        return "#" + hex(r) + hex(g) + hex(b);
    }

    function pick(h, s, v) {
        hue = h; sat = s; val = v;
        current = Qt.color(hsvToHex(h, s, v));
    }

    onActiveChanged: {
        if (!active)
            visible = false;
    }

    Shortcut {
        sequence: "Esc"
        enabled: picker.visible
        onActivated: picker.visible = false
    }

    ShellFrame {
        id: paletteCard
        anchors.fill: parent

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 12

            // --- title row (the shell's own window chrome) -----------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: "Colour palette"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize + 1
                    font.bold: true
                }
                TaskButton {
                    width: 28
                    height: 28
                    iconName: "x"
                    onClicked: picker.visible = false
                }
            }

            // --- current colour -------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Rectangle {
                    width: 44
                    height: 44
                    radius: Theme.radius
                    color: picker.current
                    border.width: 1
                    border.color: Theme.border
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: picker.current.toString().toUpperCase()
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize
                        font.bold: true
                    }
                    Text {
                        text: "Picked colour"
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 2
                    }
                }
            }

            // Saturation (x) and value (y) square of the current hue.
            Rectangle {
                id: svSquare
                Layout.fillWidth: true
                Layout.preferredHeight: 170
                radius: Theme.radius
                border.width: 1
                border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.9)

                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: Qt.hsla(picker.hue / 360, 1, 0.5, 1)
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "#ffffffff" }
                        GradientStop { position: 1; color: "#00ffffff" }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#00000000" }
                        GradientStop { position: 1; color: "#ff000000" }
                    }
                }
                Rectangle {
                    x: parent.width * picker.sat - 6
                    y: parent.height * (1 - picker.val) - 6
                    width: 12
                    height: 12
                    radius: 6
                    color: "transparent"
                    border.width: 2
                    border.color: "#ffffff"
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.CrossCursor
                    onClicked: (mouse) => picker.pick(picker.hue,
                                                       mouse.x / width,
                                                       1 - mouse.y / height)
                    onPositionChanged: (mouse) => {
                        if (pressed)
                            picker.pick(picker.hue, mouse.x / width, 1 - mouse.y / height);
                    }
                }
            }

            // Hue bar.
            Rectangle {
                id: hueBar
                Layout.fillWidth: true
                Layout.preferredHeight: 16
                radius: 8
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.000; color: "#ff0000" }
                    GradientStop { position: 0.167; color: "#ffff00" }
                    GradientStop { position: 0.333; color: "#00ff00" }
                    GradientStop { position: 0.500; color: "#00ffff" }
                    GradientStop { position: 0.667; color: "#0000ff" }
                    GradientStop { position: 0.833; color: "#ff00ff" }
                    GradientStop { position: 1.000; color: "#ff0000" }
                }
                Rectangle {
                    x: parent.width * (picker.hue / 360) - 3
                    width: 6
                    height: parent.height
                    radius: 3
                    color: "transparent"
                    border.width: 2
                    border.color: "#ffffff"
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: (mouse) => picker.pick(Math.round(mouse.x / width * 360),
                                                       picker.sat, picker.val)
                    onPositionChanged: (mouse) => {
                        if (pressed)
                            picker.pick(Math.round(mouse.x / width * 360),
                                         picker.sat, picker.val);
                    }
                }
            }

            Text {
                text: "Presets"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 2
            }

            // Curated preset swatches; the tall panel shows them all.
            Flickable {
                id: presetScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: presetFlow.height
                boundsBehavior: Flickable.StopAtBounds

                Flow {
                    id: presetFlow
                    width: presetScroll.width
                    spacing: 6

                    Repeater {
                        model: [
                            "#000000", "#3a3f4b", "#6b7280", "#b0b6c0", "#ffffff",
                            "#8b0000", "#e5484d", "#f76b6b", "#ff9d68", "#f5c518",
                            "#ffd166", "#a3e635", "#46a758", "#0f7b3f", "#00c2a8",
                            "#2ea8a0", "#38bdf8", "#1e6fd9", "#1d3fbf", "#5b21b6",
                            "#8b5cf6", "#d946ef", "#f472b6", "#8a5a44", "#5b4636",
                            "#1e2127", "#2a2e36", "#40485a", "#c9a227", "#d4d4d4"
                        ]

                        delegate: Rectangle {
                            required property var modelData
                            width: 30
                            height: 30
                            radius: 8
                            color: modelData
                            border.width: picker.current.toString().toUpperCase()
                                          === modelData.toUpperCase() ? 2 : 1
                            border.color: border.width === 2 ? Theme.foreground
                                                             : Qt.rgba(Theme.border.r, Theme.border.g,
                                                                       Theme.border.b, 0.8)

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    const parts = picker.rgbToHsv(parent.color.r, parent.color.g,
                                                                   parent.color.b);
                                    picker.pick(parts[0], parts[1], parts[2]);
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                TaskButton {
                    iconName: "check"
                    label: "Apply"
                    active: true
                    onClicked: {
                        picker.applied(picker.current);
                        picker.visible = false;
                    }
                }
                TaskButton {
                    iconName: "x"
                    label: "Cancel"
                    onClicked: picker.visible = false
                }
            }
        }
    }
}
