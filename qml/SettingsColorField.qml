import QtQuick

// Hex colour field: a swatch and a text input, used by the theme editor and
// the window style settings. Clicking the swatch (or the palette button) asks
// the host window to open the colour palette — SettingsApp connects
// `paletteRequested` to its ColorPaletteWindow.
Row {
    id: control

    property color colorValue: "#000000"
    signal edited(string hex)
    signal paletteRequested()

    spacing: 6

    // Sets both the swatch and the text; used when another theme is loaded.
    function setValue(c) {
        colorValue = c;
        input.text = c.toString().toUpperCase();
    }

    // The swatch itself opens the palette.
    Rectangle {
        width: 30
        height: 30
        radius: Theme.radius
        color: control.colorValue
        border.width: 1
        border.color: control.swatchHover.hovered ? Theme.accent
                      : Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.9)

        HoverHandler { id: swatchHover }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: control.paletteRequested()
        }
    }

    // Palette button, so the affordance is discoverable even without hover.
    Rectangle {
        width: 30
        height: 30
        radius: Theme.radius
        color: paletteArea.pressed
               ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)
               : paletteHover.hovered
                 ? Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.10)
                 : Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.05)
        border.width: 1
        border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.9)

        HoverHandler { id: paletteHover }
        MouseArea {
            id: paletteArea
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: control.paletteRequested()
        }

        // Mini palette glyph: three colour dots.
        Row {
            anchors.centerIn: parent
            spacing: 2
            Rectangle { width: 6; height: 6; radius: 3; color: "#e5484d" }
            Rectangle { width: 6; height: 6; radius: 3; color: "#f5c518" }
            Rectangle { width: 6; height: 6; radius: 3; color: "#46a758" }
        }
    }

    Rectangle {
        width: 92
        height: 30
        radius: Theme.radius
        color: Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.10)
        border.width: input.activeFocus ? 1 : 0
        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.7)

        TextInput {
            id: input
            anchors.fill: parent
            anchors.margins: 8
            clip: true
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 1
            verticalAlignment: TextInput.AlignVCenter
            selectByMouse: true
            Component.onCompleted: text = control.colorValue.toString().toUpperCase()
            onEditingFinished: {
                const candidate = text.trim();
                if (Qt.color(candidate).toString() !== "#00000000") {
                    control.colorValue = Qt.color(candidate);
                    control.edited(candidate);
                } else {
                    text = control.colorValue.toString().toUpperCase();
                }
            }
        }
    }
}
