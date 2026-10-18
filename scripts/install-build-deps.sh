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

apt-get update
apt-get install -y --no-install-recommends \
    ca-certificates git build-essential cmake ninja-build zip \
    qt6-base-dev qt6-declarative-dev qt6-svg-dev \
    liblayershellqtinterface-dev libwayland-dev libxcb1-dev \
    rustc cargo python3 python3-venv
bash "$root/scripts/install-runtime-deps.sh"
