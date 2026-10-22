import QtQuick

// Desktop Sticky Notes Widget
// Allows creating, editing, and managing sticky notes on the desktop.
Rectangle {
    id: widget

    readonly property var pluginSettings: (parent && parent.modelData && parent.modelData.settings)
                                          ? parent.modelData.settings : ({})
    property color noteColor: pluginSettings.noteColor ? Qt.color(pluginSettings.noteColor) : "#ffffdc"
    property color textColor: pluginSettings.textColor ? Qt.color(pluginSettings.textColor) : "#1a1a1a"
    property int fontSize: pluginSettings.fontSize !== undefined ? Number(pluginSettings.fontSize) : 11
    property bool autoSave: pluginSettings.autoSave !== false

    property var notes: [
        {
            id: 1,
            text: "Welcome to NewTeller!\n\nClick to edit this note.\nDrag from the header to move.",
            x: 0,
            y: 0,
            width: 220,
            height: 150
        }
    ]

    width: 300
    height: 200
    color: "transparent"

    function addNote() {
        var nextId = 1
        for (var i = 0; i < notes.length; ++i) {
            if (notes[i].id >= nextId)
                nextId = notes[i].id + 1
        }
        var updated = notes.slice()
        updated.push({
            id: nextId,
            text: "New note",
            x: 20 * (updated.length % 5),
            y: 20 * (updated.length % 5),
            width: 220,
            height: 150
        })
        notes = updated
    }

    function deleteNote(idx) {
        if (idx < 0 || idx >= notes.length)
            return
        var updated = notes.slice()
        updated.splice(idx, 1)
        notes = updated
    }

    function updateNoteField(idx, field, val) {
        if (idx < 0 || idx >= notes.length)
            return
        notes[idx][field] = val
    }

    // Empty-state card when all notes are closed
    Rectangle {
        visible: widget.notes.length === 0
        anchors.fill: parent
        radius: Theme.radius + 4
        color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, Theme.opacity * 0.9)
        border.width: 1
        border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)

        Text {
            anchors.centerIn: parent
            text: "No notes yet. Click to add one."
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: widget.addNote()
        }
    }

    Item {
        id: notesContainer
        anchors.fill: parent
        clip: false

        Repeater {
            model: widget.notes
            delegate: NoteItem {
                required property var modelData
                required property int index

                noteIndex: index
                noteText: modelData.text
                noteColor: widget.noteColor
                textColor: widget.textColor
                fontSize: widget.fontSize
                noteWidth: modelData.width
                noteHeight: modelData.height
                x: modelData.x
                y: modelData.y

                onNoteEdited: (updatedText) => widget.updateNoteField(index, "text", updatedText)
                onDeleted: widget.deleteNote(index)
                onGeometryCommitted: (nx, ny, nw, nh) => {
                    widget.updateNoteField(index, "x", nx)
                    widget.updateNoteField(index, "y", ny)
                    widget.updateNoteField(index, "width", nw)
                    widget.updateNoteField(index, "height", nh)
                }
            }
        }
    }

    Rectangle {
        id: addButton
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 8
        width: 36
        height: 36
        radius: 18
        color: Theme.accent
        visible: widget.notes.length > 0

        Image {
            anchors.centerIn: parent
            width: 20
            height: 20
            sourceSize.width: 40
            sourceSize.height: 40
            source: Shell.tintedIcon("plus", "#ffffff")
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: widget.addNote()
        }
    }

    component NoteItem: Rectangle {
        id: note

        property int noteIndex: 0
        property string noteText: ""
        property color noteColor: "#ffffdc"
        property color textColor: "#1a1a1a"
        property int fontSize: 11
        property real noteWidth: 220
        property real noteHeight: 150

        signal noteEdited(string updatedText)
        signal deleted()
        signal geometryCommitted(real nx, real ny, real nw, real nh)

        width: noteWidth
        height: noteHeight
        radius: 6
        color: noteColor
        border.width: 1
        border.color: Qt.darker(noteColor, 1.25)

        Rectangle {
            id: header
            width: parent.width
            height: 28
            radius: 6
            color: Qt.darker(note.noteColor, 1.08)

            // Drag area covers header beneath the delete button
            MouseArea {
                id: dragArea
                anchors.fill: parent
                anchors.rightMargin: 28
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                drag.target: note
                drag.axis: Drag.XAndYAxis
                onReleased: note.geometryCommitted(note.x, note.y, note.width, note.height)
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Repeater {
                        model: 3
                        delegate: Rectangle {
                            width: 3
                            height: 3
                            radius: 1.5
                            color: Qt.darker(note.noteColor, 1.8)
                        }
                    }
                }

                Text {
                    text: "Note #" + (note.noteIndex + 1)
                    color: Qt.darker(note.noteColor, 2.2)
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                    font.bold: true
                }
            }

            MouseArea {
                width: 24
                height: 24
                anchors.right: parent.right
                anchors.rightMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                cursorShape: Qt.PointingHandCursor
                onClicked: note.deleted()

                Image {
                    anchors.centerIn: parent
                    width: 14
                    height: 14
                    sourceSize.width: 28
                    sourceSize.height: 28
                    source: Shell.tintedIcon("x", Qt.darker(note.noteColor, 2.2))
                }
            }
        }

        TextEdit {
            id: editor
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 8
            clip: true
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            text: note.noteText
            color: note.textColor
            font.family: Theme.fontFamily
            font.pointSize: note.fontSize
            onTextChanged: {
                if (text !== note.noteText)
                    note.noteEdited(text)
            }
        }

        Rectangle {
            id: resizeHandle
            width: 12
            height: 12
            radius: 3
            color: Qt.darker(note.noteColor, 1.25)
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 2

            MouseArea {
                property real startX: 0
                property real startY: 0

                anchors.fill: parent
                cursorShape: Qt.SizeFDiagCursor
                onPressed: (mouse) => {
                    startX = mouse.x
                    startY = mouse.y
                }
                onPositionChanged: (mouse) => {
                    if (pressed) {
                        note.noteWidth = Math.max(140, note.noteWidth + (mouse.x - startX))
                        note.noteHeight = Math.max(96, note.noteHeight + (mouse.y - startY))
                    }
                }
                onReleased: note.geometryCommitted(note.x, note.y, note.width, note.height)
            }
        }
    }
}
