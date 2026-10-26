#!/usr/bin/env bash
# Start a real NewTeller compositor without a display, run the shell and a
# native client in it, and capture the result through wlr-screencopy.
set -euo pipefail
# Name the command that failed. Without this, a `bash -x` trace of a failed run
# ends inside the EXIT trap (the cleanup kills the compositor) and the actual
# error is buried far above it.
set -E
trap 'status=$?; echo "session smoke: FAILED (exit $status) at ${BASH_SOURCE[0]}:${LINENO}: ${BASH_COMMAND}" >&2' ERR
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${NEWTELLER_BUILD_DIR:-$root/build}
compositor="$build/newteller-compositor"
shell="$build/newteller"

if [[ ! -x "$compositor" || ! -x "$shell" ]]; then
    echo "Build newteller and newteller-compositor first ($build)." >&2
    exit 1
fi
if ! command -v grim >/dev/null 2>&1; then
    echo 'grim is required for the session smoke test (apt install grim).' >&2
    exit 1
fi

# Software rendering is the common denominator for the compositor and clients
# when the test runs in a container without a GPU.
if [[ ${WLR_RENDERER:-} == pixman ]]; then
    export QT_QUICK_BACKEND=software
    export LIBGL_ALWAYS_SOFTWARE=1
    export GSK_RENDERER=cairo
fi

# A private session bus gives the shell somewhere to export org.newteller.Shell.
# The start menu is opened through it: the dock pill is centred on the bar, so
# there is no fixed click coordinate that reliably hits the Start button.
# Without dbus-daemon the menu leg is skipped; everything else still runs.
bus_pid=''
if [[ -z ${DBUS_SESSION_BUS_ADDRESS:-} ]] && command -v dbus-daemon >/dev/null 2>&1; then
    bus_info=$(dbus-daemon --session --fork --print-address=1 --print-pid=1 2>/dev/null || true)
    bus_address=$(printf '%s\n' "$bus_info" | sed -n 1p)
    bus_pid=$(printf '%s\n' "$bus_info" | sed -n 2p)
    if [[ -n $bus_address ]]; then
        export DBUS_SESSION_BUS_ADDRESS="$bus_address"
    fi
fi

runtime=$(mktemp -d "${TMPDIR:-/tmp}/newteller-session-test.XXXXXX")
chmod 700 "$runtime"
socket=newteller-smoke
log="$runtime/compositor.log"
before="$runtime/before.png"
after="$runtime/after.png"
compositor_pid=''
client_pid=''

cleanup() {
    if [[ -n $client_pid ]]; then
        kill "$client_pid" 2>/dev/null || true
        wait "$client_pid" 2>/dev/null || true
    fi
    if [[ -n $compositor_pid ]]; then
        kill -TERM "$compositor_pid" 2>/dev/null || true
        for _ in $(seq 1 20); do
            kill -0 "$compositor_pid" 2>/dev/null || break
            sleep 0.1
        done
        kill -KILL "$compositor_pid" 2>/dev/null || true
        wait "$compositor_pid" 2>/dev/null || true
    fi
    if [[ -n ${bus_pid:-} ]]; then
        kill "$bus_pid" 2>/dev/null || true
    fi
    rm -rf "$runtime"
}
trap cleanup EXIT

XDG_RUNTIME_DIR="$runtime" "$compositor" \
    --headless=1280x800 --socket "$socket" --no-xwayland \
    --shell "$shell" >"$log" 2>&1 &
compositor_pid=$!

for _ in $(seq 1 100); do
    [[ -S "$runtime/$socket" ]] && break
    if ! kill -0 "$compositor_pid" 2>/dev/null; then
        cat "$log" >&2
        exit 1
    fi
    sleep 0.1
done
if [[ ! -S "$runtime/$socket" ]]; then
    echo 'the compositor did not create its Wayland socket' >&2
    cat "$log" >&2
    exit 1
