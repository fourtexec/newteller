import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import org.kde.layershell 1.0 as LayerShell

// Quick settings: the card behind the indicators button at the end of the
// panel. Media controls, volume, brightness, network and do-not-disturb in
// one place. Every row hides itself when its backend is missing, so the
// popup always fits what the machine actually offers.
Window {
    id: popup

    property int panelHeight: Settings.values["panel.height"] || 48
    property int panelMargin: Settings.values["panel.margin"] || 0
    // The notification centre on the primary screen; set by Main.qml so the
    // History button can open it.
    property var notificationCenter: null
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
        return rows + 1 /* dnd */ + 1 /* footer separator */;
    }

    function show() {
        popupCard.prepareOpen();
        visible = true;
        requestActivate();
        Quick.refresh();
        popupCard.playOpen();
    }

    function hide() {
        if (!visible || popupCard.closing)
            return;
        popupCard.playClose(function() { visible = false; });
    }

    function toggle() {
        if (visible)
            hide();
        else
            show();
    }

    width: 360
    height: Math.max(240, Math.min(560, 24 + 46 * visibleRows + (Quick.mediaAvailable ? 74 : 0)))
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
    x: Screen.virtualX + Screen.width - width - 8
    y: Screen.virtualY + Screen.height - height - panelHeight - panelMargin - 12

    LayerShell.Window.anchors: LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorRight
    LayerShell.Window.margins { right: 8; bottom: popup.panelHeight + popup.panelMargin + 12 }
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityOnDemand
    LayerShell.Window.scope: "newteller-quick-settings"

    onActiveChanged: {
        if (!active)
            closeTimer.restart();
    }

    Timer {
        id: closeTimer
        interval: 200
        onTriggered: {
            if (!popup.active)
                popup.hide();
        }
    }

    ShellFrame {
        id: popupCard
        anchors.fill: parent
        openOrigin: Item.BottomRight

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 4

            ShellTitleBar {
                Layout.fillWidth: true
                title: "Quick settings"
                iconName: "sliders-horizontal"
                onCloseClicked: popup.hide()
            }

            // --- media ------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                visible: Quick.mediaAvailable
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: Quick.mediaTitle.length > 0 ? Quick.mediaTitle : "Not playing"
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
                    tooltipText: Quick.volumeMuted ? "Unmute" : "Mute"
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
                        text: "Wi-Fi"
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
                    text: "Do not disturb"
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize
                }
                SettingsSwitch {
                    settingKey: "notifications.doNotDisturb"
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
                    label: "All settings"
                    onClicked: { Shell.openSettings(); popup.hide(); }
                }
                Item { Layout.fillWidth: true }
                TaskButton {
                    iconName: "bell"
                    label: "History"
                    onClicked: {
                        popup.hide();
                        if (popup.notificationCenter)
                            popup.notificationCenter.show();
                    }
                }
            }
        }
    }
}
