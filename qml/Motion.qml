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
    // Hover and press feedback: almost instant.
    readonly property int micro: 110
    // Small state changes: dimming, pills, fades, the auto-hide bar.
    readonly property int fast: 150
    // Arrivals: popups and cards. Quick, then settle.
    readonly property int enter: 220
    // Departures are quicker than arrivals.
    readonly property int exit: 140
    // Stacks settling into place around a card that arrived or left.
    readonly property int move: 240
    // Things arrive fast and slow down ...
    readonly property int easeEnter: Easing.OutCubic
    // ... and leave quickly, speeding up.
    readonly property int easeExit: Easing.InCubic
    // How far notification cards slide while fading, in px.
    readonly property int slide: 36
}
