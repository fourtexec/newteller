import QtQuick
import QtQuick.Window
import QtQuick.Layouts

// Sound, opened from the volume row of quick settings: pick the output
// (speakers, headphones, HDMI, a Bluetooth headset) and set the volume of
// each application that is playing.
DropdownWindow {
    id: popup

    cardWidth: 380
    cardHeight: Math.min(520, Math.max(220, content.implicitHeight + 2 * padding))
    padding: 12
    scopeName: "newteller-sound"

    function opened() {
        Audio.refresh();
    }

    // While the card is open, applications start and stop playing.
    Timer {
        interval: 2000
        repeat: true
        running: popup.visible
        onTriggered: Audio.refresh()
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        spacing: 6

        ShellTitleBar {
            Layout.fillWidth: true
            title: qsTr("Sound")
            iconName: "volume-2"
            onCloseClicked: popup.hide()
        }

        Text {
            Layout.fillWidth: true
            visible: !Audio.available
            wrapMode: Text.WordWrap
            text: qsTr("The sound server could not be read (pactl from pulseaudio-utils is needed).")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 1
        }

        Text {
            visible: Audio.available
            text: qsTr("Output")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 1
        }

        Repeater {
            model: Audio.available ? Audio.outputs : []

            delegate: TaskButton {
                required property var modelData
                objectName: "soundOutput"
                Layout.fillWidth: true
                height: 34
                fillWidth: true
                active: modelData.isDefault
                iconName: modelData.bluetooth ? "bluetooth" : "volume-2"
                iconSize: 18
                label: modelData.description
                onClicked: {
                    if (!modelData.isDefault)
                        Audio.setDefaultOutput(modelData.name);
                }
            }
        }

        Text {
            visible: Audio.available
            topPadding: 6
            text: Audio.streams.length > 0 ? qsTr("Applications") : qsTr("Nothing is playing")
            color: Theme.muted
            font.family: Theme.fontFamily
            font.pointSize: Theme.fontSize - 1
        }

        Repeater {
            model: Audio.available ? Audio.streams : []

            delegate: ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 0

                Text {
                    Layout.fillWidth: true
                    text: modelData.title.length > 0 && modelData.title !== modelData.app
                          ? qsTr("%1: %2").arg(modelData.app).arg(modelData.title)
                          : (modelData.app || qsTr("Application"))
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pointSize: Theme.fontSize - 1
                    elide: Text.ElideRight
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    TaskButton {
                        width: 30
                        height: 30
                        iconSize: 18
                        iconName: modelData.muted ? "volume-x" : "volume-2"
                        tooltipText: modelData.muted ? qsTr("Unmute") : qsTr("Mute")
                        onClicked: Audio.toggleStreamMuted(modelData.index)
                    }
                    SettingsSlider {
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: modelData.volume
                        suffix: "%"
                        onChanged: (v) => Audio.setStreamVolume(modelData.index, Math.round(v))
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }

        TaskButton {
            Layout.alignment: Qt.AlignRight
            visible: Session.soundSettingsTool.length > 0
            iconName: "settings"
            label: qsTr("More sound settings")
            onClicked: {
                popup.hide();
                Session.openSoundSettings();
            }
        }
    }
}
