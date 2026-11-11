#!/usr/bin/env python3
"""Smoke test for NewTeller's QML, without building the C++ code.

Loads qrc:/qml/Main.qml (compiled from resources.qrc, exactly as the shell
does) on Qt's offscreen platform with two simulated screens, using stand-ins
for the C++ context objects (Settings, Theme, Apps, Shell, Plugins) and for the
QML modules the shell imports (org.kde.layershell and NewTeller). The test
fails on any QML warning or error and then checks that:

  * every screen gets a desktop and a panel covering the right area,
  * the windows carry the expected layer-shell and ShellWindow settings,
  * clicking Start opens the start menu with the application list, and
    clicking it again closes the menu,
  * every dropdown opens anchored to the panel button that toggled it, keeps
    its pointer on the edge facing the bar, follows a new anchor, flips to the
    other side of a top or left bar, stays inside the screen and closes with
    Escape.

Usage (no compositor or X server needed):

    pip install PySide6-Essentials
    python3 tests/qml_smoke.py
"""

import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ElementTree
from enum import IntEnum

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCREENS = [
    {"name": "Left", "x": 0, "y": 0, "width": 1920, "height": 1080},
    {"name": "Right", "x": 1920, "y": 0, "width": 1280, "height": 1024},
]

# The platform must be configured before Qt is loaded.
_config = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False)
json.dump({"synchronousWindowSystemEvents": True, "windowFrameMargins": False,
           "screens": [dict(s, logicalDpi=96, logicalBaseDpi=96, dpr=1.0) for s in SCREENS]}, _config)
_config.close()
os.environ["QT_QPA_PLATFORM"] = "offscreen:configfile=" + _config.name
os.environ["QT_QUICK_BACKEND"] = "software"

import PySide6  # noqa: E402
from PySide6.QtCore import (Q_ARG, Q_RETURN_ARG, QBuffer, QByteArray, QDateTime,  # noqa: E402
                            QDir, QEnum, QFile, QFileInfo, QIODevice, QMargins, QMetaObject,
                            QObject, QPoint, QPointF, QRectF, QtMsgType, QTimer, QTranslator, QUrl,
                            Property, Qt, Signal, Slot, qInstallMessageHandler)
from PySide6.QtGui import QColor, QGuiApplication, QImage, QImageReader  # noqa: E402
from PySide6.QtQml import (QmlAnonymous, QmlAttached, QmlNamedElement,  # noqa: E402
                           QmlUncreatable, QQmlApplicationEngine, QQmlComponent)
from PySide6.QtQuick import QQuickItem  # noqa: E402
from PySide6.QtTest import QTest  # noqa: E402

# ---------------------------------------------------------------------------
# Stand-ins for the QML modules.

# Records every attached object so the checks can inspect them.
ATTACHED = {"layershell": [], "shellwindow": []}

QML_IMPORT_NAME = "org.kde.layershell"
QML_IMPORT_MAJOR_VERSION = 1
QML_IMPORT_MINOR_VERSION = 0


@QmlAnonymous
class LayerShellAttached(QObject):
    """The properties of LayerShellQt::Window used by the QML files."""

    changed = Signal()

    def __init__(self, window):
        super().__init__(window)
        self.window = window
        self._values = {"anchors": 0, "layer": 0, "exclusionZone": 0, "keyboardInteractivity": 0,
                        "scope": "", "margins": QMargins()}
        ATTACHED["layershell"].append(self)

    def _get(name):
        return lambda self: self._values[name]

    def _set(name):
        def setter(self, value):
            self._values[name] = value
            self.changed.emit()
        return setter

    anchors = Property(int, _get("anchors"), _set("anchors"), notify=changed)
    layer = Property(int, _get("layer"), _set("layer"), notify=changed)
    exclusionZone = Property(int, _get("exclusionZone"), _set("exclusionZone"), notify=changed)
    keyboardInteractivity = Property(int, _get("keyboardInteractivity"), _set("keyboardInteractivity"),
                                     notify=changed)
    scope = Property(str, _get("scope"), _set("scope"), notify=changed)
    margins = Property(QMargins, _get("margins"), _set("margins"), notify=changed)


@QmlNamedElement("Window")
@QmlUncreatable("attached properties only")
@QmlAttached(LayerShellAttached)
class LayerShellWindow(QObject):
    @QEnum
    class Anchor(IntEnum):
        AnchorNone = 0
        AnchorTop = 1
        AnchorBottom = 2
        AnchorLeft = 4
        AnchorRight = 8

    @QEnum
    class Layer(IntEnum):
        LayerBackground = 0
        LayerBottom = 1
        LayerTop = 2
        LayerOverlay = 3

    @QEnum
    class KeyboardInteractivity(IntEnum):
        KeyboardInteractivityNone = 0
        KeyboardInteractivityExclusive = 1
        KeyboardInteractivityOnDemand = 2

    @staticmethod
    def qmlAttachedProperties(self, o):
        return LayerShellAttached(o)


QML_IMPORT_NAME = "NewTeller"


@QmlAnonymous
class ShellWindowAttached(QObject):
    """Mirrors the ShellWindow attached type from src/shellwindow.h."""

    roleChanged = Signal()

    def __init__(self, window):
        super().__init__(window)
        self.window = window
        self._role = 0
        ATTACHED["shellwindow"].append(self)

    def _get_role(self):
        return self._role

    def _set_role(self, role):
        self._role = role
        self.roleChanged.emit()

    role = Property(int, _get_role, _set_role, notify=roleChanged)


@QmlNamedElement("ShellWindow")
@QmlUncreatable("ShellWindow is only available as an attached property")
@QmlAttached(ShellWindowAttached)
class ShellWindowType(QObject):
    @QEnum
    class Role(IntEnum):
        Normal = 0
        Desktop = 1
        Dock = 2

    @staticmethod
    def qmlAttachedProperties(self, o):
        return ShellWindowAttached(o)


# ---------------------------------------------------------------------------
# Stand-ins for the C++ context objects (see src/*.h for the real API).

APPS = [
    {"id": "firefox", "name": "Firefox", "icon": "firefox",
     "generic": "web browser", "comment": "Browse the web",
     "categories": ["Network"], "keywords": ["internet", "web"],
     "actions": [{"id": "new-private-window", "name": "New Private Window"}]},
    {"id": "org.gnome.Terminal", "name": "Terminal", "icon": "utilities-terminal",
     "generic": "terminal emulator", "comment": "Use the command line",
     "categories": ["System"], "keywords": ["shell", "console"]},
    {"id": "org.gnome.Nautilus", "name": "Files", "icon": "system-file-manager",
     "generic": "file manager", "comment": "Organize your files",
     "categories": ["System", "Utility"], "keywords": ["folder"]},
]


class Settings(QObject):
    valuesChanged = Signal()
    dirtyChanged = Signal()

    def __init__(self):
        super().__init__()
        self._values = {
            "panel.height": 48,
            "panel.clockFormat": "HH:mm",
            # An unknown id checks that Apps.byId() returning {} is handled.
            "panel.pinned": ["firefox", "org.gnome.Terminal", "not-installed"],
            "startmenu.width": 440,
            "startmenu.height": 580,
            "desktop.wallpaper": "",
            "run.history": ["cmatrix", "htop"],
            "run.historyLimit": 25,
            # Surfaces: the same defaults as src/settings.cpp, so the material
            # bindings take their real code paths in the test.
            "panel.material": "theme",
            "panel.customColors": False,
            "panel.backgroundColor": "",
            "panel.backgroundOpacity": 0.92,
            "panel.foregroundColor": "",
            "dock.material": "none",
            "dock.group": False,
            "dock.backgroundColor": "",
            "dock.backgroundOpacity": 0.5,
            "dock.hoverOpacity": 0.15,
            "dock.activeOpacity": 0.30,
            "windows.style": "card",
            "windows.material": "theme",
            "windows.radius": -1,
            "windows.opacityPercent": 0,
            "windows.shadow": 1,
            "notifications.theme": "system",
            "notifications.material": "theme",
            "notifications.opacityPercent": 0,
            "notifications.cornerRadius": -1,
            # Updates: the defaults of src/settings.cpp.
            "updates.autoCheck": True,
            "updates.panelIndicator": True,
            "updates.skippedBuild": "",
        }

    @Property("QVariantMap", notify=valuesChanged)
    def values(self):
        return self._values

    dirty = Property(bool, lambda self: False, notify=dirtyChanged)

    @Slot(str, "QVariant", result="QVariant")
    def value(self, key, fallback=None):
        return self._values.get(key, fallback)

    @Slot(str, "QVariant")
    def setValue(self, key, value):
        # QML lists arrive as QJSValue on some Qt versions.
        if hasattr(value, "toVariant"):
            value = value.toVariant()
        self._values[key] = value
        self.valuesChanged.emit()

    @Slot(str)
    def reset(self, key):
        self._values.pop(key, None)
        self.valuesChanged.emit()

    @Slot(result="QVariantMap")
    def defaults(self):
        return dict(self._values)

    @Slot(result="QVariantList")
    def keys(self):
        return list(self._values)

    @Slot(result="QVariantList")
    def validate(self):
        return []

    @Slot(result=str)
    def configPath(self):
        return "/tmp/newteller-test/shell.json"

    @Slot()
    def flush(self):
        pass

    @Slot()
    def reload(self):
        pass


class Theme(QObject):
    themeChanged = Signal()

    VALUES = {"background": "#1e2127", "foreground": "#e6e6e6", "accent": "#4c8bf5",
              "surface": "#2a2e36", "border": "#3a3f4b", "muted": "#8a8f98",
              "opacity": 0.92, "radius": 8, "fontSize": 10, "fontFamily": "Sans"}

    def _const(value, kind, notify=themeChanged):
        return Property(kind, lambda self: value, notify=notify)

    name = _const("default", str)
    background = _const(QColor("#1e2127"), QColor)
    foreground = _const(QColor("#e6e6e6"), QColor)
    accent = _const(QColor("#4c8bf5"), QColor)
    surface = _const(QColor("#2a2e36"), QColor)
    border = _const(QColor("#3a3f4b"), QColor)
    muted = _const(QColor("#8a8f98"), QColor)
    opacity = _const(0.92, float)
    radius = _const(8, int)
    fontSize = _const(10, int)
    fontFamily = _const("Sans", str)

    @Slot(QColor, QColor, float, result=QColor)
    def mix(self, a, b, t):
        t = max(0.0, min(1.0, t))
        return QColor.fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                               a.greenF() + (b.greenF() - a.greenF()) * t,
                               a.blueF() + (b.blueF() - a.blueF()) * t,
                               a.alphaF() + (b.alphaF() - a.alphaF()) * t)

    @Slot(result="QVariantList")
    def availableThemes(self):
        return ["default", "light", "nord"]

    @Slot(str, result="QVariantMap")
    def themeValues(self, name):
        return dict(self.VALUES)

    @Slot(str, result=bool)
    def hasUserTheme(self, name):
        return False

    @Slot(str, "QVariantMap", result=bool)
    def saveUserTheme(self, name, values):
        return True

    @Slot(str, result=bool)
    def deleteUserTheme(self, name):
        return True

    @Slot(result=str)
    def userThemesPath(self):
        return "/tmp/newteller-test/themes"


class Apps(QObject):
    runningChanged = Signal()

    def __init__(self):
        super().__init__()
        self.launched = []
        self.running = set()

    count = Property(int, lambda self: len(APPS), notify=runningChanged)

    runningIds = Property("QVariantList", lambda self: sorted(self.running),
                          notify=runningChanged)

    def _categories(self):
        counts = {}
        for a in APPS:
            for c in a.get("categories", []):
                counts[c] = counts.get(c, 0) + 1
        return [{"name": k, "count": v} for k, v in sorted(counts.items())]

    categories = Property("QVariantList", _categories, notify=runningChanged)

    @Slot(str, result="QVariantMap")
    def byId(self, app_id):
        return next((a for a in APPS if a["id"] == app_id), {})

    @Slot(str, result=bool)
    def launchAsRoot(self, app_id):
        self.launched.append(("root", app_id))
        return True

    @Slot(result=bool)
    def canLaunchAsRoot(self):
        return True

    @Slot(str, str, result="QVariantMap")
    def matchWindowApp(self, app_id, title=""):
        return next((a for a in APPS if a["id"] == app_id), {})

    @Slot(str, int, result="QVariantList")
    def search(self, query, limit):
        q = query.lower()
        return [dict(a) for a in APPS
                if q in a["name"].lower() or q in a["id"].lower()][:limit]

    @Slot(str, int, result="QVariantList")
    def searchDetailed(self, query, limit):
        # Rank first by usage and name, and add the StyledText highlight the
        # start menu shows; the empty query lists everything.
        q = query.lower().strip()
        out = []
        for a in APPS:
            if q and q not in a["name"].lower() and q not in a["id"].lower() \
                    and q not in a.get("comment", "").lower():
                continue
            hit = dict(a)
            name = a["name"]
            i = name.lower().find(q) if q else -1
            if i >= 0:
                hit["highlight"] = (name[:i] + "<font color=\"#ffffff\">"
                                    + name[i:i + len(q)] + "</font>" + name[i + len(q):])
            else:
                hit["highlight"] = name
            out.append(hit)
        return out[:limit]

    @Slot(int, result="QVariantList")
    def recent(self, limit):
        return [dict(a) for a in APPS[:limit]]

    @Slot(str, result=bool)
    def isRunning(self, app_id):
        return app_id in self.running

    @Slot(str, str, result=bool)
    def launchAction(self, app_id, action_id):
        self.launched.append((app_id, action_id))
        return True

    @Slot(str, result=bool)
    def launch(self, app_id):
        self.launched.append(app_id)
        self.running.add(app_id)  # a launched application keeps running
        self.runningChanged.emit()
        return True

    @Slot()
    def forgetRecent(self):
        pass


class Shell(QObject):
    nowChanged = Signal()
    settingsRequested = Signal()
    settingsPageRequested = Signal(str)
    startMenuRequested = Signal()
    startMenuToggleRequested = Signal()
    notificationsToggleRequested = Signal()
    quickSettingsToggleRequested = Signal()
    clipboardToggleRequested = Signal()
    taskViewToggleRequested = Signal()
    forceQuitRequested = Signal()
    runDialogRequested = Signal()
    dockItemRequested = Signal(int)
    screenRecordingToggleRequested = Signal()

    def __init__(self, desktop_dir, home_dir):
        super().__init__()
        self._desktop = QUrl.fromLocalFile(desktop_dir).toString()
        self._home = home_dir
        self.calls = []
        self._now = QDateTime.currentDateTime()
        self._ticker = QTimer(self)
        self._ticker.timeout.connect(self._tick)
        self._ticker.start(1000)

    def _tick(self):
        self._now = QDateTime.currentDateTime()
        self.nowChanged.emit()

    now = Property(QDateTime, lambda self: self._now, notify=nowChanged)
    version = Property(str, lambda self: "0.3.0-test", constant=True)
    home = Property(str, lambda self: self._home, constant=True)
    desktopUrl = Property(str, lambda self: self._desktop, constant=True)
    picturesPath = Property(str, lambda self: self._home + "/Pictures", constant=True)
    documentsPath = Property(str, lambda self: self._home + "/Documents", constant=True)
    downloadsPath = Property(str, lambda self: self._home + "/Downloads", constant=True)
    userName = Property(str, lambda self: "tester", constant=True)
    sessionType = Property(str, lambda self: "wayland", constant=True)
    hostname = Property(str, lambda self: "testhost", constant=True)
    distro = Property(str, lambda self: "TestOS 1.0", constant=True)
    kernelVersion = Property(str, lambda self: "6.1.0-test", constant=True)
    isWayland = Property(bool, lambda self: True, constant=True)

    @Slot(str, result=QUrl)
    def iconUrl(self, name):
        # Mirror the real resolution for the bundled core set, so the smoke
        # test actually renders the compiled icons (a wrong resources.qrc
        # entry would fail the test with a missing-resource error). The ':/'
        # form works on every Qt build; 'qrc:/' does not from Python.
        url = QUrl(f":/icons/{name}.svg")
        return url if QFile.exists(url.toString()) else QUrl()

    @Slot(str, QColor, result=QUrl)
    def tintedIcon(self, name, color):
        # The real implementation recolours the compiled IconKit SVGs to the
        # theme foreground (stroke="currentColor" would render black); the
        # test serves the original so the resource is still exercised.
        return self.iconUrl(name)

    @Slot(str)
    def openUrl(self, url):
        self.calls.append(("openUrl", url))

    @Slot(str)
    def openPath(self, path):
        self.calls.append(("openPath", path))

    @Slot(int, result="QVariantList")
    def recentFiles(self, limit):
        return [{"path": "/home/test/Documents/Report 2026.odt", "name": "Report 2026.odt",
                 "folder": False, "image": False}][:limit]

    @Slot(str, result=int)
    def pasteFiles(self, folder):
        self.calls.append(("pasteFiles", folder))
        return 0

    @Slot(result=str)
    def clipboardText(self):
        return "pasted text"

    @Slot(str, str, result=bool)
    def renameFile(self, path, name):
        self.calls.append(("renameFile", path, name))
        return True

    @Slot(str, result=str)
    def pathKind(self, path):
        p = path.strip()
        if p == "~" or p.startswith("~/"):
            p = self._home + p[1:]
        if not os.path.isabs(p):
            return ""
        if os.path.isdir(p):
            return "dir"
        if os.path.isfile(p):
            return "exec" if os.access(p, os.X_OK) else "file"
        return ""

    @Slot(str, result=bool)
    def addToDesktop(self, path):
        self.calls.append(("addToDesktop", path))
        return True

    @Slot(str, str, result=str)
    def createFolder(self, parent, name):
        self.calls.append(("createFolder", parent, name))
        return ""

    @Slot(str, result=bool)
    def moveToTrash(self, path):
        self.calls.append(("moveToTrash", path))
        return True

    @Slot(str)
    def copyToClipboard(self, text):
        self.calls.append(("copyToClipboard", text))

    @Slot(str, str, result=bool)
    def saveTextFile(self, path, text):
        self.calls.append(("saveTextFile", path))
        return True

    # --- file helpers (src/shellcontroller.cpp) --------------------------
    # Same normalisation as the real controller: the pickers hand over plain
    # paths, "~/..." strings, file:// URLs and percent-encoded paths, and must
    # always end up at an existing directory.
    @Slot(str, result=str)
    def folderPath(self, path):
        p = (path or "").strip()
        if not p:
            return self._home
        if p.startswith("file:"):
            local = QUrl(p).toLocalFile()
            p = local or p[7:]
        elif "://" in p:
            return self._home
        if p == "~":
            p = self._home
        elif p.startswith("~/"):
            p = self._home + p[1:]
        if not QDir.isAbsolutePath(p):
            p = self._home + "/" + p
        p = QDir.cleanPath(p)
        info = QFileInfo(p)
        if info.isDir():
            return info.absoluteFilePath()
        return info.absolutePath() if info.isFile() else self._home

    @Slot(str, result=bool)
    def isImageFile(self, path):
        p = (path or "").strip()
        if not p or not QFileInfo(p).isFile():
            return False
        return QImageReader(p).canRead()

    @Slot(str, result=bool)
    def fileExists(self, path):
        p = (path or "").strip()
        return bool(p) and QFileInfo(p).isFile()

    @Slot(str)
    def terminal(self, command=""):
        self.calls.append(("terminal", command))

    @Slot(str)
    def runShell(self, command):
        self.calls.append(("runShell", command))

    @Slot()
    def openConfig(self):
        self.calls.append(("openConfig",))

    @Slot()
    def openSettings(self):
        self.calls.append(("openSettings",))
        self.settingsRequested.emit()

    @Slot(str)
    def openSettingsPage(self, page):
        self.calls.append(("openSettingsPage", page))

    @Slot()
    def lock(self):
        self.calls.append(("lock",))

    @Slot()
    def logout(self):
        self.calls.append(("logout",))

    @Slot()
    def suspend(self):
        self.calls.append(("suspend",))

    @Slot()
    def reboot(self):
        self.calls.append(("reboot",))

    @Slot()
    def powerOff(self):
        self.calls.append(("powerOff",))

    @Slot(result="QVariantMap")
    def diagnostics(self):
        return {"platform": "offscreen", "sessionType": "offscreen", "hostname": "testhost",
                "distro": "TestOS 1.0", "kernel": "6.1.0-test", "user": "tester",
                "configPath": "/tmp/newteller-test/shell.json"}

    @Slot(str, str)
    def notify(self, title, body):
        self.calls.append(("notify", title))

    @Slot(str, str, str)
    def notifyUrgent(self, title, body, icon=""):
        self.calls.append(("notifyUrgent", title))


