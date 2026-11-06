import QtQuick
import QtQuick.Window
import Qt.labs.folderlistmodel
import org.kde.layershell 1.0 as LayerShell
import NewTeller 1.0

// Desktop background: wallpaper, icons from ~/Desktop, desktop plugins and a
// right-click menu.
Window {
    id: desk

    readonly property string wallpaper: Settings.values["desktop.wallpaper"] || ""
    readonly property string wallpaperMode: Settings.values["desktop.wallpaperMode"] || "fill"
    readonly property int wallpaperBlur: Settings.values["desktop.wallpaperBlur"] || 0
    readonly property int wallpaperDarken: Settings.values["desktop.wallpaperDarken"] || 0
    readonly property bool perMonitorWallpaper: Settings.values["desktop.perMonitorWallpaper"] === true
    readonly property bool wallpaperSlideshow: Settings.values["desktop.wallpaperSlideshow"] === true
    readonly property int wallpaperSlideshowInterval: Settings.values["desktop.wallpaperSlideshowInterval"] || 60
    readonly property bool showIcons: Settings.values["desktop.showIcons"] !== false
    readonly property int iconSize: Settings.values["desktop.iconSize"] || 48
    readonly property int cellWidth: Settings.values["desktop.cellWidth"] || 96
    readonly property int cellHeight: Settings.values["desktop.cellHeight"] || 110
    readonly property bool contextMenuEnabled: Settings.values["desktop.contextMenu"] !== false
    readonly property var desktopPlugins: Plugins.plugins.filter(p => p.type === "desktop" && p.enabled)
    readonly property string desktopBackgroundColor: Settings.values["desktop.backgroundColor"] || ""
    readonly property bool desktopBackgroundGradient: Settings.values["desktop.backgroundGradient"] !== false
    readonly property string iconColor: Settings.values["desktop.iconColor"] || "#ffffff"
    readonly property bool iconShadow: Settings.values["desktop.iconShadow"] !== false
    readonly property bool iconGlow: Settings.values["desktop.iconGlow"] === true
    // Labels under the icons: their own colour, outline and size (0 follows
    // the theme font), independent of the icon tint.
    readonly property string labelColor: Settings.values["desktop.iconTextColor"] || "#ffffff"
    readonly property bool labelShadow: Settings.values["desktop.iconTextShadow"] !== false
    readonly property int labelSize: Number(Settings.values["desktop.iconTextSize"] ?? 0)
    readonly property int labelLines: Math.max(1, Math.min(4, Number(Settings.values["desktop.iconTextLines"] ?? 2)))
    readonly property bool showHiddenFiles: Settings.values["desktop.showHiddenFiles"] === true
    readonly property bool autoArrangeIcons: Settings.values["desktop.autoArrangeIcons"] !== false
    // Exposed for the smoke test and for plugins.
    property alias contextMenuVisible: contextMenu.visible

    visible: true
    color: "#101418"
    flags: Qt.FramelessWindowHint
    // Positioning used only without layer-shell (X11, including XWayland on GNOME).
    x: Screen.virtualX
    y: Screen.virtualY
    width: Screen.width
    height: Screen.height

    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.layer: LayerShell.Window.LayerBottom
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.scope: "newteller-desktop"

    // X11: a desktop window, kept below all other windows by the window manager.
    ShellWindow.role: ShellWindow.Desktop

    Motion {
        id: motion
    }

    // Without a wallpaper the desktop is a calm gradient in the theme colours
    // instead of a flat dark slab.
    Rectangle {
        visible: !desk.desktopBackgroundColor || desk.desktopBackgroundColor.length === 0 || desk.desktopBackgroundGradient
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.lighter(Theme.background, 1.45) }
            GradientStop { position: 0.55; color: Theme.background }
            GradientStop { position: 1.0; color: Qt.darker(Theme.background, 1.18) }
        }
    }
    // Custom background color
    Rectangle {
        visible: desk.desktopBackgroundColor && desk.desktopBackgroundColor.length > 0 && !desk.desktopBackgroundGradient
        anchors.fill: parent
        color: desk.desktopBackgroundColor
    }

    Image {
        id: wallpaperImage
        anchors.fill: parent
        asynchronous: true
        smooth: true
        fillMode: {
            switch (desk.wallpaperMode) {
            case "fill": return Image.PreserveAspectCrop
            case "fit": return Image.PreserveAspectFit
            case "stretch": return Image.Stretch
            case "tile": return Image.Tile
            case "center": return Image.Pad
            default: return Image.PreserveAspectCrop
            }
        }
        // A corrupt, unsupported or deleted wallpaper leaves the themed
        // gradient visible instead of a black (or half-loaded) screen.
        visible: status === Image.Ready
        // Never upload a full-resolution photo: a modern camera image is
        // tens of megapixels, which exceeds GL_MAX_TEXTURE_SIZE on many
        // drivers - the scene graph aborts there. The wallpaper is only drawn
        // at the window size anyway, so the decode is capped to it (the blur
        // setting shrinks it further). Tiling keeps the natural size.
        readonly property real decodeScale: desk.wallpaperBlur > 0
                                            ? 1 + desk.wallpaperBlur * 0.4 : 1
        sourceSize: desk.wallpaperMode === "tile" || width <= 0 || height <= 0
                    ? Qt.size(0, 0)
                    : Qt.size(Math.max(16, Math.min(4096, Math.round(width / decodeScale))),
                              Math.max(16, Math.min(4096, Math.round(height / decodeScale))))
        source: {
            const wp = desk.wallpaper;
            if (wp.length === 0) {
                // The bundled wallpaper, unless a plain colour was chosen or
                // it is switched off.
                return desk.desktopBackgroundColor.length === 0
                       && Settings.values["desktop.defaultWallpaper"] !== false
                       ? "qrc:/data/wallpapers/default.jpg" : "";
            }
            if (wp.indexOf("://") > 0)
                return wp;
            // Encode the path: a "#" or "%" in a file name must not become a
            // URL fragment or an invalid escape.
            return "file://" + wp.split("/").map(p => encodeURIComponent(p)).join("/");
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: desk.wallpaperDarken > 0
        color: "#000000"
        opacity: Math.max(0, Math.min(1, desk.wallpaperDarken / 100))
    }

    FolderListModel {
        id: desktopFiles
        folder: Shell.desktopUrl
        showDirs: true
        showDotAndDotDot: false
        showHidden: desk.showHiddenFiles
        sortField: FolderListModel.Name
    }

    // Images in ~/Pictures, used by "Change wallpaper".
    FolderListModel {
        id: wallpaperFiles
        folder: Shell.picturesPath
        nameFilters: ["*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp", "*.gif", "*.svg", "*.xpm",
                      "*.tif", "*.tiff", "*.avif", "*.heic", "*.jxl"]
        showDirs: false
        showDotAndDotDot: false
    }

    // --- the desktop grid -----------------------------------------------------
    // The whole desktop is an invisible grid of cells (desktop.cellWidth x
    // desktop.cellHeight). An icon takes one cell, a widget as many as its
    // size needs. Nothing is ever placed on a taken cell, so icons and
    // widgets cannot overlap; dragging drops onto the nearest free cells.
    //
    // desktop.gridPositions keeps where the user put things:
    //   { "icon:<file name>": {col, row}, "widget:<plugin id>": {col, row} }
    // Everything without a saved place is arranged: icons in columns from
    // the top-left corner, widgets in columns from the top-right corner.
    readonly property var gridSaved: Settings.values["desktop.gridPositions"] || ({})
    readonly property bool autoArrange: Settings.values["desktop.autoArrangeIcons"] !== false
    readonly property string panelEdge: Settings.values["panel.position"] || "bottom"
    readonly property int panelSize: (Settings.values["panel.height"] || 48) + (Settings.values["panel.margin"] || 0)
    readonly property int gridMargin: 12
    readonly property int gridLeft: gridMargin + (panelEdge === "left" ? panelSize : 0)
    readonly property int gridTop: gridMargin + (panelEdge === "top" ? panelSize : 0)
    readonly property int gridColumns: Math.max(1, Math.floor((width - gridLeft - gridMargin
                                                               - (panelEdge === "right" ? panelSize : 0))
                                                              / Math.max(1, cellWidth)))
    readonly property int gridRows: Math.max(1, Math.floor((height - gridTop - gridMargin
                                                            - (panelEdge === "bottom" ? panelSize : 0))
                                                           / Math.max(1, cellHeight)))
    // key -> {col, row, cols, rows}, recomputed by layoutGrid().
    property var gridCells: ({})

    function spanOf(size, cell) {
        return Math.max(1, Math.ceil((size + 8) / Math.max(1, cell)));
    }

    // Every element on the desktop: {key, cols, rows, widget}.
    function gridElements() {
        const list = [];
        if (desk.showIcons) {
            for (var i = 0; i < iconRepeater.count; ++i) {
                const icon = iconRepeater.itemAt(i);
                if (icon)
                    list.push({ key: "icon:" + icon.fileName, cols: 1, rows: 1, widget: false });
            }
        }
        for (var w = 0; w < widgetRepeater.count; ++w) {
            const loader = widgetRepeater.itemAt(w);
            if (!loader || !loader.item || loader.item.width <= 0 || loader.item.height <= 0)
                continue;
            list.push({ key: "widget:" + loader.pluginId, widget: true,
                        cols: Math.min(gridColumns, spanOf(loader.item.width, cellWidth)),
                        rows: Math.min(gridRows, spanOf(loader.item.height, cellHeight)) });
        }
        return list;
    }

    function cellsFree(taken, col, row, cols, rows) {
        if (col < 0 || row < 0 || col + cols > gridColumns || row + rows > gridRows)
            return false;
        for (var c = col; c < col + cols; ++c)
            for (var r = row; r < row + rows; ++r)
                if (taken[c + "," + r])
                    return false;
        return true;
    }
    function takeCells(taken, col, row, cols, rows) {
        for (var c = col; c < col + cols; ++c)
            for (var r = row; r < row + rows; ++r)
                taken[c + "," + r] = true;
    }
    // The free spot closest to (col, row), searched ring by ring.
    function nearestFree(taken, col, row, cols, rows) {
        const limit = Math.max(gridColumns, gridRows);
        for (var d = 0; d <= limit; ++d) {
            var best = null;
            var bestDistance = 1e9;
            for (var dc = -d; dc <= d; ++dc) {
                for (var dr = -d; dr <= d; ++dr) {
                    if (Math.max(Math.abs(dc), Math.abs(dr)) !== d)
                        continue;
                    if (cellsFree(taken, col + dc, row + dr, cols, rows)) {
                        const distance = dc * dc + dr * dr;
                        if (distance < bestDistance) {
                            bestDistance = distance;
                            best = { col: col + dc, row: row + dr };
                        }
                    }
                }
            }
            if (best)
                return best;
        }
        return null;
    }

    // Places saved before the grid existed (pixel positions in
    // desktop.iconPositions / desktop.widgetPositions), as grid cells, so an
    // update keeps the user's layout. Read only; the next drag saves the grid.
    readonly property var legacyIcons: Settings.values["desktop.iconPositions"] || ({})
    readonly property var legacyWidgets: Settings.values["desktop.widgetPositions"] || ({})
    function legacyCell(e) {
        const name = e.key.substring(e.key.indexOf(":") + 1);
        const old = (e.widget ? legacyWidgets : legacyIcons)[name];
        if (!old || old.x === undefined || old.y === undefined)
            return null;
        return { col: Math.max(0, Math.round((Number(old.x) - gridLeft) / Math.max(1, cellWidth))),
                 row: Math.max(0, Math.round((Number(old.y) - gridTop) / Math.max(1, cellHeight))) };
    }

    function layoutGrid() {
        const taken = {};
        const cells = {};
        const elements = gridElements();
        const pending = [];
        // 1. Places the user chose (icons only while auto-arrange is off).
        for (const e of elements) {
            var saved = gridSaved[e.key];
            if (!saved || saved.col === undefined)
                saved = legacyCell(e);
            const useSaved = saved && saved.col !== undefined && (e.widget || !autoArrange);
            if (useSaved && cellsFree(taken, Number(saved.col), Number(saved.row), e.cols, e.rows)) {
                cells[e.key] = { col: Number(saved.col), row: Number(saved.row), cols: e.cols, rows: e.rows };
                takeCells(taken, Number(saved.col), Number(saved.row), e.cols, e.rows);
            } else {
                pending.push(e);
            }
        }
        // 2. Widgets: columns from the right edge, top to bottom.
        for (const e of pending) {
            if (!e.widget)
                continue;
            var spot = null;
            for (var col = gridColumns - e.cols; col >= 0 && !spot; --col)
                for (var row = 0; row + e.rows <= gridRows && !spot; ++row)
                    if (cellsFree(taken, col, row, e.cols, e.rows))
                        spot = { col: col, row: row };
            if (!spot)
                spot = { col: Math.max(0, gridColumns - e.cols), row: 0 };
            cells[e.key] = { col: spot.col, row: spot.row, cols: e.cols, rows: e.rows };
            takeCells(taken, spot.col, spot.row, e.cols, e.rows);
        }
        // 3. Icons: columns from the left edge, top to bottom.
        var next = 0;
        for (const e of pending) {
            if (e.widget)
                continue;
            var place = null;
            while (!place && next < gridColumns * gridRows) {
                const c = Math.floor(next / gridRows);
                const r = next % gridRows;
                ++next;
                if (cellsFree(taken, c, r, 1, 1))
                    place = { col: c, row: r };
            }
            if (!place)
                place = { col: 0, row: 0 };
            cells[e.key] = { col: place.col, row: place.row, cols: 1, rows: 1 };
            takeCells(taken, place.col, place.row, 1, 1);
        }
        gridCells = cells;
    }
    function scheduleGridLayout() {
        gridLayoutTimer.restart();
    }

    // Pixel position of an element: centred in the cells it takes.
    function gridX(key, size) {
        const cell = gridCells[key];
        if (!cell)
            return -10000;
        return Math.round(gridLeft + cell.col * cellWidth + (cell.cols * cellWidth - size) / 2);
    }
    function gridY(key, size, centred) {
        const cell = gridCells[key];
        if (!cell)
            return -10000;
        return Math.round(gridTop + cell.row * cellHeight
                          + (centred ? (cell.rows * cellHeight - size) / 2 : 0));
    }

    // Drops a dragged element on the nearest free cells and remembers them.
    function dropOnGrid(key, x, y, size) {
        const cell = gridCells[key];
        const cols = cell ? cell.cols : 1;
        const rows = cell ? cell.rows : 1;
        const col = Math.round((x - gridLeft - (cols * cellWidth - size.width) / 2) / cellWidth);
        const row = Math.round((y - gridTop - (key.startsWith("widget:")
                                               ? (rows * cellHeight - size.height) / 2 : 0)) / cellHeight);
        const taken = {};
        for (const other in gridCells) {
            if (other === key)
                continue;
            const o = gridCells[other];
            takeCells(taken, o.col, o.row, o.cols, o.rows);
        }
        const target = nearestFree(taken,
                                   Math.max(0, Math.min(gridColumns - cols, col)),
                                   Math.max(0, Math.min(gridRows - rows, row)), cols, rows);
        if (!target) {
            scheduleGridLayout();
            return;
        }
        const copy = {};
        for (const k in gridSaved)
            copy[k] = gridSaved[k];
        // Places that came from the old pixel settings become grid cells
        // now, since those settings are cleared below.
        for (const k in gridCells) {
            if (copy[k] !== undefined || k === key)
                continue;
            const isWidget = k.startsWith("widget:");
            if ((isWidget || !autoArrange) && legacyCell({ key: k, widget: isWidget }))
                copy[k] = { "col": gridCells[k].col, "row": gridCells[k].row };
        }
        copy[key] = { "col": target.col, "row": target.row };
        const values = { "desktop.gridPositions": copy,
                         "desktop.iconPositions": ({}), "desktop.widgetPositions": ({}) };
        // Moving an icon by hand ends auto-arrange, so its place sticks.
        if (key.startsWith("icon:") && autoArrange) {
            // Keep every other icon where it is now.
            for (const k in gridCells)
                if (k.startsWith("icon:") && k !== key)
                    copy[k] = { "col": gridCells[k].col, "row": gridCells[k].row };
            values["desktop.autoArrangeIcons"] = false;
        }
        Settings.setValues(values);
    }

    function resetIconLayout() {
        const copy = {};
        for (const k in gridSaved)
            if (!k.startsWith("icon:"))
                copy[k] = gridSaved[k];
        Settings.setValues({ "desktop.gridPositions": copy, "desktop.autoArrangeIcons": true,
                             "desktop.iconPositions": ({}) });
    }
    function resetWidgets() {
        const copy = {};
        for (const k in gridSaved)
            if (!k.startsWith("widget:"))
                copy[k] = gridSaved[k];
        Settings.setValues({ "desktop.gridPositions": copy, "desktop.widgetPositions": ({}) });
    }

    onWidthChanged: scheduleGridLayout()
    onHeightChanged: scheduleGridLayout()
    onGridColumnsChanged: scheduleGridLayout()
    onGridRowsChanged: scheduleGridLayout()
    onGridSavedChanged: scheduleGridLayout()
    onLegacyIconsChanged: scheduleGridLayout()
    onLegacyWidgetsChanged: scheduleGridLayout()
    onAutoArrangeChanged: scheduleGridLayout()
    onShowIconsChanged: scheduleGridLayout()

    Timer {
        id: gridLayoutTimer
        interval: 40
        onTriggered: desk.layoutGrid()
    }

    // Desktop icons (~/Desktop).
    Item {
        id: iconLayer
        objectName: "desktopIcons"
        anchors.fill: parent
        visible: desk.showIcons

        Repeater {
            id: iconRepeater
            model: desktopFiles
            onCountChanged: desk.scheduleGridLayout()

            delegate: Item {
                id: iconDelegate
                objectName: "desktopIcon"
                width: desk.cellWidth
                height: desk.cellHeight

                required property int index
                required property string fileName
                required property string fileBaseName
                required property url fileUrl
                required property bool fileIsDir

                readonly property string gridKey: "icon:" + fileName
                readonly property bool renaming: desk.renamingUrl.length > 0
                                                 && desk.renamingUrl === fileUrl.toString()
                // A launcher of an installed application shows that
                // application's icon and name, and a double-click starts it.
                readonly property bool isLauncher: !fileIsDir && fileName.toLowerCase().endsWith(".desktop")
                readonly property var launcherApp: {
                    if (!isLauncher)
                        return ({});
                    const app = Apps.byId(fileName);
                    return app && app.id !== undefined ? app : Apps.byId(fileName.slice(0, -8));
                }
                readonly property bool knownLauncher: !!launcherApp && launcherApp.id !== undefined
                readonly property string resolvedIconName: fileIsDir ? "folder"
                    : knownLauncher && launcherApp.icon ? launcherApp.icon
                    : isLauncher ? "app-window" : "file-text"
                readonly property string label: knownLauncher && launcherApp.name ? launcherApp.name : fileBaseName
                // Pictures show themselves instead of a generic icon.
                readonly property bool isPicture: !fileIsDir
                    && /\.(png|jpe?g|gif|webp|bmp|svg)$/i.test(fileName)
                readonly property bool showsThumbnail: isPicture && thumbnail.status === Image.Ready
                // While dragging, x/y follow the pointer; otherwise they are
                // the element's grid cell.
                property real dragX: 0
                property real dragY: 0
                property bool dragging: false
                property real pressSceneX: 0
                property real pressSceneY: 0
                property real startIconX: 0
                property real startIconY: 0

                x: dragging ? dragX : desk.gridX(gridKey, width)
                y: dragging ? dragY : desk.gridY(gridKey, height, false)
                z: dragging ? 20 : 0
                opacity: dragging ? 0.85 : 1.0
                Component.onCompleted: desk.scheduleGridLayout()

                Behavior on opacity {
                    NumberAnimation { duration: motion.fast }
                }

                Rectangle {
                    visible: desk.iconGlow
                    anchors.centerIn: icon
                    width: desk.iconSize + 8
                    height: desk.iconSize + 8
                    radius: (desk.iconSize + 8) / 2
                    color: desk.iconColor && desk.iconColor.length > 0 ? desk.iconColor : Theme.foreground
                    opacity: 0.18
                }

                Image {
                    visible: desk.iconShadow && !showsThumbnail
                    x: icon.x + 1
                    y: icon.y + 2
                    width: desk.iconSize
                    height: desk.iconSize
                    sourceSize.width: desk.iconSize
                    sourceSize.height: desk.iconSize
                    fillMode: Image.PreserveAspectFit
                    source: Shell.tintedIcon(resolvedIconName, "#000000")
                    opacity: 0.35
                }

                Image {
                    id: thumbnail
                    objectName: "desktopThumbnail"
                    anchors.fill: icon
                    visible: showsThumbnail
                    asynchronous: true
                    cache: false
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: desk.iconSize * 2
                    sourceSize.height: desk.iconSize * 2
                    source: isPicture ? fileUrl : ""
                }

                Image {
                    id: icon
                    opacity: showsThumbnail ? 0 : 1
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: desk.iconSize
                    height: desk.iconSize
                    sourceSize.width: desk.iconSize
                    sourceSize.height: desk.iconSize
                    fillMode: Image.PreserveAspectFit
                    source: {
                        if (desk.iconColor && desk.iconColor.length > 0) {
                            return Shell.tintedIcon(resolvedIconName, desk.iconColor)
                        }
                        return Shell.tintedIcon(resolvedIconName, Theme.foreground)
                    }
                }

                Text {
                    anchors.top: icon.bottom
                    anchors.topMargin: 4
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: desk.cellWidth - 6
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    maximumLineCount: desk.labelLines
                    visible: !iconDelegate.renaming
                    text: iconDelegate.label
                    color: desk.labelColor
                    font.family: Theme.fontFamily
                    font.pointSize: desk.labelSize > 0 ? desk.labelSize : Theme.fontSize
                    style: desk.labelShadow ? Text.Outline : Text.Plain
                    styleColor: "#80000000"
                }

                // Rename in place: Enter keeps the new name, Escape or a
                // click elsewhere cancels.
                Rectangle {
                    visible: iconDelegate.renaming
                    anchors.top: icon.bottom
                    anchors.topMargin: 4
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: desk.cellWidth - 4
                    height: renameInput.implicitHeight + 6
                    z: 2
                    radius: 3
                    color: Theme.background
                    border.width: 1
                    border.color: Theme.accent

                    TextInput {
                        id: renameInput
                        objectName: "renameInput"
                        anchors.fill: parent
                        anchors.margins: 3
                        horizontalAlignment: TextInput.AlignHCenter
                        clip: true
                        selectByMouse: true
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize
                        onAccepted: {
                            Shell.renameFile(iconDelegate.fileUrl.toString(), text);
                            desk.renamingUrl = "";
                        }
                        Keys.onEscapePressed: desk.renamingUrl = ""
                        onActiveFocusChanged: {
                            if (!activeFocus && iconDelegate.renaming)
                                desk.renamingUrl = "";
                        }
                    }
                }
                onRenamingChanged: {
                    if (!renaming)
                        return;
                    renameInput.text = fileName;
                    // The name without its extension is selected, as file
                    // managers do.
                    const dot = fileIsDir ? -1 : fileName.lastIndexOf(".");
                    renameInput.forceActiveFocus();
                    renameInput.select(0, dot > 0 ? dot : fileName.length);
                }

                MouseArea {
                    id: iconMouse
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: iconDelegate.dragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor

                    // Right button: the icon's own menu (open, copy path, trash).
                    onClicked: (mouse) => {
                        if (mouse.button !== Qt.RightButton)
                            return;
                        const at = iconDelegate.mapToItem(null, mouse.x, mouse.y);
                        desk.openIconMenu(iconDelegate.fileUrl.toString(), at.x, at.y,
                                          iconDelegate.knownLauncher ? iconDelegate.launcherApp.id : "");
                    }
                    onPressed: (mouse) => {
                        if (mouse.button !== Qt.LeftButton)
                            return;
                        iconDelegate.dragging = false;
                        const scene = iconDelegate.mapToItem(null, mouse.x, mouse.y);
                        iconDelegate.pressSceneX = scene.x;
                        iconDelegate.pressSceneY = scene.y;
                        iconDelegate.startIconX = iconDelegate.x;
                        iconDelegate.startIconY = iconDelegate.y;
                        iconDelegate.dragX = iconDelegate.x;
                        iconDelegate.dragY = iconDelegate.y;
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed)
                            return;
                        const scene = iconDelegate.mapToItem(null, mouse.x, mouse.y);
                        const dx = scene.x - iconDelegate.pressSceneX;
                        const dy = scene.y - iconDelegate.pressSceneY;
                        if (!iconDelegate.dragging && Math.sqrt(dx * dx + dy * dy) > 5)
                            iconDelegate.dragging = true;
                        if (!iconDelegate.dragging)
                            return;
                        iconDelegate.dragX = Math.max(0, Math.min(desk.width - iconDelegate.width,
                                                                   iconDelegate.startIconX + dx));
                        iconDelegate.dragY = Math.max(0, Math.min(desk.height - iconDelegate.height,
                                                                   iconDelegate.startIconY + dy));
                    }
                    onReleased: {
                        if (iconDelegate.dragging)
                            desk.dropOnGrid(iconDelegate.gridKey, iconDelegate.dragX, iconDelegate.dragY,
                                            Qt.size(iconDelegate.width, iconDelegate.height));
                        iconDelegate.dragging = false;
                    }
                    onDoubleClicked: (mouse) => {
                        if (iconDelegate.dragging || mouse.button !== Qt.LeftButton)
                            return;
                        if (iconDelegate.knownLauncher)
                            Apps.launch(iconDelegate.launcherApp.id);
                        else
                            Shell.openUrl(iconDelegate.fileUrl.toString());
                    }
                }
            }
        }
    }

    // Desktop widgets (plugins), on the same grid.
    Repeater {
        id: widgetRepeater
        model: desk.desktopPlugins
        onCountChanged: desk.scheduleGridLayout()

        delegate: Loader {
            id: widgetLoader
            objectName: "desktopWidget"
            required property var modelData
            readonly property string pluginId: modelData.id
            readonly property string gridKey: "widget:" + pluginId
            property bool dragging: false
            source: modelData.entry
            x: desk.gridX(gridKey, width)
            y: desk.gridY(gridKey, height, true)
            z: dragging ? 20 : 1
            onLoaded: desk.scheduleGridLayout()
            onWidthChanged: desk.scheduleGridLayout()
            onHeightChanged: desk.scheduleGridLayout()

            // Drag a widget anywhere; it snaps to the nearest free cells.
            DragHandler {
                target: widgetLoader
                xAxis.minimum: 0
                xAxis.maximum: desk.width - widgetLoader.width
                yAxis.minimum: 0
                yAxis.maximum: desk.height - widgetLoader.height
                onActiveChanged: {
                    if (active) {
                        widgetLoader.dragging = true;
                        return;
                    }
                    desk.dropOnGrid(widgetLoader.gridKey, widgetLoader.x, widgetLoader.y,
                                    Qt.size(widgetLoader.width, widgetLoader.height));
                    widgetLoader.dragging = false;
                    widgetLoader.x = Qt.binding(() => desk.gridX(widgetLoader.gridKey, widgetLoader.width));
                    widgetLoader.y = Qt.binding(() => desk.gridY(widgetLoader.gridKey, widgetLoader.height, true));
                }
            }
        }
    }

    // Right-click menu: wallpaper, paste, terminal, settings. Right button
    // only, and below the icons and widgets (z -1): taking left clicks on
    // top of everything swallowed every click meant for a desktop widget
    // (the weather widget's settings button, for one).
    MouseArea {
        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.RightButton
        onClicked: (mouse) => {
            if (!desk.contextMenuEnabled)
                return;
            contextMenu.x = Math.min(mouse.x, desk.width - contextMenu.width - 8);
            contextMenu.y = Math.min(mouse.y, desk.height - contextMenu.height - 8);
            iconMenu.visible = false;
            contextMenu.visible = true;
        }
    }

    Rectangle {
        id: contextMenu
        visible: false
        width: 200
        height: menuColumn.implicitHeight + 12
        radius: Theme.radius
        color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.opacity)
        border.width: 1
        border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)
        z: 100

        // Menus appear immediately but get a very short fade-in, so they do
        // not pop; they close instantly, the way menus should.
        onVisibleChanged: {
            if (visible)
                menuIn.restart();
        }
        NumberAnimation {
            id: menuIn
            target: contextMenu
            property: "opacity"
            from: 0
            to: 1
            duration: motion.micro
            easing.type: motion.easeEnter
        }

        Column {
            id: menuColumn
            anchors.fill: parent
            anchors.margins: 6
            spacing: 2

            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "image"
                label: qsTr("Change wallpaper")
                onClicked: {
                    contextMenu.visible = false;
                    if (wallpaperFiles.count > 0) {
                        const next = Math.floor(Math.random() * wallpaperFiles.count);
                        Settings.setValue("desktop.wallpaper", wallpaperFiles.get(next).filePath);
                    } else {
                        Shell.openPath(Shell.picturesPath);
                    }
                }
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "clipboard"
                label: qsTr("Paste")
                onClicked: {
                    contextMenu.visible = false;
                    // Files copied in a file manager are copied here; any
                    // other clipboard text becomes a text file.
                    if (Shell.pasteFiles(Shell.desktopUrl) > 0)
                        return;
                    const text = Shell.clipboardText();
                    if (text.length > 0) {
                        const stamp = Qt.formatDateTime(Shell.now, "yyyyMMdd-hhmmss");
                        // The URL as is: the shell decodes it (spaces, Cyrillic,
                        // '#' in the folder name), cutting "file://" off did not.
                        Shell.saveTextFile(Shell.desktopUrl + "/" + qsTr("Pasted text %1.txt").arg(stamp), text);
                    }
                }
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "folder"
                label: qsTr("New folder")
                onClicked: {
                    contextMenu.visible = false;
                    Shell.createFolder(Shell.desktopUrl, qsTr("New folder"));
                }
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "terminal"
                label: qsTr("Open terminal")
                onClicked: {
                    contextMenu.visible = false;
                    Shell.terminal("");
                }
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "settings"
                label: qsTr("Settings")
                onClicked: {
                    contextMenu.visible = false;
                    Shell.openSettings();
                }
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "layout-grid"
                label: desk.autoArrange ? qsTr("Free icon placement") : qsTr("Auto-arrange icons")
                onClicked: {
                    contextMenu.visible = false;
                    if (desk.autoArrange)
                        Settings.setValue("desktop.autoArrangeIcons", false);
                    else
                        desk.resetIconLayout();
                }
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "rotate-ccw"
                label: qsTr("Reset icon layout")
                onClicked: {
                    contextMenu.visible = false;
                    desk.resetIconLayout();
                }
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "layout-grid"
                label: qsTr("Arrange widgets")
                onClicked: {
                    contextMenu.visible = false;
                    desk.resetWidgets();
                }
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "file-code"
                label: qsTr("Config file")
                onClicked: {
                    contextMenu.visible = false;
                    Shell.openConfig();
                }
            }
        }
    }

    // --- a desktop icon's menu ------------------------------------------
    // The icon whose name is being edited (its file:// URL), or empty.
    property string renamingUrl: ""
    property string iconMenuUrl: ""
    // The application of a launcher icon: Open starts it, like a double-click.
    property string iconMenuApp: ""
    readonly property string iconMenuPath: iconMenuUrl.length > 0
                                           ? decodeURIComponent(iconMenuUrl.replace(/^file:\/\//, "")) : ""

    function openIconMenu(url, x, y, appId) {
        contextMenu.visible = false;
        iconMenuUrl = url;
        iconMenuApp = appId || "";
        iconMenu.x = Math.max(8, Math.min(x, desk.width - iconMenu.width - 8));
        iconMenu.y = Math.max(8, Math.min(y, desk.height - iconMenu.height - 8));
        iconMenu.visible = true;
    }

    function iconMenuAction(action) {
        iconMenu.visible = false;
        if (action === "open" && iconMenuApp.length > 0)
            Apps.launch(iconMenuApp);
        else if (action === "open")
            Shell.openUrl(iconMenuUrl);
        else if (action === "rename")
            renamingUrl = iconMenuUrl;
        else if (action === "copyPath")
            Shell.copyToClipboard(iconMenuPath);
        else if (action === "trash")
            Shell.moveToTrash(iconMenuUrl);
    }

    Rectangle {
        id: iconMenu
        objectName: "iconMenu"
        visible: false
        width: 200
        height: iconMenuColumn.implicitHeight + 12
        radius: Theme.radius
        color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, Theme.opacity)
        border.width: 1
        border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)
        z: 100

        Column {
            id: iconMenuColumn
            anchors.fill: parent
            anchors.margins: 6
            spacing: 2

            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "folder"
                label: qsTr("Open")
                onClicked: desk.iconMenuAction("open")
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "type"
                label: qsTr("Rename")
                onClicked: desk.iconMenuAction("rename")
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "clipboard"
                label: qsTr("Copy path")
                onClicked: desk.iconMenuAction("copyPath")
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "trash"
                label: qsTr("Move to Trash")
                onClicked: desk.iconMenuAction("trash")
            }
        }
    }

    // Clicking anywhere else closes the menus.
    MouseArea {
        anchors.fill: parent
        z: 99
        enabled: contextMenu.visible || iconMenu.visible
        onPressed: (mouse) => {
            if (!contextMenu.contains(mapToItem(contextMenu, mouse.x, mouse.y)))
                contextMenu.visible = false;
            if (!iconMenu.contains(mapToItem(iconMenu, mouse.x, mouse.y)))
                iconMenu.visible = false;
            mouse.accepted = false;
        }
    }
}
