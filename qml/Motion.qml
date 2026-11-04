import QtQuick

// Motion tokens for the whole shell. Every animation in the QML reads its
// durations and curves from here, so the desktop moves as one piece.
//
// The rules that keep the motion quiet instead of busy:
//   * arrivals are fast and settle down (OutCubic),
//   * departures are about a third quicker than arrivals (InCubic),
//   * hover/press feedback is almost instant,
//   * no springs, no bounce, no elastic: overshoot reads as slop,
//   * motion must explain cause and effect - where a window came from,
//     where a card goes - or it does not move at all.
QtObject {
    // animations.enabled turns the shell's motion off (durations 0) and
    // animations.speed scales it (2.0 = twice as fast).
    // 0 when off, else 1 / speed; a duration never rounds to 0 while on.
    readonly property real factor: Settings.values["animations.enabled"] === false ? 0
        : 1 / Math.max(0.1, Number(Settings.values["animations.speed"]) || 1.0)

    // Hover and press feedback: almost instant.
    readonly property int micro: factor > 0 ? Math.max(1, Math.round(110 * factor)) : 0
    // Small state changes: dimming, pills, fades, the auto-hide bar.
    readonly property int fast: factor > 0 ? Math.max(1, Math.round(150 * factor)) : 0
    // Arrivals: popups and cards. Quick, then settle.
    readonly property int enter: factor > 0 ? Math.max(1, Math.round(220 * factor)) : 0
    // Departures are quicker than arrivals.
    readonly property int exit: factor > 0 ? Math.max(1, Math.round(140 * factor)) : 0
    // Stacks settling into place around a card that arrived or left.
    readonly property int move: factor > 0 ? Math.max(1, Math.round(240 * factor)) : 0
    // Things arrive fast and slow down ...
    readonly property int easeEnter: Easing.OutCubic
    // ... and leave quickly, speeding up.
    readonly property int easeExit: Easing.InCubic
    // How far notification cards slide while fading, in px.
    readonly property int slide: 36
}
