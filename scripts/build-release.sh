#!/usr/bin/env bash
# Build and stage an installable Debian 13 x86_64 release ZIP.
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

name="newteller-commit-${sha}-debian13-x86_64"
stage="$root/build/release/$name"
cmake -S "$root" -B "$root/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$root/build" --parallel "$(nproc)"
# Do not let a rerun leave stale files in the archive.
rm -rf -- "$stage"
mkdir -p "$stage" "$root/dist"
DESTDIR="$stage" cmake --install "$root/build" --strip
install -m 644 README.md "$stage/README.md"
install -m 755 scripts/install-runtime-deps.sh "$stage/install-runtime-deps.sh"
install -m 755 scripts/install-release.sh "$stage/install.sh"
printf 'commit=%s\ntarget=debian13-x86_64\n' "$sha" > "$stage/BUILDINFO"

# QML and built-in themes are compiled into newteller via resources.qrc.
test -x "$stage/usr/bin/newteller"
test -f "$stage/usr/share/newteller/plugins/desktop-clock/manifest.json"
test -f "$stage/usr/share/newteller/plugins/desktop-clock/Widget.qml"
test -f "$stage/usr/share/applications/org.newteller.Shell.desktop"
test -f "$stage/usr/share/newteller/newteller-autostart.desktop"
archive="$root/dist/$name.zip"
rm -f -- "$archive"
(cd "$(dirname "$stage")" && zip -qr "$archive" "$name")
(cd "$root/dist" && sha256sum "$name.zip" > "$name.zip.sha256")
printf 'Created %s\n' "$archive"
