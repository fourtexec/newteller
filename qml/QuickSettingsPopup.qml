import QtQuick
import QtQuick.Window
import QtQuick.Layouts

// Quick settings: the card behind the indicators button at the end of the
// panel. Media controls, volume, brightness, network and do-not-disturb in
// one place. Every row hides itself when its backend is missing, so the
// popup always fits what the machine actually offers.
//
// The card is anchored to the button that toggled it (the panel hands over the
// button's rect), so it opens next to that button on every panel edge and
// follows it when another panel's indicators button is clicked.
DropdownWindow {
    id: popup

    // The notification centre on the primary screen; set by Main.qml so the
    // History button can open it.
    property var notificationCenter: null
    // The Wi-Fi network list, opened from the network row.
    property var wifiPopup: null
    // The Bluetooth device list.
    property var bluetoothPopup: null
    // Outputs and per-application volume.
    property var soundPopup: null
    // Rows that are on right now, for the height computation.
    readonly property int visibleRows: {
        var rows = 0;
        if (Quick.mediaAvailable)
            rows++;
        if (Quick.volumeAvailable)
            rows++;
        if (Quick.brightnessAvailable)
            rows++;
        if (Quick.networkAvailable)
            rows++;
        if (System.batteryPresent)
            rows++;
        if (Session.nightLightAvailable)
            rows++;
        if (Bluetooth.available)
            rows++;
        if (Session.powerProfiles.length > 0)
            rows++;
        if (Recorder.available)
            rows++;
        return rows + 1 /* dnd */ + 1 /* footer separator */;
    }

    cardWidth: 360
    cardHeight: Math.max(240, Math.min(560, 24 + 46 * visibleRows + (Quick.mediaAvailable ? (Quick.mediaArtUrl.length > 0 ? 84 : 74) : 0)))
    padding: 12
    scopeName: "newteller-quick-settings"

    // 75 -> "1:15".
    function formatDuration(seconds) {
        const s = Math.max(0, Math.floor(seconds));
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
    }

    // Reading the mixer on the way in, so the sliders show the live values.
    function opened() {
        Quick.refresh();
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        ShellTitleBar {
            Layout.fillWidth: true
            title: qsTr("Quick settings")
            iconName: "sliders-horizontal"
            onCloseClicked: popup.hide()
        }

        // --- media ------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            visible: Quick.mediaAvailable
            spacing: 2

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                // The cover the player announces (mpris:artUrl).
                Image {
                    id: mediaArt
                    objectName: "mediaArt"
                    visible: Quick.mediaArtUrl.length > 0 && status === Image.Ready
                    Layout.preferredWidth: 44
                    Layout.preferredHeight: 44
                    sourceSize.width: 88
                    sourceSize.height: 88
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    smooth: true
                    source: Quick.mediaArtUrl
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        Layout.fillWidth: true
                        text: Quick.mediaTitle.length > 0 ? Quick.mediaTitle : qsTr("Not playing")
                        color: Theme.foreground
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize + 1
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: Quick.mediaArtist.length > 0
                        text: Quick.mediaArtist
                        color: Theme.muted
                        font.family: Theme.fontFamily
                        font.pointSize: Theme.fontSize - 1
                        elide: Text.ElideRight
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                Item { Layout.fillWidth: true }
                TaskButton {
                    width: 34
                    height: 34
                    iconName: "skip-back"
                    onClicked: Quick.mediaPrevious()
                }
                TaskButton {
                    width: 38
                    height: 38
                    iconName: Quick.mediaPlaying ? "pause" : "play"
                    active: Quick.mediaPlaying
                    onClicked: Quick.mediaPlayPause()
                }
                TaskButton {
                    width: 34
                    height: 34
                    iconName: "skip-forward"
                    onClicked: Quick.mediaNext()
                }
                Item { Layout.fillWidth: true }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: Quick.mediaAvailable
            color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)
        }

        // --- volume -----------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            visible: Quick.volumeAvailable
            spacing: 8

            TaskButton {
                width: 34
                height: 34
                iconName: Quick.volumeMuted ? "volume-x" : "volume-2"
                tooltipText: Quick.volumeMuted ? qsTr("Unmute") : qsTr("Mute")
                onClicked: Quick.toggleMuted()
            }
            SettingsSlider {
                id: volumeSlider
                Layout.fillWidth: true
                from: 0
                to: 100
                value: Quick.volume
                suffix: "%"
                onChanged: (v) => Quick.setVolume(Math.round(v))
                Connections {
                    target: Quick
                    function onVolumeChanged() { volumeSlider.value = Quick.volume }
                }
            }
            TaskButton {
                objectName: "soundDevicesButton"
                visible: (popup.soundPopup !== null && Audio.available)
                         || Session.soundSettingsTool.length > 0
                width: 30
                iconName: "chevron-right"
                tooltipText: qsTr("Sound devices")
                onClicked: {
                    popup.hide();
                    if (popup.soundPopup !== null && Audio.available)
                        popup.soundPopup.show();
                    else
                        Session.openSoundSettings();
                }
            }
        }

        // --- brightness -------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            visible: Quick.brightnessAvailable
            spacing: 8

            Item {
                width: 34
                height: 34
                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    sourceSize.width: 36
                    sourceSize.height: 36
                    fillMode: Image.PreserveAspectFit
                    source: Shell.tintedIcon("sun", Theme.foreground)
                }
            }
            SettingsSlider {
                id: brightnessSlider
                Layout.fillWidth: true
                from: 0
                to: 100
                value: Quick.brightness
                suffix: "%"
                onChanged: (v) => Quick.setBrightness(Math.round(v))
                Connections {
                    target: Quick
                    function onBrightnessChanged() { brightnessSlider.value = Quick.brightness }
                }
            }
        }

        // --- network ----------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            visible: Quick.networkAvailable
            spacing: 8

            Item {
                width: 34
                height: 34
                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    sourceSize.width: 36
                    sourceSize.height: 36
                    fillMode: Image.PreserveAspectFit
                    source: Shell.tintedIcon(Quick.wifiEnabled ? "wifi" : "wifi-off",
                                             Theme.foreground)
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Text {
                    text: qsTr("Wi-Fi")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                }
                Text {
                    text: Quick.networkStatus
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                }
            }
            TaskButton {
                objectName: "wifiNetworksButton"
                visible: Session.wifiAvailable && Quick.wifiEnabled && popup.wifiPopup !== null
                width: 30
                iconName: "chevron-right"
                tooltipText: qsTr("Choose a network")
                onClicked: {
                    popup.hide();
                    popup.wifiPopup.show();
                }
            }
            SettingsSwitch {
                id: wifiSwitch
                checked: Quick.wifiEnabled
                onChanged: (v) => {
                    if (v !== Quick.wifiEnabled)
                        Quick.toggleWifi();
                }
                Connections {
                    target: Quick
                    function onNetworkChanged() { wifiSwitch.checked = Quick.wifiEnabled }
                }
            }
        }

        // --- bluetooth ---------------------------------------------------
        RowLayout {
            objectName: "bluetoothRow"
            Layout.fillWidth: true
            visible: Bluetooth.available
            spacing: 8

            Item {
                width: 34
                height: 34
                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    sourceSize.width: 36
                    sourceSize.height: 36
                    fillMode: Image.PreserveAspectFit
                    opacity: Bluetooth.powered ? 1.0 : 0.5
                    source: Shell.tintedIcon("bluetooth", Theme.foreground)
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    text: qsTr("Bluetooth")
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                }
                Text {
                    text: {
                        if (!Bluetooth.powered)
                            return qsTr("Off");
                        var names = [];
                        for (var i = 0; i < Bluetooth.devices.length; ++i) {
                            if (Bluetooth.devices[i].connected)
                                names.push(Bluetooth.devices[i].name);
                        }
                        return names.length > 0 ? names.join(", ") : qsTr("On");
                    }
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            TaskButton {
                visible: popup.bluetoothPopup !== null && Bluetooth.powered
                width: 30
                iconName: "chevron-right"
                tooltipText: qsTr("Devices")
                onClicked: {
                    popup.hide();
                    popup.bluetoothPopup.show();
                }
            }
            SettingsSwitch {
                checked: Bluetooth.powered
                onChanged: (v) => {
                    if (v !== Bluetooth.powered)
                        Bluetooth.setPowered(v);
                }
            }
        }

        // --- do not disturb ---------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item {
                width: 34
                height: 34
                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    sourceSize.width: 36
                    sourceSize.height: 36
                    fillMode: Image.PreserveAspectFit
                    source: Shell.tintedIcon(
                                Settings.values["notifications.doNotDisturb"] ? "bell-off"
                                                                              : "bell",
                                Theme.foreground)
                }
            }
            Text {
                Layout.fillWidth: true
                readonly property real until: Number(Settings.values["notifications.dndUntil"]) || 0
                text: Settings.values["notifications.doNotDisturb"] === true && until > 0
                      ? qsTr("Do not disturb until %1")
                            .arg(Qt.formatTime(new Date(until), Qt.locale().timeFormat(Locale.ShortFormat)))
                      : qsTr("Do not disturb")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize
                elide: Text.ElideRight
            }
            // Quiet for an hour, then notifications come back by themselves.
            TaskButton {
                objectName: "dndHourButton"
                visible: Settings.values["notifications.doNotDisturb"] !== true
                label: qsTr("1 h")
                tooltipText: qsTr("Do not disturb for an hour")
                onClicked: {
                    Settings.setValue("notifications.dndUntil", Date.now() + 3600 * 1000);
                    Settings.setValue("notifications.doNotDisturb", true);
                }
            }
            SettingsSwitch {
                settingKey: "notifications.doNotDisturb"
            }
        }

        // --- night light (wlsunset) ----------------------------------------
        RowLayout {
            objectName: "nightLightRow"
            Layout.fillWidth: true
            visible: Session.nightLightAvailable
            spacing: 8

            Item {
                width: 34
                height: 34
                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    sourceSize.width: 36
                    sourceSize.height: 36
                    fillMode: Image.PreserveAspectFit
                    source: Shell.tintedIcon(
                                Settings.values["nightLight.enabled"] ? "moon" : "sun",
                                Theme.foreground)
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Night light")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize
            }
            SettingsSwitch {
                settingKey: "nightLight.enabled"
            }
        }

        // --- screen recording (wf-recorder) -------------------------------
        RowLayout {
            objectName: "recorderRow"
            Layout.fillWidth: true
            visible: Recorder.available
            spacing: 4

            Item {
                width: 34
                height: 34
                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    sourceSize.width: 36
                    sourceSize.height: 36
                    fillMode: Image.PreserveAspectFit
                    source: Shell.tintedIcon("video", Recorder.recording ? "#e5484d"
                                                                         : Theme.foreground)
                }
            }
            Text {
                Layout.fillWidth: true
                text: Recorder.recording
                      ? qsTr("Recording %1").arg(popup.formatDuration(Recorder.elapsed))
                      : qsTr("Record the screen")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pointSize: Theme.fontSize
                elide: Text.ElideRight
            }
            // The microphone goes into the video too (recorder.audio).
            TaskButton {
                objectName: "recorderAudio"
                visible: !Recorder.busy
                iconName: "mic"
                iconSize: 18
                active: Settings.values["recorder.audio"] === true
                onClicked: Settings.setValue("recorder.audio",
                                             Settings.values["recorder.audio"] !== true)
            }
            TaskButton {
                objectName: "recorderScreen"
                visible: !Recorder.busy
                label: qsTr("Screen")
                onClicked: {
                    popup.hide();
                    Recorder.start(false, popup.screen ? popup.screen.name : "");
                }
            }
            TaskButton {
                objectName: "recorderArea"
                visible: !Recorder.busy && Recorder.areaAvailable
                label: qsTr("Area")
                onClicked: {
                    popup.hide();
                    Recorder.start(true, "");
                }
            }
            TaskButton {
                objectName: "recorderStop"
                visible: Recorder.busy
                iconName: "circle-stop"
                label: qsTr("Stop")
                onClicked: Recorder.stop()
            }
        }

        // --- power profile (power-profiles-daemon) ----------------------------
        RowLayout {
            objectName: "powerProfileRow"
            Layout.fillWidth: true
            visible: Session.powerProfiles.length > 0
            spacing: 4

            Item {
                width: 34
                height: 34
                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    sourceSize.width: 36
                    sourceSize.height: 36
                    fillMode: Image.PreserveAspectFit
                    source: Shell.tintedIcon("battery", Theme.foreground)
                }
            }
            Repeater {
                model: [
                    { value: "power-saver", label: qsTr("Saver") },
                    { value: "balanced", label: qsTr("Balanced") },
                    { value: "performance", label: qsTr("Performance") }
                ]
                delegate: TaskButton {
                    required property var modelData
                    Layout.fillWidth: true
                    label: modelData.label
                    active: Session.powerProfile === modelData.value
                    onClicked: Session.setPowerProfile(modelData.value)
                }
            }
        }

        // --- battery ----------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            visible: System.batteryPresent
            spacing: 8

            Item {
                width: 34
                height: 34
                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    sourceSize.width: 36
                    sourceSize.height: 36
                    fillMode: Image.PreserveAspectFit
                    source: Shell.tintedIcon(System.batteryCharging ? "battery-charging"
                                                                    : "battery",
                                             System.batteryCritical ? Theme.accent
                                                                    : Theme.foreground)
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Text {
                    text: Math.round(System.batteryPercent) + "%"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                }
                Text {
                    visible: System.batteryTimeText.length > 0
                    text: System.batteryTimeText
                    color: Theme.muted
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.6)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            TaskButton {
                iconName: "settings"
                label: qsTr("All settings")
                onClicked: { Shell.openSettings(); popup.hide(); }
            }
            Item { Layout.fillWidth: true }
            TaskButton {
                objectName: "quickLockButton"
                iconName: "lock"
                label: qsTr("Lock")
                onClicked: { popup.hide(); Shell.lock(); }
            }
            TaskButton {
                iconName: "bell"
                label: qsTr("History")
                onClicked: {
                    popup.hide();
                    if (popup.notificationCenter)
                        popup.notificationCenter.show();
                }
            }
        }
    }
}