fi

# Give the shell time to create the desktop and panel layer surfaces and render
# the first frame (cold caches on a CI runner can take a moment).
sleep 3
XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" grim -o HEADLESS-1 "$before"

# Exercise the virtual-pointer protocol. The click is a smoke test of the
# protocol only (it lands on empty desktop); the menu is opened over D-Bus
# below, where the result can be asserted.
if command -v wlrctl >/dev/null 2>&1; then
    XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" \
        wlrctl pointer move 40 0 >/dev/null 2>&1 || true
    XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" \
        wlrctl pointer click left >/dev/null 2>&1 || true
fi

python=${PYTHON:-python3}
if "$python" -c 'import PySide6' >/dev/null 2>&1; then
    XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" \
    QT_QPA_PLATFORM=wayland "$python" "$root/tests/session_client.py" &
    client_pid=$!
elif command -v zenity >/dev/null 2>&1; then
    XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" GDK_BACKEND=wayland \
        zenity --info --title='NewTeller session test' \
        --text='A native Wayland application inside NewTeller.' &
    client_pid=$!
else
    echo 'A test client is required: install PySide6 or zenity.' >&2
    exit 1
fi

sleep 2
XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" grim -o HEADLESS-1 "$after"
"$python" "$root/tests/session_check.py" "$before" "$after"

# Exercise the Print-key helper too: it captures the same output through
# wlr-screencopy and writes a PNG under XDG_PICTURES_DIR/Screenshots.
if command -v grim >/dev/null 2>&1; then
    shots="$runtime/shots"
    XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" XDG_PICTURES_DIR="$shots" \
        "$root/scripts/newteller-screenshot" >/dev/null
    shot=$(find "$shots" -type f -name '*.png' -print -quit 2>/dev/null || true)
    if [[ -z $shot || ! -s $shot ]]; then
        echo 'the screenshot helper did not write a PNG' >&2
        exit 1
    fi
fi

# The start menu, opened through org.newteller.Shell.showStartMenu. This is the
# only deterministic way to hit the Start button: the dock pill hugs its
# contents and is centred on the bar, so its offset depends on the number of
# dock entries, the clock format and the font metrics of the host.
has_shell_name() {
    command -v gdbus >/dev/null 2>&1 || return 1
    [[ -n ${DBUS_SESSION_BUS_ADDRESS:-} ]] || return 1
    gdbus call --session --dest org.freedesktop.DBus \
        --object-path /org/freedesktop/DBus \
        --method org.freedesktop.DBus.NameHasOwner org.newteller.Shell 2>/dev/null \
        | grep -q true
}

if has_shell_name; then
    menu_before="$runtime/menu-before.png"
    menu="$runtime/menu.png"
    XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" grim -o HEADLESS-1 "$menu_before"
    if XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" \
        gdbus call --session --dest org.newteller.Shell \
            --object-path /org/newteller/Shell \
            --method org.newteller.Shell.showStartMenu >/dev/null 2>&1; then
        sleep 1
        XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" grim -o HEADLESS-1 "$menu"
        "$python" "$root/tests/session_check.py" "$menu_before" "$menu"
    else
        echo 'the shell did not open the start menu over D-Bus' >&2
        exit 1
    fi
else
    echo 'session smoke: no session bus or gdbus, skipping the start menu check' >&2
fi

# Closing a normal window destroys a View that also owns a foreign-toplevel
# handle. This used to corrupt the native/foreign listener lists and take the
# compositor down, so keep the session alive and prove the window really went
# away.
if [[ -n $client_pid ]]; then
    kill "$client_pid" 2>/dev/null || true
    wait "$client_pid" 2>/dev/null || true
    client_pid=''
    sleep 1
    if ! kill -0 "$compositor_pid" 2>/dev/null; then
        echo 'the compositor died when the test window closed' >&2
        cat "$log" >&2
        exit 1
    fi
fi
