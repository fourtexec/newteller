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
    readonly property bool showIcons: Settings.values["desktop.showIcons"] !== false
    readonly property int iconSize: Settings.values["desktop.iconSize"] || 48
    readonly property int cellWidth: Settings.values["desktop.cellWidth"] || 96
    readonly property int cellHeight: Settings.values["desktop.cellHeight"] || 110
    readonly property bool contextMenuEnabled: Settings.values["desktop.contextMenu"] !== false
    readonly property var desktopPlugins: Plugins.plugins.filter(p => p.type === "desktop" && p.enabled)
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

    // Without a wallpaper the desktop is a calm gradient in the theme colours
    // instead of a flat dark slab.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.lighter(Theme.background, 1.45) }
            GradientStop { position: 0.55; color: Theme.background }
            GradientStop { position: 1.0; color: Qt.darker(Theme.background, 1.18) }
        }
    }

    Image {
        anchors.fill: parent
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        source: desk.wallpaper.length === 0 ? ""
                : (desk.wallpaper.indexOf("://") > 0 ? desk.wallpaper : "file://" + desk.wallpaper)
    }

    FolderListModel {
        id: desktopFiles
        folder: Shell.desktopUrl
        showDirs: true
        showDotAndDotDot: false
        showHidden: false
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

    GridView {
        anchors.fill: parent
        anchors.margins: 12
        visible: desk.showIcons
        cellWidth: desk.cellWidth
        cellHeight: desk.cellHeight
        model: desktopFiles

        delegate: Item {
            width: desk.cellWidth
            height: desk.cellHeight

            required property string fileName
            required property string fileBaseName
            required property url fileUrl
            required property bool fileIsDir

            Image {
                id: icon
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                width: desk.iconSize
                height: desk.iconSize
                sourceSize.width: desk.iconSize
                sourceSize.height: desk.iconSize
                fillMode: Image.PreserveAspectFit
                source: Shell.tintedIcon(fileIsDir ? "folder" : "file", Theme.foreground)
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
                color: "white"
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize
                style: Text.Outline
                styleColor: "#80000000"
            }

            MouseArea {
                anchors.fill: parent
                onDoubleClicked: Shell.openUrl(fileUrl.toString())
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