class FilePortal(QObject):
    """Stand-in for the FilePortal context property (src/fileportal.h)."""

    filePicked = Signal(str)
    pickFailed = Signal()

    def __init__(self):
        super().__init__()
        self.calls = []

    available = Property(bool, lambda self: True, constant=True)

    @Slot(str, str, bool)
    def pickFile(self, title, start_folder, images_only):
        self.calls.append(("pickFile", title, start_folder, images_only))


class System(QObject):
    """Stand-in for the SystemMonitor context property (src/systemmonitor.h)."""

    snapshotUpdated = Signal()
    cpuPercent = Property(float, lambda self: 12.5, constant=True)
    cpuCount = Property(int, lambda self: 8, constant=True)
    load1 = Property(float, lambda self: 0.5, constant=True)
    load5 = Property(float, lambda self: 0.7, constant=True)
    load15 = Property(float, lambda self: 0.9, constant=True)
    memoryPercent = Property(float, lambda self: 40.0, constant=True)
    memoryText = Property(str, lambda self: "3.2 GiB / 8 GiB", constant=True)
    memoryUsedText = Property(str, lambda self: "3.2 GiB", constant=True)
    memoryTotalText = Property(str, lambda self: "8 GiB", constant=True)
    networkDownText = Property(str, lambda self: "1.2 MiB/s", constant=True)
    networkUpText = Property(str, lambda self: "240 KiB/s", constant=True)
    uptimeText = Property(str, lambda self: "1h 2m", constant=True)
    # (present, percent, charging); the battery test changes it.
    battery = (False, 0.0, False)
    batteryPresent = Property(bool, lambda self: self.battery[0], notify=snapshotUpdated)
    batteryPercent = Property(float, lambda self: self.battery[1], notify=snapshotUpdated)
    batteryStatus = Property(str, lambda self: "", constant=True)
    batteryCharging = Property(bool, lambda self: self.battery[2], notify=snapshotUpdated)
    batteryCritical = Property(bool, lambda self: False, constant=True)
    batteryTimeText = Property(str, lambda self: "", constant=True)
    # An int: the desktop-sysinfo widget prints "N processes".
    processes = Property(int, lambda self: 123, constant=True)


class Plugins(QObject):
    pluginsChanged = Signal()

    def __init__(self, plugins):
        super().__init__()
        self._plugins = plugins

    @Property("QVariantList", notify=pluginsChanged)
    def plugins(self):
        return self._plugins

    @Slot(str, bool)
    def setEnabled(self, plugin_id, enabled):
        for p in self._plugins:
            if p["id"] == plugin_id:
                p["enabled"] = enabled
        self.pluginsChanged.emit()

    @Slot()
    def rescan(self):
        self.pluginsChanged.emit()


class Windows(QObject):
    """Stand-in for the WindowList context property (src/windowlist.h)."""

    windowsChanged = Signal()

    def __init__(self):
        super().__init__()
        self._windows = [
            {"id": "w1", "title": "Firefox — Startpage", "appId": "firefox",
             "active": True, "minimized": False, "maximized": False},
            {"id": "w2", "title": "Terminal", "appId": "org.gnome.Terminal",
             "active": False, "minimized": False, "maximized": True},
            # A read-only toplevel as ext_foreign_toplevel_list reports them
            # (GNOME 48+): it is listed but cannot be focused or closed.
            {"id": "e1", "title": "Files", "appId": "org.gnome.Nautilus",
             "active": False, "minimized": False, "maximized": False,
             "readOnly": True},
        ]
        self.calls = []

    @Property("QVariantList", notify=windowsChanged)
    def windows(self):
        return self._windows

    count = Property(int, lambda self: len(self._windows), notify=windowsChanged)
    backend = Property(str, lambda self: "x11", constant=True)

    @Slot(str)
    def activate(self, window_id):
        self.calls.append(("activate", window_id))

    @Slot(str)
    def closeWindow(self, window_id):
        self.calls.append(("close", window_id))

    @Slot(str)
    def minimize(self, window_id):
        self.calls.append(("minimize", window_id))

    @Slot(str)
    def toggleMaximized(self, window_id):
        self.calls.append(("toggleMaximized", window_id))

    @Slot(result=str)
    def activeWindow(self):
        return "w1"

    @Slot(str, result="QStringList")
    def windowsOfApp(self, app_id):
        return [w["id"] for w in self._windows if w["appId"] == app_id]

    @Slot(str, result=QUrl)
    def thumbnailUrl(self, window_id):
        # The offscreen test has no X server; the dock falls back to icons.
        return QUrl()


class Notifications(QObject):
    """Stand-in for the NotificationDaemon context property (src/notifications.h)."""
    activeChanged = Signal()
    historyChanged = Signal()
    dbusChanged = Signal()
    arrived = Signal("QVariantMap")

    def __init__(self):
        super().__init__()
        self._active = []
        self._history = []
        self.calls = []

    active = Property("QVariantList", lambda self: self._active, notify=activeChanged)
    history = Property("QVariantList", lambda self: self._history, notify=historyChanged)
    ownsDBus = Property(bool, lambda self: False, notify=dbusChanged)
    dbusAvailable = Property(bool, lambda self: False, constant=True)

    @Slot(str, str, str, str, int, result="QVariant")
    def send(self, app, summary, body, icon, timeout):
        return 1

    @Slot("QVariant", "QVariant")
    def dismiss(self, notification_id, reason=2):
        pass

    @Slot("QVariant", str)
    def invokeAction(self, notification_id, action_key):
        self.calls.append(("invokeAction", notification_id, action_key))

    @Slot("QVariant", str)
    def reply(self, notification_id, text):
        self.calls.append(("reply", notification_id, text))

    @Slot()
    def clearHistory(self):
        self._history.clear()
        self.historyChanged.emit()

    @Slot("QVariant")
    def removeHistory(self, notification_id):
        pass


class Tray(QObject):
    """Stand-in for the TrayWatcher context property (src/tray.h)."""

    itemsChanged = Signal()
    menuReady = Signal(str, "QVariantList")
    menuChanged = Signal(str)
    activateFailed = Signal(str, "QStringList")

    def __init__(self):
        super().__init__()
        self.calls = []
        self.menus = {"item-1": [
            {"id": 1, "label": "Mute", "enabled": True, "visible": True, "separator": False,
             "checkable": True, "checked": False, "iconName": "", "hasChildren": False,
             "children": []},
            {"id": 2, "label": "Quit", "enabled": True, "visible": True, "separator": False,
             "checkable": False, "checked": False, "iconName": "", "hasChildren": False,
             "children": []},
        ]}
        self._items = [{
            "id": ":1.42|/StatusNotifierItem",
            "service": ":1.42",
            "path": "/StatusNotifierItem",
            "name": "Volume applet",
            "title": "Volume applet",
            "status": "Active",
            "tooltipTitle": "Volume",
            "tooltipBody": "Output 42%",
        }]

    items = Property("QVariantList", lambda self: self._items, notify=itemsChanged)
    count = Property(int, lambda self: len(self._items), notify=itemsChanged)
    available = Property(bool, lambda self: True, constant=True)
    enabled = Property(bool, lambda self: True, constant=True)

    @Slot(str, result=QUrl)
    def iconUrl(self, item_id):
        # Serves a compiled icon so the panel actually renders something.
        return QUrl(":/icons/bell.svg")

    @Slot(str)
    def activate(self, item_id):
        self.calls.append(("activate", item_id))

    @Slot(str)
    def secondaryActivate(self, item_id):
        self.calls.append(("secondaryActivate", item_id))

    @Slot(str, int, int)
    def contextMenu(self, item_id, x, y):
        self.calls.append(("contextMenu", item_id, x, y))

    @Slot(str, int, bool)
    def scroll(self, item_id, delta, horizontal):
        self.calls.append(("scroll", item_id, delta, horizontal))

    @Slot(str, result=bool)
    def hasMenu(self, item_id):
        return False

    @Slot(str)
    def requestMenu(self, item_id):
        self.calls.append(("requestMenu", item_id))
        # Answers later, like the real watcher's asynchronous GetLayout.
        items = self.menus.get(item_id, [])
        QTimer.singleShot(50, lambda: self.menuReady.emit(item_id, items))

    @Slot(str, int)
    def menuAboutToShow(self, item_id, menu_item_id):
        self.calls.append(("menuAboutToShow", item_id, menu_item_id))

    @Slot(str, int, str)
    def menuEvent(self, item_id, menu_item_id, event):
        self.calls.append(("menuEvent", item_id, menu_item_id, event))


class Session(QObject):
    """Stand-in for the Session context property (src/sessionservices.h)."""

    compositorChanged = Signal()
    keyboardChanged = Signal()
    lockedChanged = Signal()
    nightLightChanged = Signal()

    compositorConnected = Property(bool, lambda self: True, notify=compositorChanged)
    # Two layouts, so the panel shows its layout indicator.
    keyboardLayouts = Property("QVariantList", lambda self: ["us", "ru"], notify=keyboardChanged)
    keyboardLayout = Property(int, lambda self: 1, notify=keyboardChanged)
    keyboardLayoutLabel = Property(str, lambda self: "RU", notify=keyboardChanged)
    keyboardLayoutName = Property(str, lambda self: "Russian", notify=keyboardChanged)
    locked = Property(bool, lambda self: False, notify=lockedChanged)
    lockAvailable = Property(bool, lambda self: True, constant=True)
    idleAvailable = Property(bool, lambda self: True, constant=True)
    nightLightAvailable = Property(bool, lambda self: True, constant=True)
    nightLightRunning = Property(bool, lambda self: False, notify=nightLightChanged)
    polkitAgent = Property(str, lambda self: "/usr/bin/lxpolkit", constant=True)
    displaySettingsTool = Property(str, lambda self: "wdisplays", constant=True)
    soundSettingsTool = Property(str, lambda self: "pavucontrol", constant=True)

    @Slot()
    def openSoundSettings(self):
        self.calls.append(("openSoundSettings",))
    workspacesChanged = Signal()
    switcherChanged = Signal()
    mediaKey = Signal(str)
    workspace = Property(int, lambda self: 0, notify=workspacesChanged)
    workspaceCount = Property(int, lambda self: 4, notify=workspacesChanged)
    workspaceOccupied = Property(int, lambda self: 0b101, notify=workspacesChanged)
    switcherEntries = Property("QVariantList", lambda self: [], notify=switcherChanged)
    switcherIndex = Property(int, lambda self: -1, notify=switcherChanged)
    powerProfileChanged = Signal()
    powerProfile = Property(str, lambda self: "balanced", notify=powerProfileChanged)
    powerProfiles = Property("QVariantList", lambda self: ["power-saver", "balanced", "performance"],
                             notify=powerProfileChanged)

    wifiChanged = Signal()
    wifiConnectFinished = Signal(bool, str, str)
    wifiAvailable = Property(bool, lambda self: True, notify=wifiChanged)
    wifiStatus = Property(str, lambda self: "", notify=wifiChanged)
    pointerPressed = Signal(int, int)
    hungWindowsChanged = Signal()
    windowHung = Signal(int)
    windowMenuRequested = Signal(int, int, int, int, str, str)
    activeOutputChanged = Signal()
    activeOutput = Property(str, lambda self: "", notify=activeOutputChanged)

    @Slot(int)
    def minimizeWindow(self, window_id):
        self.calls.append(("minimizeWindow", window_id))

    @Slot(int)
    def toggleMaximizeWindow(self, window_id):
        self.calls.append(("toggleMaximizeWindow", window_id))

    @Slot(int)
    def toggleFullscreenWindow(self, window_id):
        self.calls.append(("toggleFullscreenWindow", window_id))

    @Slot(int, bool)
    def setWindowAlwaysOnTop(self, window_id, on_top):
        self.calls.append(("setWindowAlwaysOnTop", window_id, on_top))

    @Slot(int, bool)
    def setWindowSticky(self, window_id, sticky):
        self.calls.append(("setWindowSticky", window_id, sticky))

    @Slot(int)
    def moveWindowToNextScreen(self, window_id):
        self.calls.append(("moveWindowToNextScreen", window_id))
    hungWindows = Property("QVariantList", lambda self: [], notify=hungWindowsChanged)

    @Slot(int, result=str)
    def killWindow(self, window_id):
        self.calls.append(("killWindow", window_id))
        return ""
    wifiBusy = Property(bool, lambda self: False, notify=wifiChanged)
    wifiNetworks = Property("QVariantList", lambda self: [
        {"ssid": "Home", "signal": 80, "secure": True, "active": True},
        {"ssid": "Cafe", "signal": 40, "secure": False, "active": False}], notify=wifiChanged)

    @Slot()
    def scanWifi(self):
        self.calls.append(("scanWifi",))

    @Slot(str, str)
    def connectWifi(self, ssid, password):
        self.calls.append(("connectWifi", ssid))

    @Slot()
    def disconnectWifi(self):
        self.calls.append(("disconnectWifi",))

    @Slot(result="QVariantList")
    def windows(self):
        return [{"id": 1, "workspace": 0, "appId": "firefox", "title": "Web", "focused": True,
                 "minimized": False, "maximized": False},
                {"id": 2, "workspace": 0, "appId": "org.gnome.Terminal", "title": "Shell",
                 "focused": False, "minimized": False, "maximized": False},
                {"id": 3, "workspace": 1, "appId": "", "title": "Elsewhere", "focused": False,
                 "minimized": True, "maximized": False}]

    @Slot(int, int, result=str)
    def thumbnail(self, window_id, size):
        self.calls.append(("thumbnail", window_id))
        return ""

    @Slot(int, result=str)
    def cachedThumbnail(self, window_id):
        return ""

    @Slot(str, str, str, int, int, int, int, int)
    def setBlur(self, owner, scope, screen, x, y, width, height, radius):
        self.calls.append(("setBlur", scope))

    @Slot(str, str, str)
    def clearBlur(self, owner, scope, screen):
        self.calls.append(("clearBlur", scope))

    @Slot(int)
    def activateWindow(self, window_id):
        self.calls.append(("activateWindow", window_id))

    @Slot(int)
    def closeWindow(self, window_id):
        self.calls.append(("closeWindow", window_id))

    @Slot(int, int)
    def moveWindow(self, window_id, workspace):
        self.calls.append(("moveWindow", window_id, workspace))

    @Slot(result="QVariantList")
    def outputs(self):
        return [{"name": "HDMI-A-1", "description": "Test Monitor", "enabled": True,
                 "width": 1920, "height": 1080, "refresh": 60000, "x": 0, "y": 0,
                 "scale": 1.0, "transform": 0,
                 "modes": [{"width": 1920, "height": 1080, "refresh": 60000},
                           {"width": 1920, "height": 1080, "refresh": 144000},
                           {"width": 1280, "height": 720, "refresh": 60000}]}]

    @Slot(str, bool, int, int, int, int, int, float, int, result=str)
    def setOutput(self, name, enabled, width, height, refresh, x, y, scale, transform):
        self.calls.append(("setOutput", name, width, height))
        return ""

    @Slot(result="QVariantList")
    def gtkThemes(self):
        return ["Adwaita", "Adwaita-dark"]

    @Slot(result="QVariantList")
    def iconThemes(self):
        return ["Adwaita", "hicolor"]

    @Slot(result="QVariantList")
    def cursorThemes(self):
        return ["Adwaita", "DMZ-White"]

    @Slot(result="QVariantList")
    def autostartEntries(self):
        return [{"name": "Network", "file": "/etc/xdg/autostart/nm-applet.desktop",
                 "exec": "nm-applet"}]

    @Slot(str)
    def setPowerProfile(self, profile):
        self.calls.append(("setPowerProfile", profile))

    def __init__(self):
        super().__init__()
        self.calls = []

    @Slot(int)
    def switchWorkspace(self, index):
        self.calls.append(("switchWorkspace", index))

    @Slot(int)
    def moveToWorkspace(self, index):
        self.calls.append(("moveToWorkspace", index))

    @Slot()
    def nextKeyboardLayout(self):
        self.calls.append(("nextKeyboardLayout",))

    @Slot(int)
    def setKeyboardLayout(self, index):
        self.calls.append(("setKeyboardLayout", index))

    @Slot()
    def lock(self):
        self.calls.append(("lock",))

    @Slot()
    def screenOff(self):
        self.calls.append(("screenOff",))

    @Slot()
    def openDisplaySettings(self):
        self.calls.append(("openDisplaySettings",))

    @Slot()
    def logout(self):
        self.calls.append(("logout",))

    @Slot(str, result=str)
    def layoutDisplayName(self, code):
        return {"us": "English (US)", "ru": "Russian"}.get(code, code)


