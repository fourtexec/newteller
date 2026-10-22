import QtQuick
import QtQuick.Controls
import NewTeller 1.0

// Desktop Calendar Widget
// Shows a monthly calendar with the current date highlighted
Rectangle {
    id: widget
    
    property bool showWeekNumbers: manifest.settings ? manifest.settings.showWeekNumbers : false
    property int firstDayOfWeek: manifest.settings ? manifest.settings.firstDayOfWeek : 0 // 0=Sunday, 1=Monday
    property bool showHeader: manifest.settings ? manifest.settings.showHeader : true
    property bool showTodayMarker: manifest.settings ? manifest.settings.showTodayMarker : true
    
    width: 280
    height: showHeader ? 320 : 280
    radius: Theme.radius
    color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, Theme.opacity * 0.95)
    border.width: 1
    border.color: Theme.border
    
    // Get current date
    property int currentYear: new Date().getFullYear()
    property int currentMonth: new Date().getMonth()
    property int today: new Date().getDate()
    
    // Navigation
    property int displayYear: currentYear
    property int displayMonth: currentMonth
    
    // Week day names
    readonly property var weekDays: ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]
    
    // Month names
    readonly property var monthNames: ["January", "February", "March", "April", "May", "June", 
                                         "July", "August", "September", "October", "November", "December"]
    
    Column {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        
        // Header with month/year and navigation
        Row {
            visible: showHeader
            width: parent.width
            spacing: 8
            
            // Previous month button
            MouseArea {
                width: 32
                height: 32
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    displayMonth--
                    if (displayMonth < 0) {
                        displayMonth = 11
                        displayYear--
                    }
                }
                
                Image {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    source: Shell.iconUrl("chevron-left", Theme.foreground)
                }
            }
            
            // Month and year
            Column {
                spacing: 2
                Text {
                    text: monthNames[displayMonth]
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize + 4
                    font.bold: true
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    text: displayYear
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
            
            // Next month button
            MouseArea {
                width: 32
                height: 32
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    displayMonth++
                    if (displayMonth > 11) {
                        displayMonth = 0
                        displayYear++
                    }
                }
                
                Image {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    source: Shell.iconUrl("chevron-right", Theme.foreground)
                }
            }
        }
        
        // Week day headers
        Row {
            width: parent.width
            spacing: 4
            
            // Week numbers column (optional)
            Item {
                width: showWeekNumbers ? 24 : 0
                height: 24
            }
            
            Repeater {
                model: weekDays.length
                delegate: Text {
                    width: (parent.width - (showWeekNumbers ? 24 : 0)) / 7 - 4
                    height: 24
                    text: weekDays[(index + firstDayOfWeek) % 7]
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
        
        // Calendar grid
        Grid {
            width: parent.width
            columns: 7
            spacing: 4
            
            // Week numbers column (optional)
            Item {
                width: showWeekNumbers ? 24 : 0
                height: 1
            }
            
            // Get days in month
            property int daysInMonth: new Date(displayYear, displayMonth + 1, 0).getDate()
            property int firstDay: new Date(displayYear, displayMonth, 1).getDay()
            
            // Get days from previous month to show
            property int offset: (firstDay - firstDayOfWeek + 7) % 7
            property int prevMonthDays: new Date(displayYear, displayMonth, 0).getDate()
            
            // Days from previous month
            Repeater {
                model: offset
                delegate: Text {
                    width: (parent.width - (showWeekNumbers ? 24 : 0)) / 7 - 4
                    height: 32
                    text: prevMonthDays - offset + modelData + 1
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    opacity: 0.5
                }
            }
            
            // Days in current month
            Repeater {
                id: daysRepeater
                model: daysInMonth
                delegate: Rectangle {
                    width: (parent.width - (showWeekNumbers ? 24 : 0)) / 7 - 4
                    height: 32
                    radius: showTodayMarker && (displayYear === currentYear && displayMonth === currentMonth && modelData === today) ? 16 : 4
                    color: "transparent"
                    border.width: 0
                    
                    // Today marker
                    Rectangle {
                        visible: showTodayMarker && (displayYear === currentYear && displayMonth === currentMonth && modelData === today)
                        anchors.fill: parent
                        radius: 16
                        color: Theme.accent
                        opacity: 0.2
                        border.width: 1
                        border.color: Theme.accent
                    }
                    
                    Text {
                        anchors.fill: parent
                        text: modelData + 1
                        color: (displayYear === currentYear && displayMonth === currentMonth && modelData + 1 === today) ? Theme.accent : Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize
                        font.bold: (displayYear === currentYear && displayMonth === currentMonth && modelData + 1 === today)
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        
                        // Highlight on hover
                        Rectangle {
                            anchors.fill: parent
                            radius: 16
                            color: Theme.accent
                            opacity: parent.hovered ? 0.1 : 0
                        }
                    }
                }
            }
            
            // Days from next month
            property int totalCells: offset + daysInMonth
            property int remainingCells: Math.ceil(totalCells / 7) * 7 - totalCells
            
            Repeater {
                model: remainingCells > 0 ? remainingCells : 0
                delegate: Text {
                    width: (parent.width - (showWeekNumbers ? 24 : 0)) / 7 - 4
                    height: 32
                    text: modelData + 1
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    opacity: 0.5
                }
            }
        }
        
        // Week numbers (optional)
        Column {
            visible: showWeekNumbers
            width: 24
            height: daysRepeater.height
            spacing: 4
            
            // Calculate week numbers
            property int firstWeek: Math.ceil((offset + 1) / 7)
            
            Repeater {
                model: Math.ceil((offset + daysInMonth) / 7) + 1
                delegate: Text {
                    width: 24
                    height: 32
                    text: firstWeek + modelData - 1
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 2
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    opacity: 0.7
                }
            }
        }
    }
    
    // Reset to current month
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.MiddleButton
        onClicked: (mouse) => {
            if (mouse.button === Qt.MiddleButton) {
                displayYear = currentYear
                displayMonth = currentMonth
            }
        }
    }
    
    // Make widget draggable
    MouseArea {
        anchors.fill: parent
        drag.target: widget
        cursorShape: Qt.OpenHandCursor
        onPressed: cursorShape = Qt.ClosedHandCursor
        onReleased: cursorShape = Qt.OpenHandCursor
    }
    
    // Tooltip
    ShellToolTip {
        text: "Middle-click to reset to current month"
        target: widget
    }
}
