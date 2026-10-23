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
        sourceSize: desk.wallpaperBlur > 0 && width > 0 && height > 0
                    ? Qt.size(Math.max(16, Math.round(width / (1 + desk.wallpaperBlur * 0.4))),
                              Math.max(16, Math.round(height / (1 + desk.wallpaperBlur * 0.4))))
                    : Qt.size(0, 0)
        source: desk.wallpaper.length === 0 ? ""
                : (desk.wallpaper.indexOf("://") > 0 ? desk.wallpaper : "file://" + desk.wallpaper)
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
        folder: Shell.home + "/Pictures"
        nameFilters: ["*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp"]
        showDirs: false
        showDotAndDotDot: false
    }

    // Desktop icons. Auto-arrange lays them out on the configured grid; once
    // an icon is dragged its position is stored in desktop.iconPositions and
    // restored on the next start. Dragging the first icon also turns
    // auto-arrange off, so the layout the user made stays put.
    Item {
        id: iconLayer
        anchors.fill: parent
        anchors.margins: 12
        visible: desk.showIcons
        clip: true

        readonly property var savedPositions: Settings.values["desktop.iconPositions"] || ({})
        readonly property bool autoArrange: Settings.values["desktop.autoArrangeIcons"] !== false
        readonly property int columns: Math.max(1, Math.floor(width / Math.max(1, desk.cellWidth)))

        function gridX(index) {
            return (index % columns) * desk.cellWidth;
        }
        function gridY(index) {
            return Math.floor(index / columns) * desk.cellHeight;
        }
        function savedX(name, fallback) {
            const entry = savedPositions[name];
            return (autoArrange || !entry || entry.x === undefined) ? fallback : Number(entry.x);
        }
        function savedY(name, fallback) {
            const entry = savedPositions[name];
            return (autoArrange || !entry || entry.y === undefined) ? fallback : Number(entry.y);
        }

        function savePosition(name, x, y) {
            const old = savedPositions || ({});
            const copy = {};
            for (const key in old)
                copy[key] = old[key];
            copy[name] = { "x": Math.round(x), "y": Math.round(y) };
            if (autoArrange)
                Settings.setValues({ "desktop.autoArrangeIcons": false,
                                     "desktop.iconPositions": copy });
            else
                Settings.setValue("desktop.iconPositions", copy);
        }

        function resetPositions() {
            Settings.setValues({ "desktop.iconPositions": ({}),
                                 "desktop.autoArrangeIcons": true });
        }

        Repeater {
            model: desktopFiles

            delegate: Item {
                id: iconDelegate
                width: desk.cellWidth
                height: desk.cellHeight

                required property int index
                required property string fileName
                required property string fileBaseName
                required property url fileUrl
                required property bool fileIsDir

                readonly property string resolvedIconName: fileIsDir ? "folder" : "file-text"
                // While dragging, x/y follow the pointer; otherwise they are a
                // pure binding to the saved position or the auto-arrange grid.
                property real dragX: 0
                property real dragY: 0
                property bool dragging: false
                property real pressSceneX: 0
                property real pressSceneY: 0
                property real startIconX: 0
                property real startIconY: 0

                x: dragging ? dragX : iconLayer.savedX(fileName, iconLayer.gridX(index))
                y: dragging ? dragY : iconLayer.savedY(fileName, iconLayer.gridY(index))
                z: dragging ? 20 : 0
                opacity: dragging ? 0.85 : 1.0

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
                    visible: desk.iconShadow
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
                    id: icon
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
                    maximumLineCount: 2
                    text: fileBaseName
                    color: desk.iconColor && desk.iconColor.length > 0 ? desk.iconColor : "white"
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                    style: desk.iconShadow ? Text.Outline : Text.Plain
                    styleColor: "#80000000"
                }

                MouseArea {
                    id: iconMouse
                    anchors.fill: parent
                    cursorShape: iconDelegate.dragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor

                    onPressed: (mouse) => {
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
                        if (!iconDelegate.dragging
                            && Math.sqrt(dx * dx + dy * dy) > 5) {
                            iconDelegate.dragging = true;
                        }
                        if (!iconDelegate.dragging)
                            return;
                        iconDelegate.dragX = Math.max(0, Math.min(iconLayer.width - iconDelegate.width,
                                                                   iconDelegate.startIconX + dx));
                        iconDelegate.dragY = Math.max(0, Math.min(iconLayer.height - iconDelegate.height,
                                                                   iconDelegate.startIconY + dy));
                    }
                    onReleased: {
                        if (iconDelegate.dragging) {
                            iconLayer.savePosition(iconDelegate.fileName,
                                                   iconDelegate.dragX,
                                                   iconDelegate.dragY);
                        }
                        iconDelegate.dragging = false;
                    }
                    onDoubleClicked: {
                        if (!iconDelegate.dragging)
                            Shell.openUrl(iconDelegate.fileUrl.toString());
                    }
                }
            }
        }
    }

    Repeater {
        model: desk.desktopPlugins

        delegate: Loader {
            required property var modelData
            source: modelData.entry
            x: modelData.x
            y: modelData.y
        }
    }

    // Right-click menu: wallpaper, paste, terminal, settings.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton | Qt.LeftButton
        onClicked: (mouse) => {
            if (mouse.button !== Qt.RightButton || !desk.contextMenuEnabled)
                return;
            contextMenu.x = Math.min(mouse.x, desk.width - contextMenu.width - 8);
            contextMenu.y = Math.min(mouse.y, desk.height - contextMenu.height - 8);
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
                label: "Change wallpaper"
                onClicked: {
                    contextMenu.visible = false;
                    if (wallpaperFiles.count > 0) {
                        const next = Math.floor(Math.random() * wallpaperFiles.count);
                        Settings.setValue("desktop.wallpaper", wallpaperFiles.get(next).filePath);
                    } else {
                        Shell.openPath(Shell.home + "/Pictures");
                    }
                }
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "clipboard"
                label: "Paste"
                onClicked: {
                    contextMenu.visible = false;
                    const text = Shell.clipboardText();
                    if (text.length > 0) {
                        const stamp = Qt.formatDateTime(Shell.now, "yyyyMMdd-hhmmss");
                        Shell.saveTextFile(Shell.desktopUrl.replace("file://", "")
                                           + "/Pasted " + stamp + ".txt", text);
                    }
                }
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "terminal"
                label: "Open terminal"
                onClicked: {
                    contextMenu.visible = false;
                    Shell.terminal("");
                }
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "settings"
                label: "Settings"
                onClicked: {
                    contextMenu.visible = false;
                    Shell.openSettings();
                }
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "layout-grid"
                label: iconLayer.autoArrange ? "Free icon placement" : "Auto-arrange icons"
                onClicked: {
                    contextMenu.visible = false;
                    if (iconLayer.autoArrange)
                        Settings.setValue("desktop.autoArrangeIcons", false);
                    else
                        iconLayer.resetPositions();
                }
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "rotate-ccw"
                label: "Reset icon layout"
                onClicked: {
                    contextMenu.visible = false;
                    iconLayer.resetPositions();
                }
            }
            TaskButton {
                width: parent.width
                fillWidth: true
                iconName: "file-code"
                label: "Config file"
                onClicked: {
                    contextMenu.visible = false;
                    Shell.openConfig();
                }
            }
        }
    }

    // Clicking anywhere else closes the menu.
    MouseArea {
        anchors.fill: parent
        z: 99
        enabled: contextMenu.visible
        onPressed: (mouse) => {
            if (!contextMenu.contains(mapToItem(contextMenu, mouse.x, mouse.y)))
                contextMenu.visible = false;
            mouse.accepted = false;
        }
    }
}
