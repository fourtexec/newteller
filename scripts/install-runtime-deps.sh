#!/usr/bin/env bash
# Run as root on Debian 13. Shared by CI and the release installer.
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo 'Run this script as root (sudo bash install-runtime-deps.sh).' >&2
    exit 1
fi
source /etc/os-release
if [[ ${ID:-} != debian || ${VERSION_ID:-} != 13 ]]; then
    echo 'This build targets Debian 13 (trixie). Other distributions are not supported by this installer.' >&2
    exit 1
fi

apt-get update
packages=(
    libqt6core6t64 libqt6gui6 libqt6qml6 libqt6quick6 libqt6svg6
    liblayershellqtinterface6 layer-shell-qt
    qt6-qpa-plugins qt6-wayland qt6-image-formats-plugins
    qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtquick-layouts
    qml6-module-qtqml qml6-module-qtqml-models qml6-module-qtqml-workerscript
    qml6-module-qt-labs-folderlistmodel
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
