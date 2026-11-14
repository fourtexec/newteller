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
# A private configuration: the frame checks look for the panel against the
# plain themed gradient, so the default wallpaper is switched off here.
export XDG_CONFIG_HOME="$runtime/config"
mkdir -p "$XDG_CONFIG_HOME/newteller"
printf '{\n    "desktop.defaultWallpaper": false\n}\n' > "$XDG_CONFIG_HOME/newteller/shell.json"
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

# The control socket: the shell's settings, the idle manager (swayidle) and
# scripts reach the compositor through `newteller-compositor --msg`.
msg() {
    XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" "$compositor" --msg "$@"
}
reply=$(msg ping || true)
if [[ $reply != 'ok pong' ]]; then
    echo "the control socket did not answer ping ($reply)" >&2
    exit 1
fi
reply=$(msg outputs || true)
if [[ $reply != *'HEADLESS-1:1280x800@'* ]]; then
    echo "the control socket lists no headless output ($reply)" >&2
    exit 1
fi
msg reload >/dev/null
reply=$(msg locked || true)
if [[ $reply != 'ok 0' ]]; then
    echo "a fresh session reports itself locked ($reply)" >&2
    exit 1
fi
# The overview's data: the test window is listed and its thumbnail is read
# back from the renderer into a PNG.
reply=$(msg windows || true)
window_id=$(printf '%s' "${reply#ok }" | cut -d $'\x1f' -f1 | cut -d $'\x1e' -f1)
if [[ $reply != ok* || -z $window_id ]]; then
    echo "the compositor lists no windows ($reply)" >&2
    exit 1
fi
# No polkit agent window: polkit refuses the built-in agent here (no login
# session), and an external agent started anyway only showed an "Error"
# window - the same one users saw at every login.
agent_windows=$(printf '%s' "${reply#ok }" | tr '\037' '\n' \
    | awk -F $'\x1e' 'tolower($3) ~ /polkit/ { print $3 ": " $4 }')
if [[ -n $agent_windows ]]; then
    echo "a polkit agent opened a window at login: $agent_windows" >&2
    exit 1
