import QtQuick

// Desktop Weather Widget
// Shows current weather conditions, temperature, and optional 5-day forecast.
Rectangle {
    id: widget

    readonly property var pluginSettings: (parent && parent.modelData && parent.modelData.settings)
                                          ? parent.modelData.settings : ({})
    property string location: pluginSettings.location ? String(pluginSettings.location) : "auto"
    property string units: pluginSettings.units ? String(pluginSettings.units) : "metric"
    property bool showForecast: pluginSettings.showForecast !== false
    property int refreshInterval: pluginSettings.refreshInterval !== undefined
                                  ? Math.max(1, Number(pluginSettings.refreshInterval)) : 30

    property var weatherData: ({
        temperature: "21",
        condition: "Sunny",
        conditionIcon: "sun",
        high: "24",
        low: "14",
        humidity: 48,
        windSpeed: 12,
        forecast: mockForecast
    })

    readonly property var mockForecast: [
        { day: "Mon", icon: "sun", high: 22, low: 15 },
        { day: "Tue", icon: "cloud-rain", high: 18, low: 12 },
        { day: "Wed", icon: "cloud", high: 19, low: 14 },
        { day: "Thu", icon: "sun", high: 21, low: 16 },
        { day: "Fri", icon: "cloud-rain", high: 17, low: 11 }
    ]

    function formatTemp(celsius) {
        const c = Number(celsius)
        if (isNaN(c))
            return "--"
        const val = units === "imperial" ? Math.round(c * 9 / 5 + 32) : Math.round(c)
        return String(val) + (units === "imperial" ? "°F" : "°C")
    }

    function updateWeather() {
        const samples = [
            { temp: 22, cond: "Sunny", icon: "sun", high: 25, low: 15, hum: 42, wind: 11 },
            { temp: 19, cond: "Partly Cloudy", icon: "cloud", high: 21, low: 13, hum: 55, wind: 14 },
            { temp: 17, cond: "Rain", icon: "cloud-rain", high: 18, low: 12, hum: 78, wind: 19 },
            { temp: 21, cond: "Clear", icon: "sun", high: 24, low: 14, hum: 46, wind: 9 }
        ]
        const pick = samples[Math.floor(Math.random() * samples.length)]
        weatherData = {
            temperature: pick.temp,
            condition: pick.cond,
            conditionIcon: pick.icon,
            high: pick.high,
            low: pick.low,
            humidity: pick.hum,
            windSpeed: pick.wind,
            forecast: mockForecast
        }
    }

    Component.onCompleted: updateWeather()

    width: 248
    height: showForecast ? 216 : 132
    radius: Theme.radius + 4
    color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, Theme.opacity * 0.95)
    border.width: 1
    border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)

    Timer {
        id: refreshTimer
        interval: widget.refreshInterval * 60 * 1000
        running: true
        repeat: true
        onTriggered: widget.updateWeather()
    }

    // Background drag handle beneath interactive controls
    MouseArea {
        anchors.fill: parent
        drag.target: widget
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
    }

    Column {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Item {
            width: parent.width
            height: 24

            Text {
                anchors.left: parent.left
                anchors.right: settingsBtn.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: widget.location === "auto" ? "Current Location" : widget.location
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize + 1
                font.bold: true
                elide: Text.ElideRight
            }

            Rectangle {
                id: settingsBtn
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: 24
                radius: Theme.radius
                color: settingsMouse.containsMouse
                       ? Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.08)
                       : "transparent"

                Image {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    sourceSize.width: 32
                    sourceSize.height: 32
                    source: Shell.tintedIcon("settings", Theme.foreground)
                }

                MouseArea {
                    id: settingsMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: settingsPopup.visible = !settingsPopup.visible
                }
            }
        }

        Row {
            width: parent.width
            spacing: 12

            Image {
                width: 44
                height: 44
                sourceSize.width: 88
                sourceSize.height: 88
                anchors.verticalCenter: parent.verticalCenter
                source: Shell.tintedIcon(widget.weatherData.conditionIcon || "sun", Theme.accent)
            }

            Column {
                spacing: 2
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    text: widget.formatTemp(widget.weatherData.temperature)
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize + 9
                    font.bold: true
                }

                Text {
                    text: widget.weatherData.condition || ""
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                }
            }

            Column {
                spacing: 2
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    text: "H: " + widget.formatTemp(widget.weatherData.high)
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                }

                Text {
                    text: "L: " + widget.formatTemp(widget.weatherData.low)
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                }
            }
        }

        Row {
            width: parent.width
            spacing: 16
            visible: !widget.showForecast

            Text {
                text: "Humidity " + widget.weatherData.humidity + "%"
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }

            Text {
                text: "Wind " + widget.weatherData.windSpeed
                      + (widget.units === "imperial" ? " mph" : " km/h")
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }
        }

        Row {
            visible: widget.showForecast
            width: parent.width
            spacing: 6

            Repeater {
                model: widget.weatherData.forecast || []
                delegate: Column {
                    required property var modelData
                    spacing: 3
                    width: (widget.width - 24 - 24) / 5

                    Text {
                        text: modelData.day
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 2
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    Image {
                        width: 20
                        height: 20
                        sourceSize.width: 40
                        sourceSize.height: 40
                        source: Shell.tintedIcon(modelData.icon, Theme.foreground)
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    Text {
                        text: widget.formatTemp(modelData.high)
                        color: Theme.accent
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 2
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    Text {
                        text: widget.formatTemp(modelData.low)
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 2
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        z: 99
        enabled: settingsPopup.visible
        onPressed: (mouse) => {
            const mapped = mapToItem(settingsPopup, mouse.x, mouse.y)
            if (!settingsPopup.contains(Qt.point(mapped.x, mapped.y)))
                settingsPopup.visible = false
        }
    }

    Rectangle {
        id: settingsPopup
        visible: false
        width: 204
        height: popupColumn.implicitHeight + 20
        radius: Theme.radius
        color: Theme.background
        border.width: 1
        border.color: Theme.border
        z: 100
        x: widget.width - width - 8
        y: 36

        Column {
            id: popupColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 10
            spacing: 8

            Text {
                text: "Weather Settings"
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize
                font.bold: true
            }

            Row {
                spacing: 6
                Text {
                    text: "Units"
                    width: 84
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                }
                Rectangle {
                    width: 44
                    height: 24
                    radius: Theme.radius
                    color: widget.units === "metric" ? Theme.accent : Theme.surface
                    Text {
                        anchors.centerIn: parent
                        text: "°C"
                        color: widget.units === "metric" ? "#ffffff" : Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 1
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: widget.units = "metric"
                    }
                }
                Rectangle {
                    width: 44
                    height: 24
                    radius: Theme.radius
                    color: widget.units === "imperial" ? Theme.accent : Theme.surface
                    Text {
                        anchors.centerIn: parent
                        text: "°F"
                        color: widget.units === "imperial" ? "#ffffff" : Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 1
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: widget.units = "imperial"
                    }
                }
            }

            Row {
                spacing: 6
                Text {
                    text: "Forecast"
                    width: 84
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                }
                Rectangle {
                    width: 94
                    height: 24
                    radius: Theme.radius
                    color: widget.showForecast ? Theme.accent : Theme.surface
                    Text {
                        anchors.centerIn: parent
                        text: widget.showForecast ? "Shown" : "Hidden"
                        color: widget.showForecast ? "#ffffff" : Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 1
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: widget.showForecast = !widget.showForecast
                    }
                }
            }
        }
    }
}
