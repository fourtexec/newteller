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

    // Settings pages the search finds ("wifi", "resolution", "раскладка"):
    // the title and the keywords, which translations extend with their own
    // words. A hit opens Settings on that page.
    readonly property var settingsPages: [
        { page: "appearance", title: qsTr("Appearance"),
          keywords: "theme dark light wallpaper font colour color language icons cursor " + qsTr("theme dark light wallpaper font colour language icons cursor") },
        { page: "panel", title: qsTr("Panel & dock"),
          keywords: "panel dock taskbar tray clock pinned " + qsTr("panel dock taskbar tray clock pinned") },
        { page: "desktop", title: qsTr("Desktop"),
          keywords: "desktop wallpaper icons widgets background " + qsTr("desktop wallpaper icons widgets background") },
        { page: "apps", title: qsTr("Applications"),
          keywords: "applications startup autostart default " + qsTr("applications startup autostart default") },
        { page: "system", title: qsTr("System"),
          keywords: "system clipboard history network wifi bluetooth " + qsTr("system clipboard history network wifi bluetooth") },
        { page: "notifications", title: qsTr("Notifications"),
          keywords: "notifications sounds do not disturb popups bell " + qsTr("notifications sounds do not disturb popups bell") },
        { page: "windows", title: qsTr("Windows"),
          keywords: "windows title bar snapping hot corners rules shortcuts focus " + qsTr("windows title bar snapping hot corners rules shortcuts focus") },
        { page: "keyboard", title: qsTr("Keyboard"),
          keywords: "keyboard layout language input repeat num lock caps " + qsTr("keyboard layout language input repeat num lock caps") },
        { page: "mouse", title: qsTr("Mouse & touchpad"),
          keywords: "mouse touchpad pointer cursor scroll tap speed " + qsTr("mouse touchpad pointer cursor scroll tap speed") },
        { page: "display", title: qsTr("Displays"),
          keywords: "display screen monitor resolution refresh scale rotation night light " + qsTr("display screen monitor resolution refresh scale rotation night light") },
        { page: "power", title: qsTr("Power & lock"),
          keywords: "power battery sleep suspend lock idle screen off saver " + qsTr("power battery sleep suspend lock idle screen off saver") },
        { page: "updates", title: qsTr("Updates"),
          keywords: "update updates upgrade new version build download " + qsTr("update updates upgrade new version build download") },
        { page: "about", title: qsTr("About"),
          keywords: "about version system information diagnostics " + qsTr("about version system information diagnostics") }
    ]

    // A small calculator: "2+2*3", "(15,5-3)/4", "2^10", "-3 % 2". Numbers,
    // + - * / % ^ and parentheses only (no eval); NaN for anything else.
    function calculate(text) {
        const src = String(text).replace(/,/g, ".").replace(/\s+/g, "");
        if (!/^[0-9.+\-*\/%^()]+$/.test(src) || !/[0-9]/.test(src)
            || !/[+\-*\/%^]/.test(src.replace(/^-/, "")))
            return NaN;
        let pos = 0;
        function peek() { return src[pos]; }
        function number() {
            const start = pos;
            while (pos < src.length && /[0-9.]/.test(src[pos]))
                ++pos;
            const piece = src.slice(start, pos);
            if (piece.length === 0 || piece.split(".").length > 2)
                throw "number";
            return parseFloat(piece);
        }
        function factor() {
            if (peek() === "-") { ++pos; return -factor(); }
            if (peek() === "+") { ++pos; return factor(); }
            let value;
            if (peek() === "(") {
                ++pos;
                value = sum();
                if (peek() !== ")")
                    throw "paren";
                ++pos;
            } else {
                value = number();
            }
            if (peek() === "^") {
                ++pos;
                value = Math.pow(value, factor());
            }
            return value;
        }
        function product() {
            let value = factor();
            while (peek() === "*" || peek() === "/" || peek() === "%") {
                const op = src[pos++];
                const rhs = factor();
                value = op === "*" ? value * rhs : op === "/" ? value / rhs : value % rhs;
            }
            return value;
        }
        function sum() {
            let value = product();
            while (peek() === "+" || peek() === "-") {
                const op = src[pos++];
                const rhs = product();
                value = op === "+" ? value + rhs : value - rhs;
            }
            return value;
        }
        try {
            const value = sum();
            return pos === src.length && isFinite(value) ? value : NaN;
        } catch (err) {
            return NaN;
        }
    }

    function recentFileMatches(query) {
        if (Settings.values["startmenu.recentFiles"] === false)
            return [];
        const needle = query.toLowerCase();
        const out = [];
        const files = Shell.recentFiles(300);
        for (let i = 0; i < files.length && out.length < 5; ++i) {
            const f = files[i];
            if (String(f.name).toLowerCase().indexOf(needle) < 0)
                continue;
            out.push({ id: "", filePath: f.path,
                       icon: f.folder ? "folder" : f.image ? "image" : "file-text",
                       name: qsTr("%1 (recent file)").arg(f.name) });
        }
        return out;
    }

    function settingsMatches(query) {
        const words = query.toLowerCase().split(/\s+/).filter(w => w.length > 0);
        if (words.length === 0)
            return [];
        const out = [];
        for (const page of settingsPages) {
            const haystack = (page.title + " " + page.keywords).toLowerCase();
            if (words.every(w => haystack.indexOf(w) >= 0))
                out.push({ id: "", settingsPage: page.page, icon: "settings",
                           name: qsTr("Settings: %1").arg(page.title) });
        }
        return out;
    }
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
        // Settings pages after the applications (never inside a category),
        // then recently used files whose name matches.
        if (q.length >= 2 && categoryFilter.length === 0) {
            hits = hits.concat(settingsMatches(q));
            hits = hits.concat(recentFileMatches(q));
        }
        // An arithmetic expression: its result comes first.
        const value = calculate(q);
        if (!isNaN(value)) {
            const shown = String(Math.round(value * 1e10) / 1e10);
            hits = [{ id: "", calculation: shown, icon: "type",
                      name: qsTr("= %1 (Enter copies it)").arg(shown) }].concat(hits);
        }
        // Last: hand the words to the browser. Nothing leaves the computer
        // until this row is chosen.
        if (q.length >= 2 && categoryFilter.length === 0 && isNaN(value)
            && Settings.values["startmenu.webSearch"] !== false) {
            hits = hits.concat([{ id: "", webSearch: q, icon: "globe",
                                  name: qsTr("Search the web for \"%1\"").arg(q) }]);
        }
        results = hits;
        selectedIndex = hits.length > 0 ? 0 : -1;
    }

    function launch(app) {
        if (app.filePath) {
            Shell.openPath(app.filePath);
            hide();
            return;
        }
        if (app.calculation !== undefined) {
            Shell.copyToClipboard(app.calculation);
            hide();
            return;
        }
        if (app.settingsPage) {
            Shell.openSettingsPage(app.settingsPage);
            hide();
            return;
        }
        if (app.webSearch !== undefined) {
            const address = String(Settings.values["startmenu.webSearchUrl"]
                                   || "https://duckduckgo.com/?q=%1");
            Shell.openUrl(address.replace("%1", encodeURIComponent(app.webSearch)));
            hide();
            return;
        }
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

    // --- log out / restart / shut down, confirmed ----------------------------
    property string pendingPower: ""
    property int powerCountdown: 0

    function askPower(action) {
        pendingPower = action;
        powerCountdown = 30;
        powerTimer.restart();
    }
    function cancelPower() {
        pendingPower = "";
        powerTimer.stop();
    }
    function runPower() {
        const action = pendingPower;
        cancelPower();
        menu.hide();
        if (action === "logout")
            Shell.logout();
        else if (action === "reboot")
            Shell.reboot();
        else if (action === "powerOff")
            Shell.powerOff();
    }
    Timer {
        id: powerTimer
        interval: 1000
        repeat: true
        onTriggered: {
            menu.powerCountdown -= 1;
            if (menu.powerCountdown <= 0)
                menu.runPower();
        }
    }
    onVisibleChanged: {
        if (!visible && pendingPower.length > 0)
            cancelPower();
    }

    function openPinMenu(button, app) {
        // Settings rows cannot be pinned.
        if (!app || !app.id)
            return;
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

            // The application's own actions ("New Private Window", ...).
            Repeater {
                model: {
                    if (!menu.pinTarget || !menu.pinTarget.id)
                        return [];
                    const app = Apps.byId(menu.pinTarget.id);
                    return app && app.actions ? app.actions : [];
                }

                delegate: TaskButton {
                    required property var modelData
                    objectName: "startAppAction"
                    width: 180
                    height: 32
                    fillWidth: true
                    iconName: "chevron-right"
                    label: modelData.name || modelData.id || ""
                    onClicked: {
                        const app = menu.pinTarget;
                        pinMenu.close();
                        menu.hide();
                        if (app && app.id)
                            Apps.launchAction(app.id, modelData.id);
                    }
                }
            }

            TaskButton {
                width: 180
                height: 32
                fillWidth: true
                iconName: menu.isPinned(menu.pinTarget) ? "pin-off" : "pin"
                label: menu.isPinned(menu.pinTarget) ? qsTr("Unpin from dock") : qsTr("Pin to dock")
                onClicked: {
                    menu.togglePin(menu.pinTarget);
                    pinMenu.close();
                }
            }
            TaskButton {
                objectName: "startAddToDesktop"
                width: 180
                height: 32
                fillWidth: true
                iconName: "monitor"
                label: qsTr("Add to desktop")
                onClicked: {
                    const app = menu.pinTarget;
                    pinMenu.close();
                    if (app && app.path)
                        Shell.addToDesktop(app.path);
                }
            }
            TaskButton {
                objectName: "startOpenWithRoot"
                width: 180
                height: 32
                fillWidth: true
                iconName: "shield"
                label: qsTr("Open with root")
                onClicked: {
                    const app = menu.pinTarget;
                    pinMenu.close();
                    menu.hide();
                    if (app)
                        Apps.launchAsRoot(app.id);
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
                        text: qsTr("Type to search apps...")
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
            // search.showCategories (Settings -> Start menu).
            visible: Apps.categories.length > 0 && Settings.values["search.showCategories"] !== false

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
                text: qsTr("No applications match")
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
                text: qsTr("Recently used")
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

        // Log out, restart and shut down ask first: one stray click must not
        // throw away unsaved work. The action runs on confirmation or after
        // the countdown; closing the menu cancels it.
        Rectangle {
            id: powerConfirm
            objectName: "powerConfirm"
            visible: menu.pendingPower.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: powerRow.implicitHeight + 16
            radius: Theme.radius
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
            border.width: 1
            border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)

            RowLayout {
                id: powerRow
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: menu.pendingPower === "logout" ? qsTr("Logging out in %1 s").arg(menu.powerCountdown)
                          : menu.pendingPower === "reboot" ? qsTr("Restarting in %1 s").arg(menu.powerCountdown)
                          : qsTr("Shutting down in %1 s").arg(menu.powerCountdown)
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                }
                TaskButton {
                    objectName: "powerConfirmNow"
                    label: menu.pendingPower === "logout" ? qsTr("Log out")
                           : menu.pendingPower === "reboot" ? qsTr("Restart") : qsTr("Shut down")
                    active: true
                    onClicked: menu.runPower()
                }
                TaskButton {
                    label: qsTr("Cancel")
                    onClicked: menu.cancelPower()
                }
            }
        }

        RowLayout {
            id: footer
            Layout.fillWidth: true
            spacing: 4

            TaskButton {
                iconName: "lock"
                tooltipText: qsTr("Lock")
                onClicked: { Shell.lock(); menu.hide(); }
            }
            TaskButton {
                iconName: "log-out"
                tooltipText: qsTr("Log out")
                onClicked: menu.askPower("logout")
            }
            TaskButton {
                iconName: "moon"
                tooltipText: qsTr("Sleep")
                onClicked: { Shell.suspend(); menu.hide(); }
            }
            TaskButton {
                objectName: "hibernateButton"
                visible: Shell.canHibernate
                iconName: "save"
                tooltipText: qsTr("Hibernate")
                onClicked: { Shell.hibernate(); menu.hide(); }
            }
            TaskButton {
                iconName: "rotate-cw"
                tooltipText: qsTr("Restart")
                onClicked: menu.askPower("reboot")
            }
            TaskButton {
                iconName: "power"
                tooltipText: qsTr("Shut down")
                onClicked: menu.askPower("powerOff")
            }
            Item { Layout.fillWidth: true }
            TaskButton {
                iconName: "settings"
                label: qsTr("Settings")
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
