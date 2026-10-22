import QtQuick
import QtQuick.Controls
import NewTeller 1.0

// Desktop Sticky Notes Widget
// Allows creating, editing, and managing sticky notes on the desktop
Rectangle {
    id: widget
    
    property color noteColor: manifest.settings ? manifest.settings.noteColor : "#ffffdc"
    property color textColor: manifest.settings ? manifest.settings.textColor : "#000000"
    property int fontSize: manifest.settings ? manifest.settings.fontSize : 12
    property bool autoSave: manifest.settings ? manifest.settings.autoSave : true
    
    // List of notes
    property var notes: [
        { id: 1, text: "Welcome to NewTeller!\n\nDouble-click to edit this note.\nDrag from the header to move.", x: 0, y: 0, width: 200, height: 150 }
    ]
    
    // Currently selected note for editing
    property int selectedNoteIndex: -1
    
    width: 300
    height: 200
    color: "transparent"
    
    // Background for the widget container (only visible when no notes)
    Rectangle {
        visible: notes.length === 0
        anchors.fill: parent
        radius: Theme.radius
        color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, Theme.opacity * 0.9)
        border.width: 1
        border.color: Theme.border
        
        Text {
            anchors.centerIn: parent
            text: "No notes yet. Click + to add one."
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize
        }
        
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: addNote()
        }
    }
    
    // Note container
    Item {
        id: notesContainer
        anchors.fill: parent
        clip: false
        
        Repeater {
            model: notes
            delegate: NoteItem {
                id: noteItem
                required property var modelData
                required property int index
                
                noteText: modelData.text
                noteColor: widget.noteColor
                textColor: widget.textColor
                fontSize: widget.fontSize
                noteWidth: modelData.width
                noteHeight: modelData.height
                
                x: modelData.x
                y: modelData.y
                
                onTextChanged: {
                    notes[index].text = noteText
                    if (widget.autoSave) saveNotes()
                }
                
                onDeleted: {
                    notes.splice(index, 1)
                    saveNotes()
                }
                
                onMoved: (dx, dy) => {
                    notes[index].x += dx
                    notes[index].y += dy
                    if (widget.autoSave) saveNotes()
                }
                
                onResized: (dw, dh) => {
                    notes[index].width += dw
                    notes[index].height += dh
                    if (widget.autoSave) saveNotes()
                }
            }
        }
    }
    
    // Add note button
    Rectangle {
        id: addButton
        anchors {
            bottom: parent.bottom
            right: parent.right
            margins: 8
        }
        width: 40
        height: 40
        radius: 20
        color: Theme.accent
        visible: notes.length > 0
        
        Image {
            anchors.centerIn: parent
            width: 24
            height: 24
            source: Shell.iconUrl("plus", "#ffffff")
        }
        
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: addNote()
        }
        
        // Tooltip
        ShellToolTip {
            text: "Add new note"
            target: addButton
        }
    }
    
    function addNote() {
        var newId = notes.length > 0 ? Math.max(...notes.map(n => n.id)) + 1 : 1
        notes.push({
            id: newId,
            text: "New note",
            x: 20,
            y: 20,
            width: 200,
            height: 150
        })
        saveNotes()
    }
    
    function saveNotes() {
        // In a real implementation, this would save to a file
        // For now, we'll just log it
        console.log("Notes saved:", notes.length, "notes")
    }
    
    function loadNotes() {
        // In a real implementation, this would load from a file
    }
    
    Component.onCompleted: loadNotes()
    
    // Note item component
    component NoteItem: Rectangle {
        id: note
        
        property string noteText
        property color noteColor
        property color textColor
        property int fontSize
        property int noteWidth: 200
        property int noteHeight: 150
        
        signal textChanged(string text)
        signal deleted()
        signal moved(real dx, real dy)
        signal resized(real dw, real dh)
        
        width: noteWidth
        height: noteHeight
        radius: 4
        color: noteColor
        border.width: 1
        border.color: Qt.darker(noteColor, 1.3)
        
        // Header bar
        Rectangle {
            id: header
            width: parent.width
            height: 28
            radius: 4
            topLeftRadius: 4
            topRightRadius: 4
            color: Qt.darker(noteColor, 1.1)
            border.width: 0
            
            Row {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4
                
                // Drag handle
                Rectangle {
                    width: 16
                    height: 16
                    radius: 8
                    color: Qt.lighter(noteColor, 1.5)
                    
                    Image {
                        anchors.centerIn: parent
                        width: 12
                        height: 12
                        source: Shell.iconUrl("grip-vertical", Qt.darker(noteColor, 2))
                    }
                }
                
                Item { width: 8; height: 1 }
                
                // Title
                Text {
                    text: "Note #" + (index + 1)
                    color: Qt.darker(noteColor, 2)
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                    font.bold: true
                    elide: Text.ElideRight
                }
                
                Item { width: 8; height: 1; Layout.fill: true }
                
                // Delete button
                MouseArea {
                    width: 20
                    height: 20
                    cursorShape: Qt.PointingHandCursor
                    onClicked: deleted()
                    
                    Image {
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        source: Shell.iconUrl("x", Qt.darker(noteColor, 2))
                    }
                    
                    ShellToolTip {
                        text: "Delete note"
                        target: parent
                    }
                }
            }
        }
        
        // Text area
        TextEdit {
            id: textEdit
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 8
            
            text: noteText
            color: textColor
            font.family: Theme.fontFamily
            font.pointSize: fontSize
            background: Rectangle {
                color: "transparent"
                border.width: 0
            }
            
            onTextChanged: note.textChanged(text)
        }
        
        // Resize handle
        Rectangle {
            id: resizeHandle
            width: 12
            height: 12
            radius: 6
            color: Qt.darker(noteColor, 1.3)
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeFDiagCursor
                drag.target: note
                drag.axis: Drag.XAndYAxis
                drag.minimumX: 100
                drag.minimumY: 80
                onMoved: {
                    var dx = drag.x - drag.startX
                    var dy = drag.y - drag.startY
                    if (dx !== 0 || dy !== 0) {
                        resized(dx, dy)
                    }
                }
            }
        }
        
        // Make note draggable
        MouseArea {
            id: dragArea
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: header.height
            cursorShape: Qt.OpenHandCursor
            drag.target: note
            drag.axis: Drag.XAndYAxis
            
            onPressed: cursorShape = Qt.ClosedHandCursor
            onReleased: cursorShape = Qt.OpenHandCursor
            onMoved: {
                if (drag.active) {
                    moved(drag.x - drag.startX, drag.y - drag.startY)
                }
            }
        }
        
        // Tooltip for note
        ShellToolTip {
            text: "Double-click to edit"
            target: note
            visible: !textEdit.activeFocus
        }
    }
}
