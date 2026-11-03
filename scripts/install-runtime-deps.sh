#!/usr/bin/env bash
# Run as root on a supported system. Shared by CI and releases.
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo 'Run this script as root (sudo bash install-runtime-deps.sh).' >&2
    exit 1
fi
source "$(dirname -- "${BASH_SOURCE[0]}")/platform.sh"
newteller_detect_platform

# Desktop services the session starts when they are installed: the screen
# locker and idle manager, night light, screen sharing portals, a polkit
# agent for administrator prompts, and the network (nm-applet), Bluetooth
# (blueman) and sound (pavucontrol) tools that GNOME and KDE users expect.
# Each one is optional, so a package that a release of the distribution
# renamed or dropped never blocks the install.

# power-profiles-daemon conflicts with other power managers; never let the
# package manager remove one the user chose.
power_manager_installed() {
    local manager
    for manager in tlp laptop-mode-tools tuned tuned-ppd auto-cpufreq; do
        case "$NEWTELLER_FAMILY" in
            debian) dpkg -s "$manager" > /dev/null 2>&1 && return 0 ;;
            arch) pacman -Qi "$manager" > /dev/null 2>&1 && return 0 ;;
            fedora) rpm -q "$manager" > /dev/null 2>&1 && return 0 ;;
        esac
    done
    return 1
}

install_debian() {
    local wlroots_package=libwlroots-0.18
    if [[ "$NEWTELLER_TARGET" == ubuntu26.04-* ]]; then
        wlroots_package=libwlroots-0.19
    fi
    apt-get update
    local packages=(
        libqt6core6t64 libqt6gui6 libqt6dbus6 libqt6qml6 libqt6quick6 libqt6svg6
        liblayershellqtinterface6 layer-shell-qt
        qt6-qpa-plugins qt6-wayland qt6-image-formats-plugins
        qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtquick-layouts
        qml6-module-qtqml qml6-module-qtqml-models qml6-module-qtqml-workerscript
        qml6-module-qt-labs-folderlistmodel
        "$wlroots_package" xwayland dbus dbus-user-session libglib2.0-bin
        libxkbcommon0 libseat1 libinput10 libdrm2 libgbm1 libegl1 libgles2
        libcairo2 libpangocairo-1.0-0
        grim
    )
    # Some packaging splits the QML extension out of layer-shell-qt.
    if apt-cache show qml6-module-org-kde-layershell > /dev/null 2>&1; then
        packages+=(qml6-module-org-kde-layershell)
    fi
    apt-get install -y --no-install-recommends "${packages[@]}"

    local optional=(
        swaylock swayidle wlsunset
        xdg-desktop-portal xdg-desktop-portal-wlr xdg-desktop-portal-gtk
        lxpolkit procps wdisplays
        network-manager-gnome blueman pavucontrol slurp wl-clipboard
    )
    power_manager_installed || optional+=(power-profiles-daemon)
    local available=() package
    for package in "${optional[@]}"; do
        if apt-cache show "$package" > /dev/null 2>&1; then
            available+=("$package")
        else
            echo "note: optional package $package is not available here" >&2
        fi
    done
    if [[ ${#available[@]} -gt 0 ]]; then
        apt-get install -y --no-install-recommends "${available[@]}"
    fi
}

install_arch() {
    # Arch keeps wlroots versions side by side (wlroots0.18, wlroots0.19);
    # the build picks 0.19 when it is there, so the runtime does too.
    pacman -Sy --noconfirm > /dev/null
    local wlroots
    if ! wlroots=$(newteller_arch_wlroots); then
        echo 'Arch packages no wlroots 0.18-0.20 right now.' >&2
        exit 1
    fi
    pacman -Syu --noconfirm --needed \
        qt6-base qt6-declarative qt6-svg qt6-wayland qt6-imageformats layer-shell-qt \
        "$wlroots" xorg-xwayland dbus glib2 libxkbcommon seatd libinput libdrm mesa \
        cairo pango grim

    local optional=(
        swaylock swayidle wlsunset
        xdg-desktop-portal xdg-desktop-portal-wlr xdg-desktop-portal-gtk
        polkit-gnome procps-ng wdisplays
        network-manager-applet blueman pavucontrol slurp wl-clipboard
    )
    power_manager_installed || optional+=(power-profiles-daemon)
    local available=() package
    for package in "${optional[@]}"; do
        if pacman -Si "$package" > /dev/null 2>&1; then
            available+=("$package")
        else
            echo "note: optional package $package is not available here" >&2
        fi
    done
    if [[ ${#available[@]} -gt 0 ]]; then
        pacman -S --noconfirm --needed "${available[@]}"
    fi
}

install_fedora() {
    # The wlroots library by its soname, whatever the package is called in
    # this Fedora release (wlroots, wlroots0.18, ...).
    local wlroots='libwlroots-0.19.so()(64bit)'
    if ! dnf -q repoquery --whatprovides "$wlroots" 2>/dev/null | grep -q .; then
        wlroots='libwlroots-0.18.so()(64bit)'
    fi
    dnf install -y \
        qt6-qtbase qt6-qtbase-gui qt6-qtdeclarative qt6-qtsvg qt6-qtwayland \
        qt6-qtimageformats layer-shell-qt \
        "$wlroots" xorg-x11-server-Xwayland dbus-daemon glib2 libxkbcommon libseat \
        libinput libdrm mesa-libgbm mesa-libEGL mesa-libGLES cairo pango grim

    local optional=(
        swaylock swayidle wlsunset
        xdg-desktop-portal xdg-desktop-portal-wlr xdg-desktop-portal-gtk
        lxpolkit procps-ng wdisplays
        network-manager-applet blueman pavucontrol slurp wl-clipboard
    )
    power_manager_installed || optional+=(power-profiles-daemon)
    # dnf5 skips what this release does not have.
    dnf install -y --skip-unavailable "${optional[@]}"
}

case "$NEWTELLER_FAMILY" in
    debian) install_debian ;;
    arch) install_arch ;;
    fedora) install_fedora ;;
esac

if ! find /usr/lib /usr/lib64 -path '*/qt6/qml/org/kde/layershell/qmldir' -print -quit 2>/dev/null \
        | grep -q .; then
    echo 'The org.kde.layershell Qt6 QML module is missing.' >&2
    exit 1
fi
