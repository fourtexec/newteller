import QtQuick

// Root object. One desktop background and one panel per screen,
// plus a single start menu on the primary screen.
//
// QtObject has no default property, so the windows are held in named
// properties. Instantiator (not Repeater, which can only create Items)
// creates the per-screen windows and recreates them when screens change.
QtObject {
    id: root

    readonly property StartMenu startMenu: StartMenu {
        screen: Qt.application.screens[0]
    }

    readonly property Instantiator desktops: Instantiator {
        model: Qt.application.screens

        delegate: Desktop {
            required property var modelData
            screen: modelData
        }
    }

    readonly property Instantiator panels: Instantiator {
        model: Qt.application.screens

        delegate: Panel {
            required property var modelData
            screen: modelData
            startMenu: root.startMenu
        }
    }
}
