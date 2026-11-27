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
# Firmware packages are large; a container or a CI job has no Wi-Fi adapter
# that could use them.
wants_firmware() {
    [[ -z ${CI:-} && -z ${GITHUB_ACTIONS:-} ]] || return 1
    [[ ! -e /.dockerenv && ! -e /run/.containerenv ]] || return 1
    return 0
}

# NetworkManager runs the Wi-Fi; start it on a booted systemd machine (not
# in a container or a chroot).
enable_network_manager() {
    [[ -d /run/systemd/system ]] || return 0
    command -v systemctl > /dev/null 2>&1 || return 0
    systemctl enable --now NetworkManager.service > /dev/null 2>&1 \
        || echo 'note: could not start NetworkManager' >&2
}

# Whether apt can install the package. `apt-cache show` also succeeds for
# names that are only mentioned by other packages (Debian firmware names on
# Ubuntu), which then make `apt-get install` fail; a real candidate version
# is what counts.
apt_has() {
    local candidate
    candidate=$(apt-cache policy "$1" 2>/dev/null | awk '/Candidate:/ { print $2; exit }')
    [[ -n $candidate && $candidate != '(none)' ]]
}

# A terminal (Super+Return) and a file manager (Super+E, desktop folders)
# are only added when the system has none, so nobody gets a second one.
has_any_program() {
    local program
    for program in "$@"; do
        command -v "$program" > /dev/null 2>&1 && return 0
    done
    return 1
}
# CI jobs never open a window: no extra applications there.
in_ci() {
    [[ -n ${CI:-} || -n ${GITHUB_ACTIONS:-} ]]
}
has_terminal() {
    has_any_program x-terminal-emulator foot kitty alacritty wezterm ghostty konsole \
        gnome-terminal ptyxis kgx xfce4-terminal tilix terminator qterminal lxterminal xterm
}
has_file_manager() {
    has_any_program nautilus dolphin thunar Thunar pcmanfm pcmanfm-qt nemo caja
}

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
        # The updater unpacks release archives and checks TLS certificates.
        unzip ca-certificates
        # The administrator prompt (polkit agent) is linked into the shell.
        libpolkit-agent-1-0
    )
    # Some packaging splits the QML extension out of layer-shell-qt.
    if apt_has qml6-module-org-kde-layershell; then
        packages+=(qml6-module-org-kde-layershell)
    fi
    apt-get install -y --no-install-recommends "${packages[@]}"

    local optional=(
        gtklock swaylock swayidle wlsunset polkitd pkexec
        xdg-desktop-portal xdg-desktop-portal-wlr xdg-desktop-portal-gtk
        lxpolkit procps wdisplays
        network-manager wpasupplicant network-manager-gnome
        blueman pavucontrol slurp wl-clipboard sound-theme-freedesktop udisks2
        wf-recorder pulseaudio-utils fonts-noto-color-emoji wtype
    )
    # Wi-Fi firmware for the common adapters (Debian: non-free-firmware;
    # Ubuntu: linux-firmware). Missing firmware is the usual reason an
    # adapter shows up but never finds a network.
    wants_firmware && optional+=(linux-firmware firmware-iwlwifi firmware-realtek
                                 firmware-atheros firmware-brcm80211 firmware-misc-nonfree)
    power_manager_installed || optional+=(power-profiles-daemon)
    optional+=(xdg-utils)
    in_ci || has_terminal || optional+=(foot)
    in_ci || has_file_manager || optional+=(thunar)
    local available=() package
    for package in "${optional[@]}"; do
        if apt_has "$package"; then
            available+=("$package")
        else
            echo "note: optional package $package is not available here" >&2
        fi
    done
    # Optional packages never stop the installation: if the batch fails,
    # each one is tried on its own.
    if [[ ${#available[@]} -gt 0 ]] \
        && ! apt-get install -y --no-install-recommends "${available[@]}"; then
        for package in "${available[@]}"; do
            apt-get install -y --no-install-recommends "$package" \
                || echo "note: optional package $package could not be installed" >&2
        done
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
        cairo pango grim polkit unzip

    local optional=(
        gtklock swaylock swayidle wlsunset
        xdg-desktop-portal xdg-desktop-portal-wlr xdg-desktop-portal-gtk
        polkit-gnome procps-ng wdisplays
        networkmanager wpa_supplicant network-manager-applet
        blueman pavucontrol slurp wl-clipboard sound-theme-freedesktop udisks2
        wf-recorder libpulse noto-fonts-emoji wtype
    )
    wants_firmware && optional+=(linux-firmware)
    power_manager_installed || optional+=(power-profiles-daemon)
    optional+=(xdg-utils)
    in_ci || has_terminal || optional+=(foot)
    in_ci || has_file_manager || optional+=(thunar)
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
        libinput libdrm mesa-libgbm mesa-libEGL mesa-libGLES cairo pango grim polkit polkit-libs unzip

    local optional=(
        gtklock swaylock swayidle wlsunset
        xdg-desktop-portal xdg-desktop-portal-wlr xdg-desktop-portal-gtk
        lxpolkit procps-ng wdisplays
        NetworkManager NetworkManager-wifi wpa_supplicant network-manager-applet
        blueman pavucontrol slurp wl-clipboard sound-theme-freedesktop udisks2
        wf-recorder pulseaudio-utils google-noto-color-emoji-fonts wtype
    )
    wants_firmware && optional+=(linux-firmware)
    power_manager_installed || optional+=(power-profiles-daemon)
    optional+=(xdg-utils)
    in_ci || has_terminal || optional+=(foot)
    in_ci || has_file_manager || optional+=(Thunar)
    # dnf5 skips what this release does not have.
    dnf install -y --skip-unavailable "${optional[@]}"
}

case "$NEWTELLER_FAMILY" in
    debian) install_debian ;;
    arch) install_arch ;;
    fedora) install_fedora ;;
esac
enable_network_manager

if ! find /usr/lib /usr/lib64 -path '*/qt6/qml/org/kde/layershell/qmldir' -print -quit 2>/dev/null \
        | grep -q .; then
    echo 'The org.kde.layershell Qt6 QML module is missing.' >&2
    exit 1
fi
