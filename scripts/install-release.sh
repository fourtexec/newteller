#!/usr/bin/env bash
# Included as install.sh at the root of the release ZIP.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ $EUID -ne 0 ]]; then
    echo 'Run sudo bash install.sh from the extracted release directory.' >&2
    exit 1
fi
if [[ $(uname -m) != x86_64 ]]; then
    echo 'This archive requires x86_64.' >&2
    exit 1
fi
# Reject a binary built for a different distribution before installing anything.
source "$root/platform.sh"
newteller_detect_platform
if ! grep -Fxq "target=$NEWTELLER_TARGET" "$root/BUILDINFO"; then
    echo "Wrong release archive. Download the $NEWTELLER_TARGET ZIP for this system." >&2
    exit 1
fi
# Validate before modifying the system.
test -f "$root/usr/bin/newteller"
test -f "$root/usr/bin/newteller-compositor"
test -f "$root/usr/bin/newteller-session"
test -f "$root/usr/bin/newteller-ctl"
test -f "$root/usr/bin/newteller-screenshot"
test -d "$root/usr/share/newteller/plugins"
test -f "$root/usr/share/applications/org.newteller.Shell.desktop"
test -f "$root/usr/share/wayland-sessions/newteller.desktop"
bash "$root/install-runtime-deps.sh"
install -m 755 "$root/usr/bin/newteller" "$root/usr/bin/newteller-compositor" \
    "$root/usr/bin/newteller-ctl" /usr/bin/
install -m 755 "$root/usr/bin/newteller-session" "$root/usr/bin/newteller-screenshot" /usr/bin/
install -d /usr/share/newteller/plugins /usr/share/applications /usr/share/wayland-sessions
cp -a "$root/usr/share/newteller/." /usr/share/newteller/
install -m 644 "$root/usr/share/applications/org.newteller.Shell.desktop" \
    /usr/share/applications/org.newteller.Shell.desktop
install -m 644 "$root/usr/share/wayland-sessions/newteller.desktop" \
    /usr/share/wayland-sessions/newteller.desktop
printf 'Installed NewTeller. Log out and choose the NewTeller session at the login screen.\n'
