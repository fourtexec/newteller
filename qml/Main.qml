import QtQuick

// Root object. One desktop background and one panel per screen,
// plus a single start menu, run dialog and settings window on the primary
// screen.
//
// QtObject has no default property, so the windows are held in named
// properties. Instantiator (not Repeater, which can only create Items)
// creates the per-screen windows and recreates them when screens change.
QtObject {
    id: root

    readonly property StartMenu startMenu: StartMenu {
        screen: Qt.application.screens[0]
    }

    readonly property RunDialog runDialog: RunDialog {
        screen: Qt.application.screens[0]
    }

    // The built-in settings application. Its QML is compiled into the
    // executable, so it is always there and cannot be deleted like a plugin.
    readonly property SettingsApp settingsApp: SettingsApp {
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

    // Alt+F2 opens the run dialog anywhere in the session.
    readonly property Shortcut runShortcut: Shortcut {
        sequence: "Alt+F2"
        onActivated: root.runDialog.toggle()
    }

    // The start menu, the desktop menu and `newteller --settings` all open the
    // same embedded settings window.
    readonly property Connections shellConnections: Connections {
        target: Shell
        function onSettingsRequested() {
            root.settingsApp.showFromShell();
        }
    }
}