fi
echo "session smoke: no polkit agent window at login"
reply=$(msg thumbnail "$window_id" 200 || true)
thumb=${reply#ok }
if [[ $reply != ok* || ! -s $thumb ]]; then
    echo "no thumbnail for window $window_id ($reply)" >&2
    exit 1
fi
echo "session smoke: window $window_id has a thumbnail ($(stat -c %s "$thumb") bytes)"

# The window menu's commands: always on top, maximize, minimize, and the
# state letters in `windows` that the menu reads.
window_flags() {
    msg windows | sed 's/^ok //' | tr '\037' '\n' \
        | awk -F $'\x1e' -v id="$window_id" '$1 == id { print $5 }'
}
if [[ $(msg always-on-top "$window_id" on || true) != 'ok 1' ]]; then
    echo "always-on-top does not pin the window" >&2
    exit 1
fi
if [[ $(window_flags) != *t* ]]; then
    echo "a pinned window is not listed as always on top ($(window_flags))" >&2
    exit 1
fi
msg maximize "$window_id" on >/dev/null
if [[ $(window_flags) != *x* ]]; then
    echo "maximize did not maximize the window ($(window_flags))" >&2
    exit 1
fi
msg maximize "$window_id" off >/dev/null
msg minimize "$window_id" >/dev/null
if [[ $(window_flags) != *m* ]]; then
    echo "minimize did not minimize the window ($(window_flags))" >&2
    exit 1
fi
msg minimize "$window_id" off >/dev/null
msg fullscreen "$window_id" on >/dev/null
if [[ $(window_flags) != *F* ]]; then
    echo "fullscreen did not make the window full screen ($(window_flags))" >&2
    exit 1
fi
msg fullscreen "$window_id" off >/dev/null
if [[ $(msg sticky "$window_id" on || true) != 'ok 1' || $(window_flags) != *s* ]]; then
    echo "sticky does not put the window on every desktop ($(window_flags))" >&2
    exit 1
fi
msg sticky "$window_id" off >/dev/null
msg always-on-top "$window_id" off >/dev/null
if [[ $(window_flags) == *[mxtFs]* ]]; then
    echo "the window did not come back to normal ($(window_flags))" >&2
    exit 1
fi
echo "session smoke: window menu commands (on top, maximize, minimize, full screen, every desktop) work"

# A window that asks to be maximized before it is shown (a browser that was
# closed maximized) opens maximized: the request arrives before the first
# configure and must not be lost.
if [[ -n ${client_pid:-} ]] && "$python" -c 'import PySide6' >/dev/null 2>&1; then
    XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" \
    QT_QPA_PLATFORM=wayland "$python" "$root/tests/session_client.py" --maximized &
    maximized_pid=$!
    maximized_flags=''
    for _ in $(seq 1 80); do
        maximized_flags=$(msg windows | sed 's/^ok //' | tr '\037' '\n' \
            | awk -F $'\x1e' '$4 == "NewTeller maximized test" { print $5 }')
        [[ $maximized_flags == *x* ]] && break
        sleep 0.1
    done
    if [[ $maximized_flags != *x* ]]; then
        echo "a window that asked to start maximized is not maximized (flags '$maximized_flags')" >&2
        exit 1
    fi
    kill "$maximized_pid" 2>/dev/null || true
    wait "$maximized_pid" 2>/dev/null || true
    for _ in $(seq 1 50); do
        [[ $(msg windows || true) == *'NewTeller maximized test'* ]] || break
        sleep 0.1
    done
    echo "session smoke: a window that asks for it opens maximized"
fi

# Settings -> Displays goes through output-set.
reply=$(msg outputs-detail || true)
if [[ $reply != ok\ HEADLESS-1* ]]; then
    echo "outputs-detail does not describe the headless output ($reply)" >&2
    exit 1
fi
reply=$(msg output-set HEADLESS-1 on 1280x800@0 0 0 1.50 0 || true)
if [[ $reply != ok ]]; then
    echo "output-set was refused ($reply)" >&2
    exit 1
fi
if [[ $(msg outputs || true) != *':1.50:on'* ]]; then
    echo 'output-set did not change the scale' >&2
    exit 1
fi
msg output-set HEADLESS-1 on 1280x800@0 0 0 1.00 0 >/dev/null
if [[ $(msg output-set HEADLESS-1 off - 0 0 1 0 || true) == ok ]]; then
    echo 'the last screen could be switched off' >&2
    exit 1
fi

# Effects: a blur region behind the panel is drawn without breaking the
# frame (the renderer blurs the scene below the shell layers).
if [[ $(msg blur newteller-panel HEADLESS-1 0 0 400 48 12 || true) != ok ]]; then
    echo 'the compositor refused a blur region' >&2
    exit 1
fi
sleep 0.5
XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" grim -o HEADLESS-1 "$runtime/blur.png"
"$python" "$root/tests/session_check.py" "$before" "$runtime/blur.png"
msg blur-clear newteller-panel HEADLESS-1 >/dev/null
# The frames above came from the effects renderer, not the fallback.
read -r _ effects_state effect_frames fallback_frames <<<"$(msg effects || true)"
if [[ $effects_state != on || ${effect_frames:-0} -lt 10 ]]; then
    echo "the effects renderer did not draw ($effects_state, $effect_frames frames, $fallback_frames fallbacks)" >&2
    exit 1
fi
echo "session smoke: effects renderer drew $effect_frames frames ($fallback_frames fallbacks)"
# An idle desktop is not redrawn: the effects renderer used to draw a new
# frame after every frame, at the full refresh rate, for ever.
sleep 1
read -r _ _ idle_start _ <<<"$(msg effects || true)"
sleep 2
read -r _ _ idle_end _ <<<"$(msg effects || true)"
idle_frames=$(( ${idle_end:-0} - ${idle_start:-0} ))
if (( idle_frames > 60 )); then
    echo "the effects renderer keeps drawing on an idle desktop ($idle_frames frames in 2 s)" >&2
    exit 1
fi
echo "session smoke: an idle desktop drew $idle_frames frames in 2 s"

# Blank and wake the screen the way the idle manager does.
msg dpms off >/dev/null
msg dpms on >/dev/null

# wlr-output-management: a display tool changes the scale, the compositor
# applies it and reports it back.
# A maximized window follows the screen: at scale 2 the screen is 640 wide,
# and the window must not keep its 1280.
window_width() {
    msg windows | sed 's/^ok //' | tr '\037' '\n' \
        | awk -F $'\x1e' -v id="$window_id" '$1 == id { split($7, g, ","); print g[3] }'
}
wait_window_width() {
    local width=''
    for _ in $(seq 1 40); do
        width=$(window_width)
        [[ $width == "$1" ]] && return 0
        sleep 0.1
    done
    echo "the maximized window is ${width:-?} wide, expected $1 ($2)" >&2
    return 1
}
if command -v wlr-randr >/dev/null 2>&1; then
    msg maximize "$window_id" on >/dev/null
    wait_window_width 1280 'maximized'
    XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" \
        wlr-randr --output HEADLESS-1 --scale 2
    reply=$(msg outputs || true)
    if [[ $reply != *':2.00:on'* ]]; then
        echo "wlr-randr could not change the output scale ($reply)" >&2
        exit 1
    fi
    wait_window_width 640 'after the scale went to 2'
    XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" \
        wlr-randr --output HEADLESS-1 --scale 1
    wait_window_width 1280 'after the scale went back to 1'
    msg maximize "$window_id" off >/dev/null
    echo "session smoke: a maximized window is refitted when the screen changes"
fi
sleep 1

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
        # A tap of Super calls toggleStartMenu; on an open menu it closes it.
        menu_closed="$runtime/menu-closed.png"
        if ! XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" \
            gdbus call --session --dest org.newteller.Shell \
                --object-path /org/newteller/Shell \
                --method org.newteller.Shell.toggleStartMenu >/dev/null 2>&1; then
            echo 'the shell did not answer toggleStartMenu over D-Bus' >&2
            exit 1
        fi
        sleep 1
        XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" grim -o HEADLESS-1 "$menu_closed"
        "$python" "$root/tests/session_check.py" "$menu" "$menu_closed"
    else
        echo 'the shell did not open the start menu over D-Bus' >&2
        exit 1
    fi

    # org.freedesktop.Notifications as applications call it (libnotify uses
    # the interface name and the spec's argument order), and replaces_id
    # updates a notification in place with the same id.
    notify() {
        gdbus call --session --dest org.freedesktop.Notifications \
            --object-path /org/freedesktop/Notifications \
            --method org.freedesktop.Notifications.Notify \
            session-smoke "$1" '' 'Download' "$2" '[]' '{}' 'int32 5000' 2>&1
    }
    first=$(notify 0 '10%')
    first_id=$(printf '%s' "$first" | sed -n 's/.*uint32 \([0-9]*\).*/\1/p')
    if [[ -z $first_id ]]; then
        echo "applications cannot send notifications: $first" >&2
        exit 1
    fi
    second=$(notify "$first_id" '90%')
    if [[ $second != *"uint32 $first_id,"* ]]; then
        echo "replacing notification $first_id gave another id: $second" >&2
        exit 1
    fi
    info=$(gdbus call --session --dest org.freedesktop.Notifications \
        --object-path /org/freedesktop/Notifications \
        --method org.freedesktop.Notifications.GetServerInformation 2>&1)
    if [[ $info != "('NewTeller', "* ]]; then
        echo "GetServerInformation is not four strings: $info" >&2
        exit 1
    fi
    echo "session smoke: notifications arrive over D-Bus and update in place (id $first_id)"
    # Applications read the capabilities to decide whether to send markup
    # and whether to play their own sound.
    caps=$(gdbus call --session --dest org.freedesktop.Notifications \
        --object-path /org/freedesktop/Notifications \
        --method org.freedesktop.Notifications.GetCapabilities 2>&1)
    for cap in actions body-markup sound persistence; do
        if [[ $caps != *"'$cap'"* ]]; then
            echo "the notification daemon does not advertise $cap: $caps" >&2
            exit 1
        fi
    done
    echo "session smoke: notification capabilities $caps"

    # A tray application registers its StatusNotifierItem with the shell's
    # watcher through the org.kde.StatusNotifierWatcher interface.
    if ! tray_name=$("$python" - <<'PY'
import os, sys
from PySide6.QtCore import QCoreApplication, QObject, Property, ClassInfo
from PySide6.QtDBus import QDBusConnection, QDBusInterface
app = QCoreApplication(sys.argv)
@ClassInfo({"D-Bus Interface": "org.kde.StatusNotifierItem"})
class Item(QObject):
    Category = Property(str, lambda self: "ApplicationStatus")
    Id = Property(str, lambda self: "session-smoke")
    Title = Property(str, lambda self: "Session smoke tray item")
    Status = Property(str, lambda self: "Active")
    IconName = Property(str, lambda self: "dialog-information")
bus = QDBusConnection.sessionBus()
name = "org.kde.StatusNotifierItem-%d-1" % os.getpid()
item = Item()
bus.registerObject("/StatusNotifierItem", item, QDBusConnection.ExportAllProperties)
bus.registerService(name)
watcher = QDBusInterface("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
                         "org.kde.StatusNotifierWatcher", bus)
reply = watcher.call("RegisterStatusNotifierItem", name)
if reply.errorName():
    print("RegisterStatusNotifierItem failed:", reply.errorName(), reply.errorMessage(),
          file=sys.stderr)
    sys.exit(1)
registered = watcher.property("RegisteredStatusNotifierItems") or []
if not any(name in entry for entry in registered):
    print("the watcher does not list the item:", registered, file=sys.stderr)
    sys.exit(1)
print(name)
PY
    ); then
        echo 'a tray application could not register its icon' >&2
        exit 1
    fi
    echo 'session smoke: tray applications register with the StatusNotifierWatcher'
    # The application has quit: its icon goes away with it.
    tray_items=''
    for _ in $(seq 1 50); do
        tray_items=$(gdbus call --session --dest org.kde.StatusNotifierWatcher \
            --object-path /StatusNotifierWatcher \
            --method org.freedesktop.DBus.Properties.Get \
            org.kde.StatusNotifierWatcher RegisteredStatusNotifierItems 2>/dev/null || true)
        [[ $tray_items != *"$tray_name"* ]] && break
        sleep 0.1
    done
    if [[ -z $tray_name || $tray_items == *"$tray_name"* ]]; then
        echo "the icon of a tray application that quit is still listed ($tray_items)" >&2
        exit 1
    fi
    echo 'session smoke: the icon of a tray application that quits goes away'

    # org.freedesktop.ScreenSaver.Inhibit (video players, XWayland apps)
    # keeps the screen awake while the caller stays on the bus, and is
    # released when it leaves.
    if [[ $(msg idle-inhibit || true) != 'ok 0' ]]; then
        echo "idle is inhibited before anyone asked ($(msg idle-inhibit || true))" >&2
        exit 1
    fi
    "$python" - <<'PY' &
import sys, time
from PySide6.QtCore import QCoreApplication
from PySide6.QtDBus import QDBusConnection, QDBusInterface
app = QCoreApplication(sys.argv)
bus = QDBusConnection.sessionBus()
screensaver = QDBusInterface("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
                             "org.freedesktop.ScreenSaver", bus)
reply = screensaver.call("Inhibit", "session-smoke", "playing a video")
if reply.errorName():
    print("Inhibit failed:", reply.errorName(), reply.errorMessage(), file=sys.stderr)
    sys.exit(1)
time.sleep(3)  # stay on the bus: the inhibition holds until we leave
PY
    inhibitor=$!
    inhibited=
    for _ in $(seq 1 20); do
        if [[ $(msg idle-inhibit || true) == 'ok 1' ]]; then
            inhibited=1
            break
        fi
        sleep 0.1
    done
    if [[ -z $inhibited ]]; then
        echo 'org.freedesktop.ScreenSaver.Inhibit did not keep the screen awake' >&2
        exit 1
    fi
    wait "$inhibitor" || {
        echo 'calling org.freedesktop.ScreenSaver.Inhibit failed' >&2
        exit 1
    }
    released=
    for _ in $(seq 1 30); do
        if [[ $(msg idle-inhibit || true) == 'ok 0' ]]; then
            released=1
            break
        fi
        sleep 0.1
    done
    if [[ -z $released ]]; then
        echo 'an inhibition outlived the application that took it' >&2
        exit 1
    fi
    echo 'session smoke: org.freedesktop.ScreenSaver inhibits idle and lets go'
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

# A crashed shell is restarted; the session and its windows survive.
if command -v pgrep >/dev/null 2>&1; then
    old_shell=$(pgrep -x newteller | head -n 1 || true)
    if [[ -n $old_shell ]]; then
        kill -KILL "$old_shell"
        sleep 4
        if ! kill -0 "$compositor_pid" 2>/dev/null; then
            echo 'the session ended when the shell crashed' >&2
            exit 1
        fi
        new_shell=$(pgrep -x newteller | head -n 1 || true)
        if [[ -z $new_shell || $new_shell == "$old_shell" ]]; then
            echo 'the compositor did not restart the crashed shell' >&2
            exit 1
        fi
        echo "session smoke: the shell came back after a crash ($old_shell -> $new_shell)"
    fi
fi

# Workspaces over the control socket (Ctrl+Super+Left/Right do the same).
msg workspace 1 >/dev/null
msg workspace 0 >/dev/null
# A desktop that is not a number, or does not exist, is refused instead of
# being read as desktop 0.
msg workspace 1 >/dev/null
for bad in two 1x 99 -1; do
    reply=$(msg workspace "$bad" || true)
    if [[ $reply != error* ]]; then
        echo "workspace $bad was accepted ($reply)" >&2
        exit 1
    fi
done
if [[ $(msg workspace next || true) != ok ]]; then
    echo 'workspace next stopped working' >&2
    exit 1
fi
msg workspace 0 >/dev/null
echo "session smoke: bad desktop numbers are refused"

# The lock screen. swaylock takes the session through ext-session-lock-v1:
# the screen shows nothing but the locker, and killing the locker must leave
# the session locked, never fall back to the desktop.
if command -v swaylock >/dev/null 2>&1 && command -v pkill >/dev/null 2>&1; then
    XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" \
        swaylock --daemonize --color 000000 >/dev/null 2>&1 &
    lock_starter=$!
    for _ in $(seq 1 50); do
        [[ $(msg locked || true) == 'ok 1' ]] && break
        sleep 0.1
    done
    if [[ $(msg locked || true) != 'ok 1' ]]; then
        echo 'swaylock could not lock the session' >&2
        exit 1
    fi
    sleep 1
    XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" grim -o HEADLESS-1 "$runtime/locked.png"
    "$python" "$root/tests/session_check.py" --locked "$runtime/locked.png"

    # A screen that moves in the layout while locked (a monitor unplugged
    # from a docked laptop) takes its lock surface along.
    if command -v wlr-randr >/dev/null 2>&1; then
        XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" \
            wlr-randr --output HEADLESS-1 --pos 300,200
        reply=$(msg outputs || true)
        if [[ $reply != *':300,200:'* ]]; then
            echo "wlr-randr could not move the output ($reply)" >&2
            exit 1
        fi
        sleep 1
        XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" grim -o HEADLESS-1 "$runtime/locked-moved.png"
        "$python" "$root/tests/session_check.py" --lock-covers "$runtime/locked-moved.png"
        XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" \
            wlr-randr --output HEADLESS-1 --pos 0,0
        sleep 0.5
    fi

    pkill -KILL -x swaylock || true
    wait "$lock_starter" 2>/dev/null || true
    sleep 1
    if [[ $(msg locked || true) != 'ok 1' ]]; then
        echo 'the session unlocked itself when the locker was killed' >&2
        exit 1
    fi
    XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY="$socket" grim -o HEADLESS-1 "$runtime/locked-killed.png"
    "$python" "$root/tests/session_check.py" --locked "$runtime/locked-killed.png"
else
    echo 'session smoke: swaylock or pkill missing, skipping the lock screen check' >&2
fi
