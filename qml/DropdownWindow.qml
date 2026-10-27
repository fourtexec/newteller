import QtQuick
import QtQuick.Window
import org.kde.layershell 1.0 as LayerShell
import "Anchoring.js" as Anchoring

// Base for every dropdown the shell shows: the start menu, quick settings, the
// notification centre, the clock card, the dock menu and the tray menu.
//
// The whole point of the component is that a dropdown is *linked* to the
// widget that toggled it. The owner passes `anchor`, the global rect of its
// button (Anchoring.panelButtonRect() builds one from panel coordinates), and
// the card positions itself next to that rect: centred on the button, clamped
// into the screen, on the side of the bar away from the panel, with a small
// pointer on the card edge that faces the button. Pass a new anchor while the
// card is open (the dock reordered, the screen changed, another button of the
// same kind was clicked) and it slides there.
//
// The card also grows out of its button: openOrigin is derived from where the
// pointer sits, so the scale animation starts at the corner nearest the
// button instead of at the middle of the card.
//
// Content goes inside the card, in a padded item that fills it:
//
//     DropdownWindow {
//         cardWidth: 360
//         cardHeight: 240
//         ColumnLayout { anchors.fill: parent ... }
//     }
//
// Dismissal is uniform: Escape, clicking anywhere else, or the window losing
// focus all route through hide(), which plays the close animation first.
Window {
    id: root

    default property alias content: body.data
    // The padded area the content lives in, for the rare dropdown that needs
    // to position something against it (the start menu's pin popup).
    readonly property alias bodyItem: body

    // --- anchoring ---------------------------------------------------------
    // Global rect of the widget that opens this dropdown, see
    // Anchoring.panelButtonRect(). An empty rect (a keyboard shortcut, or a
    // dropdown opened before any button was clicked) puts the card in the
    // bottom-right corner above the bar.
    property rect anchor: Qt.rect(0, 0, 0, 0)
    // The screen this dropdown belongs to, in global coordinates. Defaults to
    // the window's own screen, which is right for single-screen setups and
    // for the offscreen test platform.
    property rect screenRect: Qt.rect(Screen.virtualX, Screen.virtualY, Screen.width, Screen.height)
    // "bottom", "top", "left" or "right": the edge the panel sits on. Decides
    // which side of the bar the card opens on and which edge the pointer is on.
    property string panelPosition: "bottom"

    // --- card --------------------------------------------------------------
    // Size of the card itself, without the shadow room or the pointer strip
    // the frame adds around it.
    property int cardWidth: 360
    property int cardHeight: 320
    property int padding: 12
    // Space between the card (and its pointer) and the edge of the bar. The
    // frame's shadow room adds a few pixels on top of it.
    property int gap: 2
    // Smallest distance the card keeps to the screen edges.
    property int screenInset: 6
    property int nubSize: 13
    property string scopeName: "newteller-dropdown"
    // The start menu takes the keyboard exclusively (the compositor focuses it
    // and the search field gets every keystroke); menus that only need clicks
    // ask on demand instead, so they never swallow global shortcuts.
    property bool exclusiveKeyboard: false
    // Where to put the card when it is opened without a button (a keyboard
    // shortcut, D-Bus): "bottomRight" or "bottomLeft".
    property string fallbackCorner: "bottomRight"

    // Surface overrides, forwarded to the frame. The notification centre reads
    // them from its own appearance settings.
    property string materialOverride: ""
    property bool useBackgroundColorOverride: false
    property color backgroundColorOverride: Theme.background
    property int opacityPercentOverride: -1
    property int cornerRadiusOverride: -1

    readonly property bool nubVertical: panelPosition === "top" || panelPosition === "bottom"
    readonly property int frameInset: card.inset
    // How far the pointer sticks out of the card edge.
    readonly property int nubRoom: Math.round(nubSize / Math.SQRT2)

    width: cardWidth + 2 * (frameInset + (nubVertical ? 0 : nubRoom))
    height: cardHeight + 2 * (frameInset + (nubVertical ? nubRoom : 0))

    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint

    // --- placement ---------------------------------------------------------
    // Recomputed by reposition() whenever the anchor, the card size, the panel
    // or the screen changes, so an open dropdown follows its button instead of
    // staying put. The position is assigned rather than bound: the platform
    // writes x/y back when a window is shown, which would break a binding on
    // the first frame and leave the card where it started.
    property var placement: ({ x: 0, y: 0, nubEdge: "", nubOffset: 0, flipped: false })
    // The same placement written as layer-shell margins (assigned together
    // with the position, never bound: the grouped margins assignment reads the
    // old value back, which a binding would see as a loop).
    property var layerMargins: ({ left: 0, right: 0, top: 0, bottom: 0 })
    // The pointer points at the button only when there is one.
    readonly property bool anchored: root.anchor.width > 0 && root.anchor.height > 0

    function reposition() {
        const placed = Anchoring.place(root.anchor, Qt.size(root.width, root.height),
                                       root.screenRect, root.panelPosition,
                                       root.gap, root.screenInset, root.fallbackCorner);
        root.placement = placed;
        root.layerMargins = Anchoring.layerMargins(placed, Qt.size(root.width, root.height),
                                                   root.screenRect, root.panelPosition);
        root.x = placed.x;
        root.y = placed.y;
    }

    // The card's own size is what the placement depends on, and it can change
    // after the window is already up (a dock menu gaining rows, a media row
    // appearing in quick settings), so it is watched directly as well.
    onCardWidthChanged: root.reposition()
    onCardHeightChanged: root.reposition()
    onAnchorChanged: root.reposition()
    onWidthChanged: root.reposition()
    onHeightChanged: root.reposition()
    onPanelPositionChanged: root.reposition()
    onScreenRectChanged: root.reposition()
    Component.onCompleted: root.reposition()

    // The first frame after opening is laid out by the scene (text metrics,
    // repeated rows), which can change the card size once more; re-run the
    // placement on the next event-loop turn so the card never opens clipped.
    Timer {
        id: settle
        interval: 0
        repeat: false
        onTriggered: root.reposition()
    }

    function scheduleReposition() {
        settle.restart();
    }

    // The card edge facing the button, and where the pointer sits along it, in
    // card coordinates.
    readonly property string nubEdge: root.anchored ? root.placement.nubEdge : ""
    readonly property real nubOffset: root.anchored
                                      ? Math.round(root.placement.nubOffset
                                                   - root.frameInset - root.nubRoom)
                                      : -1

    // The corner the card scales out of: the one nearest the pointer.
    readonly property int openOrigin: {
        if (root.nubEdge.length === 0)
            return Item.Center;
        const horizontal = root.nubEdge === "top" || root.nubEdge === "bottom";
        const extent = horizontal ? root.cardWidth : root.cardHeight;
        const along = root.nubOffset;
        const atStart = along < extent * 0.34;
        const atEnd = along > extent * 0.66;
        if (root.nubEdge === "top")
            return atStart ? Item.TopLeft : atEnd ? Item.TopRight : Item.Top;
        if (root.nubEdge === "bottom")
            return atStart ? Item.BottomLeft : atEnd ? Item.BottomRight : Item.Bottom;
        if (root.nubEdge === "left")
            return atStart ? Item.TopLeft : atEnd ? Item.BottomLeft : Item.Left;
        return atStart ? Item.TopRight : atEnd ? Item.BottomRight : Item.Right;
    }

    // --- layer shell -------------------------------------------------------
    LayerShell.Window.anchors: {
        if (root.panelPosition === "top")
            return LayerShell.Window.AnchorTop | LayerShell.Window.AnchorLeft;
        if (root.panelPosition === "left")
            return LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorTop;
        if (root.panelPosition === "right")
            return LayerShell.Window.AnchorRight | LayerShell.Window.AnchorTop;
        return LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorLeft;
    }
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.keyboardInteractivity: root.exclusiveKeyboard
                                             ? LayerShell.Window.KeyboardInteractivityExclusive
                                             : LayerShell.Window.KeyboardInteractivityOnDemand
    LayerShell.Window.margins {
        left: root.layerMargins.left
        right: root.layerMargins.right
        top: root.layerMargins.top
        bottom: root.layerMargins.bottom
    }
    LayerShell.Window.scope: root.scopeName

    // --- open / close ------------------------------------------------------
    readonly property bool closing: card.closing

    function prepareOpen() {
        card.prepareOpen();
    }

    function playOpen() {
        card.playOpen();
    }

    function playClose(callback) {
        card.playClose(callback);
    }

    function show() {
        if (visible && !card.closing) {
            // Already open: an anchored dropdown just moves to the new button.
            requestActivate();
            return;
        }
        card.prepareOpen();
        visible = true;
        requestActivate();
        root.opened();
        root.reposition();
        root.scheduleReposition();
        card.playOpen();
    }

    function hide() {
        if (!visible || card.closing)
            return;
        card.playClose(function() { visible = false; });
    }

    function toggle() {
        if (visible && !card.closing)
            hide();
        else
            show();
    }

    // Overridable hooks, so a dropdown can refresh its content on the way in
    // (quick settings reads the mixer, the clock re-reads the date) without
    // having to reimplement show()/hide().
    function opened() {}
    function closed() {}

    onVisibleChanged: {
        if (visible)
            root.reposition();
        else
            root.closed();
    }

    // Escape closes the card, whatever has the keyboard focus inside it.
    Shortcut {
        sequence: "Escape"
        enabled: root.visible
        onActivated: root.hide()
    }

    // Clicking anywhere else, or switching to another window, takes the focus
    // away. A short delay keeps the dropdown from closing while the pointer
    // travels from the button into the card.
    onActiveChanged: {
        if (!active)
            closeTimer.restart();
    }

    Timer {
        id: closeTimer
        interval: 200
        onTriggered: {
            if (!root.active)
                root.hide();
        }
    }

    ShellFrame {
        id: card
        anchors.fill: parent
        openOrigin: root.openOrigin
        materialOverride: root.materialOverride
        useBackgroundColorOverride: root.useBackgroundColorOverride
        backgroundColorOverride: root.backgroundColorOverride
        opacityPercentOverride: root.opacityPercentOverride
        cornerRadiusOverride: root.cornerRadiusOverride
        nubEdge: root.nubEdge
        nubOffset: root.nubOffset
        nubSize: root.nubSize

        // Padded content area: what a dropdown declares goes in here and can
        // simply fill it.
        Item {
            id: body
            anchors.fill: parent
            anchors.margins: root.padding
        }
    }
}
