import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import org.kde.layershell 1.0 as LayerShell
import NewTeller 1.0
import "Anchoring.js" as Anchoring

// Panel: start button, the dock (pinned applications + every running window),
// plugin slots and the clock. The position is configurable (bottom by default,
// also top/left/right), the bar can be narrow and centred, and auto-hide
// slides it away after a short delay.
//
// The dock adapts to the situation: applications whose windows are all
// minimised move into a separate, dimmed zone at the end of the strip, the
// icons shrink when many applications run at once, and the strip becomes
// scrollable when even the smallest icons do not fit.
Window {
    id: panel

    // workspaces.names: "Work, Web, Chat"; unnamed desktops are numbered.
    function desktopName(index) {
        const names = String(Settings.values["workspaces.names"] || "").split(",");
        const name = index < names.length ? names[index].trim() : "";
        return name.length > 0 ? name : qsTr("Desktop %1").arg(index + 1);
    }

    property var startMenu
    // The single notification centre and quick settings card on the primary
    // screen; assigned by Main.qml. The bell and the indicators button live
    // in every panel, so they talk to these shared windows.
    property var notificationCenter: null
    property var quickSettings: null
    // "bottom", "top", "left" or "right".
    readonly property string panelPosition: Settings.values["panel.position"] || "bottom"
    readonly property bool vertical: panelPosition === "left" || panelPosition === "right"
    readonly property bool autoHide: Settings.values["panel.autoHide"] === true
    // The bar is never thinner than its dock needs: a bigger icon size or
    // more rows make it grow instead of clipping the icons.
    readonly property int barThickness: Math.max(Settings.values["panel.height"] || 48,
                                                 dockNeededThickness)
    readonly property int edgeMargin: Settings.values["panel.margin"] || 0
    readonly property int autoHideDelay: Math.max(0, Number(Settings.values["panel.autoHideDelay"] ?? 400))
    // Kept true for autoHideDelay after the pointer leaves, so the bar does
    // not vanish while the pointer crosses it.
    property bool autoHideHold: false
    // Set while a dropdown of this panel is open, so the bar does not slide
    // away from under it.
    readonly property bool dropdownOpen: panel.hasAnyOpen([panel.startMenu, panel.notificationCenter,
                                                           panel.quickSettings, clockPopup,
                                                           dockMenu, trayMenu])
    readonly property bool revealed: !autoHide || hoverArea.containsMouse
                                     || autoHideHold || dropdownOpen
    // Set while the start menu is opening, before its window is visible yet.
    property bool menuWanted: false
    readonly property var panelPlugins: Plugins.plugins.filter(p => p.type === "panel" && p.enabled)

    Motion {
        id: motion
    }

    // --- dock configuration ---------------------------------------------
    // Every started application is shown: windows come from the `Windows`
    // object (EWMH on X11, wlr-foreign-toplevel on Wayland), with the /proc
    // based application list of `Apps` as the fallback on other compositors.
    readonly property bool showRunning: Settings.values["panel.showRunning"] !== false
    readonly property bool dockShowWindows: Settings.values["dock.showWindows"] !== false
    readonly property bool dockGroup: Settings.values["dock.group"] !== false
    readonly property bool separateBackground: Settings.values["dock.separateBackground"] !== false
    readonly property bool shrinkIcons: Settings.values["dock.shrinkIcons"] !== false
    readonly property int dockIconSize: Settings.values["dock.iconSize"] || 28
    // The quick settings cluster at the end of the bar (bell + indicators).
    readonly property bool showIndicators: Settings.values["panel.indicators"] !== false
    // The desktop dots, when the compositor runs more than one desktop.
    readonly property bool showWorkspaces: Session.workspaceCount > 1
                                           && Settings.values["workspaces.showOnPanel"] !== false
    // The layout indicator, when the compositor reports more than one layout.
    readonly property bool showKeyboardLayout: Session.keyboardLayouts.length > 1
                                               && Settings.values["keyboard.showIndicator"] !== false
    // The tray shows only when the shell won the StatusNotifierWatcher name
    // and at least one item registered.
    readonly property bool showTray: Tray.available
                                     && Settings.values["tray.enabled"] !== false
                                     && Tray.count > 0
    // Icon size after the adaptive shrink pass (recomputed in rebuildDock).
    property int effectiveIconSize: dockIconSize
    // Window-title labels next to icons when ungrouped (recomputed in rebuildDock).
    property bool showDockLabels: false
    readonly property bool dockShowAppLabels: Settings.values["dock.showLabels"] === true
    readonly property string dockLabelPosition: Settings.values["dock.labelPosition"] || "bottom"
    readonly property string dockIconColorMode: Settings.values["dock.iconColorMode"] || "theme"
    readonly property string dockCustomIconColor: Settings.values["dock.customIconColor"] || "#ffffff"
    readonly property bool useCustomPanelColors: Settings.values["panel.customColors"] === true
    readonly property string panelBackgroundColor: Settings.values["panel.backgroundColor"] || ""
    readonly property real panelBackgroundOpacity: Settings.values["panel.backgroundOpacity"] ?? 0.92
    readonly property string panelForegroundColor: Settings.values["panel.foregroundColor"] || ""
    readonly property string panelMaterial: Settings.values["panel.material"] || "theme"
    readonly property color panelSurfaceColor: useCustomPanelColors && panelBackgroundColor.length > 0
                                               ? Qt.color(panelBackgroundColor) : Theme.background
    readonly property real panelSurfaceOpacity: {
        if (panel.panelMaterial === "opaque") return 1;
        if (panel.panelMaterial === "translucent") return 0.5;
        if (panel.panelMaterial === "frosted") return 0.68;
        if (panel.panelMaterial === "liquid") return 0.5;
        return useCustomPanelColors && panelBackgroundColor.length > 0
               ? Math.max(0, Math.min(1, panel.panelBackgroundOpacity)) : Theme.opacity;
    }
    // panel.style: "pill" (only the dock pill is drawn, the default) or
    // "bar" (a bar along the whole panel, in panel.material and the custom
    // panel colours, with panel.cornerRadius; -1 rounds a floating bar).
    readonly property bool barStyle: Settings.values["panel.style"] === "bar"
    readonly property int barRadius: {
        const r = Number(Settings.values["panel.cornerRadius"] ?? -1);
        if (r >= 0)
            return Math.min(r, Math.floor(barThickness / 2));
        return edgeMargin > 0 || panelLength < screenSpan ? Theme.radius + 2 : 0;
    }
    // panel.length: the share of the screen edge the panel takes (percent),
    // centred; shorter than 100 makes a floating bar.
    readonly property int screenSpan: vertical ? Screen.height : Screen.width
    readonly property int panelLength: Math.round(screenSpan * Math.max(30, Math.min(100, Number(Settings.values["panel.length"] ?? 100))) / 100)
    readonly property int alongOffset: Math.max(0, Math.round((screenSpan - panelLength) / 2))
    readonly property bool panelBlurs: ["frosted", "liquid", "translucent"].indexOf(panelMaterial) >= 0
    readonly property color panelTextColor: useCustomPanelColors && panelForegroundColor.length > 0
                                           ? Qt.color(panelForegroundColor) : Theme.foreground
    readonly property string dockMaterial: Settings.values["dock.material"] || "none"
    // The floating dock pill (set by the bar layouts) and whether the
    // compositor should blur behind it.
    property Item dockPill: null
    readonly property bool dockBlurs: ["frosted", "liquid", "translucent"].indexOf(dockMaterial) >= 0
    function updateBlur() {
        const screenName = panel.screen ? panel.screen.name : "";
        // The bar style blurs behind the whole bar, the pill style behind
        // the pill only.
        const target = panel.barStyle ? barBackground : panel.dockPill;
        const blurs = panel.barStyle ? panel.panelBlurs : panel.dockBlurs;
        if (!panel.visible || !target || !blurs || !panel.revealed) {
            Session.clearBlur(String(panel), "newteller-panel", screenName);
            return;
        }
        const p = target.mapToItem(null, 0, 0);
        Session.setBlur(String(panel), "newteller-panel", screenName, Math.round(p.x), Math.round(p.y),
                        Math.round(target.width), Math.round(target.height),
                        panel.barStyle ? panel.barRadius
                                       : Math.round(Math.min(target.width, target.height) / 2));
    }
    onBarStyleChanged: Qt.callLater(panel.updateBlur)
    onPanelBlursChanged: Qt.callLater(panel.updateBlur)
    onBarRadiusChanged: Qt.callLater(panel.updateBlur)
    onDockBlursChanged: Qt.callLater(panel.updateBlur)
    onRevealedChanged: Qt.callLater(panel.updateBlur)
    onWidthChanged: Qt.callLater(panel.updateBlur)
    onHeightChanged: Qt.callLater(panel.updateBlur)
    readonly property color dockSurfaceColor: Settings.values["dock.backgroundColor"]
                                              && Settings.values["dock.backgroundColor"].length > 0
                                              ? Qt.color(Settings.values["dock.backgroundColor"])
                                              : Theme.surface
    // `dock.backgroundOpacity` (0..1, 0.5 = neutral) biases the material's own
    // preset: sliding up makes the pill more solid, sliding down clears it.
    // The presets themselves live in DockSurface, so each material keeps the
    // transparency that makes it recognisable.
    readonly property real dockSurfaceOpacity: {
        if (panel.dockMaterial === "opaque") return 1;
        if (panel.dockMaterial === "none") return 0;
        const bias = Math.max(0, Math.min(1, Number(Settings.values["dock.backgroundOpacity"] ?? 0.5)));
        const preset = panel.dockMaterial === "frosted" ? 0.50
                     : panel.dockMaterial === "liquid" ? 0.30
                     : panel.dockMaterial === "translucent" ? 0.42
                     : 0.88;
        // 0.2 -> 0.85x (clearer), 0.5 -> 1x (preset), 1.0 -> 1.4x (more solid).
        return Math.max(0.12, Math.min(1, preset * (0.75 + bias * 0.5)));
    }
    // Room for the floating dock itself; it never becomes a full-width bar.
    readonly property int dockMaxWidth: Math.max(220, Math.min(1200, panel.width - 380))
    readonly property int dockPadding: 8
    readonly property real dockHoverOpacity: Math.max(0, Math.min(1, Number(Settings.values["dock.hoverOpacity"] ?? 0.15)))
    readonly property real dockActiveOpacity: Math.max(0, Math.min(1, Number(Settings.values["dock.activeOpacity"] ?? 0.30)))
    readonly property int panelSpacing: Settings.values["panel.spacing"] || 8
    readonly property int panelLeftSpacing: Settings.values["panel.leftSpacing"] || 8
    readonly property int panelRightSpacing: Settings.values["panel.rightSpacing"] || 8
    readonly property int panelZoneSpacing: Settings.values["panel.zoneSpacing"] || 8
    readonly property string dockPositionInPanel: Settings.values["dock.position"] || "left"
    readonly property bool centerDock: Settings.values["panel.centerDock"] === true
    readonly property int dockRows: Math.max(1, Math.min(3, Number(Settings.values["dock.rows"] ?? 1)))
    // Wrapped rows only make sense on a horizontal bar; a vertical bar keeps
    // the single column.
    readonly property bool dockMultiRow: Settings.values["dock.multiRow"] === true && !vertical
    readonly property bool dockScrollable: Settings.values["dock.scrollable"] !== false
    readonly property int dockIconSpacing: Math.max(0, Number(Settings.values["dock.iconSpacing"] ?? 4))
    // Inside the bar: the floating pill is 4 px smaller than the bar and
    // padded by dockPadding on each side.
    readonly property int dockNeededThickness: dockMultiRow
        ? dockRows * (dockIconSize + 8) + dockIconSpacing * (dockRows - 1) + 4 + 2 * dockPadding
        : dockIconSize + 4 + 2 * dockPadding
    // Button height of a wrapped dock; -1 means "fill the strip".
    readonly property int dockButtonHeight: dockMultiRow
        ? Math.max(20, Math.floor((barThickness - 4 - 2 * dockPadding
                                   - dockIconSpacing * (dockRows - 1)) / dockRows))
        : -1
    // A wrapped dock is as wide as its longest row, never wider than the
    // room the bar has.
    readonly property int dockWrapWidth: Math.ceil(Math.max(1, dockEntries.length) / dockRows)
                                         * (effectiveIconSize + 16 + dockIconSpacing) + 8
    // A vertical dock leaves room for the start button, tray and clock.
    readonly property int dockMaxHeight: Math.max(120, panel.height - 220)
    // 0 = start of the strip, 1 = centred, 2 = end (dock.position/panel.centerDock).
    readonly property int dockAlign: {
        if (centerDock || dockPositionInPanel === "center")
            return 1;
        return dockPositionInPanel === "right" ? 2 : 0;
    }
    // One entry per dock button:
    //   { appId, app, windows: [], running, active, badge, title, pinned, background }
    // `dockEntries` is the combined list (main zone first), so the dock menu
    // and external users always see everything.
    property var dockEntries: []
    property var dockMainEntries: []
    property var dockBackgroundEntries: []
    // Pinned buttons are draggable: the app id being dragged and the insertion
    // slot currently under the pointer (index into the main dock list).
    property string dockDraggingAppId: ""
    property int dockDropIndex: -1

    visible: true
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool
    // The hover strip of an auto-hidden panel is a few pixels at the edge.
    readonly property int stripSize: 4
    readonly property int span: revealed ? barThickness : stripSize

    width: vertical ? span + (revealed ? edgeMargin : 0) : panelLength
    height: vertical ? panelLength : span + (revealed ? edgeMargin : 0)
    x: {
        if (panelPosition === "left")
            return Screen.virtualX + (revealed ? edgeMargin : 0);
        if (panelPosition === "right")
            return Screen.virtualX + Screen.width - width;
        return Screen.virtualX + alongOffset;
    }
    y: {
        if (panelPosition === "top")
            return Screen.virtualY + (revealed ? edgeMargin : 0);
        if (panelPosition === "bottom")
            return Screen.virtualY + Screen.height - height;
        return Screen.virtualY + alongOffset;
    }

    // Wayland: a layer surface along the chosen edge that reserves its space.
    // A shorter panel is anchored to its edge only, which centres it.
    LayerShell.Window.anchors: {
        if (panelLength < screenSpan) {
            if (panelPosition === "top")
                return LayerShell.Window.AnchorTop;
            if (panelPosition === "left")
                return LayerShell.Window.AnchorLeft;
            if (panelPosition === "right")
                return LayerShell.Window.AnchorRight;
            return LayerShell.Window.AnchorBottom;
        }
        if (panelPosition === "top")
            return LayerShell.Window.AnchorTop | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight;
        if (panelPosition === "left")
            return LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom;
        if (panelPosition === "right")
            return LayerShell.Window.AnchorRight | LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom;
        return LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight;
    }
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.exclusionZone: revealed ? barThickness : stripSize
    LayerShell.Window.scope: "newteller-panel"
    LayerShell.Window.margins {
        left: panel.panelPosition === "left" && panel.revealed ? panel.edgeMargin : 0
        right: panel.panelPosition === "right" && panel.revealed ? panel.edgeMargin : 0
        top: panel.panelPosition === "top" && panel.revealed ? panel.edgeMargin : 0
        bottom: panel.panelPosition === "bottom" && panel.revealed ? panel.edgeMargin : 0
    }

    // X11: a dock window that reserves the chosen edge.
    ShellWindow.role: ShellWindow.Dock

    // The clock popup, opened by tapping the clock at the end of the bar.
    ClockPopup {
        id: clockPopup
        panelPosition: panel.panelPosition
        screenRect: panel.screenRect
    }

    // One tooltip window per panel; hovering a panel button points it at the
    // button (showToolTip below).
    PanelToolTip {
        id: panelToolTip
        position: panel.panelPosition
        panelThickness: panel.barThickness
    }

    function showToolTip(item, text) {
        if (!text || text.length === 0)
            return;
        const centre = item.mapToItem(null, item.width / 2, item.height / 2);
        panelToolTip.showAt(text, centre.x + (vertical ? 0 : alongOffset),
                            centre.y + (vertical ? alongOffset : 0));
    }

    function hideToolTip() {
        panelToolTip.hide();
    }

    // --- dropdowns --------------------------------------------------------
    // Every floating surface hangs off the panel button that toggles it. The
    // button's rect is turned into a global one here (the panel knows where it
    // sits on its screen) and handed to the dropdown, which keeps following
    // it: click another button of the same kind and the open card slides over.

    readonly property rect screenRect: Qt.rect(Screen.virtualX, Screen.virtualY,
                                               Screen.width, Screen.height)

    // The global rect of a panel item.
    function anchorRect(item) {
        if (!item)
            return Qt.rect(0, 0, 0, 0);
        const local = item.mapToItem(null, 0, 0);
        // Window coordinates -> screen coordinates (a shorter panel is centred).
        return Anchoring.panelButtonRect(Qt.rect(local.x + (vertical ? 0 : alongOffset),
                                                 local.y + (vertical ? alongOffset : 0),
                                                 item.width, item.height),
                                         panelPosition, barThickness, edgeMargin,
                                         panel.screenRect);
    }

    function hasAnyOpen(list) {
        for (let i = 0; i < list.length; ++i) {
            const d = list[i];
            if (d && d.visible)
                return true;
        }
        return false;
    }

    // Only one dropdown is open at a time: the start menu closes when quick
    // settings opens, a dock menu closes when the clock card opens, and so on.
    function closeDropdowns(except) {
        const all = [panel.startMenu, panel.notificationCenter, panel.quickSettings,
                     clockPopup, dockMenu, trayMenu];
        for (let i = 0; i < all.length; ++i) {
            const d = all[i];
            if (d && d !== except && d.visible)
                d.hide();
        }
    }

    // The buttons of the bar that is shown (horizontal or vertical), for
    // cards opened from the keyboard.
    property Item startButtonRef: null
    property Item bellButtonRef: null
    property Item indicatorsButtonRef: null

    // A card asked for from the keyboard (Super, Super+A, Super+N, Super+V)
    // opens on this panel's screen, from its own button when it has one.
    function openFromKeyboard(dropdown, kind) {
        const button = kind === "start" ? panel.startButtonRef
                     : kind === "bell" ? panel.bellButtonRef
                     : kind === "indicators" ? panel.indicatorsButtonRef : null;
        const usable = button && button.visible ? button : null;
        if (!usable)
            dropdown.anchor = Qt.rect(0, 0, 0, 0);
        if (kind === "start")
            panel.menuWanted = true;
        panel.toggleDropdown(dropdown, usable);
    }

    // Open the dropdown next to `item`, or close it when it is already open.
    function toggleDropdown(dropdown, item) {
        if (!dropdown)
            return;
        if (dropdown.visible && !dropdown.closing) {
            dropdown.hide();
            return;
        }
        panel.closeDropdowns(dropdown);
        if (item)
            dropdown.anchor = panel.anchorRect(item);
        // Shared windows follow the panel they were opened from, so a
        // secondary screen gets its cards next to its own buttons.
        dropdown.screenRect = panel.screenRect;
        dropdown.panelPosition = panel.panelPosition;
        if (dropdown.screen !== panel.screen)
            dropdown.screen = panel.screen;
        dropdown.show();
    }

    // A dropdown opened from elsewhere (another panel, the run dialog, D-Bus)
    // still closes the ones this panel owns.
    Connections {
        target: panel.startMenu
        function onVisibleChanged() {
            panel.menuWanted = panel.startMenu.visible;
            if (panel.startMenu.visible)
                panel.closeDropdowns(panel.startMenu);
        }
    }
    Connections {
        target: panel.notificationCenter
        function onVisibleChanged() {
            if (panel.notificationCenter.visible)
                panel.closeDropdowns(panel.notificationCenter);
        }
    }
    Connections {
        target: panel.quickSettings
        function onVisibleChanged() {
            if (panel.quickSettings.visible)
                panel.closeDropdowns(panel.quickSettings);
        }
    }

    // --- dock model -------------------------------------------------------

    function rebuildDock() {
        var result = [];
        var seen = {};

        // Pinned applications first, in the pinned order. They keep their slot
        // even when all of their windows are minimised.
        const pinned = Settings.values["panel.pinned"] || [];
        for (const id of pinned) {
            const app = Apps.byId(id);
            if (app.id === undefined)
                continue;
            seen[id] = true;
            result.push({ appId: id, app: app, windows: [], running: false, active: false,
                          badge: 0, pinned: true, background: false });
        }

        // One button per window (the default, like a taskbar): a pinned
        // application's first window takes the pinned slot, every further
        // window gets its own button next to the others.
        if (dockShowWindows && !dockGroup) {
            for (const w of Windows.windows) {
                const app = Apps.matchWindowApp(w.appId || "", w.title || "");
                const appId = app.id || "";
                var slot = null;
                if (appId.length > 0) {
                    seen[appId] = true;
                    for (const e of result) {
                        if (e.pinned && e.appId === appId && e.windows.length === 0) {
                            slot = e;
                            break;
                        }
                    }
                }
                if (slot) {
                    slot.windows = [w];
                    slot.title = w.title || "";
                } else {
                    const entry = { appId: appId, app: appId.length > 0 ? Apps.byId(appId) : {},
                                    windows: [w], running: true, active: !!w.active, badge: 1,
                                    title: w.title || "", pinned: false, background: false };
                    // Windows of one application sit side by side: right
                    // after its last button, not at the far end of the dock.
                    var after = -1;
                    if (appId.length > 0) {
                        for (var i = 0; i < result.length; ++i) {
                            if (result[i].appId === appId)
                                after = i;
                        }
                    }
                    if (after >= 0)
                        result.splice(after + 1, 0, entry);
                    else
                        result.push(entry);
                }
            }
        }

        // Or grouped into one button per application.
        if (dockShowWindows && dockGroup) {
            var loose = [];
            var groups = {};
            var order = [];
            for (const w of Windows.windows) {
                const app = Apps.matchWindowApp(w.appId || "", w.title || "");
                const appId = app.id || "";
                if (appId.length > 0) {
                    if (!groups[appId]) {
                        groups[appId] = [];
                        order.push(appId);
                    }
                    groups[appId].push(w);
                } else {
                    loose.push({ appId: "", app: {}, windows: [w], running: true, active: !!w.active,
                                 badge: 1, title: w.title || "", pinned: false, background: false });
                }
            }
            for (const appId of order) {
                const wins = groups[appId];
                var target = null;
                for (const e of result) {
                    if (e.appId === appId) {
                        target = e;
                        break;
                    }
                }
                if (target) {
                    target.windows = wins;
                } else {
                    seen[appId] = true;
                    result.push({ appId: appId, app: Apps.byId(appId), windows: wins,
                                  running: true, active: false, badge: wins.length,
                                  pinned: false, background: false });
                }
            }
            for (const entry of loose)
                result.push(entry);
        }

        // Only without any window list (an unsupported compositor) does the
        // dock fall back to the /proc based running applications; otherwise
        // background programs without a window never show up.
        const runningIds = Windows.backend === "none" ? Apps.runningIds : [];
        for (const id of runningIds) {
            if (seen[id])
                continue;
            seen[id] = true;
            result.push({ appId: id, app: Apps.byId(id), windows: [], running: true,
                          active: false, badge: 0, pinned: false, background: false });
        }

        for (const e of result) {
            for (const w of e.windows) {
                if (w.active) {
                    e.active = true;
                    e.running = true;
                }
            }
            if (e.windows.length > 0) {
                e.running = true;
                e.badge = e.windows.length;
            }
            if (e.appId.length > 0 && Apps.isRunning(e.appId))
                e.running = true;
            // Everything that only has minimised windows went to the
            // background: parked at the end of the strip, dimmed.
            const allMinimized = e.windows.length > 0 && e.windows.every(w => w.minimized);
            e.background = separateBackground && allMinimized && !e.pinned;
        }

        var main = [];
        var background = [];
        for (const e of result)
            (e.background ? background : main).push(e);
        dockMainEntries = main;
        dockBackgroundEntries = background;
        dockEntries = main.concat(background);
        updateAdaptiveDock(main.length + background.length);
    }

    // Shrinks the dock icons while the strip would overflow, and hides the
    // window-title labels first. The strip itself scrolls when even the
    // smallest icons do not fit.
    function updateAdaptiveDock(count) {
        const base = dockIconSize;
        const span = panelLength;
        const reserved = 64 /* start */ + 14 /* separators */
                       + (vertical ? 26 : 88) /* clock */
                       + panelPlugins.length * 30 + 24;
        const available = Math.max(120, span - reserved);
        var size = base;
        var labels = !dockGroup && !vertical;
        const widthFor = (s, withLabels) => count * (s + 14 + (withLabels ? 108 : 0));
        if (widthFor(size, labels) > available)
            labels = false;
        if (shrinkIcons) {
            while (size > 16 && widthFor(size, labels) > available)
                size -= 2;
        }
        effectiveIconSize = Math.max(16, size);
        showDockLabels = labels;
    }

    function activateEntry(entry) {
        if (entry.windows.length > 0) {
            var target = null;
            for (const w of entry.windows) {
                if (w.active) {
                    target = w;
                    break;
                }
            }
            target = target || entry.windows[0];
            if (target.readOnly) {
                // A read-only toplevel (ext_foreign_toplevel_list, e.g. GNOME
                // Wayland) cannot be focused from here: launching the matched
                // application is the best effort - single-instance apps raise
                // their existing window.
                if (entry.appId.length > 0)
                    Apps.launch(entry.appId);
                return;
            }
            Windows.activate(target.id);
        } else if (entry.appId.length > 0) {
            Apps.launch(entry.appId);
        }
    }

    // A dock click is a toggle, the way modern docks behave: an already
    // focused single window is sent to the background, everything else is
    // activated (which also restores a minimised window).
    function dockClicked(entry) {
        if (entry.windows.length === 1 && entry.windows[0].active && !entry.windows[0].minimized) {
            Windows.minimize(entry.windows[0].id);
            return;
        }
        activateEntry(entry);
    }

    // Super+1..9: the Nth button of the dock, in the order shown (background
    // entries last). Like a click, but an application with several windows
    // cycles through them instead of minimising.
    function activateDockIndex(index) {
        const entries = panel.dockEntries;
        if (index < 0 || index >= entries.length)
            return;
        const entry = entries[index];
        if (entry.windows.length > 1) {
            var current = -1;
            for (var i = 0; i < entry.windows.length; ++i) {
                if (entry.windows[i].active)
                    current = i;
            }
            if (current >= 0) {
                const next = entry.windows[(current + 1) % entry.windows.length];
                if (!next.readOnly) {
                    Windows.activate(next.id);
                    return;
                }
            }
        }
        dockClicked(entry);
    }

    function closeEntry(entry) {
        for (const w of entry.windows)
            Windows.closeWindow(w.id);
    }

    function openDockMenu(entry, button) {
        panel.closeDropdowns(dockMenu);
        dockMenu.openFor(entry, panel.anchorRect(button));
    }

    function openTrayMenu(button, itemId) {
        panel.closeDropdowns(trayMenu);
        trayMenu.openFor(itemId, panel.anchorRect(button));
    }

    // --- dock drag-to-reorder ---------------------------------------------
    // Only pinned applications move; running applications that were never
    // pinned keep following the window order.

    function dockDropTarget(sceneX, sceneY, repeater) {
        const pointer = vertical ? sceneY : sceneX;
        var target = 0;
        for (var i = 0; i < repeater.count; ++i) {
            const item = repeater.itemAt(i);
            if (!item)
                continue;
            const centre = item.mapToItem(null, item.width / 2, item.height / 2);
            const centrePos = vertical ? centre.y : centre.x;
            if (pointer > centrePos)
                target = i + 1;
        }
        // Never drop past the pinned block: running entries follow it.
        const pinned = Settings.values["panel.pinned"] || [];
        var pinnedVisible = 0;
        for (const id of pinned) {
            if (Apps.byId(id).id !== undefined)
                pinnedVisible += 1;
        }
        return Math.max(0, Math.min(target, pinnedVisible));
    }

    function dockReorderStarted(entry) {
        if (!entry.pinned)
            return;
        dockDraggingAppId = entry.appId;
        dockDropIndex = -1;
    }

    function dockReorderMoved(entry, sceneX, sceneY, repeater) {
        if (!entry.pinned)
            return;
        dockDropIndex = dockDropTarget(sceneX, sceneY, repeater);
    }

    function dockReorderFinished(entry, sceneX, sceneY, repeater) {
        if (!entry.pinned) {
            dockDraggingAppId = "";
            dockDropIndex = -1;
            return;
        }
        const target = dockDropTarget(sceneX, sceneY, repeater);
        const list = (Settings.values["panel.pinned"] || []).slice();
        const from = list.indexOf(entry.appId);
        if (from >= 0) {
            list.splice(from, 1);
            const to = Math.max(0, Math.min(target > from ? target - 1 : target, list.length));
            list.splice(to, 0, entry.appId);
            Settings.setValue("panel.pinned", list);
        }
        dockDraggingAppId = "";
        dockDropIndex = -1;
    }

    Component.onCompleted: rebuildDock()
    onDockIconSizeChanged: updateAdaptiveDock(dockEntries.length)
    onVerticalChanged: updateAdaptiveDock(dockEntries.length)
    Connections {
        target: Windows
        function onWindowsChanged() { panel.rebuildDock(); dockMenu.refreshEntry(); }
    }
    Connections {
        target: Apps
        function onRunningChanged() { panel.rebuildDock(); }
        function onCountChanged() { panel.rebuildDock(); }
    }
    Connections {
        target: Settings
        function onValuesChanged() { panel.rebuildDock(); }
    }

    DockMenu {
        id: dockMenu
        screen: panel.screen
        entriesProvider: panel
        panelPosition: panel.panelPosition
        screenRect: panel.screenRect
    }

    // Menus exported over com.canonical.dbusmenu by tray items. Items without
    // one keep the old behaviour (ask the application for its own popup).
    TrayMenu {
        id: trayMenu
        screen: panel.screen
        panelPosition: panel.panelPosition
        screenRect: panel.screenRect
    }

    // Shared pieces of the bar, used by both layouts below.
    component StartButton: TaskButton {
        id: startButton
        objectName: "startButton"
        Component.onCompleted: panel.startButtonRef = startButton
        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
        iconName: "layout-grid"
        label: panel.vertical ? "" : qsTr("Start")
        foregroundColor: panel.panelTextColor
        onClicked: {
            panel.menuWanted = true;
            panel.toggleDropdown(panel.startMenu, startButton);
        }
    }

    component DockApps: Repeater {
        id: dockAppsRepeater
        property bool backgroundZone: false
        // Height of a button (and its width on a vertical bar, where the
        // buttons are square), given by the strip.
        property real extent: 32
        model: backgroundZone ? panel.dockBackgroundEntries : panel.dockMainEntries

        delegate: TaskButton {
            id: dockButton
            required property var modelData
            required property int index
            readonly property var entry: modelData
            readonly property string appName: entry.app && entry.app.name ? entry.app.name
                                                                          : (entry.title || qsTr("Application"))

            height: Math.round(dockAppsRepeater.extent)
            width: panel.vertical ? Math.round(dockAppsRepeater.extent) : implicitWidth
            reorderable: entry.pinned === true
            reordering: panel.dockDraggingAppId.length > 0
                        && panel.dockDraggingAppId === entry.appId
            iconName: entry.app && entry.app.icon ? entry.app.icon
                    : (entry.appId.length > 0 ? "square" : "app-window")
            label: panel.showDockLabels && entry.title && entry.title.length > 0
                   ? entry.title : ""
            dockLabel: panel.dockShowAppLabels ? dockButton.appName : ""
            iconSize: Math.max(14, Math.min(panel.effectiveIconSize,
                                            Math.round(dockAppsRepeater.extent) - 4))
            showDockLabels: panel.dockShowAppLabels
            dockIconColorMode: panel.dockIconColorMode
            dockCustomIconColor: panel.dockCustomIconColor
            foregroundColor: panel.panelTextColor
            // The material belongs to the floating dock pill, not to every
            // icon. Icons keep only the subtle hover/press tint.
            surfaceMaterial: "none"
            hoverOpacity: panel.dockHoverOpacity
            activeOpacity: panel.dockActiveOpacity
            running: entry.running && panel.showRunning
            active: entry.active
            dimmed: entry.background === true
            badge: entry.badge
            onClicked: panel.dockClicked(entry)
            onMiddleClicked: panel.closeEntry(entry)
            onReorderStarted: panel.dockReorderStarted(entry)
            onReorderMoved: (x, y) => panel.dockReorderMoved(entry, x, y, dockAppsRepeater)
            onReorderFinished: (x, y) => panel.dockReorderFinished(entry, x, y, dockAppsRepeater)

            // Insertion marker while a pinned button is dragged.
            Rectangle {
                readonly property bool before: panel.dockDropIndex === index
                readonly property bool atEnd: panel.dockDropIndex === dockAppsRepeater.count
                                              && index === dockAppsRepeater.count - 1
                visible: entry.pinned && (before || atEnd)
                width: panel.vertical ? parent.width : 2
                height: panel.vertical ? 2 : parent.height
                radius: 1
                color: Theme.accent
                z: 30
                anchors.left: panel.vertical ? parent.left : undefined
                anchors.right: !panel.vertical && atEnd && !before ? parent.right : undefined
                anchors.top: panel.vertical && !atEnd ? parent.top : undefined
                anchors.bottom: panel.vertical && atEnd && !before ? parent.bottom : undefined
            }

            HoverHandler {
                onHoveredChanged: {
                    if (hovered)
                        panel.showToolTip(dockButton,
                                          dockButton.entry.windows.length > 1
                                              ? dockButton.appName + " ("
                                                + dockButton.entry.windows.length + " windows)"
                                              : (dockButton.entry.title && dockButton.entry.title.length > 0
                                                 ? dockButton.entry.title : dockButton.appName));
                    else
                        panel.hideToolTip();
                }
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: panel.openDockMenu(dockButton.entry, dockButton)
            }
        }
    }

    // Divider between the running zone and the minimised/background zone.
    component BackgroundSeparator: Rectangle {
        // Explicit size: the strip uses a Row/Column/Flow, where Layout hints
        // are ignored and the divider would collapse to zero pixels.
        property real extent: 32
        visible: panel.separateBackground && panel.dockBackgroundEntries.length > 0
        width: panel.vertical ? Math.round(extent) : 1
        height: panel.vertical ? 1 : Math.round(Math.min(extent, panel.dockIconSize + 8))
        Layout.preferredWidth: width
        Layout.preferredHeight: height
        Layout.alignment: Qt.AlignVCenter
        color: Qt.rgba(1, 1, 1, 0.20)
    }

    component PluginSlot: Repeater {
        model: panel.panelPlugins

        delegate: Loader {
            required property var modelData
            Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
            source: modelData.entry
        }
    }

    component ClockText: Text {
        // A vertical bar is too narrow for "HH:mm" on one line: hours above
        // minutes.
        text: panel.vertical
              ? Qt.formatDateTime(Shell.now, "HH") + "\n" + Qt.formatDateTime(Shell.now, "mm")
              : (Settings.values["panel.showDate"]
               ? Qt.formatDateTime(Shell.now, Settings.values["panel.dateFormat"] || "ddd d MMM") + "  "
               : "")
              + Qt.formatDateTime(Shell.now, Settings.values["panel.clockFormat"] || "HH:mm")
        color: panel.panelTextColor
        font.family: Theme.fontFamily
        font.pointSize: Theme.fontSize
    }

    // The clock is a button: tapping it opens the clock popup with the
    // analogue dial and the month calendar.
    component ClockButton: Rectangle {
        id: clockButton
        objectName: "clockButton"
        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
        implicitWidth: panel.vertical ? Math.min(clockLabel.implicitWidth + 12, panel.barThickness - 8)
                                      : clockLabel.implicitWidth + 18
        implicitHeight: panel.vertical ? clockLabel.implicitHeight + 10
                                       : Math.min(panel.barThickness - 10, clockLabel.implicitHeight + 10)
        radius: Theme.radius
        color: clockHover.hovered
               ? Qt.rgba(panel.panelTextColor.r, panel.panelTextColor.g, panel.panelTextColor.b, 0.08)
               : "transparent"
        Behavior on color {
            ColorAnimation {
                duration: motion.micro
            }
        }

        HoverHandler {
            id: clockHover
            onHoveredChanged: {
                if (hovered)
                    panel.showToolTip(clockButton,
                                      Qt.formatDateTime(Shell.now, "dddd, d MMMM yyyy  HH:mm"));
                else
                    panel.hideToolTip();
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: panel.toggleDropdown(clockPopup, clockButton)
        }
        ClockText {
            id: clockLabel
            anchors.centerIn: parent
            horizontalAlignment: Text.AlignHCenter
        }
    }

    component Separator: Rectangle {
        Layout.preferredWidth: panel.vertical ? 24 : 1
        Layout.preferredHeight: panel.vertical ? 1 : 24
        Layout.alignment: Qt.AlignCenter
        color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.8)
    }

    // --- system tray -------------------------------------------------------
    // Icons of applications that registered with our StatusNotifierWatcher:
    // left click activates, middle click secondary-activates, right click
    // asks the item to open its own context menu, and the wheel scrolls.
    // A column on a vertical bar: a row there was wider than the bar and
    // was cut off.
    component TrayBox: Grid {
        spacing: 2
        columns: panel.vertical ? 1 : Math.max(1, Tray.count)
        visible: panel.showTray
        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter

        Repeater {
            model: Tray.items

            delegate: Item {
                id: trayIcon
                required property var modelData
                readonly property string entryId: modelData.id
                objectName: "trayButton"

                width: 26
                height: 26
                opacity: modelData.status === "Passive" ? 0.55 : 1.0
                Behavior on opacity {
                    NumberAnimation { duration: motion.fast }
                }

                Image {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    sourceSize.width: 40
                    sourceSize.height: 40
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    asynchronous: true
                    source: Tray.iconUrl(trayIcon.entryId)
                    // Growing in when the icon first renders, so a tray
                    // refresh does not pop.
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity {
                        NumberAnimation { duration: motion.micro }
                    }
                }

                // Attention dot for items that ask for it.
                Rectangle {
                    visible: trayIcon.modelData.status === "NeedsAttention"
                    width: 7
                    height: 7
                    radius: 3.5
                    color: Theme.accent
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: 1
                    anchors.rightMargin: 1
                }

                HoverHandler {
                    onHoveredChanged: {
                        if (hovered)
                            panel.showToolTip(trayIcon,
                                              trayIcon.modelData.tooltipTitle
                                              || trayIcon.modelData.title
                                              || trayIcon.modelData.name || "");
                        else
                            panel.hideToolTip();
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: (mouse) => {
                        panel.hideToolTip();
                        if (mouse.button === Qt.RightButton) {
                            if (Settings.values["tray.menus"] !== false
                                && Tray.hasMenu(trayIcon.entryId)) {
                                panel.openTrayMenu(trayIcon, trayIcon.entryId);
                            } else {
                                const global = trayIcon.mapToGlobal(mouse.x, mouse.y);
                                Tray.contextMenu(trayIcon.entryId,
                                                 Math.round(global.x),
                                                 Math.round(global.y));
                            }
                        } else if (mouse.button === Qt.MiddleButton) {
                            Tray.secondaryActivate(trayIcon.entryId);
                        } else {
                            Tray.activate(trayIcon.entryId);
                        }
                    }
                    onWheel: (wheel) => {
                        Tray.scroll(trayIcon.entryId,
                                    wheel.angleDelta.y >= 0 ? 1 : -1, false);
                    }
                }
            }
        }
    }

    // --- workspaces: one dot per virtual desktop ----------------------------
    // The current desktop is a wide accent pill, desktops with windows are
    // solid dots, empty ones hollow. Click one to go there; the wheel steps.
    component WorkspaceStrip: Rectangle {
        id: strip
        objectName: "workspaceStrip"

        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
        implicitWidth: panel.vertical ? 30 : dots.implicitWidth + 16
        implicitHeight: panel.vertical ? dots.implicitHeight + 16 : Math.min(panel.barThickness - 10, 34)
        radius: Theme.radius
        color: stripHover.hovered
               ? Qt.rgba(panel.panelTextColor.r, panel.panelTextColor.g, panel.panelTextColor.b, 0.08)
               : "transparent"

        HoverHandler {
            id: stripHover
            onHoveredChanged: {
                if (hovered)
                    panel.showToolTip(strip, qsTr("%1 (%2 of %3) - Ctrl+Super+Left/Right")
                                      .arg(panel.desktopName(Session.workspace))
                                      .arg(Session.workspace + 1).arg(Session.workspaceCount));
                else
                    panel.hideToolTip();
            }
        }
        WheelHandler {
            onWheel: (event) => {
                const step = event.angleDelta.y > 0 ? -1 : 1;
                Session.switchWorkspace(Math.max(0, Math.min(Session.workspaceCount - 1,
                                                             Session.workspace + step)));
            }
        }

        // workspaces.indicator: "dots", "numbers" or "names" (names only
        // on a horizontal bar; a vertical one shows numbers instead).
        readonly property string style: {
            const value = Settings.values["workspaces.indicator"] || "dots";
            return value === "names" && panel.vertical ? "numbers" : value;
        }
        readonly property bool labelled: style === "numbers" || style === "names"

        Grid {
            id: dots
            anchors.centerIn: parent
            columns: panel.vertical ? 1 : Session.workspaceCount
            spacing: strip.labelled ? 3 : 6

            Repeater {
                model: Session.workspaceCount

                delegate: Rectangle {
                    id: dot
                    required property int index
                    readonly property bool current: index === Session.workspace
                    readonly property bool occupied: (Session.workspaceOccupied & (1 << index)) !== 0

                    width: strip.labelled ? Math.max(20, dotLabel.implicitWidth + 10)
                                          : panel.vertical ? 8 : (current ? 18 : 8)
                    height: strip.labelled ? 20 : panel.vertical ? (current ? 18 : 8) : 8
                    radius: strip.labelled ? Math.min(6, Theme.radius) : 4
                    color: current ? Theme.accent
                                   : occupied ? Qt.rgba(panel.panelTextColor.r, panel.panelTextColor.g,
                                                        panel.panelTextColor.b, 0.75)
                                              : "transparent"
                    border.width: current || occupied ? 0 : 1
                    border.color: Qt.rgba(panel.panelTextColor.r, panel.panelTextColor.g,
                                          panel.panelTextColor.b, 0.6)
                    Behavior on width { NumberAnimation { duration: motion.fast } }
                    Behavior on height { NumberAnimation { duration: motion.fast } }

                    Text {
                        id: dotLabel
                        anchors.centerIn: parent
                        visible: strip.labelled
                        text: strip.style === "names" ? panel.desktopName(dot.index) : String(dot.index + 1)
                        color: dot.current ? "white" : panel.panelTextColor
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 1
                        font.bold: dot.current
                    }

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Session.switchWorkspace(index)
                    }
                }
            }
        }
    }

    // --- keyboard layout: shown when more than one layout is configured -----
    // Click (or Super+Space) switches to the next layout; the compositor owns
    // the keyboard, the shell only shows and asks.
    component LayoutButton: Rectangle {
        id: layoutButton
        objectName: "layoutButton"

        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
        implicitWidth: Math.max(30, layoutLabel.implicitWidth + 12)
        height: Math.min(panel.barThickness - 10, 34)
        radius: Theme.radius
        color: layoutHover.hovered
               ? Qt.rgba(panel.panelTextColor.r, panel.panelTextColor.g, panel.panelTextColor.b, 0.08)
               : "transparent"
        Behavior on color {
            ColorAnimation { duration: motion.micro }
        }

        HoverHandler {
            id: layoutHover
            onHoveredChanged: {
                if (hovered)
                    panel.showToolTip(layoutButton, qsTr("%1 - Super+Space switches")
                                                        .arg(Session.keyboardLayoutName));
                else
                    panel.hideToolTip();
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                panel.hideToolTip();
                Session.nextKeyboardLayout();
            }
        }
        Text {
            id: layoutLabel
            anchors.centerIn: parent
            text: Session.keyboardLayoutLabel
            color: panel.panelTextColor
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 1
            font.bold: true
        }
    }

    // --- bell: the notification centre --------------------------------------
    // --- screen recording: a red pill with the time; a click stops it ------
    component RecordingButton: Rectangle {
        id: recordingButton
        objectName: "recordingButton"

        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
        visible: Recorder.recording
        implicitWidth: panel.vertical ? 30 : recordingRow.implicitWidth + 16
        height: Math.min(panel.barThickness - 10, 30)
        radius: height / 2
        color: Qt.rgba(0.9, 0.28, 0.3, recordingHover.hovered ? 0.32 : 0.2)

        HoverHandler {
            id: recordingHover
            onHoveredChanged: {
                if (hovered)
                    panel.showToolTip(recordingButton, qsTr("Stop recording (Super+Alt+R)"));
                else
                    panel.hideToolTip();
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                panel.hideToolTip();
                Recorder.stop();
            }
        }
        Row {
            id: recordingRow
            anchors.centerIn: parent
            spacing: 6

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 10
                radius: 5
                color: "#e5484d"
                SequentialAnimation on opacity {
                    running: Recorder.recording
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.35; duration: 700 }
                    NumberAnimation { to: 1; duration: 700 }
                }
            }
            Text {
                visible: !panel.vertical
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    const s = Recorder.elapsed;
                    return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
                }
                color: panel.panelTextColor
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }
        }
    }

    component BellButton: Rectangle {
        id: bellButton
        objectName: "bellButton"
        Component.onCompleted: panel.bellButtonRef = bellButton
        readonly property bool dnd: Settings.values["notifications.doNotDisturb"] === true
        readonly property int unread: panel.notificationCenter
                                      ? panel.notificationCenter.unreadCount : 0

        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
        width: 34
        height: Math.min(panel.barThickness - 10, 34)
        radius: Theme.radius
        color: bellHover.hovered
               ? Qt.rgba(panel.panelTextColor.r, panel.panelTextColor.g, panel.panelTextColor.b, 0.08)
               : "transparent"
        Behavior on color {
            ColorAnimation { duration: motion.micro }
        }

        HoverHandler {
            id: bellHover
            onHoveredChanged: {
                if (hovered) {
                    const unread = bellButton.unread;
                    panel.showToolTip(bellButton,
                                      unread > 0 ? qsTr("Notifications (%1 new)").arg(unread)
                                                 : qsTr("Notifications"));
                } else {
                    panel.hideToolTip();
                }
            }
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor
            onClicked: (mouse) => {
                panel.hideToolTip();
                // Right click is the do-not-disturb shortcut, the same switch
                // the centre and the quick settings card show.
                if (mouse.button === Qt.RightButton) {
                    Settings.setValue("notifications.doNotDisturb", !bellButton.dnd);
                    panel.showToolTip(bellButton, bellButton.dnd ? qsTr("Do not disturb off")
                                                                 : qsTr("Do not disturb on"));
                    return;
                }
                panel.toggleDropdown(panel.notificationCenter, bellButton);
            }
        }

        Image {
            anchors.centerIn: parent
            width: 18
            height: 18
            sourceSize.width: 36
            sourceSize.height: 36
            fillMode: Image.PreserveAspectFit
            source: Shell.tintedIcon(bellButton.dnd ? "bell-off" : "bell", panel.panelTextColor)
        }

        // Unread badge: a count up to nine, a dot beyond that.
        Rectangle {
            visible: bellButton.unread > 0 && !bellButton.dnd
            width: bellButton.unread > 9 ? 14 : Math.max(14, unreadLabel.implicitWidth + 8)
            height: 14
            radius: 7
            color: Theme.accent
            border.width: 1
            border.color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.9)
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: 1
            anchors.rightMargin: 1

            Text {
                id: unreadLabel
                anchors.centerIn: parent
                visible: bellButton.unread <= 9
                text: String(bellButton.unread)
                color: "#ffffff"
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 3
                font.bold: true
            }
        }
    }

    // --- indicators: the quick settings button ------------------------------
    // One button that summarises the machine: the battery level when there is
    // one, otherwise the volume or network state. Clicking it opens the
    // quick settings card.
    component IndicatorsButton: Rectangle {
        id: indicatorsButton
        objectName: "indicatorsButton"
        Component.onCompleted: panel.indicatorsButtonRef = indicatorsButton
        readonly property bool playing: Quick.mediaAvailable && Quick.mediaPlaying
        readonly property string glyph: Quick.volumeMuted ? "volume-x"
                                        : System.batteryCharging ? "battery-charging"
                                        : System.batteryPresent ? "battery"
                                        : Quick.networkAvailable && !Quick.wifiEnabled ? "wifi-off"
                                        : Quick.networkAvailable ? "wifi"
                                        : "sliders-horizontal"

        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
        implicitWidth: indicatorsRow.implicitWidth + 14
        height: Math.min(panel.barThickness - 10, 34)
        radius: Theme.radius
        color: indicatorsHover.hovered
               ? Qt.rgba(panel.panelTextColor.r, panel.panelTextColor.g, panel.panelTextColor.b, 0.08)
               : "transparent"
        Behavior on color {
            ColorAnimation { duration: motion.micro }
        }

        HoverHandler {
            id: indicatorsHover
            onHoveredChanged: {
                if (hovered) {
                    var tip = Quick.networkStatus;
                    if (System.batteryPresent)
                        tip = Math.round(System.batteryPercent) + "% "
                              + (System.batteryTimeText.length > 0
                                 ? System.batteryTimeText : System.batteryStatus);
                    else if (Quick.volumeAvailable)
                        tip = (Quick.volumeMuted ? qsTr("Muted") : qsTr("Volume %1%").arg(Quick.volume))
                              + (tip.length > 0 ? " - " + tip : "");
                    panel.showToolTip(indicatorsButton, tip.length > 0 ? tip : qsTr("Quick settings"));
                } else {
                    panel.hideToolTip();
                }
            }
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
            cursorShape: Qt.PointingHandCursor
            onClicked: (mouse) => {
                panel.hideToolTip();
                if (mouse.button === Qt.MiddleButton) {
                    if (Quick.volumeAvailable)
                        Quick.toggleMuted();
                    return;
                }
                panel.toggleDropdown(panel.quickSettings, indicatorsButton);
            }
            // Scrolling over the indicators changes the volume by 5% a notch,
            // so it can be set without opening the card.
            onWheel: (wheel) => {
                if (!Quick.volumeAvailable)
                    return;
                wheel.accepted = true;
                const notches = wheel.angleDelta.y / 120;
                const value = Math.round(Quick.volume) + Math.round(notches * 5);
                Quick.setVolume(Math.max(0, Math.min(100, value)));
                panel.showToolTip(indicatorsButton,
                                  Quick.volumeMuted ? qsTr("Muted") : qsTr("Volume %1%").arg(Quick.volume));
            }
        }

        Row {
            id: indicatorsRow
            anchors.centerIn: parent
            spacing: 5

            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                sourceSize.width: 36
                sourceSize.height: 36
                fillMode: Image.PreserveAspectFit
                source: Shell.tintedIcon(indicatorsButton.glyph,
                                         System.batteryCritical ? Theme.accent : panel.panelTextColor)
            }
            Text {
                visible: System.batteryPresent
                anchors.verticalCenter: parent.verticalCenter
                text: Math.round(System.batteryPercent) + "%"
                color: panel.panelTextColor
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }
        }
    }

    // The bar itself in the bar style, centred on the strip like the pill.
    SurfaceStyle {
        id: barBackground
        visible: panel.barStyle && panel.revealed
        opacity: barLoader.opacity
        x: panel.vertical ? Math.round((panel.width - panel.barThickness) / 2) : 0
        y: panel.vertical ? 0 : Math.round((panel.height - panel.barThickness) / 2)
        width: panel.vertical ? panel.barThickness : panel.width
        height: panel.vertical ? panel.height : panel.barThickness
        material: panel.panelMaterial
        materialOpacity: panel.panelSurfaceOpacity
        baseColor: panel.panelSurfaceColor
        cornerRadius: panel.barRadius
        borderColor: Theme.border
        borderOpacity: 0.6
        borderWidth: panel.barRadius > 0 ? 1 : 0
        onXChanged: Qt.callLater(panel.updateBlur)
        onYChanged: Qt.callLater(panel.updateBlur)
    }

    Loader {
        id: barLoader
        anchors.fill: parent
        anchors.margins: 0
        sourceComponent: panel.vertical ? verticalBar : horizontalBar
        // Slide the bar content in and out of the reserved strip.
        opacity: panel.revealed ? 1.0 : 0.0
        // While fading, the bar also sinks back towards its own edge, so
        // auto-hide reads as the bar sliding away rather than a blink.
        transform: Translate {
            x: (panel.vertical ? (panel.panelPosition === "left" ? -1 : 1) : 0)
               * (1 - barLoader.opacity) * 14
            y: (panel.vertical ? 0 : (panel.panelPosition === "top" ? -1 : 1))
               * (1 - barLoader.opacity) * 14
        }
        Behavior on opacity {
            NumberAnimation {
                duration: motion.fast
                easing.type: motion.easeEnter
            }
        }
    }

    // Scrollable dock strip: it only becomes interactive when the buttons do
    // not fit, so nothing steals wheel events on a quiet desktop.
    //
    // Only the layout in use is created (a Loader picks a Row, a Column or a
    // Flow): the three used to exist at once, three times the buttons, and
    // the hidden ones sized themselves from the strip that sized itself from
    // them - a binding loop that kept a vertical dock busy and clipped.
    component DockStrip: Flickable {
        id: dockStrip
        // Height of one button; on a vertical bar the buttons are squares as
        // wide as the strip.
        // On a vertical bar the strip is exactly as wide as the pill's inside
        // (not whatever the column layout hands out, which can be wider).
        readonly property int verticalWidth: Math.max(16, panel.barThickness - 4 - 2 * panel.dockPadding)
        readonly property real buttonExtent: panel.vertical ? verticalWidth
                                             : (panel.dockButtonHeight > 0 ? panel.dockButtonHeight
                                                                           : height)
        Layout.fillWidth: false
        Layout.fillHeight: !panel.vertical
        Layout.alignment: Qt.AlignCenter
        Layout.leftMargin: panel.vertical ? 0 : Math.max(0, panel.panelSpacing - 4)
        Layout.rightMargin: panel.vertical ? 0 : Math.max(0, panel.panelSpacing - 4)
        Layout.topMargin: panel.vertical ? Math.max(0, panel.panelSpacing - 4) : 0
        Layout.bottomMargin: panel.vertical ? Math.max(0, panel.panelSpacing - 4) : 0
        Layout.preferredWidth: panel.vertical
                               ? verticalWidth
                               : (panel.dockMultiRow
                                  ? Math.min(panel.dockWrapWidth, panel.dockMaxWidth)
                                  : Math.min(contentWidth, panel.dockMaxWidth))
        Layout.preferredHeight: panel.vertical ? Math.min(contentHeight, panel.dockMaxHeight) : -1
        Layout.minimumWidth: 0
        Layout.maximumWidth: panel.vertical ? verticalWidth : panel.dockMaxWidth
        clip: true
        contentWidth: stripLoader.item ? stripLoader.item.width : 0
        contentHeight: stripLoader.item ? stripLoader.item.height : 0
        // Only scroll when the setting allows it; when the buttons fit, the
        // wheel belongs to the application underneath.
        readonly property bool overflowing: panel.vertical || panel.dockMultiRow
                                            ? contentHeight > height + 1
                                            : contentWidth > width + 1
        interactive: panel.dockScrollable && overflowing
        flickableDirection: panel.vertical || panel.dockMultiRow ? Flickable.VerticalFlick
                                                                 : Flickable.HorizontalFlick

        WheelHandler {
            enabled: dockStrip.interactive
            orientation: panel.vertical || panel.dockMultiRow ? Qt.Vertical : Qt.Horizontal
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        }

        Loader {
            id: stripLoader
            sourceComponent: panel.vertical ? columnStrip
                             : (panel.dockMultiRow ? flowStrip : rowStrip)
            // Whole pixels, so the icons stay sharp and the dock sits exactly
            // in the middle.
            x: {
                if (!item)
                    return 0;
                if (panel.vertical || panel.dockMultiRow)
                    return Math.max(0, Math.round((dockStrip.width - item.width) / 2));
                return panel.dockAlign === 1 ? Math.max(0, Math.round((dockStrip.width - item.width) / 2))
                     : panel.dockAlign === 2 ? Math.max(0, Math.round(dockStrip.width - item.width)) : 0;
            }
            y: {
                if (!item)
                    return 0;
                if (!panel.vertical)
                    return Math.max(0, Math.round((dockStrip.height - item.height) / 2));
                return panel.dockAlign === 1 ? Math.max(0, Math.round((dockStrip.height - item.height) / 2))
                     : panel.dockAlign === 2 ? Math.max(0, Math.round(dockStrip.height - item.height)) : 0;
            }
        }

        // One row, aligned at the start, centre or end of the strip.
        Component {
            id: rowStrip
            Row {
                spacing: panel.dockIconSpacing
                DockApps { extent: dockStrip.buttonExtent }
                BackgroundSeparator {
                    extent: dockStrip.buttonExtent
                    anchors.verticalCenter: parent.verticalCenter
                }
                DockApps { backgroundZone: true; extent: dockStrip.buttonExtent }
            }
        }

        // Vertical panel: the dock runs down the edge.
        Component {
            id: columnStrip
            Column {
                spacing: panel.dockIconSpacing
                DockApps { extent: dockStrip.buttonExtent }
                BackgroundSeparator { extent: dockStrip.buttonExtent }
                DockApps { backgroundZone: true; extent: dockStrip.buttonExtent }
            }
        }

        // Multi-row dock: the buttons wrap inside the (taller) bar.
        Component {
            id: flowStrip
            Flow {
                width: dockStrip.width
                spacing: panel.dockIconSpacing
                DockApps { extent: dockStrip.buttonExtent }
                BackgroundSeparator { extent: dockStrip.buttonExtent }
                DockApps { backgroundZone: true; extent: dockStrip.buttonExtent }
            }
        }
    }

    Component {
        id: horizontalBar

        Item {
            id: horizontalRoot

            // The pill hugs its contents and is centred on the output. The
            // panel window itself is transparent; only this pill carries the
            // dock material and the reserved edge strip remains full-width.
            Item {
                id: dockCluster
                // Whole pixels: centring a fractional width put the dock half
                // a pixel off and blurred it.
                width: Math.round(Math.min(horizontalRoot.width - 8,
                                           dockLayout.implicitWidth + panel.dockPadding * 2))
                height: Math.max(34, panel.barThickness - 4)
                x: Math.round((horizontalRoot.width - width) / 2)
                y: Math.round((horizontalRoot.height - height) / 2)
                Component.onCompleted: {
                    panel.dockPill = dockCluster;
                    Qt.callLater(panel.updateBlur);
                }
                onWidthChanged: Qt.callLater(panel.updateBlur)
                onXChanged: Qt.callLater(panel.updateBlur)

                DockSurface {
                    anchors.fill: parent
                    material: panel.dockMaterial
                    materialOpacity: panel.dockSurfaceOpacity
                    baseColor: panel.dockSurfaceColor
                    accentColor: Theme.accent
                    cornerRadius: height / 2
                    effectStrength: 1
                }

                RowLayout {
                    id: dockLayout
                    anchors.fill: parent
                    anchors.margins: panel.dockPadding
                    spacing: Math.max(2, panel.panelZoneSpacing - 4)

                    StartButton {
                        Layout.leftMargin: Math.max(0, panel.panelLeftSpacing - 4)
                    }
                    // (The clock mirrors this margin, so both ends of the pill
                    // have the same padding.)
                    WorkspaceStrip {
                        visible: panel.showWorkspaces
                    }
                    Separator {}
                    DockStrip {}
                    PluginSlot {}

                    TrayBox {}
                    RecordingButton {}
                    LayoutButton {
                        visible: panel.showKeyboardLayout
                    }
                    BellButton {
                        visible: panel.showIndicators
                    }
                    IndicatorsButton {
                        visible: panel.showIndicators
                    }
                    Separator {
                        visible: panel.showTray || panel.showIndicators
                    }

                    ClockButton {
                        Layout.rightMargin: Math.max(0, panel.panelRightSpacing - 4)
                    }
                }
            }
        }
    }

    Component {
        id: verticalBar

        Item {
            id: verticalRoot

            Item {
                id: verticalDockCluster
                width: Math.max(34, panel.barThickness - 4)
                height: Math.round(Math.min(verticalRoot.height - 8,
                                            verticalLayout.implicitHeight + panel.dockPadding * 2))
                x: Math.round((verticalRoot.width - width) / 2)
                y: Math.round((verticalRoot.height - height) / 2)
                Component.onCompleted: {
                    panel.dockPill = verticalDockCluster;
                    Qt.callLater(panel.updateBlur);
                }
                onHeightChanged: Qt.callLater(panel.updateBlur)
                onYChanged: Qt.callLater(panel.updateBlur)

                DockSurface {
                    anchors.fill: parent
                    material: panel.dockMaterial
                    materialOpacity: panel.dockSurfaceOpacity
                    baseColor: panel.dockSurfaceColor
                    accentColor: Theme.accent
                    cornerRadius: width / 2
                    effectStrength: 1
                }

                ColumnLayout {
                    id: verticalLayout
                    anchors.fill: parent
                    anchors.margins: panel.dockPadding
                    spacing: Math.max(2, panel.panelZoneSpacing - 4)

                    StartButton {}
                    WorkspaceStrip {
                        visible: panel.showWorkspaces
                    }
                    Separator {}
                    DockStrip {}
                    PluginSlot {}

                    TrayBox {}
                    RecordingButton {}
                    LayoutButton {
                        visible: panel.showKeyboardLayout
                    }
                    BellButton {
                        visible: panel.showIndicators
                    }
                    IndicatorsButton {
                        visible: panel.showIndicators
                    }
                    Separator {
                        visible: panel.showTray || panel.showIndicators
                    }

                    ClockButton {
                        Layout.bottomMargin: 4
                        Layout.topMargin: 4
                    }
                }
            }
        }
    }

    // Auto-hide: the pointer leaving starts the configured delay, so the bar
    // stays put while the pointer travels towards it (and does not flicker).
    Timer {
        id: autoHideTimer
        interval: panel.autoHideDelay
        onTriggered: panel.autoHideHold = false
    }

    // Invisible hover surface over the whole panel; it keeps auto-hide from
    // flickering while the pointer travels across the bar.
    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton

        onContainsMouseChanged: {
            if (containsMouse) {
                autoHideTimer.stop();
                panel.autoHideHold = false;
            } else if (panel.autoHide) {
                panel.autoHideHold = true;
                autoHideTimer.restart();
            }
        }
    }
}
