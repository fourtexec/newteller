import QtQuick
import QtQuick.Controls
import NewTeller 1.0

// Desktop Weather Widget
// Shows current weather conditions, temperature, and optional forecast
Rectangle {
    id: widget
    
    property string location: manifest.settings ? manifest.settings.location : "auto"
    property string units: manifest.settings ? manifest.settings.units : "metric"
    property bool showForecast: manifest.settings ? manifest.settings.showForecast : true
    property int refreshInterval: manifest.settings ? manifest.settings.refreshInterval : 30
    
    // Weather data (would be populated from a weather API in a real implementation)
    property var weatherData: ({
        temperature: "--",
        condition: "Loading...",
        conditionIcon: "sun",
        high: "--",
        low: "--",
        humidity: 0,
        windSpeed: 0,
        forecast: []
    })
    
    // For demo purposes, we'll use mock data
    property var mockForecast: [
        { day: "Mon", icon: "sun", high: "22", low: "15" },
        { day: "Tue", icon: "cloud-rain", high: "18", low: "12" },
        { day: "Wed", icon: "cloud", high: "19", low: "14" },
        { day: "Thu", icon: "sun", high: "21", low: "16" },
        { day: "Fri", icon: "cloud-rain", high: "17", low: "11" }
    ]
    
    width: 240
    height: showForecast ? 220 : 120
    radius: Theme.radius
    color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, Theme.opacity * 0.95)
    border.width: 1
    border.color: Theme.border
    
    // Simulate weather data updates
    Timer {
        id: refreshTimer
        interval: refreshInterval * 60 * 1000 // minutes to ms
        running: true
        repeat: true
        onTriggered: updateWeather()
    }
    
    function updateWeather() {
        // In a real implementation, this would fetch from a weather API
        // For now, we'll cycle through mock data
        var temps = ["22", "18", "19", "21", "17", "20", "23"]
        var conditions = ["Sunny", "Partly Cloudy", "Cloudy", "Rain", "Clear", "Foggy"]
        var icons = ["sun", "cloud-sun", "cloud", "cloud-rain", "sun", "cloud-fog"]
        
        var idx = Math.floor(Math.random() * temps.length)
        weatherData.temperature = temps[idx]
        weatherData.condition = conditions[idx]
        weatherData.conditionIcon = icons[idx]
        weatherData.high = String(Math.floor(Math.random() * 10) + 15)
        weatherData.low = String(Math.floor(Math.random() * 10) + 5)
        weatherData.humidity = Math.floor(Math.random() * 60) + 20
        weatherData.windSpeed = Math.floor(Math.random() * 20) + 5
        weatherData.forecast = mockForecast
    }
    
    Component.onCompleted: {
        // Initialize with mock data
        updateWeather()
    }
    
    Column {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        
        // Header with location and settings button
        Row {
            width: parent.width
            spacing: 8
            
            Text {
                text: location === "auto" ? "Current Location" : location
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize + 2
                font.bold: true
                elide: Text.ElideRight
            }
            
            Item { width: 8; height: 1 }
            
            // Settings button
            MouseArea {
                width: 24
                height: 24
                cursorShape: Qt.PointingHandCursor
                onClicked: settingsPopup.visible = !settingsPopup.visible
                
                Image {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    source: Shell.iconUrl("settings", Theme.foreground)
                }
            }
        }
        
        // Current weather
        Row {
            width: parent.width
            spacing: 12
            
            // Weather icon
            Image {
                width: 48
                height: 48
                source: Shell.iconUrl(weatherData.conditionIcon, Theme.accent)
            }
            
            // Temperature and condition
            Column {
                spacing: 2
                
                Text {
                    text: weatherData.temperature + (units === "metric" ? "°C" : "°F")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize + 10
                    font.bold: true
                }
                
                Text {
                    text: weatherData.condition
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                }
            }
            
            // High/Low
            Column {
                spacing: 2
                
                Text {
                    text: "H: " + weatherData.high + (units === "metric" ? "°C" : "°F")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                
                Text {
                    text: "L: " + weatherData.low + (units === "metric" ? "°C" : "°F")
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
        
        // Additional weather details
        Row {
            width: parent.width
            spacing: 16
            visible: !showForecast
            
            Column {
                spacing: 2
                Text {
                    text: "Humidity"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 2
                }
                Text {
                    text: weatherData.humidity + "%"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                }
            }
            
            Column {
                spacing: 2
                Text {
                    text: "Wind"
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 2
                }
                Text {
                    text: weatherData.windSpeed + (units === "metric" ? " km/h" : " mph")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                }
            }
        }
        
        // Forecast
        Row {
            visible: showForecast
            width: parent.width
            spacing: 8
            
            Repeater {
                model: weatherData.forecast
                delegate: Column {
                    spacing: 4
                    width: 40
                    
                    Text {
                        text: modelData.day
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 1
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    
                    Image {
                        width: 24
                        height: 24
                        source: Shell.iconUrl(modelData.icon, Theme.foreground)
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    
                    Text {
                        text: modelData.high + (units === "metric" ? "°C" : "°F")
                        color: Theme.accent
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 1
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    
                    Text {
                        text: modelData.low + (units === "metric" ? "°C" : "°F")
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 2
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
            }
        }
    }
    
    // Settings popup
    Rectangle {
        id: settingsPopup
        visible: false
        width: 200
        height: 180
        radius: Theme.radius
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
        z: 100
        x: widget.width - width - 8
        y: 8
        
        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            
            Text {
                text: "Weather Settings"
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize + 2
                font.bold: true
            }
            
            SettingsRow {
                width: parent.width
                title: "Temperature units"
                SettingsChoice {
                    model: [
                        { value: "metric", label: "Celsius (°C)" },
                        { value: "imperial", label: "Fahrenheit (°F)" }
                    ]
                    currentValue: units
                    onChanged: (v) => { units = v; settingsPopup.visible = false }
                }
            }
            
            SettingsRow {
                width: parent.width
                title: "Show forecast"
                SettingsSwitch {
                    checked: showForecast
                    onChanged: (v) => { showForecast = v; widget.height = showForecast ? 220 : 120 }
                }
            }
            
            SettingsRow {
                width: parent.width
                title: "Refresh interval (min)"
                SettingsStepper {
                    from: 10
                    to: 120
                    step: 10
                    value: refreshInterval
                    onChanged: (v) => { refreshInterval = v; refreshTimer.interval = v * 60 * 1000 }
                }
            }
        }
    }
    
    // Close settings when clicking outside
    MouseArea {
        anchors.fill: parent
        z: 99
        enabled: settingsPopup.visible
        onPressed: (mouse) => {
            if (!settingsPopup.contains(mapToItem(settingsPopup, mouse.x, mouse.y)))
                settingsPopup.visible = false
            mouse.accepted = false
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
}
