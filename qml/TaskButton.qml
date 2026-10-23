import QtQuick

// Reusable clickable button with an optional icon and label.
// Icons are resolved by Shell.iconUrl(): IconKit first, then the system icon theme.
Rectangle {
    id: btn

    Motion {
        id: motion
    }

    property string iconName: ""
    property string label: ""
    property bool active: false
    property bool fillWidth: false
    // The label is Qt StyledText (used for search hit highlighting).
    property bool richLabel: false
    // A running application: small indicator under the icon or after the label.
    property bool running: false
    // Number of windows behind the button; > 1 widens the running indicator.
    property int badge: 0
    // All windows of the entry are minimised (background zone of the dock).
    property bool dimmed: false
    property int iconSize: 22
    // A dot pinned to the top-right corner (used by the category chips).
    property bool cornerDot: false
    // Set to let the window's tooltip surface (PanelToolTip for the panel,
    // ShellToolTip elsewhere) describe the button while hovered.
    property string tooltipText: ""
    // Label for dock buttons
    property string dockLabel: ""
    property bool showDockLabels: false
    property string dockIconColorMode: "theme"
    property string dockCustomIconColor: ""
    property color foregroundColor: Theme.foreground
    // Optional idle material, used by dock icons. Normal buttons remain flat.
    property string surfaceMaterial: "none"
    property color surfaceColor: Theme.surface
    property real surfaceOpacity: -1
    property real hoverOpacity: 0.08
    property real activeOpacity: 0.30
    property color hoverTint: foregroundColor
    property color activeTint: Theme.accent
    // Dock buttons: drag a pinned button to reorder the pinned list.
    property bool reorderable: false
    property bool reordering: false
    readonly property bool hovered: hoverHandler.hovered

    signal clicked()
    // Middle click closes windows (dock behaviour); optional.
    signal middleClicked()
    signal reorderStarted()
    signal reorderMoved(real sceneX, real sceneY)
    signal reorderFinished(real sceneX, real sceneY)

    property real pressSceneX: 0
    property real pressSceneY: 0
    property bool reorderActive: false
    property bool suppressClick: false

    radius: Theme.radius
    implicitWidth: row.implicitWidth + 16
    implicitHeight: Math.max(row.implicitHeight + 10, iconSize + 12)
    // HoverHandler (not MouseArea.containsMouse) so a full-panel hover
    // surface can coexist with the buttons without stealing their hover.
    HoverHandler {
        id: hoverHandler
    }
    // Background-zone buttons fade until the pointer reaches them.
    opacity: reordering ? 0.8
             : (dimmed && !hoverHandler.hovered && !area.pressed) ? 0.55 : 1.0
    Behavior on opacity {
        NumberAnimation { duration: motion.fast }
    }
    color: area.pressed
           ? Qt.rgba(btn.activeTint.r, btn.activeTint.g, btn.activeTint.b, 0.45)
           : btn.active
             ? Qt.rgba(btn.activeTint.r, btn.activeTint.g, btn.activeTint.b,
                       Math.max(0, Math.min(1, btn.activeOpacity)))
             : hoverHandler.hovered
               ? Qt.rgba(btn.hoverTint.r, btn.hoverTint.g, btn.hoverTint.b,
                         Math.max(0, Math.min(1, btn.hoverOpacity)))
               : "transparent"

    Behavior on color {
        ColorAnimation { duration: motion.micro }
    }

    SurfaceStyle {
        anchors.fill: parent
        z: -1
        visible: btn.surfaceMaterial !== "none"
        material: btn.surfaceMaterial
        baseColor: btn.surfaceColor
        materialOpacity: btn.surfaceOpacity
        cornerRadius: btn.radius
        borderWidth: 0
        effectStrength: 0.8
    }

    Row {
        id: row
        spacing: 8
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: btn.fillWidth ? parent.left : undefined
        anchors.leftMargin: 8
        anchors.horizontalCenter: btn.fillWidth ? undefined : parent.horizontalCenter

        Item {
            width: btn.iconSize
            height: btn.iconSize
            anchors.verticalCenter: parent.verticalCenter
            visible: btn.iconName !== ""

            Image {
                id: icon
                anchors.centerIn: parent
                width: btn.iconSize
                height: btn.iconSize
                sourceSize.width: btn.iconSize * 2
                sourceSize.height: btn.iconSize * 2
                fillMode: Image.PreserveAspectFit
                smooth: true
                // Use custom icon color if specified
                source: {
                    var iconColor = btn.foregroundColor
                    // Check if dock icon color mode is custom
                    if (btn.dockIconColorMode === "custom" && btn.dockCustomIconColor.length > 0) {
                        iconColor = btn.dockCustomIconColor
                    } else if (btn.dockIconColorMode === "original") {
                        // Return original icon without tinting
                        return Shell.iconUrl(btn.iconName)
                    }
                    return Shell.tintedIcon(btn.iconName, iconColor)
                }
                // Tiny press dip, hover lift and a lift while reordering.
                scale: btn.reordering ? 1.12
                       : area.pressed ? 0.88 : hoverHandler.hovered ? 1.08 : 1.0
                Behavior on scale {
                    NumberAnimation { duration: motion.micro; easing.type: motion.easeEnter }
                }
            }
        }

        Text {
            visible: btn.label !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: btn.label
            textFormat: btn.richLabel ? Text.StyledText : Text.PlainText
            color: btn.foregroundColor
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
            elide: Text.ElideRight
        }
    }

    // Running indicator
    Rectangle {
        visible: btn.running
        width: Math.min(btn.badge > 1 ? 22 : 12, btn.width - 12)
        height: 3
        radius: 1.5
        color: btn.active ? Theme.accent : btn.dimmed ? Theme.muted : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.75)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 3
        Behavior on width { NumberAnimation { duration: motion.fast; easing.type: motion.easeEnter } }
        Behavior on color { ColorAnimation { duration: motion.fast } }
    }
    // Dock label
    Text {
        visible: btn.dockLabel.length > 0 && btn.showDockLabels
        text: btn.dockLabel
        color: btn.foregroundColor
        font.family: Theme.fontFamily
        font.pointSize: Theme.fontSize - 2
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        anchors.horizontalCenter: parent.horizontalCenter
    }

    Rectangle {
        visible: btn.cornerDot
        width: 8
        height: 8
        radius: 4
        color: Theme.accent
        anchors.right: parent.right
        anchors.rightMargin: 3
        anchors.top: parent.top
        anchors.topMargin: 3
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        cursorShape: btn.reorderable && btn.hovered && !btn.reordering
                     ? Qt.OpenHandCursor : Qt.PointingHandCursor

        onPressed: (mouse) => {
            btn.suppressClick = false;
            btn.reorderActive = false;
            if (mouse.button !== Qt.LeftButton || !btn.reorderable)
                return;
            const scene = btn.mapToItem(null, mouse.x, mouse.y);
            btn.pressSceneX = scene.x;
            btn.pressSceneY = scene.y;
        }
        onPositionChanged: (mouse) => {
            if (!btn.reorderable || !pressed || (mouse.buttons & Qt.LeftButton) === 0)
                return;
            const scene = btn.mapToItem(null, mouse.x, mouse.y);
            const dx = scene.x - btn.pressSceneX;
            const dy = scene.y - btn.pressSceneY;
            if (!btn.reorderActive && Math.sqrt(dx * dx + dy * dy) > 5) {
                btn.reorderActive = true;
                btn.reorderStarted();
            }
            if (btn.reorderActive)
                btn.reorderMoved(scene.x, scene.y);
        }
        onReleased: (mouse) => {
            if (btn.reorderActive) {
                btn.reorderActive = false;
                btn.suppressClick = true;
                const scene = btn.mapToItem(null, mouse.x, mouse.y);
                btn.reorderFinished(scene.x, scene.y);
            }
        }
        onClicked: (mouse) => {
            if (btn.suppressClick) {
                btn.suppressClick = false;
                return;
            }
            if (mouse.button === Qt.MiddleButton)
                btn.middleClicked();
            else
                btn.clicked();
        }
    }

    // Panel buttons report their hover to the panel's tooltip window; other
    // windows keep the in-window ShellToolTip bubble. (In the panel the
    // bubble would fall outside the bar window, so the panel never sets
    // tooltipText - it wires its own HoverHandlers instead.)
    Loader {
        active: btn.tooltipText.length > 0
        anchors.fill: parent
        sourceComponent: ShellToolTip {
            text: btn.tooltipText
        }
    }
}
