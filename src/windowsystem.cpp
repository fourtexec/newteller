#include "windowsystem.h"

#include <QByteArray>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QString>
#include <QtGlobal>

#include <cstring>

#include <wayland-client.h>

namespace {

// Set when selectPlatform() chose xcb by itself, so that finishSetup() can
// take QT_QPA_PLATFORM out of the environment again.
bool s_forcedXcb = false;

struct WaylandProbe {
    bool connected = false;
    bool layerShell = false;
};

void handleGlobal(void *data, wl_registry *, uint32_t, const char *interface, uint32_t)
{
    if (std::strcmp(interface, "zwlr_layer_shell_v1") == 0)
        static_cast<WaylandProbe *>(data)->layerShell = true;
}

void handleGlobalRemove(void *, wl_registry *, uint32_t)
{
}

const wl_registry_listener registryListener = {
    handleGlobal,
    handleGlobalRemove,
};

// Connects to the compositor (WAYLAND_DISPLAY, or wayland-0 when unset),
// lists the globals it offers and disconnects again. Qt opens its own
// connection afterwards.
WaylandProbe probeWayland()
{
    WaylandProbe probe;
    wl_display *display = wl_display_connect(nullptr);
    if (!display)
        return probe;

    if (wl_registry *registry = wl_display_get_registry(display)) {
        wl_registry_add_listener(registry, &registryListener, &probe);
        // A single roundtrip delivers every global the compositor announces.
        probe.connected = wl_display_roundtrip(display) >= 0;
        wl_registry_destroy(registry);
    }
    wl_display_disconnect(display);
    return probe;
}

bool hasPlatformArgument(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-platform") == 0 || std::strcmp(argv[i], "--platform") == 0)
            return true;
    }
    return false;
}

} // namespace

void WindowSystem::selectPlatform(int argc, char **argv)
{
    // A compositor-provided socket (WAYLAND_SOCKET) accepts one connection
    // only, and that one belongs to Qt.
    const bool canProbe = !qEnvironmentVariableIsSet("WAYLAND_SOCKET");
    const QByteArray requested = qgetenv("QT_QPA_PLATFORM");

    if (hasPlatformArgument(argc, argv))
        return;

    if (!requested.isEmpty()) {
        // The user's choice wins, but say early why the shell will look wrong.
        if (requested.startsWith("wayland") && canProbe) {
            const WaylandProbe probe = probeWayland();
            if (probe.connected && !probe.layerShell) {
                qWarning("NewTeller: this Wayland compositor does not support wlr-layer-shell, so the "
                         "desktop, panel and start menu will be ordinary windows. QT_QPA_PLATFORM=%s "
                         "is set; unset it to run through XWayland instead.",
                         requested.constData());
            }
        }
        return;
    }

    const bool waylandSession = !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        || qgetenv("XDG_SESSION_TYPE") == "wayland";
    if (!waylandSession || !canProbe)
        return; // X11 session, or nothing we can check: Qt decides.

    const WaylandProbe probe = probeWayland();
    if (!probe.connected || probe.layerShell)
        return; // Native Wayland with layer surfaces, or Qt reports the connection problem.

    if (!qEnvironmentVariableIsEmpty("DISPLAY")) {
        qInfo("NewTeller: this Wayland compositor does not support wlr-layer-shell "
              "(GNOME does not, for example); using XWayland instead.");
        qputenv("QT_QPA_PLATFORM", "xcb");
        s_forcedXcb = true;
    } else {
        qWarning("NewTeller: this Wayland compositor does not support wlr-layer-shell and XWayland "
                 "is not available, so the desktop, panel and start menu will be ordinary windows.");
    }
}

void WindowSystem::finishSetup()
{
    // Qt has read QT_QPA_PLATFORM by now. Remove it again so applications
    // started from the shell are not pushed onto XWayland as well.
    if (s_forcedXcb)
        qunsetenv("QT_QPA_PLATFORM");

    // The org.kde.layershell attached properties in the QML files only apply
    // on Wayland. On X11, layer-shell-qt would print a warning for every window.
    if (isX11())
        QLoggingCategory::setFilterRules(QStringLiteral("layershellqt.warning=false"));
}

bool WindowSystem::isX11()
{
    return QGuiApplication::platformName() == QLatin1String("xcb");
}
