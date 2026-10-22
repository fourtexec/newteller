import QtQuick
import NewTeller 1.0

// Panel CPU Monitor Widget
// Shows CPU usage as a bar graph
Item {
    id: widget
    
    property bool showPercentage: manifest.settings ? manifest.settings.showPercentage : true
    property string barColor: manifest.settings ? manifest.settings.barColor : "accent"
    property int widgetWidth: manifest.settings ? manifest.settings.width : 80
    property bool showPerCore: manifest.settings ? manifest.settings.showPerCore : false
    
    readonly property int panelHeight: panel ? panel.barThickness : 48
    
    width: widgetWidth
    height: panelHeight
    
    // Get the panel reference from the parent
    property var panel: parent.parent ? parent.parent : null
    
    // CPU usage from System object
    readonly property real cpuPercent: System ? System.cpuPercent : 0
    readonly property int cpuCount: System ? System.cpuCount : 1
    readonly property var perCoreUsage: System && System.perCoreCpu ? System.perCoreCpu : []
    
    Row {
        anchors.fill: parent
        spacing: 4
        
        // CPU bar
        Rectangle {
            width: showPercentage ? parent.width - 40 : parent.width
            height: parent.height - 4
            radius: 2
            color: Theme.border
            
            Rectangle {
                width: parent.width * (cpuPercent / 100)
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
            text: Math.round(cpuPercent) + "%"
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
        
        Row {
            spacing: 8
            
            Column {
                spacing: 2
                Text {
                    text: "CPU Usage"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                    font.bold: true
                }
                Text {
                    text: "Total: " + Math.round(cpuPercent) + "%"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                }
                
                // Per-core usage
                Repeater {
                    visible: showPerCore && perCoreUsage.length > 0
                    model: perCoreUsage
                    delegate: Text {
                        text: "Core " + (index + 1) + ": " + Math.round(modelData) + "%"
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 2
                    }
                }
            }
        }
    }
    
    // Show tooltip on hover
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onEntered: tooltip.showAt("CPU: " + Math.round(cpuPercent) + "%", widget.width / 2, widget.height)
        onExited: tooltip.hide()
    }
}