class PolkitStub(QObject):
    """Stand-in for the Polkit context property (src/polkitagent.h)."""

    changed = Signal()
    registeredChanged = Signal()
    finished = Signal(bool)

    def __init__(self):
        super().__init__()
        self.calls = []
        self._active = False
        self._busy = False
        self._error = ""
        self._identity = 0

    available = Property(bool, lambda self: True, constant=True)
    registered = Property(bool, lambda self: True, notify=registeredChanged)
    active = Property(bool, lambda self: self._active, notify=changed)
    message = Property(str, lambda self: "Authentication is needed to run `/usr/bin/env' as the super user",
                       notify=changed)
    actionId = Property(str, lambda self: "org.freedesktop.policykit.exec", notify=changed)
    iconName = Property(str, lambda self: "", notify=changed)
    program = Property(str, lambda self: "/usr/bin/env WAYLAND_DISPLAY=/run/user/1000/wayland-0 /usr/bin/gedit",
                       notify=changed)
    details = Property("QVariantMap", lambda self: {}, notify=changed)
    identities = Property("QVariantList", lambda self: ["tester", "root"], notify=changed)
    identityIndex = Property(int, lambda self: self._identity, notify=changed)
    prompt = Property(str, lambda self: "Password:", notify=changed)
    echo = Property(bool, lambda self: False, notify=changed)
    busy = Property(bool, lambda self: self._busy, notify=changed)
    error = Property(str, lambda self: self._error, notify=changed)
    info = Property(str, lambda self: "", notify=changed)

    def request(self):
        self._active = True
        self._error = ""
        self.changed.emit()

    @Slot(str)
    def respond(self, response):
        self.calls.append(("respond", response))
        self._error = "That password did not work. Try again."
        self.changed.emit()

    @Slot()
    def cancel(self):
        self.calls.append(("cancel",))
        self._active = False
        self.changed.emit()
        self.finished.emit(False)

    @Slot(int)
    def selectIdentity(self, index):
        self.calls.append(("selectIdentity", index))
        self._identity = index
        self.changed.emit()


class BluetoothStub(QObject):
    """Stand-in for the Bluetooth context property (src/bluetooth.h)."""

    changed = Signal()
    operationFinished = Signal(str, str)
    available = Property(bool, lambda self: True, notify=changed)
    powered = Property(bool, lambda self: True, notify=changed)
    discovering = Property(bool, lambda self: False, notify=changed)
    busyDevice = Property(str, lambda self: "", notify=changed)
    devices = Property("QVariantList", lambda self: [
        {"path": "/org/bluez/hci0/dev_1", "address": "00:11", "name": "Headphones",
         "icon": "audio-headset", "paired": True, "connected": True, "trusted": True, "rssi": -40,
         "battery": 80},
        {"path": "/org/bluez/hci0/dev_2", "address": "00:22", "name": "Mouse",
         "icon": "input-mouse", "paired": False, "connected": False, "trusted": False, "rssi": -60,
         "battery": -1}],
        notify=changed)

    def __init__(self):
        super().__init__()
        self.calls = []

    @Slot(bool)
    def setPowered(self, on):
        self.calls.append(("setPowered", on))

    @Slot()
    def startDiscovery(self):
        self.calls.append(("startDiscovery",))

    @Slot()
    def stopDiscovery(self):
        self.calls.append(("stopDiscovery",))

    @Slot(str)
    def connectDevice(self, path):
        self.calls.append(("connectDevice", path))

    @Slot(str)
    def disconnectDevice(self, path):
        self.calls.append(("disconnectDevice", path))

    @Slot(str)
    def pairDevice(self, path):
        self.calls.append(("pairDevice", path))

    @Slot(str)
    def forgetDevice(self, path):
        self.calls.append(("forgetDevice", path))


class ClipboardStub(QObject):
    """Stand-in for the Clipboard context property (src/clipboardhistory.h)."""

    itemsChanged = Signal()
    available = Property(bool, lambda self: True, constant=True)

    def __init__(self):
        super().__init__()
        self._items = ["first copied text", "second\nline"]
        self._pinned = ["pinned snippet"]
        self.calls = []

    items = Property("QVariantList", lambda self: self._items, notify=itemsChanged)
    pinnedItems = Property("QVariantList", lambda self: self._pinned, notify=itemsChanged)

    @Slot(str)
    def copyText(self, text):
        self.calls.append(("copyText", text))

    @Slot(str)
    def pin(self, text):
        self.calls.append(("pin", text))

    @Slot(str)
    def unpin(self, text):
        self.calls.append(("unpin", text))

    @Slot(int)
    def copy(self, index):
        self.calls.append(("copy", index))

    @Slot(int)
    def remove(self, index):
        self.calls.append(("remove", index))

    @Slot()
    def clear(self):
        self.calls.append(("clear",))


class AudioStub(QObject):
    """Stand-in for the Audio context property (src/audiodevices.h)."""

    changed = Signal()

    def __init__(self):
        super().__init__()
        self.calls = []

    available = Property(bool, lambda self: True, notify=changed)
    outputs = Property("QVariantList", lambda self: [
        {"name": "alsa_output.speakers", "description": "Speakers", "isDefault": True,
         "volume": 40, "muted": False, "bluetooth": False},
        {"name": "bluez_output.headset", "description": "Headset", "isDefault": False,
         "volume": 60, "muted": False, "bluetooth": True}], notify=changed)
    streams = Property("QVariantList", lambda self: [
        {"index": 12, "app": "Firefox", "title": "Video", "icon": "firefox", "volume": 80,
         "muted": False}], notify=changed)
    inputs = Property("QVariantList", lambda self: [
        {"name": "alsa_input.internal", "description": "Internal microphone", "isDefault": True,
         "volume": 70, "muted": False, "bluetooth": False},
        {"name": "bluez_input.headset", "description": "Headset microphone", "isDefault": False,
         "volume": 70, "muted": False, "bluetooth": True}], notify=changed)

    @Slot(str)
    def setDefaultInput(self, name):
        self.calls.append(("setDefaultInput", name))

    @Slot()
    def refresh(self):
        self.calls.append(("refresh",))

    @Slot(str)
    def setDefaultOutput(self, name):
        self.calls.append(("setDefaultOutput", name))

    @Slot(int, int)
    def setStreamVolume(self, index, percent):
        self.calls.append(("setStreamVolume", index, percent))

    @Slot(int)
    def toggleStreamMuted(self, index):
        self.calls.append(("toggleStreamMuted", index))


class RecorderStub(QObject):
    """Stand-in for the Recorder context property (src/screenrecorder.h)."""

    stateChanged = Signal()
    elapsedChanged = Signal()

    def __init__(self):
        super().__init__()
        self.calls = []
        self._recording = False

    available = Property(bool, lambda self: True, constant=True)
    areaAvailable = Property(bool, lambda self: True, constant=True)
    busy = Property(bool, lambda self: self._recording, notify=stateChanged)
    recording = Property(bool, lambda self: self._recording, notify=stateChanged)
    elapsed = Property(int, lambda self: 75 if self._recording else 0, notify=elapsedChanged)

    @Slot(bool, str)
    def start(self, area, screen):
        self.calls.append(("start", area))
        self._recording = True
        self.stateChanged.emit()
        self.elapsedChanged.emit()

    @Slot()
    def stop(self):
        self.calls.append(("stop",))
        self._recording = False
        self.stateChanged.emit()
        self.elapsedChanged.emit()

    @Slot()
    def toggle(self):
        if self._recording:
            self.stop()
        else:
            self.start(False, "")


class UpdaterStub(QObject):
    """Stand-in for the Updater context property (src/updater.h)."""

    changed = Signal()
    progressChanged = Signal()
    checked = Signal()

    def __init__(self):
        super().__init__()
        self.calls = []
        self._state = "idle"
        self._skipped = False
        self._rolled_back = False

    state = Property(str, lambda self: self._state, notify=changed)
    error = Property(str, lambda self: "", notify=changed)
    busy = Property(bool, lambda self: self._state in ("checking", "downloading", "installing"),
                    notify=changed)
    source = Property(str, lambda self: "fourtexec.site", constant=True)
    installedCommit = Property(str, lambda self: "1" * 40, constant=True)
    installedDate = Property(str, lambda self: "01.10.2026", constant=True)
    target = Property(str, lambda self: "debian13-x86_64", constant=True)
    latestCommit = Property(str, lambda self: "" if self._state == "idle" else "2" * 40,
                            notify=changed)
    latestDate = Property(str, lambda self: "" if self._state == "idle" else "09.11.2026",
                          notify=changed)
    notes = Property(str, lambda self: "" if self._state == "idle" else "A newer build",
                     notify=changed)
    lastChecked = Property(str, lambda self: "" if self._state == "idle" else "10.11.2026 07:00",
                           notify=changed)
    progress = Property(float, lambda self: 0.4 if self._state == "downloading" else 0.0,
                        notify=progressChanged)
    changes = Property("QVariantList", lambda self: [] if self._state == "idle" else [
        {"title": "Faster dock", "commit": "2222222222", "date": "09.11.2026",
         "url": "https://github.com/fourtexec/newteller/commit/" + "2" * 40},
        {"title": "Fix the clock", "commit": "3333333333", "date": "",
         "url": "https://github.com/fourtexec/newteller/commit/" + "3" * 40}], notify=changed)
    skipped = Property(bool, lambda self: self._skipped, notify=changed)
    latestSize = Property(str, lambda self: "" if self._state == "idle" else "9.4 MB", notify=changed)
    canRollback = Property(bool, lambda self: True, notify=changed)
    rolledBack = Property(bool, lambda self: self._rolled_back, notify=changed)

    def _set(self, state):
        self._state = state
        self.changed.emit()
        self.progressChanged.emit()

    @Slot()
    def check(self):
        self.calls.append(("check",))
        self._set("available")
        self.checked.emit()

    @Slot()
    def install(self):
        self.calls.append(("install",))
        self._set("downloading")

    @Slot()
    def cancel(self):
        self.calls.append(("cancel",))
        self._set("available")

    @Slot()
    def rollback(self):
        self.calls.append(("rollback",))
        self._rolled_back = True
        self._set("installed")

    @Slot()
    def skip(self):
        self.calls.append(("skip",))
        self._skipped = True
        self.changed.emit()


class Quick(QObject):
    """Stand-in for the QuickSettings context property (src/quicksettings.h)."""

    volumeChanged = Signal()
    brightnessChanged = Signal()
    networkChanged = Signal()
    mediaChanged = Signal()

    volumeAvailable = Property(bool, lambda self: True, constant=True)
    volume = Property(int, lambda self: 42, notify=volumeChanged)
    volumeMuted = Property(bool, lambda self: False, notify=volumeChanged)
    brightnessAvailable = Property(bool, lambda self: True, constant=True)
    brightness = Property(int, lambda self: 80, notify=brightnessChanged)
    networkAvailable = Property(bool, lambda self: True, constant=True)
    networkState = Property(int, lambda self: 70, notify=networkChanged)
    networkStatus = Property(str, lambda self: "Connected", notify=networkChanged)
    wifiEnabled = Property(bool, lambda self: True, notify=networkChanged)
    mediaAvailable = Property(bool, lambda self: True, constant=True)
    mediaPlayer = Property(str, lambda self: "org.mpris.MediaPlayer2.test", constant=True)
    mediaTitle = Property(str, lambda self: "Test song", notify=mediaChanged)
    mediaArtist = Property(str, lambda self: "Test artist", notify=mediaChanged)
    mediaPlaying = Property(bool, lambda self: True, notify=mediaChanged)
    mediaArtUrl = Property(str, lambda self: self._art, notify=mediaChanged)

    def __init__(self):
        super().__init__()
        self.calls = []
        # A cover on disk, as players announce it (mpris:artUrl).
        art_dir = tempfile.mkdtemp(prefix="newteller-art-")
        cover = QImage(16, 16, QImage.Format_RGB32)
        cover.fill(QColor("#cc6633"))
        cover.save(os.path.join(art_dir, "cover.png"))
        self._art = QUrl.fromLocalFile(os.path.join(art_dir, "cover.png")).toString()

    @Slot(int)
    def setVolume(self, percent):
        self.calls.append(("setVolume", percent))

    @Slot()
    def toggleMuted(self):
        self.calls.append(("toggleMuted",))

    @Slot(int)
    def setBrightness(self, percent):
        self.calls.append(("setBrightness", percent))

    @Slot()
    def toggleWifi(self):
        self.calls.append(("toggleWifi",))

    @Slot()
    def mediaPlayPause(self):
        self.calls.append(("mediaPlayPause",))

    @Slot()
    def mediaNext(self):
        self.calls.append(("mediaNext",))

    @Slot()
    def mediaPrevious(self):
        self.calls.append(("mediaPrevious",))

    @Slot()
    def refresh(self):
        self.calls.append(("refresh",))


# ---------------------------------------------------------------------------

def compile_resources():
    """Registers resources.qrc using the rcc that ships with PySide6."""
    qt = os.path.join(os.path.dirname(PySide6.__file__), "Qt")
    rcc = next((p for p in (os.path.join(qt, "libexec", "rcc"), shutil.which("pyside6-rcc")) if p and
                os.path.exists(p)), None)
    if rcc is None:
        sys.exit("rcc not found (it is part of PySide6-Essentials)")
    code = subprocess.run([rcc, "-g", "python", "resources.qrc"], cwd=ROOT, check=True,
                          capture_output=True).stdout
    # Qt keeps pointers into the resource data, so its module namespace must stay alive.
    exec(compile(code, "resources_rc", "exec"), _RESOURCE_MODULE)


_RESOURCE_MODULE = {}


def card_rect(window):
    """The visible card inside a DropdownWindow, in global coordinates.

    The window is bigger than the card: the frame keeps room for its shadow
    around it, and a strip for the pointer on both sides of the pointer's
    axis. Everything a user sees is the card, so the checks look at this rect.
    """
    inset = int(window.property("frameInset") or 0)
    room = int(window.property("nubRoom") or 0) if window.property("nubEdge") else 0
    vertical = window.property("panelPosition") in ("top", "bottom")
    return (window.x() + inset + (0 if vertical else room),
            window.y() + inset + (room if vertical else 0),
            int(window.property("cardWidth")), int(window.property("cardHeight")))


