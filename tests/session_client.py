#!/usr/bin/env python3
"""Open a visible native Wayland window for tests/session_smoke.sh.

With --maximized the window asks to be maximized before it is shown, as an
application that was closed maximized does.
"""
import sys

from PySide6.QtCore import QTimer
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine

app = QGuiApplication(sys.argv)
engine = QQmlApplicationEngine()
maximized = "--maximized" in sys.argv
source = b"""
import QtQuick
import QtQuick.Window

Window {
    width: 420
    height: 260
    visibility: VISIBILITY
    color: "#1f6feb"
    title: "TITLE"

    Rectangle {
        anchors.centerIn: parent
        width: 340
        height: 160
        radius: 16
        color: "#0d1117"
        Text {
            anchors.centerIn: parent
            text: "NewTeller compositor test window"
            color: "white"
            font.pixelSize: 18
        }
    }
}
"""
source = source.replace(b"VISIBILITY", b"Window.Maximized" if maximized else b"Window.Windowed")
source = source.replace(b"TITLE", b"NewTeller maximized test" if maximized
                        else b"NewTeller session test")
engine.loadData(source)
if not engine.rootObjects():
    sys.exit(1)
QTimer.singleShot(15000, app.quit)
sys.exit(app.exec())
