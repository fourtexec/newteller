import QtQuick

// Shared frame of every floating shell window: the start menu, the run
// dialog, the dock menu, the clock and the notification popups.
//
// The look is the "custom window style", configured in Settings -> Appearance
// and stored in the windows.* keys of shell.json. Every key has a
// follow-the-theme fallback so the frame always looks complete:
//
//   windows.style           card | glass | flat | outline
//   windows.radius          corner radius in px; -1 follows the theme
//   windows.opacityPercent  window background opacity; 0 follows the theme
//   windows.borderWidth     border width in px; -1 follows the theme
//   windows.borderColor     hex colour; empty follows the theme
//   windows.shadow          0 (none) .. 3 (strong)
//
// The default property is the content: children declared inside the frame are
// laid out inside the card. Callers reserve no extra space — the frame keeps
// room for its own shadow.
Rectangle {
    id: frame

    default property alias content: card.data

    readonly property string style: Settings.values["windows.style"] || "card"
    readonly property int shadow: style === "flat" || style === "outline"
                                  ? 0 : Math.max(0, Math.min(3, Settings.values["windows.shadow"] ?? 1))
    // Room between the window edge and the card; the shadow lives there.
    readonly property int inset: shadow > 0 ? 2 + 2 * shadow : 2
    readonly property int effRadius: {
        const r = Settings.values["windows.radius"];
        return (r === undefined || r < 0) ? Theme.radius + 2 : Math.min(32, r);
    }
    readonly property real effOpacity: {
        const p = Settings.values["windows.opacityPercent"];
        return (!p || p <= 0) ? Theme.opacity : Math.max(0.15, Math.min(1, p / 100));
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
        anchors.margins: frame.inset
        radius: frame.effRadius
        border.width: frame.style === "flat" ? 0 : frame.effBorderWidth
        border.color: {
            const base = Qt.rgba(frame.effBorderColor.r, frame.effBorderColor.g,
                                 frame.effBorderColor.b, 0.6);
            if (frame.style === "outline")
                return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.85);
            if (frame.style === "glass")
                return Qt.rgba(frame.effBorderColor.r, frame.effBorderColor.g,
                               frame.effBorderColor.b, 0.75);
            return base;
        }
        color: {
            const bg = Theme.background;
            if (frame.style === "flat")
                return Qt.rgba(bg.r, bg.g, bg.b, Math.max(frame.effOpacity, 0.97));
            if (frame.style === "outline")
                return Qt.rgba(bg.r, bg.g, bg.b, frame.effOpacity * 0.45);
            if (frame.style === "glass")
                return Qt.rgba(bg.r, bg.g, bg.b, frame.effOpacity * 0.72);
            return Qt.rgba(bg.r, bg.g, bg.b, frame.effOpacity);
        }

        // Glass: a soft light from the top, under the content.
        Rectangle {
            visible: frame.style === "glass"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height / 2
            radius: parent.radius
            z: -1
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.10) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
    }
}
