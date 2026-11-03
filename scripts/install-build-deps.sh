#!/usr/bin/env bash
# Install development and runtime dependencies on a supported system
# (Debian 13, Ubuntu 26.04, Arch Linux, Fedora 42+).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

if [[ $EUID -ne 0 ]]; then
    echo 'Run this script as root (sudo bash scripts/install-build-deps.sh).' >&2
    exit 1
fi
source "$(dirname -- "${BASH_SOURCE[0]}")/platform.sh"
newteller_detect_platform

case "$NEWTELLER_FAMILY" in
debian)
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
        libinput-dev libcairo2-dev libpango1.0-dev libpixman-1-dev libdisplay-info-dev libliftoff-dev \
        libudev-dev libvulkan-dev libegl-dev libgles-dev wayland-protocols \
        libx11-xcb-dev libxcb-randr0-dev libxcb-shape0-dev libxcb-sync-dev \
        libxcb-dri3-dev libxcb-present-dev libxcb-render0-dev \
        libxcb-render-util0-dev libxcb-shm0-dev libxcb-xfixes0-dev \
        libxcb-xinput-dev libxcb-composite0-dev libxcb-ewmh-dev \
        libxcb-errors-dev libxcb-icccm4-dev libxcb-res0-dev libxcb-image0-dev \
        libpolkit-agent-1-dev \
        rustc cargo python3 python3-venv python3-pil grim wlrctl wlr-randr \
        "$wlroots_package"
    ;;
arch)
    pacman -Sy --noconfirm > /dev/null
    if ! wlroots=$(newteller_arch_wlroots); then
        echo 'Arch packages no wlroots 0.18-0.20 right now:' >&2
        pacman -Sl | awk '$2 ~ /^wlroots/' >&2
        exit 1
    fi
    # Arch ships headers with the libraries; base-devel brings the compiler.
    pacman -Syu --noconfirm --needed \
        base-devel git zip cmake ninja pkgconf \
        qt6-base qt6-declarative qt6-svg qt6-wayland layer-shell-qt \
        wayland wayland-protocols libxkbcommon libinput seatd libdrm mesa \
        cairo pango pixman libxcb xcb-util-wm xcb-util-renderutil xcb-util-errors polkit \
        "$wlroots" rust python python-pillow grim wlr-randr
    # Test helpers that are not in every snapshot of the repositories.
    for helper in wlrctl; do
        if pacman -Si "$helper" > /dev/null 2>&1; then
            pacman -S --noconfirm --needed "$helper"
        fi
    done
    ;;
fedora)
    # Development packages by what they provide, so a renamed package in a
    # newer Fedora still resolves.
    wlroots='pkgconfig(wlroots-0.19)'
    if ! dnf -q repoquery --whatprovides "$wlroots" 2>/dev/null | grep -q .; then
        wlroots='pkgconfig(wlroots-0.18)'
    fi
    dnf install -y \
        git zip gcc gcc-c++ make cmake ninja-build pkgconf-pkg-config \
        qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel qt6-qtwayland-devel \
        layer-shell-qt-devel \
        'pkgconfig(wayland-client)' 'pkgconfig(wayland-server)' 'pkgconfig(wayland-protocols)' \
        'pkgconfig(xkbcommon)' 'pkgconfig(libinput)' 'pkgconfig(libseat)' 'pkgconfig(libdrm)' \
        'pkgconfig(gbm)' 'pkgconfig(cairo)' 'pkgconfig(pangocairo)' 'pkgconfig(pixman-1)' \
        'pkgconfig(xcb)' 'pkgconfig(polkit-agent-1)' \
        "$wlroots" rust cargo python3 python3-pillow grim wlr-randr
    dnf install -y --skip-unavailable wlrctl
    ;;
esac

bash "$root/scripts/install-runtime-deps.sh"
