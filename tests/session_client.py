#!/usr/bin/env python3
"""Open a visible native Wayland window for tests/session_smoke.sh."""
import sys

from PySide6.QtCore import QTimer
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine

app = QGuiApplication(sys.argv)
engine = QQmlApplicationEngine()
engine.loadData(b"""
import QtQuick
import QtQuick.Window

Window {
    width: 420
    height: 260
    visible: true
    color: "#1f6feb"
    title: "NewTeller session test"

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
""")
if not engine.rootObjects():
    sys.exit(1)
QTimer.singleShot(15000, app.quit)
sys.exit(app.exec())
