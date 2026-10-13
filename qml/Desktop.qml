import QtQuick
import QtQuick.Window
import Qt.labs.folderlistmodel
import org.kde.layershell 1.0 as LayerShell

// Desktop background: wallpaper, icons from ~/Desktop, and desktop plugins.
Window {
    id: desk

    readonly property string wallpaper: Settings.values["desktop.wallpaper"] || ""

    visible: true
    color: "#101418"
    flags: Qt.FramelessWindowHint
    x: screen.geometry.x
    y: screen.geometry.y
    width: screen.geometry.width
    height: screen.geometry.height

    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.layer: LayerShell.Window.LayerBottom
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.scope: "newteller-desktop"

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

    GridView {
        anchors.fill: parent
        anchors.margins: 12
        cellWidth: 96
        cellHeight: 100
        model: desktopFiles

        delegate: Item {
            width: 96
            height: 100

            required property string fileName
            required property string fileBaseName
            required property url fileUrl
            required property bool fileIsDir

            Image {
                id: icon
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                width: 40
                height: 40
                sourceSize.width: 40
                sourceSize.height: 40
                fillMode: Image.PreserveAspectFit
                source: Shell.iconUrl(fileIsDir ? "folder" : "file")
            }

            Text {
                anchors.top: icon.bottom
                anchors.topMargin: 4
                anchors.horizontalCenter: parent.horizontalCenter
                width: 90
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
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
        model: Plugins.plugins.filter(p => p.type === "desktop" && p.enabled)

        delegate: Loader {
            required property var modelData
            source: modelData.entry
            x: modelData.x
            y: modelData.y
        }
    }
}
