import QtQuick

// A quiet hover tooltip for the panel and the menus: fills the parent item,
// waits a moment, then shows one line of text in a small card above (or
// below) the pointer target. It never blocks clicks - HoverHandler is passive
// - so buttons underneath keep working.
//
//     TaskButton { ... ShellToolTip { text: "Notifications" } }
//
// Empty text never shows anything, and the bubble always stays inside the
// parent (and therefore inside the window it belongs to).
Item {
    id: tip

    property string text: ""
    // "top" (default) or "bottom": which side of the parent the bubble sits on.
    property string position: "top"
    // Milliseconds before the bubble appears; instant enough to feel alive,
    // late enough not to flicker when moving across a row of buttons.
    property int delay: 500

    readonly property bool showing: text.length > 0 && hover.hovered

    anchors.fill: parent
    enabled: text.length > 0
    z: 1000

    Motion {
        id: motion
    }

    HoverHandler {
        id: hover
    }

    Timer {
        interval: tip.delay
        running: tip.showing
        onTriggered: bubble.visible = true
    }

    // The bubble sits near the top of the parent by default; the fade covers
    // both the show and the hide so leaving never snaps.
    Rectangle {
        id: bubble
        visible: opacity > 0
        opacity: tip.showing && bubble.visible ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: motion.micro
            }
        }

        readonly property real margin: 2
        // Centred above the parent, clamped into the parent rectangle.
        x: Math.max(margin, Math.min(tip.width - width - margin,
                                     (tip.width - width) / 2))
        y: tip.position === "top" ? -height - 6 : tip.height + 6

        width: bubbleText.implicitWidth + 16
        height: bubbleText.implicitHeight + 9
        radius: 6
        color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.98)
        border.width: 1
        border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.9)

        Text {
            id: bubbleText
            anchors.centerIn: parent
            text: tip.text
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 1
        }
    }
}
