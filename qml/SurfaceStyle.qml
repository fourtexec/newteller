import QtQuick

// A lightweight, compositor-friendly surface used by panels, dock buttons and
// floating cards. These effects are drawn with the Qt Quick scene graph (no
// WebEngine/Chromium and no continuously-running blur shader).
//
// `frosted` and `liquid` use translucent tints, fine highlights and borders.
// Actual backdrop blur depends on compositor support and is deliberately not
// required: the same appearance still works on X11 and on every Wayland
// compositor supported by NewTeller.
Item {
    id: surface

    property string material: "theme" // theme | opaque | translucent | frosted | liquid | none
    property color baseColor: Theme.background
    property real materialOpacity: -1 // -1 selects the material's preset opacity
    property color borderColor: Theme.border
    property real borderOpacity: 0.55
    property real borderWidth: 1
    property real cornerRadius: Theme.radius
    property color accentColor: Theme.accent
    property real effectStrength: 1

    readonly property real resolvedOpacity: {
        if (material === "none")
            return 0;
        if (material === "opaque")
            return 1;
        if (materialOpacity >= 0)
            return Math.max(0, Math.min(1, materialOpacity));
        if (material === "translucent")
            return 0.5;
        if (material === "frosted")
            return 0.68;
        if (material === "liquid")
            return 0.50;
        return 0.92; // theme
    }

    Rectangle {
        anchors.fill: parent
        radius: surface.cornerRadius
        color: Qt.rgba(surface.baseColor.r, surface.baseColor.g,
                       surface.baseColor.b, surface.resolvedOpacity)
        border.width: Math.max(0, surface.borderWidth)
        border.color: Qt.rgba(surface.borderColor.r, surface.borderColor.g,
                              surface.borderColor.b,
                              surface.material === "none" ? 0 : surface.borderOpacity)
    }

    // Frosted satin: two restrained highlights give the translucent surface
    // depth without a costly blur pass or a dependency on Qt Quick Effects.
    Rectangle {
        anchors.fill: parent
        radius: surface.cornerRadius
        visible: surface.material === "frosted"
        opacity: Math.max(0, Math.min(1, surface.effectStrength))
        color: "transparent"
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.15) }
            GradientStop { position: 0.42; color: Qt.rgba(1, 1, 1, 0.035) }
            GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.015) }
        }
    }

    // Liquid glass: a cool accent bloom and a narrow specular edge. The layers
    // are static scene-graph rectangles, so they remain cheap to render.
    Rectangle {
        anchors.fill: parent
        radius: surface.cornerRadius
        visible: surface.material === "liquid"
        opacity: Math.max(0, Math.min(1, surface.effectStrength))
        color: "transparent"
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.23) }
            GradientStop { position: 0.16; color: Qt.rgba(1, 1, 1, 0.07) }
            GradientStop { position: 0.56; color: Qt.rgba(surface.accentColor.r,
                                                         surface.accentColor.g,
                                                         surface.accentColor.b, 0.075) }
            GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.025) }
        }
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Math.max(1, surface.cornerRadius * 0.42)
        height: 1
        radius: 0.5
        visible: surface.material === "liquid"
        color: Qt.rgba(1, 1, 1, 0.42 * Math.max(0, Math.min(1, surface.effectStrength)))
    }
}
