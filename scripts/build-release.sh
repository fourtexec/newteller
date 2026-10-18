#!/usr/bin/env bash
# Build and stage a native Debian 13 or Ubuntu 26.04 x86_64 release ZIP.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
sha=${1:-$(git rev-parse HEAD)}
if [[ ! $sha =~ ^[0-9a-f]{40}$ ]]; then
    echo 'Expected a full 40-character commit SHA.' >&2
    exit 1
fi
if [[ $(uname -m) != x86_64 ]]; then
    echo 'This release script targets x86_64.' >&2
    exit 1
fi

source "$root/scripts/platform.sh"
newteller_detect_platform
name="newteller-commit-${sha}-${NEWTELLER_TARGET}"
stage="$root/build/release/$name"
build_dir="$root/build"
# A previous `sudo cmake`/build can leave generated files owned by root.
# Fail early with the right fix instead of reporting a misleading Qt dependency error.
unwritable_path=''
if [[ -e "$build_dir" ]]; then
    unwritable_path=$(find "$build_dir" ! -w -print -quit 2>/dev/null || true)
fi
if [[ -n "$unwritable_path" ]]; then
    cat >&2 <<EOF
Build directory is not writable by $(id -un): $build_dir
It may contain files created by running the build with sudo. Fix ownership once with:
    sudo chown -R "$(id -u):$(id -g)" "$build_dir"
Then rerun scripts/build-release.sh as your normal user (not with sudo).
EOF
    exit 1
fi
if ! cmake -S "$root" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DNEWTELLER_WITH_RUST=ON; then
    cat >&2 <<'HELP'

Release configuration failed. On Debian 13 or Ubuntu 26.04, install the build dependencies first:
    sudo bash scripts/install-build-deps.sh
Then rerun scripts/build-release.sh as your normal user.

Qt runtime packages alone do not provide Qt6Config.cmake: qt6-base-dev,
qt6-declarative-dev and qt6-svg-dev are required, along with Qt6 LayerShellQt,
libwayland-dev and libxcb1-dev. Releases also require the Rust toolchain
(rustc and cargo) for the newteller-core library and newteller-ctl.
For a custom Qt SDK (6.4 or newer), set CMAKE_PREFIX_PATH to its install prefix
or Qt6_DIR to the directory containing Qt6Config.cmake, and ensure LayerShellQt
was built against the same Qt version. See the CMake error above for details.
HELP
    exit 1
fi
cmake --build "$root/build" --parallel "$(nproc)"
# Do not let a rerun leave stale files in the archive.
rm -rf -- "$stage"
mkdir -p "$stage" "$root/dist"
DESTDIR="$stage" cmake --install "$root/build" --strip
install -m 644 README.md "$stage/README.md"
install -m 755 scripts/install-runtime-deps.sh "$stage/install-runtime-deps.sh"
install -m 644 scripts/platform.sh "$stage/platform.sh"
install -m 755 scripts/install-release.sh "$stage/install.sh"
printf 'commit=%s\ntarget=%s\n' "$sha" "$NEWTELLER_TARGET" > "$stage/BUILDINFO"

# QML and built-in themes are compiled into newteller via resources.qrc.
test -x "$stage/usr/bin/newteller"
test -x "$stage/usr/bin/newteller-ctl"
for plugin in desktop-clock desktop-sysinfo panel-system-monitor; do
    test -f "$stage/usr/share/newteller/plugins/$plugin/manifest.json"
    test -f "$stage/usr/share/newteller/plugins/$plugin/Widget.qml"
done
test -f "$stage/usr/share/applications/org.newteller.Shell.desktop"
test -f "$stage/usr/share/newteller/newteller-autostart.desktop"
archive="$root/dist/$name.zip"
rm -f -- "$archive"
(cd "$(dirname "$stage")" && zip -qr "$archive" "$name")
(cd "$root/dist" && sha256sum "$name.zip" > "$name.zip.sha256")
printf 'Created %s\n' "$archive"
