#!/usr/bin/env bash
# Install development and runtime dependencies on supported Debian/Ubuntu systems.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

if [[ $EUID -ne 0 ]]; then
    echo 'Run this script as root (sudo bash scripts/install-build-deps.sh).' >&2
    exit 1
fi
source "$(dirname -- "${BASH_SOURCE[0]}")/platform.sh"
newteller_detect_platform

wlroots_package=libwlroots-0.18-dev
if [[ "$NEWTELLER_TARGET" == ubuntu26.04-* ]]; then
    wlroots_package=libwlroots-0.19-dev
fi

apt-get update
apt-get install -y --no-install-recommends \
    ca-certificates git build-essential cmake ninja-build zip \
    qt6-base-dev qt6-declarative-dev qt6-svg-dev \
    liblayershellqtinterface-dev libwayland-dev libxcb1-dev \
    pkg-config libxkbcommon-dev libseat-dev libdrm-dev libgbm-dev \
    libinput-dev libpixman-1-dev libdisplay-info-dev libliftoff-dev \
    libudev-dev libvulkan-dev libegl-dev libgles-dev wayland-protocols \
    libx11-xcb-dev libxcb-randr0-dev libxcb-shape0-dev libxcb-sync-dev \
    libxcb-dri3-dev libxcb-present-dev libxcb-render0-dev \
    libxcb-render-util0-dev libxcb-shm0-dev libxcb-xfixes0-dev \
    libxcb-xinput-dev libxcb-composite0-dev libxcb-ewmh-dev \
    libxcb-errors-dev libxcb-icccm4-dev libxcb-res0-dev libxcb-image0-dev \
    rustc cargo python3 python3-venv python3-pil grim wlrctl \
    "$wlroots_package"

bash "$root/scripts/install-runtime-deps.sh"