def main():
    messages = []

    def handler(mode, context, message):
        if mode != QtMsgType.QtDebugMsg:
            messages.append(message)
        print(message, file=sys.stderr)

    qInstallMessageHandler(handler)
    app = QGuiApplication(sys.argv)
    compile_resources()

    work = tempfile.mkdtemp(prefix="newteller-qml-")
    desktop_dir = os.path.join(work, "Desktop")
    os.makedirs(os.path.join(desktop_dir, "Projects"))
    os.makedirs(os.path.join(work, "Pictures"))
    open(os.path.join(desktop_dir, "notes.txt"), "w").close()
    # A real picture: the desktop shows it as its own thumbnail.
    photo = QImage(16, 16, QImage.Format_RGB32)
    photo.fill(QColor("#3366aa"))
    photo.save(os.path.join(desktop_dir, "photo.png"), "PNG")
    # A launcher of an installed application (the desktop shows it as that
    # application and starts it on a double-click).
    with open(os.path.join(desktop_dir, "firefox.desktop"), "w") as f:
        f.write("[Desktop Entry]\nType=Application\nName=Firefox\nExec=firefox\n")
    panel_plugin = os.path.join(work, "PanelWidget.qml")
    with open(panel_plugin, "w") as f:
        f.write("import QtQuick\nRectangle { width: 30; height: 30; color: Theme.accent }\n")
    plugins = []
    for manifest_path in sorted(glob.glob(os.path.join(ROOT, "plugins", "*", "manifest.json"))):
        manifest = json.load(open(manifest_path, encoding="utf-8"))
        plugin_dir = os.path.dirname(manifest_path)
        plugins.append({
            "id": manifest["id"],
            "name": manifest.get("name", manifest["id"]),
            "version": manifest.get("version", "1.0.0"),
            "description": manifest.get("description", ""),
            "settings": manifest.get("settings", {}),
            "type": manifest["type"],
            "enabled": True,
            "x": manifest.get("x", 40),
            "y": manifest.get("y", 40),
            "entry": QUrl.fromLocalFile(os.path.join(plugin_dir, manifest["entry"])).toString(),
        })
    plugins.extend([
        {"id": "panel-test", "name": "Panel test", "type": "panel", "enabled": True, "x": 0, "y": 0,
         "entry": QUrl.fromLocalFile(panel_plugin).toString()},
        {"id": "disabled", "name": "Disabled", "type": "desktop", "enabled": False, "x": 0, "y": 0,
         "entry": QUrl.fromLocalFile(os.path.join(work, "missing.qml")).toString()},
    ])

    settings, theme, apps = Settings(), Theme(), Apps()
    shell, plugin_manager = Shell(desktop_dir, work), Plugins(plugins)
    system, window_list = System(), Windows()
    notifications, file_portal = Notifications(), FilePortal()
    tray, quick, session = Tray(), Quick(), Session()
    clipboard = ClipboardStub()
    recorder = RecorderStub()
    updater = UpdaterStub()
    audio = AudioStub()
    bluetooth = BluetoothStub()
    polkit = PolkitStub()
    engine = QQmlApplicationEngine()
    for name, obj in (("Settings", settings), ("Theme", theme), ("Apps", apps), ("Shell", shell),
                      ("Plugins", plugin_manager), ("System", system), ("Windows", window_list),
                      ("Notifications", notifications), ("FilePortal", file_portal),
                      ("Tray", tray), ("Quick", quick), ("Session", session),
                      ("Clipboard", clipboard), ("Bluetooth", bluetooth), ("Polkit", polkit),
                      ("Recorder", recorder), ("Audio", audio), ("Updater", updater)):
        engine.rootContext().setContextProperty(name, obj)

    failures = []

    def check(condition, what):
        if not condition:
            failures.append(what)
            print("FAIL:", what, file=sys.stderr)

    # Parse (without instantiating) every QML file compiled into the binary.
    # engine.load() below only exercises the files Main.qml eagerly creates,
    # and only under this Qt's parser: newer parsers silently accept syntax
    # that older ones reject (Qt <= 6.8 still reserves the ES3 words, so
    # `const long` once shipped a clock popup that failed to parse when a
    # user started the shell on an older Qt). Parsing each compiled resource
    # here, under the oldest Qt this test runs on, catches every file up
    # front. Plugin widgets are parsed from their on-disk locations.
    qrc_qml = []
    for node in ElementTree.parse(os.path.join(ROOT, "resources.qrc")).getroot().iter("file"):
        name = (node.text or "").strip()
        if name.endswith(".qml"):
            qrc_qml.append(QUrl("qrc:/" + name))
    plugin_qml = [QUrl.fromLocalFile(p) for p in
                  sorted(glob.glob(os.path.join(ROOT, "plugins", "*", "Widget.qml")))]
    parse_errors = []
    for url in qrc_qml + plugin_qml:
        component = QQmlComponent(engine, url)
        parse_errors.extend(error.toString() for error in component.errors())
        del component
    check(not parse_errors,
          f"every QML file parses on Qt {PySide6.QtCore.qVersion()} "
          f"({len(parse_errors)} error(s): {'; '.join(parse_errors[:3])})")

    # The bundled core icons have to stay in step with the QML: every literal
    # iconName in the QML sources needs an icons/<name>.svg in the repository
    # (Shell.iconUrl embeds them through resources.qrc), and every file in
    # icons/ must be listed in resources.qrc - rcc silently skips files that
    # are not listed, which would show as a blank icon at runtime.
    icon_dir = os.path.join(ROOT, "icons")
    qrc_root = ElementTree.parse(os.path.join(ROOT, "resources.qrc")).getroot()
    qrc_files = {(node.text or "").strip() for node in qrc_root.iter("file")}
    qrc_icons = {os.path.basename(name) for name in qrc_files if name.startswith("icons/")}
    literal_icons = set()
    for qml in glob.glob(os.path.join(ROOT, "qml", "*.qml")) + \
               glob.glob(os.path.join(ROOT, "plugins", "*", "Widget.qml")):
        text = open(qml, encoding="utf-8").read()
        # Both `iconName: "x"` (TaskButton and friends) and the `icon:` field
        # of models (the settings pages sidebar) name bundled icons.
        # A quote right before the name means it is inside a string (e.g. the
        # desktop grid keys "icon:<file>"), not a property, so skip it.
        literal_icons.update(re.findall(r'(?<!")\biconName:\s*"([^"]+)"', text))
        literal_icons.update(re.findall(r'(?<!")\bicon:\s*"([^"]+)"', text))
        literal_icons.update(re.findall(r'\bShell\.(?:iconUrl|tintedIcon)\(\s*"([^"]+)"', text))
    missing_files = sorted(name for name in literal_icons
                           if not os.path.exists(os.path.join(icon_dir, name + ".svg")))
    check(not missing_files, f"every literal iconName has an icons/ file (missing: {missing_files})")
    # Every control in the settings application changes something: its key
    # is read by the C++ services, the compositor (through compositor.conf),
    # another QML file or the settings page itself, not only by the control.
    with open(os.path.join(ROOT, "qml", "SettingsApp.qml"), encoding="utf-8") as handle:
        settings_qml = handle.read()
    bound_keys = sorted(set(re.findall(r'settingKey:\s*"([^"]+)"', settings_qml)))
    readers = []
    for pattern in ("src/*.cpp", "src/*.h", "src/session/*.c", "qml/*.qml", "plugins/*/*.qml",
                    "rust/crates/*/src/**/*.rs"):
        for path in glob.glob(os.path.join(ROOT, pattern), recursive=True):
            if path.endswith(("settings.cpp", "SettingsApp.qml")):
                continue
            with open(path, encoding="utf-8") as handle:
                readers.append(handle.read())
    page_reads = re.sub(r'settingKey:\s*"[^"]+"', "", settings_qml)
    dead = [k for k in bound_keys
            if not any(k in text for text in readers) and '"%s"' % k not in page_reads]
    check(not dead, f"every settings control changes something (never read: {dead})")
    # English interface text in QML goes through qsTr(), or the Russian
    # interface shows it untranslated: labels, returned strings and the
    # branches of a ?: that pick a label.
    english = r'[A-Z][a-z]+(?: [a-z]+)*[.!]?'
    label_patterns = [
        re.compile(r'\b(?:text|label|title|subtitle|placeholder|tooltipText)\s*:\s*"(' + english + r')"'),
        re.compile(r'\breturn\s+"(' + english + r')"'),
    ]
    ternary_pattern = re.compile(r'[?:]\s*"(' + english + r')"\s*(?:[:;)]|$)')
    # Names that are the same in every language.
    untranslated = {"NewTeller", "English", "Super", "Sans", "Wayland"}
    bare = []
    for path in sorted(glob.glob(os.path.join(ROOT, "qml", "*.qml"))
                       + glob.glob(os.path.join(ROOT, "plugins", "*", "*.qml"))):
        with open(path, encoding="utf-8") as handle:
            for number, line in enumerate(handle, 1):
                code = line.split("//")[0]
                patterns = label_patterns + ([ternary_pattern] if "?" in code else [])
                for pattern in patterns:
                    for match in pattern.finditer(code):
                        if match.group(1) not in untranslated:
                            bare.append(f"{os.path.basename(path)}:{number} {match.group(1)!r}")
    check(not bare, f"interface text in QML goes through qsTr() (bare: {bare})")

    # Translations (i18n/<language>.json, read by src/translator.cpp): every
    # entry translates a real qsTr() text, keeps its %1 placeholders, and the
    # file is compiled in.
    def qml_unquote(text):
        return text.replace("\\\\", "\x00").replace("\\n", "\n").replace('\\"', '"').replace("\x00", "\\")
    source_texts = set()
    for qml in glob.glob(os.path.join(ROOT, "qml", "*.qml")) + \
               glob.glob(os.path.join(ROOT, "plugins", "*", "*.qml")):
        with open(qml, encoding="utf-8") as handle:
            source_texts.update(qml_unquote(t) for t in
                                re.findall(r'qsTr\("((?:[^"\\]|\\.)*)"\)', handle.read()))
    # C++ messages shown in the UI go through tr("..." "...") (adjacent
    # literals form one string).
    for cpp in glob.glob(os.path.join(ROOT, "src", "*.cpp")):
        with open(cpp, encoding="utf-8") as handle:
            for call in re.findall(r'(?:\btr\(|QCoreApplication::translate\(\s*"[^"]*"\s*,)'
                                   r'\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)', handle.read()):
                parts = re.findall(r'"((?:[^"\\]|\\.)*)"', call)
                source_texts.add(qml_unquote("".join(parts)))
    for path in glob.glob(os.path.join(ROOT, "i18n", "*.json")):
        with open(path, encoding="utf-8") as handle:
            table = json.load(handle)
        name = os.path.basename(path)
        stale = sorted(k for k in table if k not in source_texts)
        check(not stale, f"{name}: every entry is a qsTr() text of the QML (stale: {stale[:5]})")
        broken = sorted(k for k, v in table.items()
                        if sorted(re.findall(r"%\d", k)) != sorted(re.findall(r"%\d", v)))
        check(not broken, f"{name}: translations keep their %N placeholders ({broken[:5]})")
        check("i18n/" + name in qrc_files, f"{name} is listed in resources.qrc")
    on_disk = {os.path.basename(p) for p in glob.glob(os.path.join(icon_dir, "*.svg"))}
    unlisted = sorted(on_disk - qrc_icons)
    check(not unlisted, f"every icons/ file is listed in resources.qrc (unlisted: {unlisted})")

    # Every key the QML reads or the settings pages edit has to exist in the
    # C++ defaults (or the Rust schema): a typo would silently read as
    # undefined and the control would look broken instead of failing loudly.
    cpp_defaults = set(re.findall(r'm_defaults\.insert\(QStringLiteral\("([^"]+)"\)',
                                  open(os.path.join(ROOT, "src", "settings.cpp"),
                                       encoding="utf-8").read()))
    rust_schema = set(re.findall(r'KeySchema \{ key: "([^"]+)"',
                                 open(os.path.join(ROOT, "rust", "crates", "newteller-core",
                                                   "src", "config", "mod.rs"),
                                      encoding="utf-8").read()))
    known_keys = cpp_defaults | rust_schema
    used_keys = set()
    for qml in glob.glob(os.path.join(ROOT, "qml", "*.qml")) + \
               glob.glob(os.path.join(ROOT, "plugins", "*", "Widget.qml")):
        text = open(qml, encoding="utf-8").read()
        used_keys.update(re.findall(r'Settings\.values\[\s*"([^"]+)"\s*\]', text))
        used_keys.update(re.findall(r'(?:Settings\.value|Settings\.setValue|Settings\.reset)\(\s*"([^"]+)"',
                                    text))
        used_keys.update(re.findall(r'settingKey:\s*"([^"]+)"', text))
        used_keys.update(re.findall(r'Settings\.defaults\(\)\["([^"]+)"\]', text))
    unknown_keys = sorted(used_keys - known_keys)
    check(not unknown_keys,
          f"every Settings key used in QML has a default (missing: {unknown_keys})")
    check({"panel.material", "dock.material", "windows.material", "notifications.material"}
          <= known_keys, "the surface material keys are known to the settings core")

    engine.load(QUrl("qrc:/qml/Main.qml"))
    if not engine.rootObjects():
        print("FAIL: qrc:/qml/Main.qml did not load", file=sys.stderr)
        return 1
    QTest.qWait(300)

    check(app.platformName() == "offscreen", f"running on the offscreen platform ({app.platformName()})")
    screens = QGuiApplication.screens()
    check([s.name() for s in screens] == [s["name"] for s in SCREENS], "offscreen platform has the test screens")

    def windows(kind):
        # Match the QML type exactly, so PanelToolTip does not count as Panel.
        # Instantiated types carry _QMLTYPE_<n> and sometimes an extra _QML_<n>.
        return [w for w in QGuiApplication.topLevelWindows()
                if re.match(rf"^{kind}(_QMLTYPE_\d+)?(_QML_?\d+)?$",
                            w.metaObject().className())]

    windows_of_kind = windows

    def attached(kind, window):
        return next((a for a in ATTACHED[kind] if a.window is window), None)

    desktops, panels, menus = windows("Desktop"), windows("Panel"), windows("StartMenu")
    check(len(desktops) == len(screens), f"one desktop per screen (got {len(desktops)})")
    check(len(panels) == len(screens), f"one panel per screen (got {len(panels)})")
    check(len(menus) == 1, f"one start menu (got {len(menus)})")

    for screen in screens:
        g = screen.geometry()
        desk = next((w for w in desktops if w.screen() is screen), None)
        panel = next((w for w in panels if w.screen() is screen), None)
        check(desk is not None and desk.isVisible(), f"visible desktop on {screen.name()}")
        check(panel is not None and panel.isVisible(), f"visible panel on {screen.name()}")
        if desk:
            check(desk.geometry() == g, f"desktop on {screen.name()} covers the screen ({desk.geometry()} vs {g})")
            ls, sw = attached("layershell", desk), attached("shellwindow", desk)
            check(ls is not None and ls.anchors == 15 and ls.layer == 1 and ls.exclusionZone == -1,
                  "desktop layer-shell settings")
            check(sw is not None and sw.role == 1, "desktop has ShellWindow.Desktop")
        if panel:
            want = (g.x(), g.y() + g.height() - 48, g.width(), 48)
            got = (panel.x(), panel.y(), panel.width(), panel.height())
            check(got == want, f"panel on {screen.name()} spans the bottom edge ({got} vs {want})")
            ls, sw = attached("layershell", panel), attached("shellwindow", panel)
            check(ls is not None and ls.anchors == 14 and ls.layer == 2 and ls.exclusionZone == 48
                  and ls.scope == "newteller-panel", "panel layer-shell settings")
            check(sw is not None and sw.role == 2, "panel has ShellWindow.Dock")

    # Clicking Start below also proves each panel's startMenu points at this window.
    menu = menus[0] if menus else None
    if menu is not None and panels:
        primary = screens[0].geometry()
        ls = attached("layershell", menu)
        check(ls is not None and ls.anchors == 6 and ls.keyboardInteractivity == 1,
              f"start menu layer-shell settings ({ls and (ls.anchors, ls.keyboardInteractivity, ls.margins)})")
        check(not menu.isVisible(), "start menu starts hidden")

        panel = next(w for w in panels if w.screen() is screens[0])
        start_item = panel.findChild(QObject, "startButton")
        check(start_item is not None, "the panel exposes its Start button")
        if start_item is not None:
            centre = start_item.mapToScene(QPointF(start_item.width() / 2,
                                                  start_item.height() / 2)).toPoint()
            start_button = QPoint(centre.x(), centre.y())
        else:
            start_button = QPoint(panel.width() // 2, panel.height() // 2)
        QTest.mouseClick(panel, Qt.LeftButton, Qt.NoModifier, start_button)
        QTest.qWait(50)
        check(menu.isVisible(), "clicking Start opens the start menu")
        results = menu.property("results")
        if hasattr(results, "toVariant"):  # QJSValue
            results = results.toVariant()
        check(len(results or []) == len(APPS), "start menu lists the applications")
        # The menu is linked to the Start button that opened it: same card size
        # as before, centred on the button and sitting just above the bar, with
        # the pointer on the edge that faces it.
        bar_top = primary.y() + primary.height() - 48
        card = card_rect(menu)
        check((card[2], card[3]) == (440, 580), f"start menu keeps its card size ({card})")
        check(card[0] <= start_button.x() <= card[0] + card[2],
              f"start menu hangs off the Start button ({card} vs {start_button.x()})")
        check(0 <= bar_top - (menu.y() + menu.height()) <= 8,
              f"start menu sits just above the bar ({bar_top - (menu.y() + menu.height())})")
        check(menu.property("anchored") is True, "the start menu knows its anchor")
        check(menu.property("nubEdge") == "bottom", "the start menu points at the bar")
        ls = attached("layershell", menu)
        check(ls is not None and ls.anchors == 6 and ls.keyboardInteractivity == 1
              and ls.margins.left() == menu.x() - primary.x()
              and ls.margins.bottom() == primary.y() + primary.height() - (menu.y() + menu.height()),
              f"start menu layer-shell margins follow the anchor ({ls and ls.margins} vs x={menu.x()} y={menu.y()} h={menu.height()})")

        QTest.mouseClick(panel, Qt.LeftButton, Qt.NoModifier, start_button)
        QTest.qWait(300)  # the close animation plays before the window hides
        check(not menu.isVisible(), "clicking Start again closes the start menu")

        QMetaObject.invokeMethod(menu, "show")
        QMetaObject.invokeMethod(menu, "launch", Q_ARG("QVariant", APPS[1]))
        QTest.qWait(300)  # hide() animates the card out first
        check(apps.launched == ["org.gnome.Terminal"], f"launching from the menu ({apps.launched})")
        check(not menu.isVisible(), "launching an application closes the menu")
        check(apps.isRunning("org.gnome.Terminal"), "the launched application is marked running")

        # The search is also a calculator.
        for expression, expected in (("2+2*3", 8), ("(15,5-3)/4", 3.125), ("2^10", 1024)):
            got = QMetaObject.invokeMethod(menu, "calculate", Qt.DirectConnection,
                                           Q_RETURN_ARG("QVariant"), Q_ARG("QVariant", expression))
            got = got.toVariant() if hasattr(got, "toVariant") else got
            check(got == expected, f"the start menu calculates {expression} = {expected} ({got})")
        for text in ("firefox", "12", "2+", "alert(1)"):
            got = QMetaObject.invokeMethod(menu, "calculate", Qt.DirectConnection,
                                           Q_RETURN_ARG("QVariant"), Q_ARG("QVariant", text))
            got = got.toVariant() if hasattr(got, "toVariant") else got
            check(got != got, f"{text!r} is not a calculation ({got})")

        # Recently used files whose name matches.
        recent = QMetaObject.invokeMethod(menu, "recentFileMatches", Qt.DirectConnection,
                                          Q_RETURN_ARG("QVariant"), Q_ARG("QVariant", "report"))
        recent = recent.toVariant() if hasattr(recent, "toVariant") else recent
        check(isinstance(recent, list) and len(recent) == 1
              and recent[0].get("filePath", "").endswith("Report 2026.odt"),
              f"the start menu finds recently used files ({recent})")

        # Searching finds settings pages too ("resolution" -> Displays).
        found = QMetaObject.invokeMethod(menu, "settingsMatches", Qt.DirectConnection,
                                         Q_RETURN_ARG("QVariant"), Q_ARG("QVariant", "resolution"))
        found = found.toVariant() if hasattr(found, "toVariant") else found
        check(isinstance(found, list) and len(found) == 1
              and found[0].get("settingsPage") == "display",
              f"the start menu finds the Displays settings page ({found})")
        if isinstance(found, list) and found:
            shell.calls.clear()
            QMetaObject.invokeMethod(menu, "show")
            QMetaObject.invokeMethod(menu, "launch", Q_ARG("QVariant", found[0]))
            QTest.qWait(300)
            check(("openSettingsPage", "display") in shell.calls,
                  f"a settings result opens that page ({shell.calls})")

        # Right-clicking an application row offers pin/unpin; the function
        # writes the same panel.pinned list the dock reads.
        QMetaObject.invokeMethod(menu, "togglePin", Q_ARG("QVariant", {"id": "org.gnome.Nautilus"}))
        check("org.gnome.Nautilus" in settings._values["panel.pinned"],
              f"pinning writes panel.pinned ({settings._values['panel.pinned']})")
        QMetaObject.invokeMethod(menu, "togglePin", Q_ARG("QVariant", {"id": "org.gnome.Nautilus"}))
        check("org.gnome.Nautilus" not in settings._values["panel.pinned"],
              f"unpinning removes the id again ({settings._values['panel.pinned']})")

        # Shut down asks first; closing the menu cancels, confirming runs it.
        QMetaObject.invokeMethod(menu, "show")
        QTest.qWait(50)
        confirm = menu.findChild(QObject, "powerConfirm")
        shell.calls.clear()
        QMetaObject.invokeMethod(menu, "askPower", Q_ARG("QVariant", "powerOff"))
        QTest.qWait(50)
        check(confirm is not None and confirm.property("visible") and ("powerOff",) not in shell.calls,
              "Shut down asks for confirmation instead of shutting down at once")
        QMetaObject.invokeMethod(menu, "hide")
        QTest.qWait(400)
        check(menu.property("pendingPower") == "" and ("powerOff",) not in shell.calls,
              "closing the start menu cancels the shutdown")
        QMetaObject.invokeMethod(menu, "show")
        QTest.qWait(50)
        QMetaObject.invokeMethod(menu, "askPower", Q_ARG("QVariant", "powerOff"))
        QMetaObject.invokeMethod(menu, "runPower")
        QTest.qWait(50)
        check(("powerOff",) in shell.calls, f"confirming shuts down ({shell.calls})")
        QTest.qWait(400)

        # The same menu puts a copy of the application's launcher on the desktop.
        menu.setProperty("pinTarget", {"id": "org.gnome.Nautilus",
                                       "path": "/usr/share/applications/org.gnome.Nautilus.desktop"})
        add_button = menu.findChild(QObject, "startAddToDesktop")
        check(add_button is not None, "the application menu offers Add to desktop")
        if add_button is not None:
            shell.calls.clear()
            QMetaObject.invokeMethod(add_button, "clicked")
            check(("addToDesktop", "/usr/share/applications/org.gnome.Nautilus.desktop") in shell.calls,
                  f"Add to desktop copies the application's launcher ({shell.calls})")

        # Highlighted fuzzy search (StartMenu shows StyledText hit highlights).
        search = menu.findChild(QObject, "startSearch")
        check(search is not None, "start menu search field")
        if search is not None:
            search.setProperty("text", "term")
            QTest.qWait(20)
            results = menu.property("results")
            if hasattr(results, "toVariant"):
                results = results.toVariant()
            check(len(results or []) == 1 and results[0]["id"] == "org.gnome.Terminal",
                  f"search filters the application list ({results})")
            hl = (results[0].get("highlight", "") if results else "")
            check("<font" in hl and "Term" in hl, f"search hit is highlighted ({hl})")
            search.setProperty("text", "nothing-matches-this")
            QTest.qWait(20)
            results = menu.property("results")
            if hasattr(results, "toVariant"):
                results = results.toVariant()
            check(len(results or []) == 0, f"no stale results for a bad query ({results})")
            search.setProperty("text", "")
            QTest.qWait(20)

    # Run dialog (Alt+F2).
    dialogs = windows("RunDialog")
    check(len(dialogs) == 1, f"one run dialog (got {len(dialogs)})")
    if dialogs:
        dialog = dialogs[0]
        check(not dialog.isVisible(), "run dialog starts hidden")
        QMetaObject.invokeMethod(dialog, "show")
        QTest.qWait(50)
        check(dialog.isVisible(), "showing the run dialog")
        results = dialog.property("results")
        if hasattr(results, "toVariant"):
            results = results.toVariant()
        check(len(results or []) == len(APPS), f"run dialog lists the applications ({results})")
        rq = dialog.findChild(QObject, "runQuery")
        check(rq is not None, "run dialog query field")
        if rq is not None:
            rq.setProperty("text", "files")
            QTest.qWait(20)
            results = dialog.property("results")
            if hasattr(results, "toVariant"):
                results = results.toVariant()
            check(len(results or []) == 1 and results[0]["id"] == "org.gnome.Nautilus",
                  f"run dialog query filters ({results})")
            # History: Up walks back through the remembered commands. QTest
            # wants a window, not an item; the input holds the focus.
            QTest.keyClick(dialog, Qt.Key_Up)
            QTest.qWait(20)
            check(rq.property("text") == "cmatrix",
                  f"Up recalls the newest command ({rq.property('text')})")
            QTest.keyClick(dialog, Qt.Key_Up)
            QTest.qWait(20)
            check(rq.property("text") == "htop",
                  f"Up again reaches the older command ({rq.property('text')})")
            QTest.keyClick(dialog, Qt.Key_Down)
            QTest.qWait(20)
            check(rq.property("text") == "cmatrix",
                  f"Down walks forward again ({rq.property('text')})")
            # A bare command runs through the shell and lands in the history.
            rq.setProperty("text", "nload")
            QTest.qWait(20)
            QMetaObject.invokeMethod(dialog, "runCurrent")
            QTest.qWait(300)  # the hide animation fades the card out first
            check(("runShell", "nload") in shell.calls,
                  f"an unknown command runs through the shell ({shell.calls})")
            check(settings._values["run.history"][0] == "nload",
                  f"the command is remembered ({settings._values['run.history']})")
            check(not dialog.isVisible(), "closing the run dialog")
            # A web address and a folder open instead of running as commands.
            for typed, expected in (("https://example.org", ("openUrl", "https://example.org")),
                                    ("~", ("openPath", "~"))):
                QMetaObject.invokeMethod(dialog, "show")
                QTest.qWait(50)
                rq.setProperty("text", typed)
                QTest.qWait(20)
                shell.calls.clear()
                QMetaObject.invokeMethod(dialog, "runCurrent")
                QTest.qWait(300)
                check(expected in shell.calls and not any(c[0] == "runShell" for c in shell.calls),
                      f"{typed!r} in the run dialog opens it ({shell.calls})")

    # Desktop right-click menu.
    if desktops:
        desk = desktops[0]
        g = desk.screen().geometry()
        check(not desk.property("contextMenuVisible"), "desktop context menu starts hidden")
        QTest.mouseClick(desk, Qt.RightButton, Qt.NoModifier,
                         QPoint(g.width() - 200, g.height() - 200))
        QTest.qWait(20)
        check(desk.property("contextMenuVisible") is True,
              "right-clicking the desktop opens its menu")
        desk.setProperty("contextMenuVisible", False)

        # Right-clicking an icon opens the icon's own menu: open, copy the
        # path, move to the trash (restorable, unlike a delete).
        desk_items = []

        def collect(item):
            desk_items.append(item)
            for child in item.childItems():
                collect(child)
        collect(desk.contentItem())
        icons = [i for i in desk_items if isinstance(i, QQuickItem)
                 and i.objectName() == "desktopIcon" and i.isVisible()]
        icon_menu = desk.findChild(QQuickItem, "iconMenu")
        check(bool(icons) and icon_menu is not None, f"desktop icons and their menu exist ({len(icons)})")
        # A picture shows itself (loaded asynchronously).
        thumbs = []
        for _ in range(20):
            QTest.qWait(50)
            desk_items.clear()
            collect(desk.contentItem())
            thumbs = [i for i in desk_items if isinstance(i, QQuickItem)
                      and i.objectName() == "desktopThumbnail" and i.isVisible()]
            if thumbs:
                break
        check(len(thumbs) == 1, f"a picture on the desktop shows as its thumbnail ({len(thumbs)})")
        if icons and icon_menu is not None:
            target = icons[0]
            url = target.property("fileUrl").toString()
            spot = target.mapToScene(QPointF(target.width() / 2, target.height() / 2))
            QTest.mouseClick(desk, Qt.RightButton, Qt.NoModifier, QPoint(int(spot.x()), int(spot.y())))
            QTest.qWait(50)
            check(icon_menu.isVisible() and not desk.property("contextMenuVisible"),
                  "right-clicking an icon opens its menu, not the desktop's")
            # Rename: the name turns into an editor with the name selected
            # up to its extension; Enter renames the file.
            QMetaObject.invokeMethod(desk, "openIconMenu", Q_ARG("QVariant", url),
                                     Q_ARG("QVariant", 100), Q_ARG("QVariant", 100), Q_ARG("QVariant", ""))
            QMetaObject.invokeMethod(desk, "iconMenuAction", Q_ARG("QVariant", "rename"))
            QTest.qWait(50)
            editors = []

            def find_editors(item):
                if item.objectName() == "renameInput" and item.isVisible():
                    editors.append(item)
                for child in item.childItems():
                    find_editors(child)
            find_editors(desk.contentItem())
            check(len(editors) == 1, f"Rename shows one name editor ({len(editors)})")
            if editors:
                name = url.rsplit("/", 1)[-1]
                check(editors[0].property("text") == QUrl(url).fileName(),
                      f"the editor holds the file name ({editors[0].property('text')!r})")
                shell.calls.clear()
                editors[0].setProperty("text", "Renamed.txt")
                QMetaObject.invokeMethod(editors[0], "accepted")
                QTest.qWait(50)
                check(("renameFile", url, "Renamed.txt") in shell.calls
                      and desk.property("renamingUrl") == "",
                      f"Enter renames the file and closes the editor ({shell.calls})")
            QMetaObject.invokeMethod(desk, "openIconMenu", Q_ARG("QVariant", url),
                                     Q_ARG("QVariant", 100), Q_ARG("QVariant", 100), Q_ARG("QVariant", ""))
            shell.calls.clear()
            QMetaObject.invokeMethod(desk, "iconMenuAction", Q_ARG("QVariant", "trash"))
            check(("moveToTrash", url) in shell.calls and not icon_menu.isVisible(),
                  f"Move to Trash sends the icon's file to the trash ({shell.calls})")

        # An icon placed with the old pixel setting (desktop.iconPositions,
        # before the grid) keeps its place as the matching grid cell.
        left, top = desk.property("gridLeft"), desk.property("gridTop")
        cw, ch = desk.property("cellWidth"), desk.property("cellHeight")
        settings.setValue("desktop.autoArrangeIcons", False)
        settings.setValue("desktop.iconPositions",
                          {"Projects": {"x": left + 3 * cw, "y": top + 2 * ch}})
        QTest.qWait(120)
        cells = desk.property("gridCells")
        if hasattr(cells, "toVariant"):
            cells = cells.toVariant()
        cell = (cells or {}).get("icon:Projects", {})
        check(cell.get("col") == 3 and cell.get("row") == 2,
              f"an icon position from before the grid becomes its grid cell ({cell})")
        settings.setValue("desktop.iconPositions", {})
        settings.setValue("desktop.autoArrangeIcons", True)
        QTest.qWait(60)

    # The dock: every running window becomes a dock entry, grouped per app.
    if panels:
        panel = next(w for w in panels if w.screen() is screens[0])
        entries = panel.property("dockEntries")
        if hasattr(entries, "toVariant"):
            entries = entries.toVariant()
        check(isinstance(entries, list) and len(entries) == 3,
              f"the dock merges windows and pinned apps into three entries ({entries})")
        by_app = {e["appId"]: e for e in (entries or [])}
        firefox = by_app.get("firefox", {})
        check(firefox.get("running") is True and firefox.get("active") is True
              and len(firefox.get("windows", [])) == 1
              and firefox["windows"][0]["id"] == "w1",
              f"the firefox dock entry carries its window and focus ({firefox})")
        terminal = by_app.get("org.gnome.Terminal", {})
        check(terminal.get("windows", [{}])[0].get("id") == "w2",
              f"the terminal dock entry carries its window ({terminal})")

        # Clicking a dock button activates the focused window of that app.
        QMetaObject.invokeMethod(panel, "activateEntry", Q_ARG("QVariant", firefox))
        check(("activate", "w1") in window_list.calls,
              f"activating a dock entry talks to the window list ({window_list.calls})")

        # A read-only toplevel (ext_foreign_toplevel_list, GNOME) cannot be
        # activated: clicking its entry launches the matched application.
        nautilus = by_app.get("org.gnome.Nautilus", {})
        check(len(nautilus.get("windows", [])) == 1
              and nautilus["windows"][0].get("readOnly") is True,
              f"the read-only window is listed in the dock ({nautilus})")
        window_list.calls.clear()
        QMetaObject.invokeMethod(panel, "activateEntry", Q_ARG("QVariant", nautilus))
        check(("activate", "e1") not in window_list.calls
              and apps.launched and apps.launched[-1] == "org.gnome.Nautilus",
              f"a read-only entry launches its application instead ({window_list.calls}, "
              f"{apps.launched})")

        # One button per window: a second window of an application sits right
        # after the first one, not at the far end of the dock.
        window_list._windows.append(
            {"id": "w3", "title": "Firefox — second", "appId": "firefox",
             "active": False, "minimized": False, "maximized": False})
        window_list.windowsChanged.emit()
        QTest.qWait(20)
        grown = panel.property("dockEntries")
        if hasattr(grown, "toVariant"):
            grown = grown.toVariant()
        order = [e["windows"][0]["id"] if e.get("windows") else e["appId"] for e in (grown or [])]
        check("w1" in order and "w3" in order and order.index("w3") == order.index("w1") + 1,
              f"a second window of one application gets the button next to the first ({order})")
        window_list._windows.pop()
        window_list.windowsChanged.emit()
        QTest.qWait(20)

        # The dock buttons must have real geometry, not zero-height items: a
        # Flickable without an explicit height once collapsed the whole strip
        # (the dock looked empty although the entries were all there).
        def walk_items(item, out):
            out.append(item)
            for child in item.childItems():
                walk_items(child, out)

        content = panel.property("contentItem")
        items = []
        if content is not None:
            walk_items(content, items)
        # Inline components (`component StartButton: TaskButton`) get their own
        # class name, so both spellings have to be matched here.
        def is_task_button(i):
            name = i.metaObject().className()
            return "TaskButton" in name or "StartButton" in name

        buttons = [i for i in items if isinstance(i, QQuickItem) and is_task_button(i)]
        check(len(buttons) >= 2,
              f"the dock strip instantiates its buttons ({len(buttons)} found)")
        collapsed = [f"{b.objectName() or b.property('iconName')}:"
                     f"{b.width():.0f}x{b.height():.0f}" for b in buttons if b.height() < 16]
        check(not collapsed, f"dock buttons have real geometry (collapsed: {collapsed})")
        if collapsed:
            for b in buttons:
                chain = []
                p = b.parentItem()
                while p is not None:
                    chain.append(p.metaObject().className().split("_QML")[0] + f"({p.width():.0f}x{p.height():.0f})")
                    p = p.parentItem()
                print("    btn", b.objectName() or b.property("iconName"),
                      "icon=", repr(b.property("iconName")),
                      "size=", b.width(), "x", b.height(),
                      "implicitH=", b.property("implicitHeight"),
                      "chain=", " > ".join(chain))
        start = next((b for b in buttons if b.objectName() == "startButton"), None)
        check(start is not None and start.height() >= 16,
              "the start button keeps its geometry")

        # A bigger icon size makes the bar grow instead of clipping the icons.
        settings.setValue("dock.iconSize", 44)
        QTest.qWait(150)
        check(panel.property("barThickness") >= 64,
              f"the bar grows with the dock icon size ({panel.property('barThickness')})")
        settings.setValue("dock.iconSize", 28)
        QTest.qWait(150)

        # A vertical dock: one column of square buttons inside the bar.
        settings.setValue("panel.position", "left")
        QTest.qWait(300)
        items = []
        if panel.property("contentItem") is not None:
            walk_items(panel.property("contentItem"), items)
        column_buttons = [i for i in items if isinstance(i, QQuickItem) and is_task_button(i)
                          and i.parentItem() is not None
                          and i.parentItem().metaObject().className() == "QQuickColumn"]
        check(len(column_buttons) >= 2,
              f"a vertical dock lists its buttons in one column ({len(column_buttons)})")
        bad = [f"{b.width():.0f}x{b.height():.0f}" for b in column_buttons
               if abs(b.width() - b.height()) > 1 or b.width() < 16
               or b.width() > panel.property("barThickness")]
        check(not bad, f"vertical dock buttons are square and fit the bar ({bad})")
        settings.setValue("panel.position", "bottom")
        QTest.qWait(300)

    # Desktop widgets are laid out so that none covers another.
    QTest.qWait(200)
    for desktop in desktops[:1]:
        root_item = desktop.property("contentItem")
        widgets = []
        if root_item is not None:
            stack = [root_item]
            while stack:
                item = stack.pop()
                if (item.objectName() in ("desktopWidget", "desktopIcon")
                        and item.width() > 0 and item.height() > 0 and item.isVisible()):
                    widgets.append(item)
                stack.extend(item.childItems())
        overlaps = []
        for i, a in enumerate(widgets):
            for b in widgets[i + 1:]:
                if (a.x() < b.x() + b.width() and b.x() < a.x() + a.width()
                        and a.y() < b.y() + b.height() and b.y() < a.y() + a.height()):
                    overlaps.append((a.property("gridKey"), b.property("gridKey")))
        check(len(widgets) >= 2, f"the desktop shows its widgets and icons ({len(widgets)})")
        check(not overlaps, f"desktop widgets and icons do not overlap ({overlaps})")
        offgrid = [w.property("gridKey") for w in widgets if w.x() < -1000]
        check(not offgrid, f"every desktop element has a grid cell ({offgrid})")

        launcher = next((w for w in widgets if w.property("gridKey") == "icon:firefox.desktop"), None)
        check(launcher is not None and launcher.property("label") == "Firefox"
              and launcher.property("resolvedIconName") == "firefox",
              "a launcher on the desktop shows its application's name and icon "
              f"({launcher and launcher.property('label')}, "
              f"{launcher and launcher.property('resolvedIconName')})")
        if launcher is not None:
            centre = launcher.mapToScene(QPointF(launcher.width() / 2, 10)).toPoint()
            apps.launched.clear()
            QTest.mouseDClick(desktop, Qt.LeftButton, Qt.NoModifier, centre)
            QTest.qWait(20)
            check(apps.launched == ["firefox"],
                  f"double-clicking a launcher starts its application ({apps.launched})")

    # One dock menu per panel.
    dock_menus = windows_of_kind("DockMenu")
    check(len(dock_menus) == len(panels),
          f"one dock menu per panel ({len(dock_menus)} vs {len(panels)})")

    # The clock popup lives with its panel and starts hidden.
    clock_popups = windows_of_kind("ClockPopup")
    check(len(clock_popups) == len(panels),
          f"one clock popup per panel ({len(clock_popups)} vs {len(panels)})")
    if clock_popups:
        check(not clock_popups[0].isVisible(), "the clock popup starts hidden")
        # 1 November 2026 is a Sunday: the last cell of the first row for a
        # Monday-first locale, the first cell for a Sunday-first one.
        first = clock_popups[0].property("firstWeekday")
        cells = QMetaObject.invokeMethod(clock_popups[0], "buildCalendar", Qt.DirectConnection,
                                         Q_RETURN_ARG("QVariant"),
                                         Q_ARG("QVariant", 2026), Q_ARG("QVariant", 10))
        cells = cells.toVariant() if hasattr(cells, "toVariant") else cells
        expected = {1: 6, 0: 0}.get(first)
        check(expected is not None and cells and cells.index(1) == expected and len(cells) % 7 == 0,
              f"the calendar starts on the locale's first weekday ({first}: {cells[:8] if cells else cells})")

    # One tooltip window per panel, hidden until a button is hovered.
    tooltips = windows_of_kind("PanelToolTip")
    check(len(tooltips) == len(panels),
          f"one panel tooltip per panel ({len(tooltips)} vs {len(panels)})")
    check(all(not t.isVisible() for t in tooltips), "panel tooltips start hidden")

    # The tray: the stand-in watcher reports one item, the panels render it.
    if panels:
        panel = next(w for w in panels if w.screen() is screens[0])
        content = panel.property("contentItem")
        items = []
        if content is not None:
            walk_items(content, items)
        tray_buttons = [i for i in items
                        if isinstance(i, QQuickItem) and i.objectName() == "trayButton"]
        check(len(tray_buttons) == 1,
              f"the tray renders the watcher's items ({len(tray_buttons)})")
        collapsed = [f"{b.width():.0f}x{b.height():.0f}" for b in tray_buttons if b.height() < 16]
        check(not collapsed, f"tray buttons have real geometry (collapsed: {collapsed})")

    # The notification centre: one window on the primary screen; the bell
    # badge counts the history entries newer than the last seen timestamp.
    centers = windows_of_kind("NotificationCenter")
    check(len(centers) == 1, f"one notification centre (got {len(centers)})")
    if centers:
        center = centers[0]
        check(not center.isVisible(), "the notification centre starts hidden")
        QMetaObject.invokeMethod(center, "toggle")
        QTest.qWait(300)  # the open animation plays
        check(center.isVisible(), "toggling opens the notification centre")
        avatar = QImage(8, 8, QImage.Format_RGB32)
        avatar.fill(QColor("#33aa66"))
        avatar_png = QByteArray()
        avatar_buffer = QBuffer(avatar_png)
        avatar_buffer.open(QIODevice.WriteOnly)
        avatar.save(avatar_buffer, "PNG")
        notifications._history = [
            {"id": 1, "app": "Mail", "summary": "Hello", "body": "First", "time": 1000,
             "icon": "bell", "image": ""},
            {"id": 2, "app": "Chat", "summary": "Yo", "body": "Second", "time": 2000, "icon": "",
             "image": "data:image/png;base64," + bytes(avatar_png.toBase64()).decode()},
        ]
        notifications.historyChanged.emit()
        QTest.qWait(300)
        # A row shows the notification's picture, as its popup did.
        center_items = []
        walk_items(center.contentItem(), center_items)
        pictures = [i for i in center_items if isinstance(i, QQuickItem)
                    and i.objectName() == "historyImage" and i.isVisible()]
        check(len(pictures) == 1 and pictures[0].property("implicitWidth") > 0,
              f"the history shows a notification's picture ({len(pictures)})")
        # Grouped by application, the one with the newest notification first.
        notifications._history.append({"id": 3, "app": "Mail", "summary": "Again", "body": "Third",
                                       "time": 3000, "icon": "bell", "image": ""})
        notifications.historyChanged.emit()
        QTest.qWait(100)
        grouped = center.property("entries")
        grouped = grouped.toVariant() if hasattr(grouped, "toVariant") else grouped
        check([(e.get("app"), bool(e.get("groupStart"))) for e in (grouped or [])]
              == [("Mail", True), ("Mail", False), ("Chat", True)],
              f"the notification centre groups by application ({grouped})")
        notifications._history.pop()
        notifications.historyChanged.emit()
        QTest.qWait(50)
        # Everything on screen counts as seen; closing keeps the mark.
        check(int(center.property("unreadCount")) == 0,
              f"an open centre counts as seen ({center.property('unreadCount')})")
        QMetaObject.invokeMethod(center, "hide")
        QTest.qWait(300)
        check(not center.isVisible(), "closing the notification centre")
        center.setProperty("lastSeen", 0)
        QTest.qWait(50)
        check(int(center.property("unreadCount")) == 2,
              f"the badge counts history newer than lastSeen ({center.property('unreadCount')})")

    # The quick settings card behind the indicators button.
    quick_popups = windows_of_kind("QuickSettingsPopup")
    check(len(quick_popups) == 1, f"one quick settings popup (got {len(quick_popups)})")
    if quick_popups:
        quick_popup = quick_popups[0]
        check(not quick_popup.isVisible(), "the quick settings popup starts hidden")
        QMetaObject.invokeMethod(quick_popup, "toggle")
        QTest.qWait(300)
        check(quick_popup.isVisible(), "toggling opens the quick settings popup")
        check(("refresh",) in quick.calls, "opening refreshes volume and brightness")
        check(quick_popup.height() >= 240, "the quick settings popup has content height")
        art = quick_popup.findChild(QQuickItem, "mediaArt")
        check(art is not None and art.isVisible() and art.property("implicitWidth") > 0,
              "the media card shows the track's cover")
        QMetaObject.invokeMethod(quick_popup, "hide")
        QTest.qWait(300)
        check(not quick_popup.isVisible(), "closing the quick settings popup")

    # --- dropdowns are linked to the widget that toggles them --------------
    # Every panel button hands its own rect to the card it opens: the card
    # opens next to that button (on whichever screen edge the bar sits), keeps
    # its pointer on the edge facing the bar, follows the button when the
    # anchor changes and closes again with Escape. Only one card is open at a
    # time, so opening the bell closes the start menu and so on.
    if panels and menus:
        panel = next(w for w in panels if w.screen() is screens[0])
        geometry = screens[0].geometry()
        bar_top = geometry.y() + geometry.height() - 48
        menu = menus[0]
        centres = windows_of_kind("NotificationCenter")
        quicks = windows_of_kind("QuickSettingsPopup")
        clocks = [c for c in windows_of_kind("ClockPopup") if c.screen() is screens[0]]
        dock_menus = [m for m in windows_of_kind("DockMenu") if m.screen() is screens[0]]
        tray_menus = [m for m in windows_of_kind("TrayMenu") if m.screen() is screens[0]]

        def press(name, button=Qt.LeftButton):
            """Click a panel button by its objectName; returns its centre."""
            item = panel.findChild(QObject, name)
            check(item is not None, f"the panel exposes {name}")
            if item is None:
                return None
            centre = item.mapToScene(QPointF(item.width() / 2, item.height() / 2))
            QTest.mouseClick(panel, button, Qt.NoModifier,
                             QPoint(int(centre.x()), int(centre.y())))
            QTest.qWait(300)
            return centre

        def on_screen(win, label):
            card = card_rect(win)
            check(card[0] >= geometry.x() and card[1] >= geometry.y()
                  and card[0] + card[2] <= geometry.x() + geometry.width()
                  and card[1] + card[3] <= geometry.y() + geometry.height(),
                  f"{label} stays on the screen ({card})")
            return card

        # The bell opens the notification centre right under itself.
        bell = press("bellButton")
        if bell is not None and centres and quicks:
            center = centres[0]
            check(center.isVisible(), "the bell opens the notification centre")
            check(not quicks[0].isVisible(), "opening the centre closes quick settings")
            check(panel.property("dropdownOpen") is True,
                  "the panel knows one of its dropdowns is open")
            card = on_screen(center, "the notification centre")
            check((card[2], card[3]) == (400, 520), f"the centre keeps its size ({card})")
            check(card[0] <= bell.x() <= card[0] + card[2],
                  f"the centre hangs off its bell ({card} vs {bell.x():.0f})")
            check(0 <= bar_top - (center.y() + center.height()) <= 8,
                  f"the centre sits just above the bar ({bar_top - (center.y() + center.height())})")
            check(center.property("nubEdge") == "bottom", "the centre points at the bar")
            ls = attached("layershell", center)
            check(ls is not None and ls.anchors == 6
                  and ls.margins.left() == center.x() - geometry.x()
                  and ls.margins.bottom() == geometry.y() + geometry.height() - (center.y() + center.height()),
                  f"the centre's layer margins follow it ({ls and ls.margins})")

            # Quick settings replaces it in place: never two cards at once.
            indicators = press("indicatorsButton")
            check(quicks[0].isVisible(), "the indicators button opens quick settings")
            check(not center.isVisible(), "opening quick settings closes the centre")
            if indicators is not None:
                card = on_screen(quicks[0], "quick settings")
                check(card[0] <= indicators.x() <= card[0] + card[2],
                      f"quick settings hangs off its button ({card} vs {indicators.x():.0f})")
                check(0 <= bar_top - (quicks[0].y() + quicks[0].height()) <= 8,
                      "quick settings sits just above the bar")

        # The clock card, and Escape.
        clock_button = press("clockButton")
        if clock_button is not None and clocks:
            clock = clocks[0]
            check(clock.isVisible(), "the clock button opens the clock card")
            card = on_screen(clock, "the clock card")
            check(card[0] <= clock_button.x() <= card[0] + card[2],
                  f"the clock card hangs off its button ({card} vs {clock_button.x():.0f})")
            check(clock.property("nubEdge") == "bottom", "the clock card points at the bar")
            check(clock.isActive(), "an open dropdown takes the keyboard focus")
            QTest.keyClick(clock, Qt.Key_Escape)
            QTest.qWait(300)
            check(not clock.isVisible(), "Escape closes the dropdown")

            # An open card follows a new anchor (another button, another
            # screen, a bar that changed edge) instead of staying put.
            QMetaObject.invokeMethod(clock, "show")
            QTest.qWait(300)
            before = (clock.x(), clock.y())
            clock.setProperty("anchor", QRectF(geometry.x() + 300, bar_top, 40, 48))
            QTest.qWait(50)
            card = card_rect(clock)
            check((clock.x(), clock.y()) != before, "an open card follows a new anchor")
            check(abs((card[0] + card[2] / 2) - (geometry.x() + 320)) <= 2,
                  f"the card centres on the new anchor ({card[0] + card[2] / 2:.0f} vs 320)")

            # A left bar puts the card beside the bar, pointer on its left.
            clock.setProperty("panelPosition", "left")
            clock.setProperty("anchor", QRectF(geometry.x(), 500, 48, 40))
            QTest.qWait(50)
            card = on_screen(clock, "a card on a left bar")
            check(clock.property("nubEdge") == "left",
                  f"a left bar points the card sideways ({clock.property('nubEdge')})")
            check(clock.x() == geometry.x() + 48 + 2,
                  f"the card opens beside a left bar ({clock.x()})")
            check(abs((card[1] + card[3] / 2) - 520) <= 2,
                  f"and is centred on its button ({card[1] + card[3] / 2:.0f})")

            # A top bar opens the card below the bar.
            clock.setProperty("panelPosition", "top")
            clock.setProperty("anchor", QRectF(geometry.x() + 900, geometry.y(), 60, 48))
            QTest.qWait(50)
            card = on_screen(clock, "a card on a top bar")
            check(clock.property("nubEdge") == "top",
                  f"a top bar points the card down ({clock.property('nubEdge')})")
            check(clock.y() == geometry.y() + 48 + 2,
                  f"the card opens below a top bar ({clock.y()})")

            # Near a screen corner the card is clamped, and the pointer slides
            # along the card edge to stay in front of the button.
            clock.setProperty("panelPosition", "bottom")
            clock.setProperty("anchor", QRectF(geometry.x() + geometry.width() - 10, bar_top, 20, 48))
            QTest.qWait(50)
            card = on_screen(clock, "a card near the screen corner")
            check(card[0] + card[2] <= geometry.x() + geometry.width(),
                  f"the card is clamped into the screen ({card})")
            offset = float(clock.property("nubOffset"))
            check(0 <= offset <= card[2], f"the pointer stays on the card ({offset})")

            QMetaObject.invokeMethod(clock, "hide")
            QTest.qWait(300)
            check(not clock.isVisible(), "the clock card closes again")

        # A menu opened without a button (keyboard shortcut, D-Bus) falls back
        # to the start of the bar instead of a stray corner.
        menu.setProperty("anchor", QRectF())
        QMetaObject.invokeMethod(menu, "show")
        QTest.qWait(300)
        card = on_screen(menu, "a start menu without a button")
        check(menu.x() == geometry.x() + 6,
              f"an unanchored menu falls back to the bar's start ({menu.x()})")
        check(menu.property("nubEdge") == "", "an unanchored menu draws no pointer")
        QMetaObject.invokeMethod(menu, "hide")
        QTest.qWait(300)

        # The dock menu and the tray menu take the same rect from their icons.
        if dock_menus:
            entries = panel.property("dockMainEntries")
            if hasattr(entries, "toVariant"):  # QJSValue
                entries = entries.toVariant()
            if entries:
                rect = QRectF(geometry.x() + 500, bar_top, 40, 48)
                QMetaObject.invokeMethod(dock_menus[0], "openFor",
                                         Q_ARG("QVariant", entries[0]), Q_ARG("QVariant", rect))
                QTest.qWait(300)
                check(dock_menus[0].isVisible(), "opening a dock menu for an application")
                card = on_screen(dock_menus[0], "the dock menu")
                check(card[0] <= 520 <= card[0] + card[2],
                      f"the dock menu hangs off its icon ({card})")
                check(0 <= bar_top - (dock_menus[0].y() + dock_menus[0].height()) <= 8,
                      "the dock menu sits just above the bar")
                check(dock_menus[0].property("nubEdge") == "bottom",
                      "the dock menu points at the icon")
                app_actions = (entries[0].get("app") or {}).get("actions") or []
                if app_actions:
                    rows = []
                    stack = [dock_menus[0].contentItem()]
                    while stack:
                        item = stack.pop()
                        if item.objectName() == "dockMenuAction":
                            rows.append(item)
                        stack.extend(item.childItems())
                    check(len(rows) == len(app_actions),
                          f"the dock menu offers the application's actions ({len(rows)})")
                QMetaObject.invokeMethod(dock_menus[0], "close")
                QTest.qWait(300)
                check(not dock_menus[0].isVisible(), "the dock menu closes again")

        if tray_menus:
            rect = QRectF(geometry.x() + 700, bar_top, 26, 48)
            # An item whose menu is empty opens nothing.
            QMetaObject.invokeMethod(tray_menus[0], "openFor",
                                     Q_ARG("QVariant", "item-empty"), Q_ARG("QVariant", rect))
            QTest.qWait(300)
            check(not tray_menus[0].isVisible(), "an empty tray menu does not open")
            QMetaObject.invokeMethod(tray_menus[0], "openFor",
                                     Q_ARG("QVariant", "item-1"), Q_ARG("QVariant", rect))
            QTest.qWait(300)
            check(("requestMenu", "item-1") in tray.calls, "the tray menu is requested, not fetched blocking")
            check(tray_menus[0].isVisible(), "opening the tray menu for an item")
            current = tray_menus[0].property("currentItems")
            current = current.toVariant() if hasattr(current, "toVariant") else current
            check(len(current) == 2, f"the tray menu shows the application's entries ({len(current)})")
            card = on_screen(tray_menus[0], "the tray menu")
            check(card[0] <= 713 <= card[0] + card[2],
                  f"the tray menu hangs off its icon ({card})")
            # The application changes its menu while it is open.
            tray.menus["item-1"] = tray.menus["item-1"][:1]
            tray.menuChanged.emit("item-1")
            QTest.qWait(300)
            current = tray_menus[0].property("currentItems")
            current = current.toVariant() if hasattr(current, "toVariant") else current
            check(len(current) == 1, f"an open tray menu follows the application's updates ({len(current)})")
            QMetaObject.invokeMethod(tray_menus[0], "close")
            QTest.qWait(300)
            check(not tray_menus[0].isVisible(), "the tray menu closes again")

        # The window menu (title bar right click, Alt+Space) opens where the
        # compositor says and sends its actions back over Session.
        window_menus = windows_of_kind("WindowMenu")
        check(len(window_menus) == 1, f"one window menu ({len(window_menus)})")
        if window_menus:
            wmenu = window_menus[0]
            session.calls.clear()
            session.windowMenuRequested.emit(7, geometry.x() + 300, geometry.y() + 120, 1, "x",
                                             "Notes - Editor")
            QTest.qWait(300)
            check(wmenu.isVisible(), "a window menu request opens the window menu")
            card = on_screen(wmenu, "the window menu")
            check(card[0] <= geometry.x() + 300 <= card[0] + card[2]
                  and card[1] >= geometry.y() + 120 - 2,
                  f"the window menu hangs below the pointer ({card})")
            check(wmenu.property("isMaximized") and not wmenu.property("isOnTop"),
                  "the window menu reads the window's state")
            QMetaObject.invokeMethod(wmenu, "run", Q_ARG("QVariant", "onTop"),
                                     Q_ARG("QVariant", 0))
            QTest.qWait(300)
            check(("setWindowAlwaysOnTop", 7, True) in session.calls,
                  f"Always on top is sent to the compositor ({session.calls})")
            check(not wmenu.isVisible(), "the window menu closes after an action")
            session.windowMenuRequested.emit(7, geometry.x() + 300, geometry.y() + 120, 1, "t", "")
            QTest.qWait(300)
            QMetaObject.invokeMethod(wmenu, "run", Q_ARG("QVariant", "desktop"),
                                     Q_ARG("QVariant", 2))
            QTest.qWait(300)
            check(("moveWindow", 7, 2) in session.calls,
                  f"the window menu moves the window to another desktop ({session.calls})")

        # A tray icon that refuses Activate starts the application behind it.
        before = len(apps.launched)
        tray.activateFailed.emit("item-1", ["no-such-thing", "firefox"])
        QTest.qWait(100)
        check(apps.launched[before:] == ["firefox"],
              f"a tray icon that cannot be activated starts its application ({apps.launched[before:]})")

        # A hidden Wi-Fi network is joined by its name.
        wifi_cards = windows_of_kind("WifiPopup")
        if wifi_cards:
            toggle = wifi_cards[0].findChild(QObject, "hiddenWifiButton")
            check(toggle is not None, "the Wi-Fi card offers a hidden network")
            if toggle is not None:
                QMetaObject.invokeMethod(toggle, "clicked")
                QTest.qWait(50)
                check(bool(wifi_cards[0].property("hiddenForm")), "the hidden network form opens")
                stack = [wifi_cards[0].contentItem()]
                name_field = None
                while stack and name_field is None:
                    item = stack.pop()
                    if item.objectName() == "hiddenWifiForm":
                        fields = [c for c in item.childItems() if c.property("placeholder") is not None]
                        name_field = fields[0] if fields else None
                    stack.extend(item.childItems())
                if name_field is not None:
                    session.calls.clear()
                    name_field.setProperty("text", "Office")
                    QMetaObject.invokeMethod(wifi_cards[0], "joinHidden")
                    QTest.qWait(50)
                    check(("connectWifi", "Office") in session.calls,
                          f"joining a hidden network asks NetworkManager ({session.calls})")
                QMetaObject.invokeMethod(toggle, "clicked")

        # Do not disturb for an hour: switches on with an end time.
        dnd_cards = windows_of_kind("QuickSettingsPopup")
        if dnd_cards:
            hour = dnd_cards[0].findChild(QObject, "dndHourButton")
            check(hour is not None, "quick settings offers do not disturb for an hour")
            if hour is not None:
                was = settings._values.get("notifications.doNotDisturb")
                QMetaObject.invokeMethod(hour, "clicked")
                QTest.qWait(50)
                until = float(settings._values.get("notifications.dndUntil") or 0)
                check(settings._values.get("notifications.doNotDisturb") is True
                      and until > time.time() * 1000 + 3500 * 1000,
                      f"one hour of do not disturb ({until})")
                settings.setValue("notifications.doNotDisturb", bool(was))
                settings.setValue("notifications.dndUntil", 0)

        # The sound card: outputs to pick from, one row per playing application.
        sound_cards = windows_of_kind("SoundPopup")
        quick_cards = windows_of_kind("QuickSettingsPopup")
        check(len(sound_cards) == 1, f"one sound card ({len(sound_cards)})")
        if sound_cards and quick_cards:
            button = quick_cards[0].findChild(QObject, "soundDevicesButton")
            check(button is not None, "quick settings links to the sound card")
            if button is not None:
                QMetaObject.invokeMethod(button, "clicked")
                QTest.qWait(300)
                check(sound_cards[0].isVisible(), "the volume row opens the sound card")
                # Repeater delegates are visual children only: walk childItems().
                outputs = []
                microphones = []
                stack = [sound_cards[0].contentItem()]
                while stack:
                    item = stack.pop()
                    if item.objectName() == "soundOutput":
                        outputs.append(item)
                    elif item.objectName() == "soundInput":
                        microphones.append(item)
                    stack.extend(item.childItems())
                check(len(outputs) == 2, f"the sound card lists the outputs ({len(outputs)})")
                check(len(microphones) == 2,
                      f"the sound card lists the microphones ({len(microphones)})")
                headset = next((o for o in outputs if o.property("label") == "Headset"), None)
                if headset is not None:
                    QMetaObject.invokeMethod(headset, "clicked")
                    QTest.qWait(50)
                check(("setDefaultOutput", "bluez_output.headset") in audio.calls,
                      f"picking an output makes it the default ({audio.calls})")
                QMetaObject.invokeMethod(sound_cards[0], "hide")
                QTest.qWait(300)

        # Screen recording: quick settings starts it, the panel shows a pill
        # with the time, Super+Alt+R (through Shell) stops it.
        quick_cards = windows_of_kind("QuickSettingsPopup")
        if quick_cards:
            record = quick_cards[0].findChild(QObject, "recorderScreen")
            check(record is not None, "quick settings has a Record the screen button")
            if record is not None:
                QMetaObject.invokeMethod(record, "clicked")
                QTest.qWait(300)
                check(("start", False) in recorder.calls,
                      f"the Screen button starts a recording ({recorder.calls})")
                pill = panel.findChild(QObject, "recordingButton")
                check(pill is not None and pill.property("visible"),
                      "the panel shows the recording pill while recording")
                shell.screenRecordingToggleRequested.emit()
                QTest.qWait(100)
                check(("stop",) in recorder.calls, "Super+Alt+R stops the recording")
                check(pill is None or not pill.property("visible"),
                      "the recording pill goes away when the recording stops")

        # A newer build on offer puts an arrow on the panel; skipping the
        # build takes it away again.
        arrow = panel.findChild(QObject, "updateButton")
        check(arrow is not None and not arrow.property("visible"),
              "no update arrow on the panel before a check")
        updater.check()
        QTest.qWait(50)
        check(arrow is not None and arrow.property("visible"),
              "the panel shows an arrow while an update is on offer")
        updater.skip()
        QTest.qWait(50)
        check(arrow is None or not arrow.property("visible"),
              "a skipped build takes the arrow away")
        updater._skipped = False
        updater.changed.emit()

        # Panel shortcuts that never open a card: the middle click mutes, the
        # bell's right click flips do-not-disturb.
        if quicks and centres:
            quick.calls.clear()
            press("indicatorsButton", Qt.MiddleButton)
            check(("toggleMuted",) in quick.calls, "middle-clicking the indicators mutes")
            check(not quicks[0].isVisible(), "muting does not open the quick settings card")

            dnd = bool(settings._values.get("notifications.doNotDisturb"))
            press("bellButton", Qt.RightButton)
            check(bool(settings._values.get("notifications.doNotDisturb")) != dnd,
                  "right-clicking the bell toggles do-not-disturb")
            check(not centres[0].isVisible(), "and does not open the notification centre")

            # With two layouts the panel shows the current one. (Not clicked:
            # the pointer would stay over it and fade its tooltip in, which
            # the offscreen platform reports as a window-opacity warning.)
            layout_button = panel.findChild(QObject, "layoutButton")
            check(layout_button is not None and layout_button.property("visible"),
                  "the panel shows the keyboard layout with two layouts configured")
            strip = panel.findChild(QObject, "workspaceStrip")
            check(strip is not None and strip.property("visible"),
                  "the panel shows the desktop dots with four desktops")

            # Hardware keys arrive from the compositor through Session.
            quick.calls.clear()
            session.mediaKey.emit("volume-up")
            QTest.qWait(50)
            check(("setVolume", 47) in quick.calls,
                  f"the volume-up key raises the volume by 5% ({quick.calls})")
            osds = windows_of_kind("OsdPopup")
            check(bool(osds) and osds[0].isVisible(), "the volume key shows the on-screen level")
            session.mediaKey.emit("caps-on")
            QTest.qWait(50)
            check(bool(osds) and osds[0].property("text") == "Caps Lock on",
                  f"Caps Lock shows on screen ({osds[0].property('text') if osds else None})")

            # Super+V reaches the shell over D-Bus and opens the history.
            shell.clipboardToggleRequested.emit()
            QTest.qWait(300)
            clips = windows_of_kind("ClipboardPopup")
            check(bool(clips) and clips[0].isVisible(), "Super+V opens the clipboard history")
            if clips:
                entries = clips[0].findChild(QObject, "clipboardList")
                check(entries is not None and entries.property("count") == 3,
                      "the clipboard history lists the pinned and the copied texts")
                clips[0].setProperty("filter", "second")
                QTest.qWait(50)
                check(entries is not None and entries.property("count") == 1,
                      "searching the clipboard history narrows the list")
                clips[0].setProperty("filter", "")
                QMetaObject.invokeMethod(clips[0], "pick",
                                         Q_ARG("QVariant", {"text": "pinned snippet",
                                                            "pinned": True, "index": -1}))
                QTest.qWait(300)
                check(("copyText", "pinned snippet") in clipboard.calls,
                      f"a pinned entry goes back on the clipboard ({clipboard.calls})")
                shell.clipboardToggleRequested.emit()
                QTest.qWait(300)
                shell.clipboardToggleRequested.emit()
                QTest.qWait(400)
                check(not clips[0].isVisible(), "Super+V again closes it")

            # Super+Tab: the overview lists the windows of the current desktop.
            shell.taskViewToggleRequested.emit()
            QTest.qWait(300)
            views = windows_of_kind("TaskView")
            check(bool(views) and views[0].isVisible(), "Super+Tab opens the window overview")
            if views:
                grid = views[0].findChild(QQuickItem, "taskViewGrid")
                shown = [c for c in (grid.childItems() if grid else [])
                         if c.metaObject().className().startswith("QQuickRectangle")]
                check(len(shown) == 2, f"the overview shows the two windows of desktop 1 ({len(shown)})")
                check(("thumbnail", 1) in session.calls, "the overview asks for thumbnails")
                shell.taskViewToggleRequested.emit()
                QTest.qWait(200)
                check(not views[0].isVisible(), "Super+Tab again closes the overview")

            # The administrator prompt: polkit asks, the screen dims, the
            # password goes back, Cancel refuses.
            polkit.request()
            QTest.qWait(150)
            auths = windows_of_kind("AuthDialog")
            check(bool(auths) and auths[0].isVisible(), "a polkit request opens the administrator prompt")
            if auths:
                field = auths[0].findChild(QObject, "authPassword")
                check(field is not None, "the administrator prompt has a password field")
                if field is not None:
                    field.setProperty("text", "secret")
                    QMetaObject.invokeMethod(auths[0], "allow")
                    check(("respond", "secret") in polkit.calls, "Allow hands the password to polkit")
                    QTest.qWait(50)
                    check(field.property("text") == "", "a wrong password clears the field")
                polkit.cancel()
                QTest.qWait(100)
                check(not auths[0].isVisible(), "cancelling closes the administrator prompt")

            # Ctrl+Shift+Esc: the force-quit dialog lists windows and ends one.
            shell.forceQuitRequested.emit()
            QTest.qWait(200)
            quits = windows_of_kind("ForceQuitWindow")
            check(bool(quits) and quits[0].isVisible(), "Ctrl+Shift+Esc opens the force-quit dialog")
            if quits:
                fq_list = quits[0].findChild(QObject, "forceQuitList")
                check(fq_list is not None and fq_list.property("count") == 3,
                      "the force-quit dialog lists every window")
                session.calls.clear()
                QMetaObject.invokeMethod(quits[0], "endTask")
                check(any(c[0] == "killWindow" for c in session.calls),
                      "End task kills the selected window")
                QMetaObject.invokeMethod(quits[0], "hide")
                QTest.qWait(100)
                check(not quits[0].isVisible(), "the force-quit dialog closes")

            # The Bluetooth list scans while open and shows the devices.
            bts = windows_of_kind("BluetoothPopup")
            if bts:
                bluetooth.calls.clear()
                QMetaObject.invokeMethod(bts[0], "show")
                QTest.qWait(300)
                check(("startDiscovery",) in bluetooth.calls, "opening the Bluetooth list scans")
                bt_list = bts[0].findChild(QObject, "bluetoothList")
                check(bt_list is not None and bt_list.property("count") == 2,
                      "the Bluetooth list shows the devices")
                bt_items = []
                walk_items(bts[0].contentItem(), bt_items)
                statuses = sorted(i.property("text") for i in bt_items
                                  if isinstance(i, QQuickItem) and i.objectName() == "bluetoothStatus")
                check(statuses == ["Available", "Connected · battery 80%"],
                      f"connected devices show their charge ({statuses})")
                QMetaObject.invokeMethod(bts[0], "hide")
                QTest.qWait(400)
                check(("stopDiscovery",) in bluetooth.calls, "closing the Bluetooth list stops scanning")
            else:
                check(False, "the Bluetooth device list exists")

            # The Wi-Fi list scans on the way in and lists the networks.
            wifis = windows_of_kind("WifiPopup")
            check(bool(wifis), "the Wi-Fi network list exists")
            if wifis:
                session.calls.clear()
                QMetaObject.invokeMethod(wifis[0], "show")
                QTest.qWait(300)
                check(("scanWifi",) in session.calls, "opening the Wi-Fi list scans for networks")
                wifi_list = wifis[0].findChild(QObject, "wifiList")
                check(wifi_list is not None and wifi_list.property("count") == 2,
                      "the Wi-Fi list shows the networks in range")
                QMetaObject.invokeMethod(wifis[0], "hide")
                QTest.qWait(400)
            settings._values["notifications.doNotDisturb"] = dnd
            settings.valuesChanged.emit()
            QTest.qWait(50)

        # Keyboard: Up/Down walk the result list, Enter runs the highlighted
        # row (and closes the menu).
        menu.setProperty("anchor", QRectF(geometry.x() + 300, bar_top, 60, 48))
        QMetaObject.invokeMethod(menu, "show")
        QTest.qWait(300)
        check(int(menu.property("selectedIndex")) == 0, "the first row is highlighted")
        QMetaObject.invokeMethod(menu, "moveSelection", Q_ARG("QVariant", 1))
        check(int(menu.property("selectedIndex")) == 1, "Down moves the highlight")
        QMetaObject.invokeMethod(menu, "moveSelection", Q_ARG("QVariant", -1))
        check(int(menu.property("selectedIndex")) == 0, "Up moves it back")
        QMetaObject.invokeMethod(menu, "moveSelection", Q_ARG("QVariant", -1))
        check(int(menu.property("selectedIndex")) == 0, "the highlight stops at the top")
        launched_before = len(apps.launched)
        QMetaObject.invokeMethod(menu, "launchSelected")
        QTest.qWait(300)
        check(len(apps.launched) == launched_before + 1, "Enter launches the highlighted row")
        check(not menu.isVisible(), "launching from the keyboard closes the menu")
        check(panel.property("dropdownOpen") is False,
              "the panel lets go once every dropdown is closed")

    # One notification window per screen, hidden until something arrives.
    notification_windows = windows_of_kind("NotificationPopups")
    check(len(notification_windows) == len(screens),
          f"one notification window per screen ({len(notification_windows)} vs {len(screens)})")
    if notification_windows:
        check(not notification_windows[0].isVisible(), "notifications start hidden")

        # The popup window diffs the daemon's queue by id: arrivals and
        # departures animate per card while the model keeps its order.
        notif_win = notification_windows[0]
        first = {"id": 1, "app": "Mail", "summary": "Hello", "body": "First",
                 "icon": "bell", "actions": [], "timeout": 0}
        second = {"id": 2, "app": "Chat", "summary": "Yo", "body": "Second",
                  "icon": "bell", "actions": [], "timeout": 0}
        notifications._active = [first, second]
        notifications.activeChanged.emit()
        QTest.qWait(50)
        check(notif_win.isVisible(), "a notification makes the popup window visible")
        stack = notif_win.findChild(QObject, "notifStack")
        check(stack is not None, "the notification stack exists")

        def stack_ids():
            # Delegates keep creation order in childItems() even after the
            # model moves them, so read the visual order from y instead.
            content = stack.property("contentItem")
            rows = []
            for d in content.childItems():
                entry = d.property("entry")
                if hasattr(entry, "toVariant"):
                    entry = entry.toVariant()
                # contentItem also holds internal items without the delegate's
                # properties; only real cards carry an entry mapping.
                if isinstance(entry, dict) and "id" in entry:
                    rows.append((d.y(), entry["id"]))
            rows.sort(key=lambda row: row[0])
            return [i for _, i in rows]

        if stack is not None:
            check(stack.property("count") == 2, "two notifications are stacked")
            check(stack_ids() == [1, 2], f"cards keep the daemon's order ({stack_ids()})")

            notifications._active = [second]
            notifications.activeChanged.emit()
            QTest.qWait(300)  # let the exit transition finish
            check(stack.property("count") == 1, "the dismissed notification leaves the stack")
            check(stack_ids() == [2], f"the surviving card keeps its slot ({stack_ids()})")

            # Re-notify the older id: the diff appends it and moves it back
            # to the front of the stack.
            notifications._active = [first, second]
            notifications.activeChanged.emit()
            QTest.qWait(300)
            check(stack.property("count") == 2 and stack_ids() == [1, 2],
                  f"a re-arriving card lands in order ({stack_ids()})")

            # A notification's own picture (image-data becomes a PNG data:
            # URL in the daemon) shows in place of the app icon.
            picture = QImage(8, 8, QImage.Format_RGB32)
            picture.fill(QColor("#3366cc"))
            png = QByteArray()
            png_buffer = QBuffer(png)
            png_buffer.open(QIODevice.WriteOnly)
            picture.save(png_buffer, "PNG")
            third = {"id": 3, "app": "Chat", "summary": "Anna", "body": "Hi", "icon": "bell",
                     "image": "data:image/png;base64," + bytes(png.toBase64()).decode(),
                     "actions": ["default", "", "reply", "Reply"], "timeout": 0, "value": 40}
            notifications._active = [first, second, third]
            notifications.activeChanged.emit()
            QTest.qWait(300)
            # Delegates are visual children only, so walk childItems().
            def items_named(item, name):
                found = [item] if item.objectName() == name else []
                for child in item.childItems():
                    found += items_named(child, name)
                return found
            images = [i for i in items_named(stack.property("contentItem"), "notificationImage")
                      if i.isVisible()]
            sizes = [(i.property("sourceSize").width(), i.property("sourceSize").height())
                     for i in images]
            # The image has decoded once its source size is known.
            check(len(images) == 1 and images[0].property("source").toString().startswith("data:")
                  and images[0].property("implicitWidth") > 0,
                  f"a notification picture is shown ({len(images)} visible, {sizes})")

            # "default" is what clicking the card does, not a button; the
            # empty label next to it does not shift the other pairs.
            labels = [b.property("label") for b in items_named(stack.property("contentItem"),
                                                               "notificationAction")
                      if b.isVisible()]
            check(labels == ["Reply"], f"only real actions become buttons ({labels})")
            bars = [b for b in items_named(stack.property("contentItem"), "notificationValue")
                    if b.isVisible()]
            check(len(bars) == 1 and abs(bars[0].childItems()[0].width() - bars[0].width() * 0.4) < 2,
                  f"the value hint draws a level bar ({[(b.width(), b.childItems()[0].width()) for b in bars]})")
            def card_of(item):
                # The nearest ancestor that carries the card's entry.
                while item is not None:
                    entry = item.property("entry")
                    entry = entry.toVariant() if hasattr(entry, "toVariant") else entry
                    if isinstance(entry, dict) and "id" in entry:
                        return item, entry["id"]
                    item = item.parentItem()
                return None, None
            cards_found = [card_of(b) for b in items_named(stack.property("contentItem"),
                                                           "notificationBody")]
            notifications.calls.clear()
            for card_item, card_number in cards_found:
                if card_number == 3:
                    # What the card's MouseArea calls (offscreen clicks do not
                    # reach this window reliably).
                    QMetaObject.invokeMethod(card_item, "activate")
            check(("invokeAction", 3, "default") in notifications.calls,
                  f"clicking a notification runs its default action ({notifications.calls})")

            # Inline reply: a text field instead of an "inline-reply" button.
            notifications._active = [{"id": 9, "app": "Telegram", "summary": "Anna",
                                      "body": "Lunch?", "icon": "", "image": "",
                                      "actions": ["default", "", "inline-reply", "Reply"],
                                      "timeout": 0, "replyable": True,
                                      "replyPlaceholder": "Message Anna"}]
            notifications.activeChanged.emit()
            QTest.qWait(400)
            fields = items_named(stack.property("contentItem"), "notificationReply")
            check(len(fields) == 1 and fields[0].isVisible(),
                  f"a notification with inline-reply shows a reply field ({len(fields)})")
            reply_buttons = [b for b in items_named(stack.property("contentItem"),
                                                    "notificationAction")
                             if b.property("label") == "Reply"]
            check(not reply_buttons, "inline-reply is not shown as a button")
            if fields:
                fields[0].setProperty("text", "Yes!")
                card_item, _ = card_of(fields[0])
                if card_item is not None:
                    QMetaObject.invokeMethod(card_item, "sendReply")
                check(("reply", 9, "Yes!") in notifications.calls,
                      f"sending a reply reaches the daemon ({notifications.calls})")

        # An urgent notification lifts the popups above fullscreen windows.
        nls = attached("layershell", notif_win)
        if nls is not None:
            check(nls.layer == LayerShellWindow.Layer.LayerTop,
                  f"ordinary notifications sit in the top layer ({nls.layer})")
            notifications._active = [{"id": 11, "app": "NewTeller", "summary": "Battery critically low",
                                      "body": "4% left", "icon": "battery", "image": "",
                                      "actions": [], "timeout": 0, "urgent": True}]
            notifications.activeChanged.emit()
            QTest.qWait(100)
            check(nls.layer == LayerShellWindow.Layer.LayerOverlay,
                  f"an urgent notification shows above fullscreen windows ({nls.layer})")

        notifications._active = []
        notifications.activeChanged.emit()
        QTest.qWait(400)  # the exit animation plays before the window hides
        check(not notif_win.isVisible(), "the last notification dismiss hides the window")

    # A battery at 4% while discharging: the warning gets through
    # do-not-disturb (urgent), once.
    shell.calls.clear()
    system.battery = (True, 4.0, False)
    system.snapshotUpdated.emit()
    system.snapshotUpdated.emit()
    QTest.qWait(50)
    urgent = [c for c in shell.calls if c[0] == "notifyUrgent"]
    check(len(urgent) == 1, f"a critically low battery warns urgently, once ({shell.calls})")
    check(("setPowerProfile", "power-saver") in session.calls,
          f"a low battery switches to the power saver profile ({session.calls})")
    system.battery = (False, 0.0, False)
    system.snapshotUpdated.emit()
    QTest.qWait(50)

    # The colour palette is a panel pinned to the left edge of the screen,
    # not a floating window.
    palettes = windows_of_kind("ColorPaletteWindow")
    check(len(palettes) == 1, f"one colour palette window (got {len(palettes)})")
    if palettes:
        palette = palettes[0]
        check(not palette.isVisible(), "the colour palette starts hidden")
        pls = attached("layershell", palette)
        check(pls is not None
              and pls.anchors == (LayerShellWindow.Anchor.AnchorLeft
                                  | LayerShellWindow.Anchor.AnchorTop
                                  | LayerShellWindow.Anchor.AnchorBottom)
              and pls.layer == LayerShellWindow.Layer.LayerTop
              and pls.scope == "newteller-palette",
              "the colour palette is a left-edge layer panel")

    # The embedded settings window opens through Shell.openSettings(); the
    # start menu button, the desktop menu and `newteller --settings` share it.
    settings_windows = windows_of_kind("SettingsApp")
    check(len(settings_windows) == 1, f"one settings window (got {len(settings_windows)})")
    if settings_windows:
        settings_app = settings_windows[0]
        check(not settings_app.isVisible(), "settings window starts hidden")
        shell.settingsRequested.emit()
        QTest.qWait(50)
        check(settings_app.isVisible(), "Shell.settingsRequested opens the settings window")
        check(settings_app.isVisible() and settings_app.title() == "NewTeller Settings",
              "the settings window carries its title")

        # The shell's Russian translation (src/translator.cpp reads the same
        # JSON): installed live, every binding is re-evaluated without a QML
        # warning, and a translated text shows.
        class JsonTranslator(QTranslator):
            def __init__(self, strings):
                super().__init__()
                self.strings = strings

            def translate(self, context, source, disambiguation=None, n=-1):
                return self.strings.get(source, "")

            def isEmpty(self):
                return not self.strings

        with open(os.path.join(ROOT, "i18n", "ru.json"), encoding="utf-8") as handle:
            russian = JsonTranslator(json.load(handle))
        QGuiApplication.installTranslator(russian)
        engine.retranslate()
        QTest.qWait(50)
        check(settings_app.title() == "Настройки NewTeller",
              f"the shell switches to Russian live ({settings_app.title()!r})")
        QGuiApplication.removeTranslator(russian)
        engine.retranslate()
        QTest.qWait(50)
        check(settings_app.title() == "NewTeller Settings",
              f"and back to English ({settings_app.title()!r})")
        page = settings_app.property("currentPage")
        check(page == "appearance", f"settings starts on the appearance page ({page})")
        pages = settings_app.property("pages")
        if hasattr(pages, "toVariant"):
            pages = pages.toVariant()
        for entry in (pages or []):
            settings_app.setProperty("currentPage", entry["id"])
            QTest.qWait(15)
            check(settings_app.property("currentPage") == entry["id"],
                  f"settings page switching works ({entry['id']})")
        settings_app.setProperty("visible", False)
        QTest.qWait(20)

    # ------------------------------------------------------------------
    # Settings -> Desktop -> "Choose from file manager"
    #
    # The button used to open the system chooser (or the integrated picker)
    # with whatever folder the current wallpaper happened to imply, hand the
    # raw path to FolderListModel and apply any path that came back. A missing
    # folder, a "file://file:///..." URL or a non-image crashed the shell for
    # some users, so the whole flow is exercised here: portal result, portal
    # failure, dismissal, a non-image, odd folder names and paths that no
    # longer exist.
    # ------------------------------------------------------------------
    pickers = windows_of_kind("SettingsFilePicker")
    check(len(pickers) == 1, f"one integrated file picker window (got {len(pickers)})")
    if pickers and settings_windows:
        settings_app, picker = settings_windows[0], pickers[0]
        # The picker is a transient child of the settings window, so the
        # settings window has to be open (the previous section closed it).
        shell.settingsRequested.emit()
        QTest.qWait(60)
        check(settings_app.isVisible(), "the settings window is open for the picker tests")
        check(not picker.isVisible(), "the file picker starts hidden")

        def call(window, method, *args):
            return QMetaObject.invokeMethod(
                window, method, *[Q_ARG("QVariant", a) for a in args])

        shots = os.path.join(work, "Pictures")
        wallpaper = os.path.join(shots, "wall.png")
        image = QImage(48, 32, QImage.Format_RGB32)
        image.fill(QColor("#204060"))
        check(image.save(wallpaper), "the test wallpaper is a real PNG")
        note = os.path.join(work, "wall-note.txt")
        with open(note, "w") as fh:
            fh.write("not an image")
        # A folder name with a "%" and a "#" used to produce a broken URL.
        odd_dir = os.path.join(shots, "50% #1")
        os.makedirs(odd_dir, exist_ok=True)

        settings._values["desktop.wallpaper"] = ""
        settings.valuesChanged.emit()
        QTest.qWait(20)

        # 1. The system chooser is asked first and starts in a real folder.
        file_portal.calls.clear()
        call(settings_app, "pickWallpaper", shots)
        QTest.qWait(20)
        check(file_portal.calls and file_portal.calls[-1][0] == "pickFile",
              f"the system file chooser is asked first ({file_portal.calls[-1:]})")
        check(file_portal.calls[-1][2] == shots,
              f"the chooser starts in the wallpaper folder ({file_portal.calls[-1][2]!r})")

        # A start folder that does not exist (deleted picture, unmounted disk)
        # is replaced by the home directory instead of a broken URL.
        call(settings_app, "pickWallpaper", os.path.join(shots, "does-not-exist"))
        QTest.qWait(20)
        check(file_portal.calls[-1][2] == work,
              f"a missing start folder falls back to home ({file_portal.calls[-1][2]!r})")

        # 2. A picture chosen in the system dialog becomes the wallpaper.
        file_portal.filePicked.emit(wallpaper)
        QTest.qWait(40)
        check(settings._values.get("desktop.wallpaper") == wallpaper,
              f"the portal picture becomes the wallpaper ({settings._values.get('desktop.wallpaper')!r})")

        # 3. Dismissing, a non-image and a deleted file leave it alone.
        file_portal.filePicked.emit("")
        file_portal.filePicked.emit(note)
        file_portal.filePicked.emit(os.path.join(shots, "gone.png"))
        QTest.qWait(40)
        check(settings._values.get("desktop.wallpaper") == wallpaper,
              "a dismissal or a non-image keeps the current wallpaper")

        # 4. A portal failure opens the integrated picker, once.
        settings_app.setProperty("filePortalDir", shots)
        check(not picker.isVisible(), "the fallback picker is closed before the failure")
        file_portal.pickFailed.emit()
        QTest.qWait(60)
        check(picker.isVisible(), "a portal failure opens the integrated picker")
        check(picker.property("currentPath") == shots,
              f"the fallback picker starts where the chooser did ({picker.property('currentPath')!r})")

        # 5. Navigation: a URL, a "~" path and a folder with "%"/"#" in its name.
        call(picker, "enter", QUrl.fromLocalFile(odd_dir).toString())
        QTest.qWait(40)
        check(picker.property("currentPath") == odd_dir,
              f"the picker decodes folder URLs ({picker.property('currentPath')!r})")
        call(picker, "enter", "~/Pictures/../Pictures")
        QTest.qWait(40)
        check(picker.property("currentPath") == shots,
              f"the picker expands ~ and cleans the path ({picker.property('currentPath')!r})")
        call(picker, "goUp")
        QTest.qWait(40)
        check(picker.property("currentPath") == work, "goUp() goes to the parent folder")

        # 6. Only real images can be picked.
        call(picker, "choose", note)
        QTest.qWait(40)
        check(picker.isVisible(), "choosing a non-image keeps the dialog open")
        check(settings._values.get("desktop.wallpaper") == wallpaper,
              "choosing a non-image does not touch the wallpaper")
        call(picker, "choose", os.path.join(shots, "gone.png"))
        QTest.qWait(40)
        check(picker.isVisible(), f"choosing a deleted file keeps the dialog open")

        call(picker, "choose", wallpaper)
        QTest.qWait(400)  # the close animation has to finish
        check(not picker.isVisible(), "choosing an image closes the dialog")
        check(settings._values.get("desktop.wallpaper") == wallpaper,
              "choosing an image applies the wallpaper")

        # 7. A dialog that lost (or never got) the keyboard focus must stay
        #    open: it is a file chooser, not a popup menu. It also has to
        #    survive being reopened while the close animation is running.
        settings._values["desktop.wallpaper"] = ""
        settings.valuesChanged.emit()
        call(picker, "openFor", shots, True)
        QTest.qWait(30)
        check(picker.isVisible(), "openFor() shows the picker")
        check(picker.property("imagesOnly") is True, "openFor() enables the image filter")
        for _ in range(5):
            call(picker, "hide")
            call(picker, "openFor", shots, True)
            QTest.qWait(40)
        check(picker.isVisible(), "reopening during the close animation keeps the picker open")
        call(picker, "openFor", "", True)
        QTest.qWait(40)
        check(picker.property("currentPath") == work,
              f"an empty start folder falls back to home ({picker.property('currentPath')!r})")
        call(picker, "hide")
        QTest.qWait(400)
        check(not picker.isVisible(), "hide() closes the picker")

        settings._values["desktop.wallpaper"] = ""
        settings.valuesChanged.emit()
        QTest.qWait(20)

    QTest.qWait(100)
    check(not messages, f"no QML warnings ({len(messages)} found)")
    for w in QGuiApplication.topLevelWindows():
        w.close()
    del engine
    shutil.rmtree(work, ignore_errors=True)
    os.unlink(_config.name)
    if failures:
        print(f"\n{len(failures)} check(s) failed", file=sys.stderr)
        return 1
    print(f"QML smoke test passed (Qt {PySide6.QtCore.qVersion()})")
    return 0


if __name__ == "__main__":
    rc = main()
    if sys.version_info < (3, 12):
        # Shiboken6 6.12 under-counts Py_True in QML slot/property marshalling,
        # which triggers bool_dealloc during Py_FinalizeEx on pre-PEP-683 Pythons.
        sys.stdout.flush()
        sys.stderr.flush()
        os._exit(rc)
    sys.exit(rc)
