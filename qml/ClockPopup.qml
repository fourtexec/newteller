import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import org.kde.layershell 1.0 as LayerShell

// The clock popup: tapping the panel clock opens a card with a large digital
// read-out, an analogue dial and a month calendar. Lives on the same layer as
// the start menu, anchored at the end of the panel, on the side opposite of
// the bar so it never covers the clock it came from.
Window {
    id: clock

    // The panel that owns this popup.
    property string panelPosition: "bottom"
    property int panelThickness: 48
    property int edgeMargin: 0

    // The month the calendar shows; starts on the current month.
    property int viewYear: Shell.now.getFullYear()
    property int viewMonth: Shell.now.getMonth()

    width: 320
    height: 470
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint

    // Positioning without layer-shell (X11, XWayland): the end of the panel,
    // on the opposite side of the bar.
    readonly property int sideGap: 8
    x: {
        if (panelPosition === "right")
            return Screen.virtualX + 8;
        return Screen.virtualX + Screen.width - width - sideGap;
    }
    y: {
        if (panelPosition === "bottom")
            return Screen.virtualY + Screen.height - height - panelThickness - edgeMargin - 6;
        if (panelPosition === "top")
            return Screen.virtualY + panelThickness + edgeMargin + 6;
        return Screen.virtualY + sideGap;
    }

    LayerShell.Window.anchors: {
        if (panelPosition === "bottom")
            return LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorRight;
        if (panelPosition === "top")
            return LayerShell.Window.AnchorTop | LayerShell.Window.AnchorRight;
        if (panelPosition === "right")
            return LayerShell.Window.AnchorTop | LayerShell.Window.AnchorLeft;
        return LayerShell.Window.AnchorTop | LayerShell.Window.AnchorRight;
    }
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.scope: "newteller-clock"
    LayerShell.Window.margins {
        right: clock.panelPosition === "left" ? clock.sideGap : 8
        left: clock.panelPosition === "right" ? clock.panelThickness + 6 : 0
        top: clock.panelPosition === "top" ? clock.panelThickness + clock.edgeMargin + 6
              : clock.panelPosition === "bottom" ? 0 : clock.sideGap
        bottom: clock.panelPosition === "bottom" ? clock.panelThickness + clock.edgeMargin + 6
                 : clock.panelPosition === "top" ? 0 : 8
    }

    function show() {
        viewYear = Shell.now.getFullYear();
        viewMonth = Shell.now.getMonth();
        visible = true;
        requestActivate();
        card.scale = 0.98;
        card.opacity = 0.6;
        card.scale = 1.0;
        card.opacity = 1.0;
    }

    function hide() {
        visible = false;
    }

    function toggle() {
        if (visible)
            hide();
        else
            show();
    }

    onActiveChanged: {
        if (!active)
            closeTimer.restart();
    }

    Timer {
        id: closeTimer
        interval: 200
        onTriggered: {
            if (!clock.active)
                clock.hide();
        }
    }

    Connections {
        target: Shell
        function onNowChanged() {
            dial.requestPaint();
            if (!clock.visible)
                return;
            // Keep the calendar honest when the popup spans midnight on the
            // first of a month and no month button was used yet.
            if (clock.viewYear === Shell.now.getFullYear()
                && clock.viewMonth === Shell.now.getMonth())
                calGrid.cells = clock.buildCalendar(clock.viewYear, clock.viewMonth);
        }
    }

    // Builds a Monday-first 6x7 grid of the given month; empty cells null.
    function buildCalendar(year, month) {
        const start = (new Date(year, month, 1).getDay() + 6) % 7;
        const days = new Date(year, month + 1, 0).getDate();
        const cells = [];
        for (var i = 0; i < start; ++i)
            cells.push(null);
        for (var d = 1; d <= days; ++d)
            cells.push(d);
        while (cells.length % 7 !== 0)
            cells.push(null);
        return cells;
    }

    function monthName(month) {
        return Qt.formatDateTime(new Date(clock.viewYear, month, 1), "MMMM");
    }

    ShellFrame {
        id: card
        anchors.fill: parent

        Behavior on scale {
            NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
        }
        Behavior on opacity {
            NumberAnimation { duration: 140 }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            // --- digital clock -------------------------------------------
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: Qt.formatDateTime(Shell.now, "HH:mm:ss")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize + 16
                font.weight: Font.Light
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: Qt.formatDateTime(Shell.now, "dddd, d MMMM yyyy")
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize
            }

            // --- analogue dial -------------------------------------------
            Canvas {
                id: dial
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 128
                Layout.preferredHeight: 128

                onPaint: {
                    const ctx = getContext("2d");
                    const cx = width / 2, cy = height / 2;
                    const r = Math.min(width, height) / 2 - 3;
                    ctx.reset();
                    ctx.lineCap = "round";

                    // Face.
                    ctx.beginPath();
                    ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                    ctx.fillStyle = Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.85).toString();
                    ctx.fill();
                    ctx.lineWidth = 1.5;
                    ctx.strokeStyle = Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.9).toString();
                    ctx.stroke();

                    // Minute ticks and hour marks.
                    for (var i = 0; i < 60; ++i) {
                        const a = i * Math.PI / 30;
                        // NB: "long" is a reserved word in the QML parser,
                        // so the hour marks are called "major" here.
                        const major = i % 5 === 0;
                        const r1 = r - (major ? 8 : 4);
                        ctx.beginPath();
                        ctx.moveTo(cx + r1 * Math.sin(a), cy - r1 * Math.cos(a));
                        ctx.lineTo(cx + (r - 2) * Math.sin(a), cy - (r - 2) * Math.cos(a));
                        ctx.lineWidth = major ? 2 : 1;
                        ctx.strokeStyle = major ? Theme.muted.toString()
                                                : Qt.rgba(Theme.muted.r, Theme.muted.g, Theme.muted.b, 0.5).toString();
                        ctx.stroke();
                    }

                    // Hands.
                    const now = Shell.now;
                    const h = now.getHours() % 12 + now.getMinutes() / 60;
                    const m = now.getMinutes() + now.getSeconds() / 60;
                    const s = now.getSeconds();
                    hand(ctx, cx, cy, h * Math.PI / 6, r * 0.5, 3.5, Theme.foreground.toString());
                    hand(ctx, cx, cy, m * Math.PI / 30, r * 0.74, 2.5, Theme.foreground.toString());
                    hand(ctx, cx, cy, s * Math.PI / 30, r * 0.8, 1.2, Theme.accent.toString());
                    ctx.beginPath();
                    ctx.arc(cx, cy, 2.5, 0, 2 * Math.PI);
                    ctx.fillStyle = Theme.accent.toString();
                    ctx.fill();
                }

                function hand(ctx, cx, cy, angle, length, width, colour) {
                    ctx.beginPath();
                    ctx.moveTo(cx, cy);
                    ctx.lineTo(cx + length * Math.sin(angle), cy - length * Math.cos(angle));
                    ctx.lineWidth = width;
                    ctx.strokeStyle = colour;
                    ctx.stroke();
                }
            }

            // --- calendar --------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    text: clock.monthName(clock.viewMonth) + " " + clock.viewYear
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize + 1
                    font.bold: true
                }
                TaskButton {
                    width: 26; height: 26
                    iconName: "chevron-left"
                    onClicked: {
                        if (clock.viewMonth === 0) {
                            clock.viewMonth = 11;
                            --clock.viewYear;
                        } else {
                            --clock.viewMonth;
                        }
                        calGrid.model = clock.buildCalendar(clock.viewYear, clock.viewMonth);
                    }
                }
                TaskButton {
                    width: 26; height: 26
                    iconName: "chevron-right"
                    onClicked: {
                        if (clock.viewMonth === 11) {
                            clock.viewMonth = 0;
                            ++clock.viewYear;
                        } else {
                            ++clock.viewMonth;
                        }
                        calGrid.model = clock.buildCalendar(clock.viewYear, clock.viewMonth);
                    }
                }
            }

            Grid {
                id: calGrid
                Layout.fillWidth: true
                columns: 7
                columnSpacing: 0
                rowSpacing: 2
                property var cells: clock.buildCalendar(clock.viewYear, clock.viewMonth)

                // Weekday header, Monday first.
                Repeater {
                    model: ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"]
                    delegate: Text {
                        required property var modelData
                        width: 40
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 2
                    }
                }

                Repeater {
                    model: calGrid.cells

                    delegate: Item {
                        required property var modelData
                        width: 40
                        height: 26
                        visible: modelData !== null

                        readonly property bool today: modelData !== null
                            && clock.viewYear === Shell.now.getFullYear()
                            && clock.viewMonth === Shell.now.getMonth()
                            && modelData === Shell.now.getDate()

                        Rectangle {
                            anchors.centerIn: parent
                            width: 24
                            height: 24
                            radius: 12
                            visible: parent.today
                            color: Theme.accent
                        }
                        Text {
                            anchors.centerIn: parent
                            text: parent.modelData === null ? "" : parent.modelData
                            color: parent.today ? "#ffffff" : Theme.foreground
                            font.family: Theme.fontFamily
                            font.pointSize: Theme.fontSize - 1
                            font.bold: parent.today
                        }
                    }
                }
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "Week " + Qt.formatDateTime(Shell.now, "ww")
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 2
            }
        }
    }

    Component.onCompleted: dial.requestPaint()
}
