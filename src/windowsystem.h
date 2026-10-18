#pragma once

// Chooses how NewTeller talks to the display server.
//
// The desktop, panel and start menu are wlr-layer-shell surfaces on Wayland.
// Compositors that implement that protocol (KDE Plasma, sway, Hyprland,
// labwc, Wayfire, niri, ...) get real layer surfaces. Compositors that do
// not (GNOME/Mutter, for example) would leave the shell as ordinary floating
// windows, so on those NewTeller runs through XWayland instead, where
// ShellWindow marks the windows as desktop/dock for the X11 window manager.
namespace WindowSystem {

// Call before constructing QGuiApplication. Picks the xcb platform when the
// Wayland compositor lacks wlr-layer-shell and XWayland is available. An
// explicit QT_QPA_PLATFORM or -platform argument is always respected.
void selectPlatform(int argc, char **argv);

// Call right after constructing QGuiApplication. Undoes the environment
// change made by selectPlatform() so applications launched from the shell
// keep using native Wayland, and quiets layer-shell warnings on X11.
void finishSetup();

// True when running on X11, natively or through XWayland.
bool isX11();

} // namespace WindowSystem
