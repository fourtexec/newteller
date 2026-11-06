import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import org.kde.layershell 1.0 as LayerShell

// Notification popups, one window per screen. The daemon (src/notifications)
// owns the queue; this window mirrors `Notifications.active` as a stack of
// cards in the configured corner (notifications.position) and dismisses them
// when their timeout elapses or the user clicks.
Window {
    id: notif

    readonly property string corner: Settings.values["notifications.position"] || "top-right"
    readonly property bool topEdge: corner.indexOf("top") === 0
    readonly property bool rightEdge: corner.indexOf("right") > 0
    readonly property bool centered: corner.indexOf("center") > 0
    readonly property int screenMargin: 10
    readonly property int cardWidth: 344
    // Newest at the bottom, at most four cards on screen.
    readonly property var shown: (Notifications.active || []).slice(-4)
    readonly property NotificationAppearance appearance: NotificationAppearance {}
    readonly property int framePad: 8
    // Cards slide in from, and back out towards, the screen edge they are
    // pinned to.
    readonly property int slideFrom: rightEdge ? motion.slide : -motion.slide
    readonly property int slideOut: rightEdge ? Math.round(motion.slide * 0.75)
                                              : -Math.round(motion.slide * 0.75)

    Motion {
        id: motion
    }

    property bool initialized: false

    width: cardWidth + 2 * framePad
    height: Math.min(Screen.height - 2 * screenMargin, stack.contentHeight + 2 * framePad)
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint

    // Positioning without layer-shell (X11, XWayland).
    x: {
        if (centered)
            return Screen.virtualX + (Screen.width - width) / 2;
        return rightEdge ? Screen.virtualX + Screen.width - width - screenMargin
                         : Screen.virtualX + screenMargin;
    }
    y: topEdge ? Screen.virtualY + screenMargin
               : Screen.virtualY + Screen.height - height - screenMargin

    LayerShell.Window.anchors: {
        var a = topEdge ? LayerShell.Window.AnchorTop : LayerShell.Window.AnchorBottom;
        if (centered)
            a |= LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight;
        else
            a |= rightEdge ? LayerShell.Window.AnchorRight : LayerShell.Window.AnchorLeft;
        return a;
    }
    // Urgent notifications (critical battery, pairing codes) show above a
    // fullscreen game or video too: the top layer is hidden then.
    readonly property bool anyUrgent: {
        const list = notif.shown || [];
        for (let i = 0; i < list.length; ++i) {
            if (list[i] && list[i].urgent === true)
                return true;
        }
        return false;
    }
    LayerShell.Window.layer: anyUrgent ? LayerShell.Window.LayerOverlay
                                       : LayerShell.Window.LayerTop
    LayerShell.Window.exclusionZone: 0
    // The keyboard only when a card has a reply field (a click in it then
    // focuses the popup); otherwise popups never take the keyboard.
    readonly property bool anyReplyable: {
        const list = notif.shown || [];
        for (let i = 0; i < list.length; ++i) {
            if (list[i] && list[i].replyable === true)
                return true;
        }
        return false;
    }
    LayerShell.Window.keyboardInteractivity: anyReplyable
                                             ? LayerShell.Window.KeyboardInteractivityOnDemand
                                             : LayerShell.Window.KeyboardInteractivityNone
    LayerShell.Window.scope: "newteller-notifications"
    LayerShell.Window.margins {
        left: centered ? 0 : notif.screenMargin
        right: centered ? 0 : notif.screenMargin
        top: topEdge ? notif.screenMargin : 0
        bottom: topEdge ? 0 : notif.screenMargin
    }

    // The popup's own JSON helper: ListModel list roles come back as nested
    // model objects, so the action pairs travel as a JSON string instead.
    function parseActions(text) {
        try {
            const list = JSON.parse(text);
            return Array.isArray(list) ? list : [];
        } catch (err) {
            return [];
        }
    }

    function sync() {
        const want = notif.shown;
        // Diff the daemon's queue into `cards` by id. Only real changes touch
        // the model, so a card slides in or out on its own while the others
        // settle around it - instead of the whole stack being rebuilt.
        for (var i = cards.count - 1; i >= 0; --i) {
            const id = cards.get(i).id;
            var still = false;
            for (const e of want) {
                if (e && e.id === id) {
                    still = true;
                    break;
                }
            }
            if (!still)
                cards.remove(i);
        }
        for (i = 0; i < want.length; ++i) {
            const e = want[i] || {};
            // Every role on every append, with fixed types: the first append
            // defines the model's roles, so no field may be missing.
            const cardData = {
                id: e.id === undefined ? 0 : e.id,
                app: e.app || "",
                summary: e.summary || "",
                body: e.body || "",
                bodyMarkup: e.bodyMarkup || "",
                replyable: e.replyable === true,
                replyPlaceholder: e.replyPlaceholder || "",
                icon: e.icon || "",
                image: e.image || "",
                value: e.value === undefined ? -1 : e.value,
                actions: JSON.stringify(e.actions || []),
                timeout: e.timeout || 0,
                revision: e.revision || 0
            };
            var at = -1;
            for (var j = 0; j < cards.count; ++j) {
                if (cards.get(j).id === cardData.id) {
                    at = j;
                    break;
                }
            }
            if (at < 0) {
                cards.append(cardData);
            } else {
                // A moved card is updated too (replaced notifications keep
                // their id but not their text).
                if (at !== i)
                    cards.move(at, i, 1);
                cards.set(i, cardData);
            }
        }
        syncVisibility();
    }

    function syncVisibility() {
        const enabled = Settings.values["notifications.enabled"] !== false;
        if (enabled && notif.shown.length > 0) {
            drainTimer.stop();
            visible = true;
            return;
        }
        // Nothing left to show: keep the window up a beat longer so the
        // exit transition can play, then hide.
        if (visible)
            drainTimer.restart();
        else
            visible = false;
    }

    Timer {
        id: drainTimer
        interval: motion.exit + 60
        onTriggered: {
            if (notif.shown.length === 0)
                notif.visible = false;
        }
    }

    Connections {
        target: Notifications
        function onActiveChanged() { notif.sync(); }
        function onHistoryChanged() { notif.sync(); }
    }
    Connections {
        target: Settings
        function onValuesChanged() { notif.sync(); }
    }
    Component.onCompleted: notif.sync()

    // --- cards ---------------------------------------------------------------
    ListView {
        id: stack
        objectName: "notifStack"
        anchors.fill: parent
        anchors.margins: notif.framePad
        spacing: 8
        interactive: false
        model: ListModel {
            id: cards
        }
        // Newest at the bottom.
        verticalLayoutDirection: ListView.TopToBottom

        // A card arrives: slide in from the screen edge while fading.
        add: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: motion.enter
                easing.type: motion.easeEnter
            }
            NumberAnimation {
                property: "x"
                from: notif.slideFrom
                to: 0
                duration: motion.enter
                easing.type: motion.easeEnter
            }
        }
        // A card leaves: back towards the edge, quicker than it came.
        remove: Transition {
            NumberAnimation {
                property: "opacity"
                to: 0
                duration: motion.exit
                easing.type: motion.easeExit
            }
            NumberAnimation {
                property: "x"
                to: notif.slideOut
                duration: motion.exit
                easing.type: motion.easeExit
            }
        }
        // Neighbours settle into the freed slot; they move slower than the
        // card leaves, which reads as the stack breathing, not jumping.
        displaced: Transition {
            NumberAnimation {
                property: "y"
                duration: motion.move
                easing.type: motion.easeEnter
            }
        }

        delegate: ShellFrame {
            id: card
            width: stack.width
            height: cardColumn.implicitHeight + 26
            materialOverride: notif.appearance.material
            backgroundColorOverride: notif.appearance.background
            useBackgroundColorOverride: true
            opacityPercentOverride: notif.appearance.opacityPercent
            cornerRadiusOverride: notif.appearance.cornerRadius

            readonly property var entry: ({
                id: model.id,
                app: model.app,
                summary: model.summary,
                body: model.body,
                bodyMarkup: model.bodyMarkup,
                replyable: model.replyable,
                replyPlaceholder: model.replyPlaceholder,
                icon: model.icon,
                image: model.image,
                value: model.value,
                actions: notif.parseActions(model.actions),
                timeout: model.timeout
            })
            // The spec sends key/label pairs. "default" is not a button: it
            // is what clicking the notification itself does (open the chat,
            // the mail). Keys come first so an empty label cannot shift them.
            readonly property var buttons: {
                const list = entry.actions || [];
                const out = [];
                for (let i = 0; i + 1 < list.length; i += 2) {
                    const key = String(list[i] || "");
                    if (key.length > 0 && key !== "default" && key !== "inline-reply")
                        out.push({ key: key, label: String(list[i + 1] || "") || key });
                }
                return out;
            }
            readonly property bool hasDefault: {
                const list = entry.actions || [];
                for (let i = 0; i < list.length; i += 2) {
                    if (list[i] === "default")
                        return true;
                }
                return false;
            }
            function sendReply() {
                if (replyField.text.trim().length === 0)
                    return;
                Notifications.reply(card.entry.id, replyField.text);
                replyField.text = "";
            }

            // 1.0 -> 0.0 while the timeout runs; drives the progress line.
            property real progress: (entry.timeout || 0) > 0 ? 1.0 : 0.0
            // A new revision (the notification was replaced) starts the
            // timeout again.
            readonly property int revision: model.revision

            // Expire on the daemon-side timeout (0 = sticky).
            function arm() {
                if ((card.entry.timeout || 0) > 0 && card.visible) {
                    lifeTimer.restart();
                    lifeAnimation.restart();
                } else {
                    lifeTimer.stop();
                    lifeAnimation.stop();
                }
            }
            onRevisionChanged: arm()
            onVisibleChanged: arm()
            Component.onCompleted: arm()
            Timer {
                id: lifeTimer
                interval: card.entry.timeout || 0
                onTriggered: Notifications.dismiss(card.entry.id, 1)
            }
            NumberAnimation {
                id: lifeAnimation
                target: card
                property: "progress"
                from: 1.0
                to: 0.0
                duration: Math.max(1, card.entry.timeout || 1)
            }

            // A click on the card: its default action, or just dismiss it.
            function activate() {
                if (card.hasDefault)
                    Notifications.invokeAction(card.entry.id, "default");
                else
                    Notifications.dismiss(card.entry.id, 2);
            }

            MouseArea {
                objectName: "notificationBody"
                anchors.fill: parent
                cursorShape: card.hasDefault ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: card.activate()
            }

            RowLayout {
                id: cardColumn
                anchors.fill: parent
                anchors.margins: 12
                spacing: 11

                // The notification's own accent: a rounded vertical bar in
                // the theme accent colour, echoing the shell chrome.
                Rectangle {
                    Layout.preferredWidth: 3
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignLeft
                    radius: 1.5
                    color: notif.appearance.accent
                }

                Item {
                    // notifications.showAppIcon; the notification's own
                    // picture (an avatar, a cover) shows either way.
                    readonly property bool hasImage: (card.entry.image || "").length > 0
                    visible: hasImage || Settings.values["notifications.showAppIcon"] !== false
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 38
                    Layout.alignment: Qt.AlignTop
                    Image {
                        objectName: "notificationImage"
                        anchors.fill: parent
                        visible: parent.hasImage
                        sourceSize.width: 96
                        sourceSize.height: 96
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        smooth: true
                        source: card.entry.image || ""
                    }
                    Rectangle {
                        anchors.fill: parent
                        visible: !parent.hasImage
                        radius: 12
                        color: Qt.rgba(notif.appearance.accent.r, notif.appearance.accent.g,
                                      notif.appearance.accent.b, 0.18)
                        border.width: 1
                        border.color: Qt.rgba(notif.appearance.accent.r, notif.appearance.accent.g,
                                              notif.appearance.accent.b, 0.35)
                        Image {
                            anchors.centerIn: parent
                            width: 22
                            height: 22
                            sourceSize.width: 44
                            sourceSize.height: 44
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            source: Shell.tintedIcon(card.entry.icon && card.entry.icon.length > 0
                                                     ? card.entry.icon : "bell", notif.appearance.foreground)
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 2

                    Text {
                        Layout.fillWidth: true
                        visible: (card.entry.app || "").length > 0
                        text: (card.entry.app || "").toUpperCase()
                        color: notif.appearance.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 3
                        font.letterSpacing: 0.8
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (card.entry.summary || "").length > 0
                        text: card.entry.summary || ""
                        color: notif.appearance.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize + 1
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (card.entry.body || "").length > 0
                        // The daemon's sanitised markup (bold, italic,
                        // underline, line breaks); never raw HTML.
                        textFormat: card.entry.bodyMarkup ? Text.StyledText : Text.PlainText
                        text: card.entry.bodyMarkup || card.entry.body || ""
                        color: Qt.rgba(notif.appearance.foreground.r, notif.appearance.foreground.g,
                                       notif.appearance.foreground.b, 0.88)
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize
                        wrapMode: Text.Wrap
                        maximumLineCount: 4
                        elide: Text.ElideRight
                    }

                    // The value hint: a level or progress (volume scripts,
                    // copies), 0-100.
                    Rectangle {
                        objectName: "notificationValue"
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        Layout.preferredHeight: 4
                        visible: (card.entry.value === undefined ? -1 : card.entry.value) >= 0
                        radius: 2
                        color: Qt.rgba(notif.appearance.foreground.r, notif.appearance.foreground.g,
                                       notif.appearance.foreground.b, 0.16)
                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: parent.width * Math.max(0, Math.min(100, card.entry.value || 0)) / 100
                            radius: 2
                            color: notif.appearance.accent
                            Behavior on width {
                                NumberAnimation { duration: motion.fast }
                            }
                        }
                    }

                    // Inline reply (Telegram, KDE Connect): answer without
                    // opening the application.
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 6
                        visible: card.entry.replyable === true

                        SettingsTextField {
                            id: replyField
                            objectName: "notificationReply"
                            Layout.fillWidth: true
                            placeholder: card.entry.replyPlaceholder || qsTr("Reply")
                            // Typing keeps the popup open.
                            onTextChanged: {
                                if (text.length > 0)
                                    lifeTimer.stop();
                            }
                            onAccepted: card.sendReply()
                        }
                        TaskButton {
                            objectName: "notificationReplySend"
                            label: qsTr("Send")
                            onClicked: card.sendReply()
                        }
                    }

                    Row {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 6
                        visible: card.buttons.length > 0

                        Repeater {
                            model: card.buttons

                            delegate: TaskButton {
                                required property var modelData
                                objectName: "notificationAction"
                                label: modelData.label
                                onClicked: Notifications.invokeAction(card.entry.id, modelData.key)
                            }
                        }
                    }
                }

                TaskButton {
                    id: closeButton
                    Layout.alignment: Qt.AlignTop
                    width: 26
                    height: 26
                    iconName: "x"
                    // Quiet until the pointer reaches the card.
                    opacity: cardHover.hovered ? 1.0 : 0.35
                    Behavior on opacity {
                        NumberAnimation { duration: motion.fast }
                    }
                    onClicked: Notifications.dismiss(card.entry.id, 2)
                }
            }

            // Remaining-time line along the bottom edge.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 3
                radius: 1.5
                visible: (card.entry.timeout || 0) > 0
                clip: true
                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * card.progress
                    radius: 1.5
                    color: notif.appearance.accent
                }
            }

            HoverHandler {
                id: cardHover
            }
        }
    }
}
