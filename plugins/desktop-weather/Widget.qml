import QtQuick

// Desktop Weather Widget
// Current conditions and a 5-day forecast from Open-Meteo (free, no account
// or key). Nothing is fetched until a city is set in the widget's own
// settings (the gear): weather.location, weather.units and
// weather.showForecast live in the shell settings.
Rectangle {
    id: widget

    readonly property var pluginSettings: (parent && parent.modelData && parent.modelData.settings)
                                          ? parent.modelData.settings : ({})
    // A city set in the manifest ("auto" there means "not set") still works.
    readonly property string location: {
        const chosen = String(Settings.values["weather.location"] || "").trim();
        if (chosen.length > 0)
            return chosen;
        const manifest = pluginSettings.location ? String(pluginSettings.location).trim() : "";
        return manifest === "auto" ? "" : manifest;
    }
    readonly property string units: Settings.values["weather.units"] === "imperial" ? "imperial" : "metric"
    readonly property bool showForecast: Settings.values["weather.showForecast"] !== false
    property int refreshInterval: pluginSettings.refreshInterval !== undefined
                                  ? Math.max(10, Number(pluginSettings.refreshInterval)) : 30

    // "setup" (no city yet), "loading", "ready", "notfound", "error".
    property string status: "setup"
    property string placeName: ""
    property var weatherData: ({})
    // The geocoded city, so a refresh only asks for the forecast.
    property var place: null

    function formatTemp(celsius) {
        const c = Number(celsius);
        if (celsius === undefined || celsius === null || isNaN(c))
            return "--";
        const val = units === "imperial" ? Math.round(c * 9 / 5 + 32) : Math.round(c);
        return String(val) + (units === "imperial" ? "°F" : "°C");
    }

    function formatWind(kmh) {
        const v = Number(kmh);
        if (isNaN(v))
            return "--";
        return units === "imperial" ? qsTr("%1 mph").arg(Math.round(v * 0.621))
                                    : qsTr("%1 km/h").arg(Math.round(v));
    }

    // WMO weather codes as Open-Meteo reports them.
    function describe(code) {
        if (code === 0)
            return { text: qsTr("Clear"), icon: "sun" };
        if (code === 1 || code === 2)
            return { text: qsTr("Partly cloudy"), icon: "cloud" };
        if (code === 3)
            return { text: qsTr("Overcast"), icon: "cloud" };
        if (code === 45 || code === 48)
            return { text: qsTr("Fog"), icon: "cloud" };
        if (code >= 51 && code <= 57)
            return { text: qsTr("Drizzle"), icon: "cloud-rain" };
        if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82))
            return { text: qsTr("Rain"), icon: "cloud-rain" };
        if ((code >= 71 && code <= 77) || code === 85 || code === 86)
            return { text: qsTr("Snow"), icon: "cloud" };
        if (code >= 95)
            return { text: qsTr("Thunderstorm"), icon: "cloud-rain" };
        return { text: "", icon: "cloud" };
    }

    function getJson(url, done) {
        const request = new XMLHttpRequest();
        request.onreadystatechange = function() {
            if (request.readyState !== XMLHttpRequest.DONE)
                return;
            let data = null;
            if (request.status === 200) {
                try {
                    data = JSON.parse(request.responseText);
                } catch (err) {
                    data = null;
                }
            }
            done(data);
        };
        request.open("GET", url);
        request.send();
    }

    function refresh() {
        if (location.length === 0) {
            status = "setup";
            place = null;
            return;
        }
        if (status !== "ready")
            status = "loading";
        if (place && place.query === location) {
            fetchForecast();
            return;
        }
        // The search only matches names in the language it is given, so a
        // city typed in another script ("Москва" on an English desktop) is
        // looked up in English and Russian as well.
        const own = Qt.locale().name.substring(0, 2);
        const languages = [];
        for (const language of [own, "en", "ru"]) {
            if (/^[a-z]{2}$/.test(language) && languages.indexOf(language) < 0)
                languages.push(language);
        }
        geocode(location, languages);
    }

    function geocode(asked, languages) {
        getJson("https://geocoding-api.open-meteo.com/v1/search?count=1&format=json&language="
                + languages[0] + "&name=" + encodeURIComponent(asked), function(data) {
            if (asked !== widget.location)
                return; // the city changed meanwhile
            const hit = data && data.results && data.results.length > 0 ? data.results[0] : null;
            if (!hit) {
                if (!data && languages.length <= 1)
                    widget.status = "error";
                else if (languages.length > 1)
                    widget.geocode(asked, languages.slice(1));
                else
                    widget.status = "notfound";
                return;
            }
            widget.place = { query: asked, latitude: hit.latitude, longitude: hit.longitude,
                             name: hit.name || asked };
            widget.fetchForecast();
        });
    }

    function fetchForecast() {
        const p = place;
        if (!p)
            return;
        getJson("https://api.open-meteo.com/v1/forecast?latitude=" + p.latitude + "&longitude=" + p.longitude
                + "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
                + "&daily=weather_code,temperature_2m_max,temperature_2m_min&forecast_days=5&timezone=auto",
                function(data) {
            if (p !== widget.place)
                return;
            if (!data || !data.current || !data.daily) {
                widget.status = "error";
                return;
            }
            const now = describe(data.current.weather_code);
            const forecast = [];
            const days = data.daily.time || [];
            for (let i = 0; i < days.length && i < 5; ++i) {
                // "2026-11-04" as a local date (Date() would read it as UTC).
                const parts = String(days[i]).split("-");
                const date = new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]));
                forecast.push({
                    day: Qt.locale().standaloneDayName(date.getDay(), Locale.ShortFormat),
                    icon: describe(data.daily.weather_code[i]).icon,
                    high: data.daily.temperature_2m_max[i],
                    low: data.daily.temperature_2m_min[i]
                });
            }
            widget.placeName = p.name;
            widget.weatherData = {
                temperature: data.current.temperature_2m,
                condition: now.text,
                conditionIcon: now.icon,
                high: forecast.length > 0 ? forecast[0].high : undefined,
                low: forecast.length > 0 ? forecast[0].low : undefined,
                humidity: data.current.relative_humidity_2m,
                windSpeed: data.current.wind_speed_10m,
                forecast: forecast
            };
            widget.status = "ready";
        });
    }

    // Later, not inside the binding that noticed the change.
    onLocationChanged: Qt.callLater(refresh)
    Component.onCompleted: Qt.callLater(refresh)

    width: 248
    height: status === "ready" && showForecast ? 216 : 132
    radius: Theme.radius + 4
    color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, Theme.opacity * 0.95)
    border.width: 1
    border.color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)

    Timer {
        interval: widget.refreshInterval * 60 * 1000
        running: widget.location.length > 0
        repeat: true
        onTriggered: widget.refresh()
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
                text: widget.status === "ready" ? widget.placeName
                      : widget.location.length > 0 ? widget.location : qsTr("Weather")
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
                    onClicked: {
                        settingsPopup.visible = !settingsPopup.visible;
                        if (settingsPopup.visible) {
                            cityInput.text = widget.location;
                            cityInput.forceActiveFocus();
                        }
                    }
                }
            }
        }

        // No weather to show: what to do about it.
        Text {
            objectName: "weatherMessage"
            visible: widget.status !== "ready"
            width: parent.width
            wrapMode: Text.Wrap
            text: widget.status === "setup" ? qsTr("Set your city with the gear button to see the weather.")
                  : widget.status === "loading" ? qsTr("Loading the weather...")
                  : widget.status === "notfound" ? qsTr("No city called %1 was found.").arg(widget.location)
                  : qsTr("The weather service could not be reached. Trying again later.")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 1
        }

        Row {
            visible: widget.status === "ready"
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
                    text: qsTr("H: %1").arg(widget.formatTemp(widget.weatherData.high))
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                }

                Text {
                    text: qsTr("L: %1").arg(widget.formatTemp(widget.weatherData.low))
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                }
            }
        }

        Row {
            width: parent.width
            spacing: 16
            visible: widget.status === "ready" && !widget.showForecast

            Text {
                text: qsTr("Humidity %1%").arg(widget.weatherData.humidity)
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }

            Text {
                text: qsTr("Wind %1").arg(widget.formatWind(widget.weatherData.windSpeed))
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 1
            }
        }

        Row {
            visible: widget.status === "ready" && widget.showForecast
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
        width: 220
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
                text: qsTr("Weather settings")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize
                font.bold: true
            }

            // The city; Enter saves it. Empty turns the weather off.
            Rectangle {
                width: parent.width
                height: 28
                radius: Theme.radius
                color: Theme.surface
                border.width: 1
                border.color: cityInput.activeFocus ? Theme.accent : Theme.border

                TextInput {
                    id: cityInput
                    objectName: "weatherCity"
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                    clip: true
                    onAccepted: {
                        Settings.setValue("weather.location", text.trim());
                        settingsPopup.visible = false;
                    }

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: cityInput.text.length === 0
                        text: qsTr("City, then Enter")
                        color: Theme.muted
                        font: cityInput.font
                    }
                }
            }

            Row {
                spacing: 6
                Text {
                    text: qsTr("Units")
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
                        onClicked: Settings.setValue("weather.units", "metric")
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
                        onClicked: Settings.setValue("weather.units", "imperial")
                    }
                }
            }

            Row {
                spacing: 6
                Text {
                    text: qsTr("Forecast")
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
                        text: widget.showForecast ? qsTr("Shown") : qsTr("Hidden")
                        color: widget.showForecast ? "#ffffff" : Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 1
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Settings.setValue("weather.showForecast", !widget.showForecast)
                    }
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Data: Open-Meteo.com")
                color: Theme.muted
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize - 2
            }
        }
    }
}
