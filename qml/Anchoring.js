.pragma library

// Placement maths shared by every surface that hangs off a panel button
// (the start menu, quick settings, the notification centre, the clock, the
// dock menu and the tray menu).
//
// A popup is "linked" to the thing that toggles it in two steps:
//
//   1. panelButtonRect() turns a button's rect in *panel* coordinates into a
//      global rect. Panel coordinates are used instead of mapToGlobal()
//      because a layer-shell surface does not know its own position on
//      Wayland; the panel always spans its screen edge, so the panel edge,
//      the bar thickness and the screen rect are enough to place it exactly.
//
//   2. place() puts the card next to that rect: centred on the button and
//      clamped into the screen, on the side away from the bar. It also
//      reports which edge of the card faces the button ("bottom" for a bar at
//      the bottom of the screen) and where along that edge the little pointer
//      nub belongs, so the card visibly points at its button.
//
// Positions are rounded to whole pixels: fractional offsets make text and 1px
// borders look blurry on every compositor.

function clamp(value, low, high) {
    // A card bigger than the screen would otherwise produce low > high.
    return Math.max(low, Math.min(value, Math.max(low, high)));
}

function toRect(x, y, width, height) {
    return Qt.rect(Math.round(x), Math.round(y), Math.round(width), Math.round(height));
}

// The global rect a panel button occupies.
//   local      the button's rect in panel coordinates
//   edge       "bottom" | "top" | "left" | "right": the screen edge the panel is on
//   thickness  the panel's thickness (its height on a horizontal bar)
//   margin     the panel's gap to the screen edge (panel.margin)
//   screen     the screen's global rect
//
// The rect is stretched to the panel's thickness: the gap that matters is the
// bar's edge, and the button may sit inside a floating pill that is not flush
// with the bar.
function panelButtonRect(local, edge, thickness, margin, screen) {
    if (!local || !screen)
        return Qt.rect(0, 0, 0, 0);
    const size = Math.max(1, thickness || local.height || 0);
    if (edge === "top")
        return toRect(screen.x + local.x, screen.y + margin, local.width, size);
    if (edge === "left")
        return toRect(screen.x + margin, screen.y + local.y, size, local.height);
    if (edge === "right")
        return toRect(screen.x + screen.width - size - margin, screen.y + local.y,
                      size, local.height);
    // "bottom" (the default)
    return toRect(screen.x + local.x, screen.y + screen.height - size - margin,
                  local.width, size);
}

// Where to put a card of `size` so that it hangs off `anchor`.
// Returns { x, y, nubEdge, nubOffset, flipped }:
//   x, y       the window position (global)
//   nubEdge    the card edge facing the anchor, "" when there is none
//   nubOffset  where along that edge the nub sits, in card coordinates
//   flipped    true when the card had to move to the other side of the bar
//
// `corner` picks the fallback corner used when there is no anchor:
// "bottomRight" (the default) or "bottomLeft" for surfaces that have always
// lived at the start of the bar.
function place(anchor, size, screen, edge, gap, margin, corner) {
    gap = (gap === undefined || gap === null) ? 8 : gap;
    margin = (margin === undefined || margin === null) ? 8 : margin;
    const screenX = screen ? screen.x : 0;
    const screenY = screen ? screen.y : 0;
    const screenWidth = screen ? screen.width : 1280;
    const screenHeight = screen ? screen.height : 800;

    if (!anchor || anchor.width <= 0 || anchor.height <= 0
        || size.width <= 0 || size.height <= 0) {
        // No anchor (a keyboard invocation, or a popup opened by a shortcut):
        // fall back to the corner above the bar, as before.
        const fallbackX = corner === "bottomLeft" ? screenX + margin
                                                  : screenX + screenWidth - size.width - margin;
        return { x: Math.round(fallbackX),
                 y: Math.round(screenY + screenHeight - size.height - margin),
                 nubEdge: "",
                 nubOffset: Math.round(size.width / 2),
                 flipped: false };
    }

    const minX = screenX + margin;
    const maxX = Math.max(minX, screenX + screenWidth - size.width - margin);
    const minY = screenY + margin;
    const maxY = Math.max(minY, screenY + screenHeight - size.height - margin);
    const centreX = anchor.x + anchor.width / 2;
    const centreY = anchor.y + anchor.height / 2;

    let x = 0;
    let y = 0;
    let nubEdge = "";
    let offset = 0;
    let flipped = false;

    if (edge === "top" || edge === "bottom") {
        x = clamp(centreX - size.width / 2, minX, maxX);
        // Above the bar by default, below a top bar.
        y = edge === "top" ? anchor.y + anchor.height + gap
                           : anchor.y - gap - size.height;
        // On a short screen hang the card on the other side rather than
        // squashing it against the screen edge.
        if (edge === "bottom" && y < minY) {
            y = anchor.y + anchor.height + gap;
            flipped = true;
        } else if (edge === "top" && y > maxY) {
            y = anchor.y - gap - size.height;
            flipped = true;
        }
        if (edge === "bottom")
            nubEdge = flipped ? "top" : "bottom";
        else
            nubEdge = flipped ? "bottom" : "top";
        offset = clamp(centreX - x, 16, Math.max(16, size.width - 16));
    } else {
        y = clamp(centreY - size.height / 2, minY, maxY);
        x = edge === "left" ? anchor.x + anchor.width + gap
                            : anchor.x - gap - size.width;
        if (edge === "left" && x > maxX) {
            x = anchor.x - gap - size.width;
            flipped = true;
        } else if (edge === "right" && x < minX) {
            x = anchor.x + anchor.width + gap;
            flipped = true;
        }
        if (edge === "left")
            nubEdge = flipped ? "right" : "left";
        else
            nubEdge = flipped ? "left" : "right";
        offset = clamp(centreY - y, 16, Math.max(16, size.height - 16));
    }

    return { x: Math.round(clamp(x, minX, maxX)),
             y: Math.round(clamp(y, minY, maxY)),
             nubEdge: nubEdge,
             nubOffset: Math.round(offset),
             flipped: flipped };
}

// Layer-shell margins for a placement.
//
// Layer surfaces are positioned by anchoring them to screen edges plus a
// margin, and every popup anchors the two edges that keep its computed
// position: left+bottom for a bottom bar, left+top for a top bar, left+top
// for a left bar and right+top for a right bar. This returns the four margins
// for that convention (the unused sides stay 0).
function layerMargins(placement, size, screen, edge) {
    const left = Math.round(placement.x - screen.x);
    const top = Math.round(placement.y - screen.y);
    const right = Math.round(screen.x + screen.width - (placement.x + size.width));
    const bottom = Math.round(screen.y + screen.height - (placement.y + size.height));
    if (edge === "top")
        return { left: left, right: 0, top: top, bottom: 0 };
    if (edge === "left")
        return { left: left, right: 0, top: top, bottom: 0 };
    if (edge === "right")
        return { left: 0, right: right, top: top, bottom: 0 };
    return { left: left, right: 0, top: 0, bottom: bottom };
}
