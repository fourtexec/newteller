import QtQuick

// The floating dock surface — the pill behind the icons.
//
// Four materials, matching the classic macOS dock comparison:
//
//   translucent  "Half Transparent" — tinted glass; the wallpaper stays
//                visible through the pill.
//   opaque       "Non Transparent" — a solid, softly lit slab; no wallpaper
//                shows through it at all.
//   frosted      "Frosted Glass" — a milky, matte haze with a broad soft
//                highlight, low contrast and no sharp specular.
//   liquid       "Liquid Glass" — clear, high-gloss glass: a bright top rim, a
//                second refraction ring inside it, a glossy face with a cool
//                rake towards the accent end, and a lit foot.
//
// The look is assembled the way the reference is: an outline stroke with a
// lit top edge, a sheen across the face, darker "thicker glass" ends and a
// shaded foot, all inside a full pill. Everything below is cheap Qt Quick
// geometry (rounded rectangles, gradients and one Canvas outline), so the dock
// stays fast on X11, on every supported Wayland compositor and with the
// software renderer — no shader, no backdrop-blur pass, no Qt Quick Effects
// dependency.
//
// The materials are drawn, not sampled: a real backdrop blur needs a
// compositor protocol, so "frosted" is a matte milk-glass tint and "liquid"
// is clear glass with its own highlights rather than a live refraction.
//
// Layers that touch an edge always span the whole pill and carry its radius; a
// shorter rounded rectangle would have its corners clamped and leak outside
// the curve (the clip is rectangular). An edge panel makes the pill tall, and
// every gradient then turns a quarter turn so the light still comes from the
// screen edge.
Item {
    id: surface

    // none | opaque | translucent | frosted | liquid | theme
    property string material: "translucent"
    property color baseColor: Theme.surface
    property color accentColor: Theme.accent
    // -1 keeps the material's own preset opacity.
    property real materialOpacity: -1
    property real cornerRadius: height / 2
    // Scales every highlight and shadow; 0 leaves a flat fill.
    property real effectStrength: 1
    // Multiplies the whole dock (used by the auto-hide reveal).
    property real surfaceOpacity: 1

    readonly property real fx: Math.max(0, Math.min(1, effectStrength))
    readonly property bool frostedGlass: material === "frosted"
    readonly property bool liquidGlass: material === "liquid"
    readonly property bool solid: material === "opaque"
    readonly property bool clear: material === "translucent"
    readonly property bool glass: frostedGlass || liquidGlass
    readonly property bool visibleSurface: material !== "none"
    readonly property real radius: Math.min(cornerRadius, height / 2, width / 2)

    // --- body tint ---------------------------------------------------------
    // A translucent dock keeps the theme surface colour; an opaque one is a
    // solid slab just off the theme surface (a full slab needs its own light);
    // frosted is milked towards white; liquid stays dark and clear with a cool
    // accent lean, so the wallpaper reads through it.
    readonly property color bodyColor: {
        if (solid)
            return Qt.rgba(baseColor.r * 0.94, baseColor.g * 0.95, baseColor.b * 0.98, 1);
        if (frostedGlass)
            return Qt.rgba(baseColor.r * 0.46 + 0.58,
                           baseColor.g * 0.46 + 0.63,
                           baseColor.b * 0.46 + 0.71, 1);
        if (liquidGlass)
            return Qt.rgba(baseColor.r * 0.62 + accentColor.r * 0.10 + 0.06,
                           baseColor.g * 0.62 + accentColor.g * 0.10 + 0.12,
                           baseColor.b * 0.62 + accentColor.b * 0.10 + 0.24, 1);
        return baseColor;
    }

    // Preset body opacity. The dock is glass: even frosted keeps some
    // wallpaper in view, and liquid glass is the most transparent of all.
    readonly property real presetOpacity: {
        if (solid) return 1.0;
        if (frostedGlass) return 0.50;
        if (liquidGlass) return 0.30;
        if (clear) return 0.42;
        return 0.9; // "theme"
    }
    readonly property real resolvedOpacity: materialOpacity >= 0
                                            ? Math.max(0.05, Math.min(1, materialOpacity))
                                            : presetOpacity

    // Highlight strengths per material.
    readonly property real rimTop: liquidGlass ? 0.80 : frostedGlass ? 0.58
                                                                : solid ? 0.22 : 0.40
    readonly property real rimInner: liquidGlass ? 0.26 : frostedGlass ? 0.24
                                                                 : solid ? 0.10 : 0.14
    // Glass gets a light edge (the reference's white rim); the tinted looks
    // keep a dark hairline so they stay separated from the wallpaper.
    readonly property real rimOuter: liquidGlass ? 0.44 : frostedGlass ? 0.34 : 0.30
    readonly property bool lightRim: liquidGlass || frostedGlass
    readonly property real topGloss: liquidGlass ? 0.22 : frostedGlass ? 0.24
                                                                : solid ? 0.12 : 0.16
    readonly property real bottomShade: solid ? 0.26 : frostedGlass ? 0.10
                                                             : liquidGlass ? 0.10 : 0.18
    readonly property real endShade: solid ? 0.17 : frostedGlass ? 0.07
                                                          : liquidGlass ? 0.04 : 0.11

    // --- drop shadow -------------------------------------------------------
    // Wide and soft under a solid slab, tighter under clear glass.
    Repeater {
        model: 6

        Rectangle {
            visible: surface.visibleSurface
            z: -6 + index
            anchors.fill: parent
            anchors.margins: -1 - index * 1.7
            anchors.topMargin: -1 - index * 0.9
            anchors.bottomMargin: -2 - index * 2.3
            radius: surface.radius + 1 + index * 1.7
            color: Qt.rgba(0, 0, 0,
                           (surface.solid ? 0.10 : 0.08)
                           * (1 - index / 7) * surface.fx
                           * (surface.solid ? 1 : surface.resolvedOpacity + 0.45))
        }
    }

    // Glass catches the light: a very faint white halo around the pill.
    Rectangle {
        visible: surface.visibleSurface && surface.glass
        z: -2
        anchors.fill: parent
        anchors.margins: -2
        radius: surface.radius + 2
        color: "transparent"
        border.width: 3
        border.color: Qt.rgba(1, 1, 1, (surface.liquidGlass ? 0.09 : 0.06) * surface.fx)
    }

    // Liquid glass throws a faint cool bloom onto the wallpaper around it.
    Rectangle {
        visible: surface.visibleSurface && surface.liquidGlass
        z: -3
        anchors.fill: parent
        anchors.margins: -5
        anchors.bottomMargin: -9
        radius: surface.radius + 7
        color: "transparent"
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0) }
            GradientStop { position: 0.55; color: Qt.rgba(surface.accentColor.r,
                                                          surface.accentColor.g,
                                                          surface.accentColor.b,
                                                          0.09 * surface.fx) }
            GradientStop { position: 1.0; color: Qt.rgba(surface.accentColor.r,
                                                         surface.accentColor.g,
                                                         surface.accentColor.b, 0) }
        }
    }

    // --- the pill ----------------------------------------------------------
    // A vertical dock (an edge panel) turns every gradient a quarter turn: the
    // light still comes from "above" in the pill's own frame, which is the
    // screen edge the dock sits on.
    readonly property bool verticalPill: height > width * 1.2
    readonly property int acrossOrientation: verticalPill ? Gradient.Horizontal
                                                          : Gradient.Vertical
    readonly property int alongOrientation: verticalPill ? Gradient.Vertical
                                                         : Gradient.Horizontal
    readonly property real alongLength: verticalPill ? height : width
    readonly property real acrossLength: verticalPill ? width : height

    Item {
        anchors.fill: parent
        visible: surface.visibleSurface
        opacity: Math.max(0, Math.min(1, surface.surfaceOpacity))
        clip: true

        // Body. A gradient across the pill — lighter at the top (or left),
        // deeper at the foot — is what makes it read as one piece of glass
        // and not as a flat slab.
        Rectangle {
            anchors.fill: parent
            radius: surface.radius
            gradient: Gradient {
                orientation: surface.acrossOrientation
                GradientStop {
                    position: 0.0
                    color: Qt.rgba(surface.bodyColor.r, surface.bodyColor.g,
                                   surface.bodyColor.b, surface.resolvedOpacity)
                }
                GradientStop {
                    position: 0.55
                    color: Qt.rgba(surface.bodyColor.r * 0.96, surface.bodyColor.g * 0.96,
                                   surface.bodyColor.b * 0.96, surface.resolvedOpacity)
                }
                GradientStop {
                    position: 1.0
                    color: Qt.rgba(surface.bodyColor.r * 0.82, surface.bodyColor.g * 0.82,
                                   surface.bodyColor.b * 0.82,
                                   Math.min(1, surface.resolvedOpacity + 0.04))
                }
            }
        }

        // Top light + foot shade in one full-size pass, so the rounded ends
        // stay clean (a shorter rounded rectangle would have its corners
        // clamped and leak outside the pill's outline).
        Rectangle {
            anchors.fill: parent
            radius: surface.radius
            opacity: surface.fx
            gradient: Gradient {
                orientation: surface.acrossOrientation
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, surface.topGloss) }
                GradientStop { position: 0.34; color: Qt.rgba(1, 1, 1, surface.topGloss * 0.28) }
                GradientStop { position: 0.66; color: Qt.rgba(1, 1, 1, 0.0) }
                GradientStop { position: 0.88; color: Qt.rgba(0, 0, 0, surface.bottomShade * 0.45) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, surface.bottomShade) }
            }
        }

        // Liquid glass picks the accent back up through its foot: a cold band
        // of colour along the bottom edge.
        Rectangle {
            anchors.fill: parent
            radius: surface.radius
            visible: surface.liquidGlass
            opacity: surface.fx
            gradient: Gradient {
                orientation: surface.acrossOrientation
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0) }
                GradientStop {
                    position: 0.80
                    color: Qt.rgba(surface.accentColor.r, surface.accentColor.g,
                                   surface.accentColor.b, 0.02)
                }
                GradientStop {
                    position: 1.0
                    color: Qt.rgba(surface.accentColor.r, surface.accentColor.g,
                                   surface.accentColor.b, 0.16)
                }
            }
        }

        // The ends of the pill fall away slightly, as if the glass were
        // thicker there; it keeps the bar from looking cut off.
        Rectangle {
            id: startEnd
            readonly property real band: Math.min(surface.alongLength * 0.32,
                                                 surface.radius * 2.2)
            x: 0
            y: 0
            width: surface.verticalPill ? parent.width : band
            height: surface.verticalPill ? band : parent.height
            radius: surface.radius
            opacity: surface.fx
            gradient: Gradient {
                orientation: surface.alongOrientation
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, surface.endShade) }
                GradientStop { position: 0.42; color: Qt.rgba(0, 0, 0, surface.endShade * 0.25) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0) }
            }
        }
        Rectangle {
            id: endEnd
            readonly property real band: Math.min(surface.alongLength * 0.32,
                                                 surface.radius * 2.2)
            x: surface.verticalPill ? 0 : parent.width - band
            y: surface.verticalPill ? parent.height - band : 0
            width: surface.verticalPill ? parent.width : band
            height: surface.verticalPill ? band : parent.height
            radius: surface.radius
            opacity: surface.fx
            gradient: Gradient {
                orientation: surface.alongOrientation
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0) }
                GradientStop { position: 0.58; color: Qt.rgba(0, 0, 0, surface.endShade * 0.25) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, surface.endShade) }
            }
        }

        // --- frosted glass: a matte milk haze ------------------------------
        // No sharp reflections: a broad, low-contrast bloom towards the light
        // plus a flat white veil is what makes frosted glass look matte.
        Rectangle {
            anchors.fill: parent
            radius: surface.radius
            visible: surface.frostedGlass
            opacity: surface.fx
            gradient: Gradient {
                orientation: surface.acrossOrientation
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.10) }
                GradientStop { position: 0.45; color: Qt.rgba(1, 1, 1, 0.06) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.045) }
            }
        }
        Rectangle {
            id: frostedBloom
            readonly property real band: surface.alongLength * 0.62
            x: 0
            y: 0
            width: surface.verticalPill ? parent.width : band
            height: surface.verticalPill ? band : parent.height
            radius: surface.radius
            visible: surface.frostedGlass
            opacity: surface.fx
            gradient: Gradient {
                orientation: surface.alongOrientation
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.13) }
                GradientStop { position: 0.45; color: Qt.rgba(1, 1, 1, 0.05) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
            }
        }

        // --- liquid glass: gloss and the cool rake --------------------------
        // Two pill-shaped passes rather than one rotated band: a diagonal
        // sweep would have to be clipped, and Qt Quick clips to the
        // *rectangular* bounding box, which paints a rotated band outside the
        // rounded ends. A gloss across the pill plus a bright-to-cool rake
        // along it reads as the same curved glass and always stays inside the
        // outline, at any size and in either orientation.
        Rectangle {
            anchors.fill: parent
            radius: surface.radius
            visible: surface.liquidGlass
            opacity: surface.fx
            gradient: Gradient {
                orientation: surface.acrossOrientation
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.26) }
                GradientStop { position: 0.10; color: Qt.rgba(1, 1, 1, 0.16) }
                GradientStop { position: 0.34; color: Qt.rgba(1, 1, 1, 0.06) }
                GradientStop { position: 0.62; color: Qt.rgba(1, 1, 1, 0.015) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.09) }
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: surface.radius
            visible: surface.liquidGlass
            opacity: surface.fx
            gradient: Gradient {
                orientation: surface.alongOrientation
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.05) }
                GradientStop { position: 0.16; color: Qt.rgba(1, 1, 1, 0.11) }
                GradientStop { position: 0.34; color: Qt.rgba(1, 1, 1, 0.02) }
                GradientStop { position: 0.62; color: Qt.rgba(0, 0, 0, 0.05) }
                GradientStop { position: 0.86; color: Qt.rgba(surface.accentColor.r,
                                                              surface.accentColor.g,
                                                              surface.accentColor.b, 0.07) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.06) }
            }
        }

        // A breath of white across the face reads as glare on the glass.
        Rectangle {
            anchors.fill: parent
            radius: surface.radius
            visible: surface.liquidGlass
            opacity: surface.fx
            gradient: Gradient {
                orientation: surface.acrossOrientation
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.16) }
                GradientStop { position: 0.18; color: Qt.rgba(1, 1, 1, 0.055) }
                GradientStop { position: 0.62; color: Qt.rgba(1, 1, 1, 0.0) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.10) }
            }
        }
    }

    // --- rims and gloss edges ----------------------------------------------
    // The rim is stroked along the pill's own outline instead of being drawn
    // as a rounded rectangle border: a Canvas path follows the curve exactly
    // at every size and in both dock orientations (a horizontal bar has its
    // caps left and right, a vertical one top and bottom), and a stroke
    // gradient can light the top edge, dim the sides and bring the bottom
    // lip back up — the three cues the reference uses to sell glass.
    readonly property real rimOpacity: Math.max(0, Math.min(1, surface.surfaceOpacity))

    Canvas {
        id: rim
        anchors.fill: parent
        visible: surface.visibleSurface && surface.rimOpacity > 0.01
        opacity: surface.rimOpacity

        // Canvas does not track the properties it paints with, so repaint
        // whenever one of them changes. The dock only resizes when the app
        // list changes, so this is a handful of repaints per session.
        property real revision: 0
        Connections {
            target: surface
            function onMaterialChanged() { rim.revision += 1; }
            function onEffectStrengthChanged() { rim.revision += 1; }
            function onCornerRadiusChanged() { rim.revision += 1; }
            function onBaseColorChanged() { rim.revision += 1; }
        }
        onRevisionChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Component.onCompleted: requestPaint()

        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            if (surface.width < 4 || surface.height < 4)
                return;

            const r = surface.radius;
            const fx = surface.fx;

            function pill(ctx, inset) {
                const x = inset;
                const y = inset;
                const w = surface.width - inset * 2;
                const h = surface.height - inset * 2;
                const rr = Math.max(0, Math.min(r - inset, w / 2, h / 2));
                ctx.beginPath();
                ctx.moveTo(x + rr, y);
                ctx.lineTo(x + w - rr, y);
                ctx.arcTo(x + w, y, x + w, y + rr, rr);
                ctx.lineTo(x + w, y + h - rr);
                ctx.arcTo(x + w, y + h, x + w - rr, y + h, rr);
                ctx.lineTo(x + rr, y + h);
                ctx.arcTo(x, y + h, x, y + h - rr, rr);
                ctx.lineTo(x, y + rr);
                ctx.arcTo(x, y, x + rr, y, rr);
                ctx.closePath();
            }

            // The gradient runs across the pill: top-to-bottom on a
            // horizontal dock, left-to-right on a vertical one.
            function edge(top, side, bottom) {
                const grad = surface.verticalPill
                    ? ctx.createLinearGradient(0, 0, surface.width, 0)
                    : ctx.createLinearGradient(0, 0, 0, surface.height);
                grad.addColorStop(0.0, top);
                grad.addColorStop(0.42, side);
                grad.addColorStop(0.62, side);
                grad.addColorStop(1.0, bottom);
                return grad;
            }

            // 1. The dark hairline that separates the pill from the wallpaper.
            //    Tinted materials get a real outline; glass gets a cooler,
            //    fainter one so it reads as a lit edge.
            ctx.lineWidth = 1;
            pill(ctx, 0.5);
            ctx.strokeStyle = surface.lightRim
                ? edge(Qt.rgba(1, 1, 1, 0.34 * fx),
                       Qt.rgba(1, 1, 1, 0.14 * fx),
                       Qt.rgba(1, 1, 1, 0.26 * fx))
                : edge(Qt.rgba(0, 0, 0, 0.16 * fx),
                       Qt.rgba(0, 0, 0, surface.rimOuter * fx),
                       Qt.rgba(0, 0, 0, 0.24 * fx));
            ctx.stroke();

            // 2. The lit inner rim: bright along the top, fading down the
            //    sides, catching the light again on the bottom lip.
            ctx.lineWidth = surface.liquidGlass ? 1.4 : 1;
            pill(ctx, 1.5);
            ctx.strokeStyle = edge(Qt.rgba(1, 1, 1, surface.rimTop * fx),
                                   Qt.rgba(1, 1, 1, surface.rimInner * fx),
                                   Qt.rgba(1, 1, 1,
                                           (surface.liquidGlass ? 0.30
                                            : surface.frostedGlass ? 0.24 : 0.10) * fx));
            ctx.stroke();

            // 3. Liquid glass carries a second, cooler rim just inside the
            //    first: light refracting through the thickness of the edge.
            if (surface.liquidGlass) {
                ctx.lineWidth = 1;
                pill(ctx, 3.5);
                ctx.strokeStyle = edge(Qt.rgba(1, 1, 1, 0.30 * fx),
                                       Qt.rgba(1, 1, 1, 0.05 * fx),
                                       Qt.rgba(surface.accentColor.r, surface.accentColor.g,
                                               surface.accentColor.b, 0.18 * fx));
                ctx.stroke();
            }

            // 4. Frosted glass has a soft, wide top edge rather than a crisp
            //    specular: no inner ring, just a broad bloom under the top rim.
            if (surface.frostedGlass) {
                ctx.lineWidth = 2.5;
                pill(ctx, 2.5);
                ctx.strokeStyle = edge(Qt.rgba(1, 1, 1, 0.10 * fx),
                                       Qt.rgba(1, 1, 1, 0.04 * fx),
                                       Qt.rgba(1, 1, 1, 0.06 * fx));
                ctx.stroke();
            }
        }
    }
}
