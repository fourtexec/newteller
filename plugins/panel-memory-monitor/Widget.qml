import QtQuick
import NewTeller 1.0

// Panel Memory Monitor Widget
// Shows memory usage as a bar graph
Item {
    id: widget
    
    property bool showPercentage: manifest.settings ? manifest.settings.showPercentage : true
    property string barColor: manifest.settings ? manifest.settings.barColor : "accent"
    property int widgetWidth: manifest.settings ? manifest.settings.width : 80
    
    readonly property int panelHeight: panel ? panel.barThickness : 48
    
    width: widgetWidth
    height: panelHeight
    
    // Get the panel reference from the parent
    property var panel: parent.parent ? parent.parent : null
    
    // Memory usage from System object
    readonly property real memoryPercent: System ? System.memoryPercent : 0
    readonly property string memoryText: System ? System.memoryText : "0/0"
    
    Row {
        anchors.fill: parent
        spacing: 4
        
        // Memory bar
        Rectangle {
            width: showPercentage ? parent.width - 40 : parent.width
            height: parent.height - 4
            radius: 2
            color: Theme.border
            
            Rectangle {
                width: parent.width * (memoryPercent / 100)
                height: parent.height
                radius: 2
                color: barColor === "accent" ? Theme.accent : 
                       barColor === "foreground" ? Theme.foreground :
                       barColor === "surface" ? Theme.surface :
                       Qt.color(barColor)
                
                // Gradient for better visibility
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.lighter(parent.color, 1.2) }
                    GradientStop { position: 1.0; color: parent.color }
                }
            }
        }
        
        // Percentage text
        Text {
            visible: showPercentage
            width: 36
            height: parent.height
            text: Math.round(memoryPercent) + "%"
            color: Theme.foreground
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 1
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
        }
    }
    
    // Tooltip
    PanelToolTip {
        id: tooltip
        position: panel ? panel.panelPosition : "bottom"
        panelThickness: panel ? panel.barThickness : 48
        
        Column {
            spacing: 2
            
            Text {
                text: "Memory Usage"
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize
                font.bold: true
            }
            Text {
                text: "Usage: " + Math.round(memoryPercent) + "%"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }
            Text {
                text: memoryText
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 2
            }
        }
    }
    
    // Show tooltip on hover
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onEntered: tooltip.showAt("RAM: " + Math.round(memoryPercent) + "%", widget.width / 2, widget.height)
        onExited: tooltip.hide()
    }
}
