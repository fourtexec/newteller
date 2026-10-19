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
    clicking it again closes the menu.

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
from PySide6.QtCore import (Q_ARG, QDateTime, QEnum, QFile, QMargins, QMetaObject, QObject, QPoint,  # noqa: E402
                            QtMsgType, QTimer, QUrl, Property, Qt, Signal, Slot,
                            qInstallMessageHandler)
from PySide6.QtGui import QColor, QGuiApplication  # noqa: E402
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
     "categories": ["Network"], "keywords": ["internet", "web"]},
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
    userName = Property(str, lambda self: "tester", constant=True)
    sessionType = Property(str, lambda self: "wayland", constant=True)
    hostname = Property(str, lambda self: "testhost", constant=True)
    distro = Property(str, lambda self: "TestOS 1.0", constant=True)
    kernelVersion = Property(str, lambda self: "6.1.0-test", constant=True)

    @Slot(str, result=QUrl)
    def iconUrl(self, name):
        # Mirror the real resolution for the bundled core set, so the smoke
        # test actually renders the compiled icons (a wrong resources.qrc
        # entry would fail the test with a missing-resource error).
        url = QUrl(f"qrc:/icons/{name}.svg")
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

    @Slot(result=str)
    def clipboardText(self):
        return "pasted text"

    @Slot(str, str, result=bool)
    def saveTextFile(self, path, text):
        self.calls.append(("saveTextFile", path))
        return True

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
    batteryPresent = Property(bool, lambda self: False, constant=True)
    batteryPercent = Property(float, lambda self: 0.0, constant=True)
    batteryStatus = Property(str, lambda self: "", constant=True)
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
        pass

    @Slot()
    def clearHistory(self):
        pass


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
    for name in ("notes.txt", "photo.png"):
        open(os.path.join(desktop_dir, name), "w").close()
    panel_plugin = os.path.join(work, "PanelWidget.qml")
    with open(panel_plugin, "w") as f:
        f.write("import QtQuick\nRectangle { width: 30; height: 30; color: Theme.accent }\n")
    plugins = [
        {"id": "desktop-clock", "name": "Desktop Clock", "type": "desktop", "enabled": True, "x": 60, "y": 60,
         "entry": QUrl.fromLocalFile(os.path.join(ROOT, "plugins/desktop-clock/Widget.qml")).toString()},
        {"id": "panel-test", "name": "Panel test", "type": "panel", "enabled": True, "x": 0, "y": 0,
         "entry": QUrl.fromLocalFile(panel_plugin).toString()},
        {"id": "panel-system-monitor", "name": "System Monitor", "type": "panel",
         "enabled": True, "x": 0, "y": 0,
         "entry": QUrl.fromLocalFile(
             os.path.join(ROOT, "plugins/panel-system-monitor/Widget.qml")).toString()},
        {"id": "desktop-sysinfo", "name": "System Info", "type": "desktop", "enabled": True,
         "x": 60, "y": 220,
         "entry": QUrl.fromLocalFile(
             os.path.join(ROOT, "plugins/desktop-sysinfo/Widget.qml")).toString()},
        {"id": "disabled", "name": "Disabled", "type": "desktop", "enabled": False, "x": 0, "y": 0,
         "entry": QUrl.fromLocalFile(os.path.join(work, "missing.qml")).toString()},
    ]

    settings, theme, apps = Settings(), Theme(), Apps()
    shell, plugin_manager = Shell(desktop_dir, work), Plugins(plugins)
    system, window_list = System(), Windows()
    notifications, file_portal = Notifications(), FilePortal()
    engine = QQmlApplicationEngine()
    for name, obj in (("Settings", settings), ("Theme", theme), ("Apps", apps), ("Shell", shell),
                      ("Plugins", plugin_manager), ("System", system), ("Windows", window_list),
                      ("Notifications", notifications), ("FilePortal", file_portal)):
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
        literal_icons.update(re.findall(r'\biconName:\s*"([^"]+)"', text))
        literal_icons.update(re.findall(r'\bicon:\s*"([^"]+)"', text))
    missing_files = sorted(name for name in literal_icons
                           if not os.path.exists(os.path.join(icon_dir, name + ".svg")))
    check(not missing_files, f"every literal iconName has an icons/ file (missing: {missing_files})")
    on_disk = {os.path.basename(p) for p in glob.glob(os.path.join(icon_dir, "*.svg"))}
    unlisted = sorted(on_disk - qrc_icons)
    check(not unlisted, f"every icons/ file is listed in resources.qrc (unlisted: {unlisted})")

    engine.load(QUrl("qrc:/qml/Main.qml"))
    if not engine.rootObjects():
        print("FAIL: qrc:/qml/Main.qml did not load", file=sys.stderr)
        return 1
    QTest.qWait(300)

    check(app.platformName() == "offscreen", f"running on the offscreen platform ({app.platformName()})")
    screens = QGuiApplication.screens()
    check([s.name() for s in screens] == [s["name"] for s in SCREENS], "offscreen platform has the test screens")

    def windows(kind):
        return [w for w in QGuiApplication.topLevelWindows() if w.metaObject().className().startswith(kind)]

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
        check(ls is not None and ls.anchors == 6 and ls.keyboardInteractivity == 1
              and ls.margins == QMargins(8, 0, 0, 60), "start menu layer-shell settings")
        check(not menu.isVisible(), "start menu starts hidden")

        panel = next(w for w in panels if w.screen() is screens[0])
        start_button = QPoint(24, panel.height() // 2)  # first item in the panel
        QTest.mouseClick(panel, Qt.LeftButton, Qt.NoModifier, start_button)
        QTest.qWait(50)
        check(menu.isVisible(), "clicking Start opens the start menu")
        results = menu.property("results")
        if hasattr(results, "toVariant"):  # QJSValue
            results = results.toVariant()
        check(len(results or []) == len(APPS), "start menu lists the applications")
        want = (primary.x() + 8, primary.y() + primary.height() - 580 - 48 - 12, 440, 580)
        got = (menu.x(), menu.y(), menu.width(), menu.height())
        check(got == want, f"start menu sits above the panel ({got} vs {want})")

        QTest.mouseClick(panel, Qt.LeftButton, Qt.NoModifier, start_button)
        QTest.qWait(300)  # the close animation plays before the window hides
        check(not menu.isVisible(), "clicking Start again closes the start menu")

        QMetaObject.invokeMethod(menu, "show")
        QMetaObject.invokeMethod(menu, "launch", Q_ARG("QVariant", APPS[1]))
        QTest.qWait(300)  # hide() animates the card out first
        check(apps.launched == ["org.gnome.Terminal"], f"launching from the menu ({apps.launched})")
        check(not menu.isVisible(), "launching an application closes the menu")
        check(apps.isRunning("org.gnome.Terminal"), "the launched application is marked running")

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
        QMetaObject.invokeMethod(dialog, "hide")
        QTest.qWait(300)  # the hide animation fades the card out first
        check(not dialog.isVisible(), "closing the run dialog")

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

        notifications._active = []
        notifications.activeChanged.emit()
        QTest.qWait(400)  # the exit animation plays before the window hides
        check(not notif_win.isVisible(), "the last notification dismiss hides the window")

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
        page = settings_app.property("currentPage")
        check(page == "appearance", f"settings starts on the appearance page ({page})")
        settings_app.setProperty("currentPage", "about")
        QTest.qWait(20)
        check(settings_app.property("currentPage") == "about",
              "settings page switching works")
        settings_app.setProperty("visible", False)
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
    sys.exit(main())
