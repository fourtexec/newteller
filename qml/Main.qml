import QtQuick

// Root object. One desktop background and one panel per screen,
// plus a single start menu on the primary screen.
QtObject {
    id: root

    readonly property var primaryScreen: Qt.application.screens[0]

    StartMenu {
        id: startMenu
        screen: root.primaryScreen
    }

    Repeater {
        model: Qt.application.screens

        delegate: QtObject {
            required property var modelData

            Desktop {
                screen: modelData
            }

            Panel {
                screen: modelData
                startMenu: startMenu
            }
        }
    }
}
