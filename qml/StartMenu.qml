import QtQuick
import QtQuick.Window
import QtQuick.Layouts

// Start menu: searchable application list plus category chips, recently used
// applications and session actions.
//
// The menu hangs off the start button of whichever panel opened it: the panel
// hands over the button's rect, so the menu opens above the button on a bottom
// bar, below it on a top bar, beside it on a vertical bar, and slides along
// when the start button of another screen is clicked.
DropdownWindow {
    id: menu

    property var results: []
    property string categoryFilter: ""
    // Index of the highlighted row: the keyboard walks the list, Enter runs
    // the highlighted application.
    property int selectedIndex: -1

    cardWidth: Settings.values["startmenu.width"] || 440
    cardHeight: Settings.values["startmenu.height"] || 580
    padding: 12
    scopeName: "newteller-menu"
    exclusiveKeyboard: true
    // Opened from a keyboard shortcut or D-Bus there is no button to hang off;
    // the menu then sits at the start of the bar, as it always has.
    fallbackCorner: "bottomLeft"

    // Every open starts from a fresh search and the newest application list.
    function opened() {
        search.text = "";
        categoryFilter = "";
        refresh();
        updateRecent();
        search.forceActiveFocus();
        pinMenu.close();
    }

    // Arrow keys: step the highlight through the result list.
    function moveSelection(delta) {
        if (results.length === 0) {
            selectedIndex = -1;
            return;
        }
        var next = selectedIndex < 0 ? (delta > 0 ? 0 : results.length - 1)
                                     : selectedIndex + delta;
        next = Math.max(0, Math.min(results.length - 1, next));
        selectedIndex = next;
        if (list)
            list.positionViewAtIndex(next, ListView.Contain);
    }

    function launchSelected() {
        if (selectedIndex < 0 || selectedIndex >= results.length)
            return false;
        launch(results[selectedIndex]);
        return true;
    }

    function refresh() {
        const q = search.text.trim();
        const limit = Settings.values["startmenu.limit"] || 200;
        var hits = Apps.searchDetailed(q, limit);
        if (hits.length === 0)
            hits = Apps.search(q, limit);
        if (categoryFilter.length > 0) {
            hits = hits.filter(h => {
                const app = Apps.byId(h.id);
                return (app.categories || []).indexOf(categoryFilter) >= 0;
            });
        }
        results = hits;
        selectedIndex = hits.length > 0 ? 0 : -1;
    }

    function launch(app) {
        Apps.launch(app.id);
        hide();
    }

    // --- pin to dock --------------------------------------------------------
    // Right-clicking an application row pins or unpins it: the panel.pinned
    // list of ids is what the dock shows even when nothing runs.
    property var pinTarget: null

    function isPinned(app) {
        return app && ((Settings.values["panel.pinned"] || []).indexOf(app.id) >= 0);
    }

    function togglePin(app) {
        if (!app)
            return;
        const pinned = (Settings.values["panel.pinned"] || []).slice();
        const index = pinned.indexOf(app.id);
        if (index >= 0)
            pinned.splice(index, 1);
        else
            pinned.push(app.id);
        Settings.setValue("panel.pinned", pinned);
    }

    function openPinMenu(button, app) {
        pinTarget = app;
        const pos = button.mapToItem(menu.bodyItem, button.width, 0);
        pinMenu.x = Math.max(8, Math.min(pos.x - pinMenu.width,
                                         menu.bodyItem.width - pinMenu.width - 8));
        pinMenu.y = Math.max(8, pos.y - pinMenu.height - 4);
        pinMenu.open();
    }

    // Hiding is delayed so that clicking the Start button while the menu
    // is open can toggle it closed instead of reopening it.
    onActiveChanged: {
        if (!active)
            closeTimer.restart();
    }

    Timer {
        id: closeTimer
        interval: 200
        onTriggered: {
            if (!menu.active)
                menu.hide();
        }
    }

    // Click-away surface for the pin menu: enabled only while it shows,
    // so it never steals clicks otherwise.
    MouseArea {
        anchors.fill: parent
        z: 40
        enabled: pinMenu.shown
        onClicked: pinMenu.close()
        onWheel: (wheel) => wheel.accepted = pinMenu.shown
    }

    // Pin / unpin popup for the right-clicked application row.
    Rectangle {
        id: pinMenu
        property bool shown: false

        function open() { shown = true }
        function close() { shown = false }

        visible: opacity > 0
        opacity: shown ? 1 : 0
        Behavior on opacity {
            NumberAnimation { duration: 130; easing.type: Easing.OutCubic }
        }

        z: 50
        radius: Theme.radius + 2
        color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.98)
        border.width: 1
        border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.8)

        width: pinColumn.width + 8
        height: pinColumn.height + 8

        Column {
            id: pinColumn
            anchors.centerIn: parent

            TaskButton {
                width: 180
                height: 32
                fillWidth: true
                iconName: menu.isPinned(menu.pinTarget) ? "pin-off" : "pin"
                label: menu.isPinned(menu.pinTarget) ? "Unpin from dock" : "Pin to dock"
                onClicked: {
                    menu.togglePin(menu.pinTarget);
                    pinMenu.close();
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Rectangle {
            id: searchBox
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            radius: Theme.radius
            color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.95)
            border.width: search.activeFocus ? 1 : 0
            border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.7)

            Row {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18
                    height: 18
                    sourceSize.width: 18
                    sourceSize.height: 18
                    fillMode: Image.PreserveAspectFit
                    source: Shell.tintedIcon("search", Theme.foreground)
                    visible: Shell.tintedIcon("search", Theme.foreground).toString().length > 0
                }

                TextInput {
                    id: search
                    objectName: "startSearch"
                    width: parent.width - 26
                    anchors.verticalCenter: parent.verticalCenter
                    focus: true
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize + 1
                    verticalAlignment: TextInput.AlignVCenter
                    onTextChanged: menu.refresh()
                    Keys.onEscapePressed: {
                        if (pinMenu.shown)
                            pinMenu.close();
                        else
                            menu.hide();
                    }
                    Keys.onReturnPressed: {
                        if (!menu.launchSelected() && menu.results.length > 0)
                            menu.launch(menu.results[0]);
                    }
                    Keys.onDownPressed: (event) => {
                        menu.moveSelection(1);
                        event.accepted = true;
                    }
                    Keys.onUpPressed: (event) => {
                        menu.moveSelection(-1);
                        event.accepted = true;
                    }

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: search.text.length === 0
                        text: "Type to search apps..."
                        color: Theme.muted
                        font: search.font
                    }
                }
            }
        }

        // Category chips, filtered by the full application list.
        Flow {
            id: chips
            Layout.fillWidth: true
            spacing: 6
            visible: Apps.categories.length > 0

            Repeater {
                model: Apps.categories.slice(0, 12)

                delegate: TaskButton {
                    required property var modelData
                    label: modelData.name
                    active: menu.categoryFilter === modelData.name
                    onClicked: {
                        menu.categoryFilter = menu.categoryFilter === modelData.name
                                ? "" : modelData.name;
                        menu.refresh();
                    }
                }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: menu.results
            cacheBuffer: 400
            reuseItems: true

            delegate: TaskButton {
                id: appRow
                required property var modelData
                required property int index
                width: ListView.view.width
                fillWidth: true
                iconSize: 24
                iconName: modelData.icon || ""
                richLabel: true
                label: {
                    if (modelData.highlight && modelData.highlight.length > 0)
                        return modelData.highlight;
                    return (modelData.name || modelData.id || "");
                }
                running: Settings.values["panel.showRunning"] !== false
                         && Apps.isRunning(modelData.id)
                active: menu.selectedIndex === index
                onClicked: menu.launch(modelData)

                HoverHandler {
                    onHoveredChanged: {
                        if (hovered)
                            menu.selectedIndex = index;
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: menu.openPinMenu(appRow, modelData)
                }
            }

            Text {
                anchors.centerIn: parent
                visible: menu.results.length === 0
                text: "No applications match"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize
            }
        }

        // Recently launched applications, only with an empty search.
        Column {
            id: recentSection
            Layout.fillWidth: true
            spacing: 6
            visible: Settings.values["startmenu.showRecent"] !== false
                     && search.text.length === 0 && recentModel.length > 0

            property var recentModel: []

            Text {
                text: "Recently used"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }

            Flow {
                width: recentSection.width
                spacing: 6

                Repeater {
                    model: recentSection.recentModel

                    delegate: TaskButton {
                        required property var modelData
                        iconName: modelData.icon || ""
                        onClicked: menu.launch(modelData)
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)
        }

        RowLayout {
            id: footer
            Layout.fillWidth: true
            spacing: 4

            TaskButton {
                iconName: "lock"
                tooltipText: "Lock"
                onClicked: { Shell.lock(); menu.hide(); }
            }
            TaskButton {
                iconName: "log-out"
                tooltipText: "Log out"
                onClicked: { Shell.logout(); menu.hide(); }
            }
            TaskButton {
                iconName: "moon"
                tooltipText: "Sleep"
                onClicked: { Shell.suspend(); menu.hide(); }
            }
            TaskButton {
                iconName: "rotate-cw"
                tooltipText: "Restart"
                onClicked: { Shell.reboot(); menu.hide(); }
            }
            TaskButton {
                iconName: "power"
                tooltipText: "Shut down"
                onClicked: { Shell.powerOff(); menu.hide(); }
            }
            Item { Layout.fillWidth: true }
            TaskButton {
                iconName: "settings"
                label: "Settings"
                onClicked: { Shell.openSettings(); menu.hide(); }
            }
        }
    }

    function updateRecent() {
        recentSection.recentModel = Apps.recent(Settings.values["apps.recentLimit"] || 8);
    }

    Component.onCompleted: updateRecent()
    Connections {
        target: Apps
        function onRunningChanged() {
            // Recent order changes after every launch.
            menu.updateRecent();
        }
    }
}
