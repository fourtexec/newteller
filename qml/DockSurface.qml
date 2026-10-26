import QtQuick

// The floating dock surface. This is deliberately a dock-specific material
// rather than a stretched SurfaceStyle: the four looks in the reference are
// more than a tinted rectangle. They need a rim, a shadow, a top-light and,
// for liquid glass, a coloured caustic and a diagonal specular sheen.
//
// Everything is static Qt Quick geometry, so the dock stays cheap to render
// on X11, on every supported Wayland compositor and on a software-rendered
// test session. No shader or backdrop-blur pass is required.
Item {
    id: surface

    property string material: "translucent"
    property color baseColor: Theme.surface
    property color accentColor: Theme.accent
    property real materialOpacity: -1
    property real cornerRadius: height / 2
    property real effectStrength: 1

    readonly property bool frosted: material === "frosted"
    readonly property bool liquid: material === "liquid"
    readonly property bool opaque: material === "opaque"
    readonly property bool visibleSurface: material !== "none"
    readonly property real resolvedOpacity: {
        if (material === "none") return 0;
        if (material === "opaque") return 1;
        if (materialOpacity >= 0)
            return Math.max(0.18, Math.min(1, materialOpacity));
        if (material === "translucent") return 0.52;
        if (material === "frosted") return 0.70;
        if (material === "liquid") return 0.52;
        return 0.88;
    }
    readonly property color frostedTint: Qt.rgba(
        Math.min(1, baseColor.r * 0.34 + 0.66),
        Math.min(1, baseColor.g * 0.34 + 0.66),
        Math.min(1, baseColor.b * 0.34 + 0.66), 1)
    readonly property color liquidTint: Qt.rgba(
        Math.min(1, 0.10 + baseColor.r * 0.22),
        Math.min(1, 0.22 + baseColor.g * 0.28),
        Math.min(1, 0.44 + baseColor.b * 0.34), 1)

    // Soft, wide drop shadow. Multiple offset rounded rectangles cost almost
    // nothing and give the dock the floating look from the reference without
    // a blur pass.
    Repeater {
        model: 4
        Rectangle {
            visible: surface.visibleSurface
            z: -4 + index
            anchors.fill: parent
            anchors.margins: -2 - index * 2
            radius: surface.cornerRadius + 2 + index * 2
            color: Qt.rgba(0, 0, 0,
                           (surface.opaque ? 0.12 : 0.075)
                           * (1 - index * 0.2)
                           * Math.max(0, Math.min(1, surface.effectStrength)))
        }
    }

    Item {
        id: glass
        anchors.fill: parent
        visible: surface.visibleSurface
        clip: true

        // The base tint.
        Rectangle {
            anchors.fill: parent
            radius: surface.cornerRadius
            color: surface.opaque
                   ? surface.baseColor
                   : surface.frosted
                     ? Qt.rgba(surface.frostedTint.r, surface.frostedTint.g,
                               surface.frostedTint.b, surface.resolvedOpacity)
                     : surface.liquid
                       ? Qt.rgba(surface.liquidTint.r, surface.liquidTint.g,
                                 surface.liquidTint.b, surface.resolvedOpacity)
                       : Qt.rgba(surface.baseColor.r, surface.baseColor.g,
                                 surface.baseColor.b, surface.resolvedOpacity)
        }

        // Half-transparent / opaque depth. A gentle vertical gradient keeps
        // the bar from looking like a flat slab while leaving the wallpaper
        // visible through the top edge.
        Rectangle {
            anchors.fill: parent
            radius: surface.cornerRadius
            visible: !surface.frosted && !surface.liquid
            opacity: Math.max(0, Math.min(1, surface.effectStrength))
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, surface.opaque ? 0.07 : 0.16) }
                GradientStop { position: 0.45; color: Qt.rgba(1, 1, 1, 0.015) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, surface.opaque ? 0.16 : 0.08) }
            }
        }

        // Frosted glass: a milky sheen and a soft internal haze. The sharp
        // wallpaper behind is not sampled (that would need a compositor blur
        // pass); the layered white gradients give the same matte impression.
        Rectangle {
            anchors.fill: parent
            radius: surface.cornerRadius
            visible: surface.frosted
            opacity: Math.max(0, Math.min(1, surface.effectStrength))
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.34) }
                GradientStop { position: 0.30; color: Qt.rgba(1, 1, 1, 0.13) }
                GradientStop { position: 0.62; color: Qt.rgba(1, 1, 1, 0.045) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.11) }
            }
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: Math.max(0, surface.cornerRadius - 2)
            visible: surface.frosted
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.10 * surface.effectStrength)
        }

        // Liquid glass: a cool caustic wash, a bright top-left reflection and
        // a diagonal sheen clipped by the pill. The accent is reserved for a
        // small chromatic bloom at the bottom.
        Rectangle {
            anchors.fill: parent
            radius: surface.cornerRadius
            visible: surface.liquid
            opacity: Math.max(0, Math.min(1, surface.effectStrength))
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.40) }
                GradientStop { position: 0.16; color: Qt.rgba(1, 1, 1, 0.13) }
                GradientStop { position: 0.52; color: Qt.rgba(0.46, 0.72, 1.0, 0.10) }
                GradientStop { position: 1.0; color: Qt.rgba(surface.accentColor.r,
                                                             surface.accentColor.g,
                                                             surface.accentColor.b, 0.10) }
            }
        }
        Rectangle {
            visible: surface.liquid
            width: parent.width * 0.72
            height: parent.height * 1.8
            x: parent.width * 0.10
            y: -parent.height * 0.45
            rotation: -16
            opacity: Math.max(0, Math.min(1, surface.effectStrength))
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.0) }
                GradientStop { position: 0.45; color: Qt.rgba(1, 1, 1, 0.20) }
                GradientStop { position: 0.58; color: Qt.rgba(1, 1, 1, 0.06) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
            }
        }
        Rectangle {
            visible: surface.liquid
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.height * 0.38
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(surface.accentColor.r,
                                                             surface.accentColor.g,
                                                             surface.accentColor.b, 0.0) }
                GradientStop { position: 1.0; color: Qt.rgba(surface.accentColor.r,
                                                             surface.accentColor.g,
                                                             surface.accentColor.b, 0.16) }
            }
        }
    }

    // Rim. Opaque gets a quieter edge; frosted/liquid get a bright glass rim.
    Rectangle {
        anchors.fill: parent
        radius: surface.cornerRadius
        visible: surface.visibleSurface
        color: "transparent"
        border.width: surface.liquid ? 1.4 : 1
        border.color: surface.liquid
                      ? Qt.rgba(1, 1, 1, 0.48 * surface.effectStrength)
                      : surface.frosted
                        ? Qt.rgba(1, 1, 1, 0.42 * surface.effectStrength)
                        : surface.opaque
                          ? Qt.rgba(surface.baseColor.r * 0.7 + 0.3,
                                    surface.baseColor.g * 0.7 + 0.3,
                                    surface.baseColor.b * 0.7 + 0.3,
                                    0.24 * surface.effectStrength)
                          : Qt.rgba(1, 1, 1, 0.22 * surface.effectStrength)
    }

    // Top rim: the narrow specular line that sells the glass edge.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: surface.cornerRadius * 0.35
        anchors.rightMargin: surface.cornerRadius * 0.35
        height: 1
        radius: 0.5
        visible: surface.visibleSurface && surface.material !== "opaque"
        color: Qt.rgba(1, 1, 1, (surface.liquid ? 0.66 : 0.40)
                              * surface.effectStrength)
    }
}
