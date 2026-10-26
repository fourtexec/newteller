#!/usr/bin/env bash
# Run as root on a supported Debian/Ubuntu system. Shared by CI and releases.
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo 'Run this script as root (sudo bash install-runtime-deps.sh).' >&2
    exit 1
fi
source "$(dirname -- "${BASH_SOURCE[0]}")/platform.sh"
newteller_detect_platform

wlroots_package=libwlroots-0.18
if [[ "$NEWTELLER_TARGET" == ubuntu26.04-* ]]; then
    wlroots_package=libwlroots-0.19
fi

apt-get update
packages=(
    libqt6core6t64 libqt6gui6 libqt6dbus6 libqt6qml6 libqt6quick6 libqt6svg6
    liblayershellqtinterface6 layer-shell-qt
    qt6-qpa-plugins qt6-wayland qt6-image-formats-plugins
    qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtquick-layouts
    qml6-module-qtqml qml6-module-qtqml-models qml6-module-qtqml-workerscript
    qml6-module-qt-labs-folderlistmodel
    "$wlroots_package" xwayland dbus dbus-user-session libglib2.0-bin
    libxkbcommon0 libseat1 libinput10 libdrm2 libgbm1 libegl1 libgles2
    grim
)
# Some packaging splits the QML extension out of layer-shell-qt.
if apt-cache show qml6-module-org-kde-layershell > /dev/null 2>&1; then
    packages+=(qml6-module-org-kde-layershell)
fi
apt-get install -y --no-install-recommends "${packages[@]}"

if ! find /usr/lib -path '*/qt6/qml/org/kde/layershell/qmldir' -print -quit | grep -q .; then
    echo 'The org.kde.layershell Qt6 QML module is missing.' >&2
    exit 1
fi
