import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Qt.labs.folderlistmodel

// The "choose a photo from the file manager" dialog of the settings: browse
// the whole file system from inside the shell, preview images and pick one.
// A plain popup window that belongs to the settings window.
Window {
    id: picker

    signal accepted(string path)

    property bool imagesOnly: true
    property var anchorItem: null
    readonly property int framePad: 8

    // The folder shown right now, as a plain path (also used by the tests).
    property string currentPath: ""
    // Bumped on every open: a close that was queued before the dialog was
    // reopened must not close it again.
    property int openGeneration: 0

    function openFor(startFolder, imagesOnlyMode) {
        imagesOnly = imagesOnlyMode !== false;
        selectedPath = "";
        openGeneration += 1;
        // The caller may hand over a plain path, "~/Pictures", a file:// URL or
        // an already percent-encoded path; folderPath() turns all of it into an
        // existing directory (home when the folder is gone). Passing a broken
        // URL straight to FolderListModel is what used to leave the dialog
        // showing an unreadable folder.
        enter(startFolder);
        // Snap first so the first frame is the small, transparent card.
        pickerCard.prepareOpen();
        visible = true;
        requestActivate();
        pickerCard.playOpen();
    }

    // A path as a properly encoded file URL: every segment is encoded, so
    // names containing "#", "%" or spaces cannot turn into a fragment or a
    // malformed escape that FolderListModel cannot open.
    function fileUrlFor(path) {
        return "file://" + String(path).split("/").map(function(part) {
            return encodeURIComponent(part);
        }).join("/");
    }

    function enter(folder) {
        const path = Shell.folderPath(folder === undefined || folder === null ? "" : String(folder));
        currentPath = path;
        folderModel.folder = fileUrlFor(path);
        if (pathInput.text !== path)
            pathInput.text = path;
        selectedPath = "";
    }

    function goUp() {
        const parent = folderModel.parentFolder;
        if (parent && parent.toString().length > 0)
            enter(parent);
    }

    // Accepts a file. Only real files are accepted, and with the image filter
    // on only files Qt can actually load as an image - the desktop never gets
    // a wallpaper that would fail to render.
    function choose(filePath) {
        let path = filePath === undefined || filePath === null ? "" : String(filePath);
        if (path.indexOf("file:") === 0) {
            const encoded = path.substring(7);
            try {
                path = decodeURIComponent(encoded);
            } catch (error) {
                path = encoded;   // a stray "%" is not a fatal error
            }
        }
        if (!Shell.fileExists(path))
            return;
        if (imagesOnly && !Shell.isImageFile(path))
            return;
        accepted(path);
        hide();
    }

    // Closing happens one event-loop turn later: hiding a window from inside
    // the input event that opened or activated it is a known crash source on
    // some platform plugins, and the target may already be gone by then.
    function hide() {
        if (!visible || pickerCard.closing)
            return;
        const generation = openGeneration;
        Qt.callLater(function() {
            if (!picker.visible || pickerCard.closing || picker.openGeneration !== generation)
                return;   // reopened in the meantime
            pickerCard.playClose(function() { picker.visible = false; });
        });
    }

    property string selectedPath: ""

    width: 620
    height: 440
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint

    // A file dialog stays open when it loses the keyboard focus: the old
    // "hide as soon as it is not active" made the window close itself the
    // instant the compositor granted (or refused) activation, which looked
    // exactly like the picker crashing.
    Shortcut {
        sequence: "Esc"
        enabled: picker.visible
        onActivated: picker.hide()
    }

    onClosing: (close) => {
        close.accepted = true;
        picker.hide();
    }

    FolderListModel {
        id: folderModel
        showDirs: true
        showDirsFirst: true
        showDotAndDotDot: false
        showHidden: false
        // The same list the system chooser filters with (src/fileportal.cpp);
        // whether a file really is an image is checked with Shell.isImageFile()
        // before it is accepted.
        nameFilters: picker.imagesOnly
                     ? ["*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp", "*.gif", "*.svg", "*.xpm",
                        "*.tif", "*.tiff", "*.avif", "*.heic", "*.jxl"] : []
    }

    ShellFrame {
        id: pickerCard
        anchors.fill: parent

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            ShellTitleBar {
                Layout.fillWidth: true
                title: "Choose a file"
                iconName: "folder"
                onCloseClicked: picker.hide()
            }

            // --- location row ---------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                TaskButton {
                    width: 30
                    height: 30
                    iconName: "arrow-up"
                    onClicked: picker.goUp()
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    radius: Theme.radius
                    color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.95)
                    border.width: pathInput.activeFocus ? 1 : 0
                    border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.7)

                    TextInput {
                        id: pathInput
                        anchors.fill: parent
                        anchors.margins: 8
                        clip: true
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 1
                        verticalAlignment: TextInput.AlignVCenter
                        selectByMouse: true
                        onEditingFinished: {
                            const trimmed = text.trim();
                            if (trimmed.length === 0)
                                return;
                            const expanded = trimmed.indexOf("~") === 0
                                    ? Shell.home + trimmed.substring(1) : trimmed;
                            picker.enter(expanded);
                        }
                    }
                }
            }

            // --- shortcuts --------------------------------------------------
            Row {
                spacing: 6

                TaskButton {
                    label: "Home"
                    iconName: "house"
                    onClicked: picker.enter(Shell.home)
                }
                TaskButton {
                    label: "Pictures"
                    iconName: "image"
                    onClicked: picker.enter(Shell.home + "/Pictures")
                }
                TaskButton {
                    label: "Documents"
                    iconName: "file-text"
                    onClicked: picker.enter(Shell.home + "/Documents")
                }
                TaskButton {
                    label: "Downloads"
                    iconName: "download"
                    onClicked: picker.enter(Shell.home + "/Downloads")
                }
                TaskButton {
                    label: picker.imagesOnly ? "All files" : "Images only"
                    iconName: "funnel"
                    onClicked: {
                        picker.imagesOnly = !picker.imagesOnly;
                        folderModel.nameFilters = picker.imagesOnly
                                ? ["*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp", "*.gif",
                                   "*.svg", "*.xpm", "*.tif", "*.tiff", "*.avif", "*.heic",
                                   "*.jxl"] : [];
                    }
                }
            }

            // --- listing ----------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                ListView {
                    id: fileList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 1
                    boundsBehavior: Flickable.StopAtBounds
                    model: folderModel

                    delegate: Item {
                        id: fileRow
                        required property string fileName
                        required property string filePath
                        required property url fileUrl
                        required property bool fileIsDir
                        width: fileList.width
                        height: 30

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radius
                            color: fileRow.filePath === picker.selectedPath
                                   ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.30)
                                   : fileRowMouse.containsMouse
                                     ? Qt.rgba(Theme.foreground.r, Theme.foreground.g,
                                               Theme.foreground.b, 0.08)
                                     : "transparent"

                            Image {
                                id: rowIcon
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: 20
                                height: 20
                                sourceSize.width: 40
                                sourceSize.height: 40
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                source: fileRow.fileIsDir
                                        ? Shell.tintedIcon("folder", Theme.foreground)
                                        : Shell.tintedIcon(mimeTypeFor(fileRow.fileName), Theme.foreground)
                            }

                            Text {
                                anchors.left: rowIcon.right
                                anchors.leftMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: fileRow.fileName
                                color: Theme.foreground
                                font.family: Theme.fontFamily
                                font.pointSize: Theme.fontSize
                                elide: Text.ElideMiddle
                                width: Math.min(implicitWidth, parent.width - 40)
                            }

                            MouseArea {
                                id: fileRowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: (mouse) => {
                                    if (fileRow.fileIsDir) {
                                        picker.enter(fileRow.fileUrl);
                                    } else {
                                        picker.selectedPath = fileRow.filePath;
                                    }
                                }
                                onDoubleClicked: (mouse) => {
                                    if (!fileRow.fileIsDir)
                                        picker.choose(fileRow.filePath);
                                }
                            }
                        }
                    }
                }

                // Preview of the selected image.
                Rectangle {
                    Layout.preferredWidth: 150
                    Layout.fillHeight: true
                    radius: Theme.radius
                    color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.7)
                    border.width: 1
                    border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.8)

                    Image {
                        anchors.fill: parent
                        anchors.margins: 6
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        // Decode at preview size, not at the photo's size.
                        sourceSize.width: 300
                        sourceSize.height: 400
                        visible: picker.selectedPath.length > 0
                        source: picker.selectedPath.length > 0
                                ? "file://" + picker.selectedPath : ""
                    }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 12
                        visible: picker.selectedPath.length === 0
                        text: picker.selectedPath.length === 0 ? "Pick a file to preview it" : ""
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 2
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            // --- footer -----------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: picker.selectedPath.length > 0 ? picker.selectedPath : ""
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 2
                    elide: Text.ElideMiddle
                }
                TaskButton {
                    iconName: "check"
                    label: "Choose"
                    active: picker.selectedPath.length > 0
                    onClicked: {
                        if (picker.selectedPath.length > 0)
                            picker.choose(picker.selectedPath);
                    }
                }
                TaskButton {
                    iconName: "x"
                    label: "Cancel"
                    onClicked: picker.hide()
                }
            }
        }
    }

    // Icon name for a file name: the image extension map covers wallpapers.
    function mimeTypeFor(name) {
        const n = name.toLowerCase();
        if (n.endsWith(".png") || n.endsWith(".jpg") || n.endsWith(".jpeg")
            || n.endsWith(".webp") || n.endsWith(".bmp") || n.endsWith(".gif")
            || n.endsWith(".svg") || n.endsWith(".xpm") || n.endsWith(".tif")
            || n.endsWith(".tiff") || n.endsWith(".avif") || n.endsWith(".heic")
            || n.endsWith(".jxl"))
            return "image";
        if (n.endsWith(".txt") || n.endsWith(".md"))
            return "file-text";
        if (n.endsWith(".pdf"))
            return "file-text";
        return "file-text";
    }
}
