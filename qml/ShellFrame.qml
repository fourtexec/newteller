import QtQuick

// Shared frame of every floating shell window: the start menu, the run
// dialog, the dock menu, the clock and the notification popups.
//
// The look is the "custom window style", configured in Settings -> Appearance
// and stored in the windows.* keys of shell.json. Every key has a
// follow-the-theme fallback so the frame always looks complete:
//
//   windows.style           card | glass | flat | outline
//   windows.material        theme | opaque | translucent | frosted | liquid
//   windows.radius          corner radius in px; -1 follows the theme
//   windows.opacityPercent  window background opacity; 0 uses the material preset
//   windows.borderWidth     border width in px; -1 follows the theme
//   windows.borderColor     hex colour; empty follows the theme
//   windows.shadow          0 (none) .. 3 (strong)
//
// The default property is the content: children declared inside the frame are
// laid out inside the card. Callers reserve no extra space — the frame keeps
// room for its own shadow.
//
// The frame also owns the shared open/close choreography of every floating
// surface: show() calls prepareOpen() before the window becomes visible and
// playOpen() after; hide() routes through playClose(), which fades the card
// out before the callback hides the window. Windows can point openOrigin at
// the edge they grow from (the start menu out of its button, the clock out of
// the panel) instead of the default centre.
//
// Set nubEdge/nubOffset to draw the little pointer that links a dropdown to
// the panel button that toggles it. The frame keeps `nubRoom` pixels free on
// that side, so the window must be that much bigger than it would be without
// a pointer (see qml/Anchoring.js for the placement maths).
Rectangle {
    id: frame

    default property alias content: card.data

    Motion {
        id: motion
    }

    // The point the card scales from while opening (Item.Center by default).
    property int openOrigin: Item.Center

    // The little pointer that links the card to the button that opened it.
    // "top" | "bottom" | "left" | "right": the card edge facing the button,
    // "" for none. nubOffset is the nub's centre along that edge, in card
    // coordinates. The pointer lives in the room the frame keeps outside the
    // card, so a window that uses it must be `nubRoom` pixels bigger on both
    // sides of the pointer's axis (that strip is transparent).
    property string nubEdge: ""
    property real nubOffset: -1
    property int nubSize: 13
    readonly property bool nubVertical: nubEdge === "top" || nubEdge === "bottom"
    readonly property bool nubHorizontal: nubEdge === "left" || nubEdge === "right"
    readonly property int nubRoom: nubEdge.length > 0 ? Math.round(nubSize / Math.SQRT2) : 0
    readonly property bool nubVisible: nubEdge.length > 0 && effOpacity > 0.02
                                       && surfaceMaterial !== "none" && style !== "outline"
    // Per-surface overrides let notifications and their history window use
    // their own material without changing the rest of the shell.
    property string materialOverride: ""
    property color backgroundColorOverride: Theme.background
    property bool useBackgroundColorOverride: false
    property int opacityPercentOverride: -1
    property int cornerRadiusOverride: -1
    // Set between playClose() and the moment the card has faded away.
    property bool closing: false
    // The hide callback handed to playClose().
    property var closeHandler: null

    // Snap to the "about to open" state. Call before making the window
    // visible, so the first rendered frame is already the small, transparent
    // card and nothing flashes at full opacity.
    function prepareOpen() {
        openAnim.stop();
        closeAnim.stop();
        closing = false;
        closeHandler = null;
        scale = 0.97;
        opacity = 0;
    }

    function playOpen() {
        closing = false;
        closeAnim.stop();
        openAnim.restart();
    }

    // Fade the card away, then run onDone (which normally hides the window).
    // Interrupted closes are safe: playOpen() cancels the pending callback.
    function playClose(onDone) {
        if (closing)
            return;
        closing = true;
        closeHandler = onDone;
        openAnim.stop();
        closeAnim.restart();
    }

    ParallelAnimation {
        id: openAnim
        NumberAnimation {
            target: frame
            property: "scale"
            from: 0.97
            to: 1.0
            duration: motion.enter
            easing.type: motion.easeEnter
        }
        NumberAnimation {
            target: frame
            property: "opacity"
            from: 0
            to: 1.0
            duration: Math.round(motion.enter * 0.75)
            easing.type: motion.easeEnter
        }
    }
    ParallelAnimation {
        id: closeAnim
        // stopped() also fires when playOpen() cancels the close; the
        // closing flag separates a real finish from a cancellation.
        onStopped: {
            if (!frame.closing)
                return;
            frame.closing = false;
            const done = frame.closeHandler;
            frame.closeHandler = null;
            if (done)
                done();
        }
        // No explicit from: an interrupted open closes from where it is.
        NumberAnimation {
            target: frame
            property: "scale"
            to: 0.98
            duration: motion.exit
            easing.type: motion.easeExit
        }
        NumberAnimation {
            target: frame
            property: "opacity"
            to: 0
            duration: motion.exit
            easing.type: motion.easeExit
        }
    }

    transformOrigin: frame.openOrigin

    readonly property string style: Settings.values["windows.style"] || "card"
    readonly property string surfaceMaterial: {
        const configured = frame.materialOverride.length > 0
                           ? frame.materialOverride : Settings.values["windows.material"];
        if (!configured || configured === "theme")
            return frame.style === "glass" ? "frosted" : "theme";
        return ["opaque", "translucent", "frosted", "liquid", "none"].indexOf(configured) >= 0
               ? configured : "theme";
    }
    readonly property int shadow: style === "flat" || style === "outline"
                                  ? 0 : Math.max(0, Math.min(3, Settings.values["windows.shadow"] ?? 1))
    // Room between the window edge and the card; the shadow lives there.
    readonly property int inset: shadow > 0 ? 2 + 2 * shadow : 2
    readonly property int effRadius: {
        if (frame.cornerRadiusOverride >= 0)
            return Math.min(32, frame.cornerRadiusOverride);
        const r = Settings.values["windows.radius"];
        return (r === undefined || r < 0) ? Theme.radius + 2 : Math.min(32, r);
    }
    readonly property real effOpacity: {
        const configured = frame.opacityPercentOverride >= 0
                           ? frame.opacityPercentOverride : Settings.values["windows.opacityPercent"];
        if (configured > 0)
            return Math.max(0.15, Math.min(1, configured / 100));
        const materialSetting = frame.materialOverride.length > 0
                                ? frame.materialOverride : Settings.values["windows.material"];
        if (!materialSetting || materialSetting === "theme") {
            if (frame.style === "flat") return Math.max(Theme.opacity, 0.97);
            if (frame.style === "outline") return Theme.opacity * 0.45;
            if (frame.style === "glass") return Theme.opacity * 0.72;
        }
        if (frame.surfaceMaterial === "opaque") return 1;
        if (frame.surfaceMaterial === "translucent") return 0.5;
        if (frame.surfaceMaterial === "frosted") return 0.68;
        if (frame.surfaceMaterial === "liquid") return 0.5;
        if (frame.surfaceMaterial === "none") return 0;
        return Theme.opacity;
    }
    readonly property int effBorderWidth: {
        const w = Settings.values["windows.borderWidth"];
        return (w === undefined || w < 0) ? 1 : Math.max(0, Math.min(6, w));
    }
    readonly property color effBorderColor: {
        const c = Settings.values["windows.borderColor"];
        return (!c || c.length === 0) ? Theme.border : Qt.color(c);
    }

    color: "transparent"
    clip: false

    // --- the pointer --------------------------------------------------------
    // A rounded diamond centred on the card edge: the half that sticks out
    // lands in the strip the card left free, the half under the card is
    // hidden by it, so the visible shape is a soft triangle pointing at the
    // button. Drawn before the card, in the card's own fill colour, so it
    // reads as part of the same surface.
    Item {
        id: nub
        visible: frame.nubVisible
        width: frame.nubSize * 1.42
        height: width

        readonly property real along: frame.nubOffset >= 0
                                      ? frame.nubOffset
                                      : (frame.nubEdge === "top" || frame.nubEdge === "bottom"
                                         ? card.width / 2 : card.height / 2)

        x: frame.nubEdge === "left" ? card.x - width / 2
           : frame.nubEdge === "right" ? card.x + card.width - width / 2
           : card.x + along - width / 2
        y: frame.nubEdge === "top" ? card.y - height / 2
           : frame.nubEdge === "bottom" ? card.y + card.height - height / 2
           : card.y + along - height / 2

        Rectangle {
            anchors.centerIn: parent
            width: frame.nubSize
            height: width
            radius: Math.max(1, frame.nubSize * 0.16)
            rotation: 45
            antialiasing: true
            color: Qt.rgba((frame.useBackgroundColorOverride ? frame.backgroundColorOverride
                                                             : Theme.background).r,
                           (frame.useBackgroundColorOverride ? frame.backgroundColorOverride
                                                             : Theme.background).g,
                           (frame.useBackgroundColorOverride ? frame.backgroundColorOverride
                                                             : Theme.background).b,
                           Math.min(1, frame.effOpacity + 0.12))
        }
    }

    // --- shadow: two wide, translucent borders fake a soft blur ------------
    Rectangle {
        visible: frame.shadow > 0
        anchors.fill: card
        anchors.margins: -2 - frame.shadow
        radius: card.radius + 2 + frame.shadow
        color: "transparent"
        border.width: 2 + frame.shadow
        border.color: Qt.rgba(0, 0, 0, 0.05 * frame.shadow)
    }
    Rectangle {
        visible: frame.shadow > 0
        anchors.fill: card
        anchors.margins: -1
        radius: card.radius + 1
        color: "transparent"
        border.width: 1 + frame.shadow
        border.color: Qt.rgba(0, 0, 0, 0.09 * frame.shadow)
    }

    // --- the card -----------------------------------------------------------
    Rectangle {
        id: card
        anchors.fill: parent
        // The pointer's strip is reserved on both sides of its axis, so the
        // card stays centred in the window and the pointer can point either
        // way after the card flipped to the other side of the bar.
        anchors.topMargin: frame.inset + (frame.nubVertical ? frame.nubRoom : 0)
        anchors.bottomMargin: frame.inset + (frame.nubVertical ? frame.nubRoom : 0)
        anchors.leftMargin: frame.inset + (frame.nubHorizontal ? frame.nubRoom : 0)
        anchors.rightMargin: frame.inset + (frame.nubHorizontal ? frame.nubRoom : 0)
        radius: frame.effRadius
        color: "transparent"
        border.width: 0

        SurfaceStyle {
            anchors.fill: parent
            material: frame.surfaceMaterial
            materialOpacity: frame.effOpacity
            baseColor: frame.useBackgroundColorOverride ? frame.backgroundColorOverride : Theme.background
            cornerRadius: card.radius
            borderWidth: frame.style === "flat" ? 0 : frame.effBorderWidth
            borderColor: frame.style === "outline" ? Theme.accent : frame.effBorderColor
            borderOpacity: frame.style === "outline" ? 0.9 : frame.style === "glass" ? 0.78 : 0.6
            effectStrength: frame.style === "glass" ? 1.0 : 0.85
        }
    }

}
