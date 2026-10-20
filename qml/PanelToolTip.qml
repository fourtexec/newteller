import QtQuick
import QtQuick.Window
import org.kde.layershell 1.0 as LayerShell

// One small tooltip window per panel. Hovering a panel button asks the panel
// to point this window at it (showAt); leaving hides it. The panel window is
// only as tall as the bar, so the text cannot be drawn inside it - a separate
// layer surface above the hovered button is the honest way.
Window {
    id: tipWindow

    // Which edge the panel sits on; the tooltip appears on the screen side.
    property string position: "bottom"
    property int panelThickness: 48
    // Centre of the hovered button, in panel-local coordinates.
    property real anchorX: 0
    property real anchorY: 0
    property string text: ""

    width: tipLabel.implicitWidth + 20
    height: tipLabel.implicitHeight + 10
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint

    readonly property real clampedX: Math.max(8, Math.min(anchorX - width / 2,
                                                          Screen.width - width - 8))
    readonly property real clampedY: Math.max(8, Math.min(anchorY - height / 2,
                                                          Screen.height - height - 8))

    // Positioning without layer-shell (X11, XWayland).
    x: {
        if (position === "left")
            return Screen.virtualX + panelThickness + 6;
        if (position === "right")
            return Screen.virtualX + Screen.width - width - panelThickness - 6;
        return Screen.virtualX + clampedX;
    }
    y: {
        if (position === "bottom")
            return Screen.virtualY + Screen.height - height - panelThickness - 6;
        if (position === "top")
            return Screen.virtualY + panelThickness + 6;
        return Screen.virtualY + clampedY;
    }

    LayerShell.Window.anchors: {
        if (position === "top")
            return LayerShell.Window.AnchorTop | LayerShell.Window.AnchorLeft;
        if (position === "left")
            return LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorTop;
        if (position === "right")
            return LayerShell.Window.AnchorRight | LayerShell.Window.AnchorTop;
        return LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorLeft;
    }
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.scope: "newteller-tooltip"
    LayerShell.Window.margins {
        left: tipWindow.position === "left" ? panelThickness + 6
              : tipWindow.position === "right" ? 0 : tipWindow.clampedX
        right: tipWindow.position === "right" ? panelThickness + 6 : 0
        top: tipWindow.position === "top" ? panelThickness + 6
              : tipWindow.position === "bottom" ? 0 : tipWindow.clampedY
        bottom: tipWindow.position === "bottom" ? panelThickness + 6 : 0
    }

    function showAt(newText, centerX, centerY) {
        if (!newText || newText.length === 0)
            return;
        fadeTimer.stop();
        text = newText;
        anchorX = centerX;
        anchorY = centerY;
        if (!visible) {
            // Start transparent so the fade-in covers the first frame.
            opacity = 0;
            visible = true;
        }
        opacity = 1;
    }

    function hide() {
        if (!visible)
            return;
        opacity = 0;
        fadeTimer.restart();
    }

    // Exit is quick: fade out, then take the window off the screen. Re-shows
    // during the fade cancel the timer, so hopping across buttons never
    // flickers.
    Behavior on opacity {
        NumberAnimation {
            duration: 110
        }
    }
    Timer {
        id: fadeTimer
        interval: 120
        onTriggered: {
            if (tipWindow.opacity <= 0)
                tipWindow.visible = false;
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.98)
        border.width: 1
        border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.9)

        Text {
            id: tipLabel
            anchors.centerIn: parent
            text: tipWindow.text
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 1
        }
    }
}
