# NewTeller

A complete Wayland desktop session for Debian 13, Ubuntu 26.04, Arch Linux and Fedora 42+. NewTeller owns the
display: `newteller-compositor` takes over DRM/KMS (or a nested window for development),
starts the QML desktop as its first client, and runs Wayland and X11 applications inside
it. No GNOME, KDE or other desktop environment is needed.

The desktop itself is the NewTeller shell: a Windows-style bottom panel, start menu,
desktop icons and widgets, with themes and plugins for deep customization. It still runs
inside another Wayland or X11 session for development, but that is now the fallback, not
the product.

- **Session:** a small wlroots 0.18/0.19 compositor (`newteller-compositor`)
  with layer-shell, xdg-shell, XWayland, foreign-toplevel (wlr and ext),
  output power management (`wlopm --off '*'`),
  xdg-foreign (portal dialogs attach to their application), security-context
  (sandboxed Flatpak apps cannot capture the screen, read the clipboard of
  others, inject input or draw over the desktop), screenshots, clipboard,
  idle-inhibit and the usual input protocols. Dialogs open centred over their
  window and stay above it. `newteller-session` is the login-session
  entry point.
- **Core:** C++20 + Qt 6 (settings, themes, application catalogue, session actions, plugin loader)
- **UI:** QML components with JavaScript logic, rendered by Qt Quick's scene graph (panel, start menu, desktop, widgets, plugins). The shell does not embed Chromium/WebEngine; a literal HTML/CSS UI would need a browser engine or a separate, much larger custom renderer.
- **Layer:** the shell uses wlr-layer-shell; the compositor implements the protocol itself.
  The panel reserves screen space, the desktop is a background layer and menus are top layers.
- **Icons:** [IconKit](https://github.com/fourtexec/iconkit), with the system icon theme as fallback

## Status: early alpha

Implemented and covered by the tests (see [Tests](#tests)); verified in headless and
nested compositor sessions. It has not yet been exercised on physical DRM/KMS hardware or
inside VirtualBox. The implemented pieces are:

- Panel: Start button, pinned apps with a running badge, plugin slots, clock with
  optional date, on any edge (`panel.position`: bottom/top/left/right), optional
  auto-hide
- **System tray: the shell is the session's StatusNotifierWatcher.** Applications
  that implement `org.kde.StatusNotifierItem` (Qt's QSystemTrayIcon, Electron
  apps, GTK applets, ...) register their icons with the shell and appear at the
  end of the panel: left click activates, middle click triggers the secondary
  action, the wheel scrolls, and right click renders the item's
  `com.canonical.dbusmenu` menu (checkable items, submenus and separators
  included) instead of popping up a native menu. Items without a menu object
  keep the fallback that asks the application for its own popup. Icons arrive
  either as pixmaps (IconPixmap) or through the system icon theme, and the
  tray hides itself when another tray host owns the session (`tray.enabled`,
  `tray.menus`).
- **Sound card** (the arrow next to the volume slider): pick the output
  (speakers, headphones, HDMI, a Bluetooth headset; what is playing moves
  along), the microphone when there are several, and set or mute the volume
  of each application that plays. Uses
  `pactl` (PipeWire or PulseAudio); without it the arrow opens pavucontrol.
- **Quick settings: the indicators button at the end of the panel.** One card
  with the media player that is playing (MPRIS over D-Bus: title, artist,
  play/pause, next/previous), a volume slider (via `wpctl` or `pactl`, with a
  mute button), a brightness slider (`/sys/class/backlight`, written through
  GNOME's SettingsDaemon.Power when available), the Wi-Fi toggle and network
  status (NetworkManager), the do-not-disturb switch and the battery level.
  Every row hides itself when its backend is missing
  (`panel.indicators`).
- **Dock: every started application appears in the panel.** Real top-level windows
  are tracked on X11 (EWMH) and on Wayland compositors with wlr-foreign-toplevel
  (KDE Plasma, sway, Hyprland, labwc, Wayfire, niri, ...). On GNOME 48+ the
  read-only `ext_foreign_toplevel_list` protocol lists the native Wayland
  windows, XWayland windows come from the EWMH back end, and the /proc based
  application list fills any remaining gap. Windows can be activated,
  minimised and closed from the dock (click, middle click and a right-click
  menu; read-only toplevels fall back to launching the application), and
  windows of one application can be grouped into a single button. The dock
  menu shows a live preview of every X11/XWayland window (`dock.thumbnails`)
  and the application's own actions ("New Private Window", ...),
  and pinned buttons can be dragged to reorder them.
- Start menu: highlighted fuzzy search, category chips, recently used row,
  Lock / Log out / Sleep / Restart / Shut down, Settings; right-click an
  application to pin or unpin it on the dock
- Desktop icons and widgets share one invisible grid: an icon takes a cell,
  a widget the cells its size needs, nothing overlaps, and dragging snaps to
  the nearest free cells. The layout is saved in `desktop.gridPositions`
  (right-click the desktop to auto-arrange or reset it). A launcher
  (`.desktop` file) of an installed application shows that application's
  icon and name and starts it on a double-click; other launchers start only
  when they are marked executable (otherwise they open as text)
- **Notification centre: the bell at the end of the panel.** The history of the
  built-in notification daemon, grouped by application (with a clear button per
  application), newest first, with per-entry removal, a clear-all
  button and the do-not-disturb switch; the bell counts what arrived while it
  was closed. Per-row removal is `Notifications.removeHistory()` on the daemon.
- **Notification sounds** (`notifications.soundEnabled`): a new popup plays
  the application's `sound-file`, the chosen `notifications.soundFile` or
  the theme's message sound (canberra-gtk-play, pw-play or paplay); never
  under do-not-disturb or for `suppress-sound`.
- **Notification markup**: bold, italic, underline and line breaks in a
  notification's text are shown (body-markup); links, pictures and any
  other HTML are reduced to their text, so no notification can load remote
  content. The daemon advertises what it does (`body-markup`, `sound`,
  `persistence`, `icon-static`, `inline-reply`), so applications leave
  sounds to it.
- **Quiet in full screen** (`notifications.quietInFullscreen`): while a
  fullscreen window is shown, popups and their sounds wait in the history
  (urgent ones still show); the compositor reports it as `event fullscreen 1|0`.
- **Reply from the notification**: messengers that offer an inline reply
  (Telegram Desktop, KDE Connect) get a text field in the popup; Enter or
  Send answers without opening the application.
- **Notification popups** follow the spec's hints: pictures (`image-data`,
  `image-path`), a click runs the `default` action, `transient` ones stay out
  of the history, `resident` ones stay after an action, and a popup tagged
  with `x-canonical-private-synchronous` or `x-dunst-stack-tag` replaces the
  previous one with that tag (volume and brightness scripts); `value`
  (0-100) draws a level bar.
- **Bluetooth pairing agent**: keyboards show the code to type and phones
  the code to compare as notifications with Pair / Cancel; without an
  agent BlueZ could only pair devices that need no confirmation. The code
  gets through do-not-disturb and goes away once the device is paired.
- **Removable drives** (`drives.notify`): a USB stick or memory card that is
  plugged in is announced with Open (mounts it through UDisks2 and opens the
  folder) and Eject (unmounts it and powers the drive off); internal disks
  and loop devices stay quiet. Needs udisks2.
- **Calculator in the start menu search**: `2+2*3`, `(15,5-3)/4`, `2^10`
  show the result first; Enter copies it.
- **Recent files in the start menu search**: documents opened in other
  applications (`recently-used.xbel`) show up when part of their name is
  typed (`startmenu.recentFiles`).
- **Settings in the start menu search**: "resolution", "wifi", "раскладка"
  and the like list the matching Settings page ("Settings: Displays"), which
  opens on that page (`Shell.openSettingsPage`).
- **Search in the wrong layout**: the start menu and the run dialog retry a
  query that finds nothing in the other keyboard layout, so "ашкуащч" finds
  Firefox and "nthv" finds Терминал (ЙЦУКЕН <-> QWERTY).
- **Russian interface.** The shell follows the system language (`ru_RU`
  gives a Russian panel, menus and settings); Settings → Appearance →
  Language (`shell.language`: `auto`, `en`, `ru`) switches it live.
  Translations live in `i18n/<language>.json` (English text -> translation)
  and need no Qt Linguist tools to build.
- **Settings application: a full settings GUI built into the shell.** Every key of
  `shell.json`, a theme editor, plugin management, diagnostics and config export —
  see [Settings application](#settings-application). The QML is compiled into the
  `newteller` executable, so it cannot be deleted or damaged without replacing the
  binary itself.
- Run dialog (`Alt+F2`): run a shell command or search applications; a web
  address (`https://…`, `mailto:`) or a folder / document path (`~/Документы`,
  `/etc/fstab`) opens instead; bare commands are remembered (`run.history`,
  capped by `run.historyLimit`) and Up/Down walks the history
- **Dropdowns linked to their buttons.** Every floating surface — start menu,
  quick settings, notification centre, clock, dock menu and tray menu — opens
  anchored to the widget that toggled it: centred on the button, clamped into
  the screen, on the side of the bar away from the panel, with a small pointer
  on the edge that faces the button, and the open animation growing out of it.
  Clicking another button of the same kind slides the open card over; only one
  card is open at a time; Escape or a click elsewhere closes it. See
  [Dropdowns](#dropdowns).
- **Panel shortcuts without opening anything.** Scroll over the indicators to
  change the volume 5% a notch, middle-click them to mute, and right-click the
  bell to toggle do-not-disturb. The start menu answers to Up/Down/Enter while
  typing.
- **Tooltips.** Panel buttons (dock icons, tray icons, bell, indicators, clock)
  describe themselves in a small layer surface above the hovered button; buttons
  inside the menus (start menu session actions, quick settings, notification
  centre) use an in-window bubble.
- Desktop: wallpaper, icons from `~/Desktop` (double-click to open), desktop
  plugins, right-click menu (change wallpaper, paste to a file, new folder, open
  a terminal, settings); right-clicking an icon opens its own menu: Open,
  Rename (in place), Copy path, Move to Trash (restorable from the file
  manager)
- System monitor (`System` object, on a shared ticker) with bundled panel and
  desktop widgets
- **Surface materials:** configure panel, dock buttons, floating shell windows and notifications independently as theme, opaque, half-transparent, frosted glass or liquid glass. Inside a NewTeller session the compositor blurs what is behind see-through menus, the dock, the overview, Alt+Tab and the on-screen display.
- **Desktop effects** (Settings → Animations → Desktop effects): rounded window
  corners, backdrop blur, and window animations (open, close, minimize, restore,
  sliding desktop switches), drawn by the compositor's own renderer; off falls
  back to the plain wlroots scene renderer.
- Themes (JSON, including `nord`, `ocean` and `highcontrast`) and a settings file
  with live reload
- Plugin system (manifest + QML) with enable/disable and auto-rescan
- Window management: snap a window to the left/right half with `Super+Left`/`Super+Right`
  (then `Super+Up`/`Super+Down` for a quarter, or drag it into a screen corner;
  while dragging, a translucent preview shows where it will snap),
  maximise with `Super+Up`, restore with `Super+Down`, drag a window to an edge to snap it,
  move it between outputs with `Super+Shift+Left`/`Super+Shift+Right`, and show/hide the
  desktop with `Super+D`, and move or resize a window from anywhere inside it with
  `Super`+left or right mouse drag
- **Lock screen.** `Super+L`, the start menu, quick settings, the idle timer and
  suspend lock the session over `ext-session-lock-v1`: while locked the
  compositor draws and focuses nothing but the locker, and a crashed locker
  is restarted instead of unlocking. `newteller-lock` uses gtklock (3.0+:
  clock, a password field with an Unlock button and a "wrong password"
  message, styled dark) and falls back to swaylock (always-visible ring with
  the typing state). The wallpaper is the lock screen background
  (`power.lockAfter`, `power.lockOnSleep`).
- **Administrator prompt (like UAC).** NewTeller is its own polkit
  authentication agent (libpolkit-agent-1): when a program asks for
  administrator rights the screen dims and a card says who asks, what for
  (details: polkit action, command line), for which account, with a
  password field, "Allow" and "Cancel"; a wrong password is reported and
  asked again. `security.builtinAgent` turns it off (an external agent such
  as lxpolkit is used then, as when the build has no libpolkit-agent-1).
  The session runs exactly one agent: polkit agents from the
  distribution's autostart (`*polkit*`, `*policykit*`) are skipped, and when
  polkit refuses the built-in agent (no login session, no polkit daemon) no
  external one is started either - it would fail the same way and only show
  an "Error" window. The Security settings page shows the reason.
- **Graphics tablets**: the pen moves the pointer and touching the
  tablet clicks (no pressure yet).
- **Touchscreens**: a touch goes to the surface under the finger and
  focuses its window like a click; shell cards close on a touch outside.
- **Virtual machines and remote desktops** can take every key
  (keyboard-shortcuts-inhibit) while they have the focus, Super and
  Alt+Tab included; `Super+Escape` gives the desktop its shortcuts back,
  and `Ctrl+Alt+F1..F12` always works.
- **Games and 3D views**: relative pointer motion and pointer locking or
  confinement (pointer-constraints), so mouse look works in Wayland and
  XWayland games, Blender and remote desktops.
- **Drag and drop** between applications and inside them (files from the
  file manager into a browser or mail, text, tabs), with the dragged icon
  under the pointer.
- **Input methods (IBus, fcitx5).** The compositor relays text fields
  (text-input-v3) to the input method (input-method-v2): composed text,
  pre-edit and the candidate window under the text cursor work in Wayland
  applications. Without an input method, text fields are left alone, so GTK
  keeps handling dead keys and compose itself; the lock screen never goes
  through an input method.
- **Video keeps the screen awake.** The shell serves
  `org.freedesktop.ScreenSaver` (Inhibit / UnInhibit), which VLC, browsers
  and XWayland applications use while they play; Wayland applications use
  the idle-inhibit protocol. An inhibition ends with UnInhibit or when the
  application quits, so a crashed player never keeps the screen on.
  Flatpak applications reach it through the portal (`Inhibit=gtk` in
  `newteller-portals.conf`).
- **Open with root.** The dock's right-click menu and the start menu's
  right-click menu start an application as root:
  `newteller-run-as-root` runs it through pkexec (the administrator prompt
  above) and hands over the Wayland/X display so it opens in the session.
- **Force quit.** `Ctrl+Shift+Esc` (or `Ctrl+Alt+Esc`) lists every window
  with "Not responding" next to frozen ones; End task kills the process,
  Close asks it politely. The compositor pings windows when they are focused
  or closed; when the focused one stops answering, the dialog opens itself.
- **Touchpad gestures** (`touchpad.gestures`): three fingers sideways move
  the active window, four fingers sideways switch desktops.
- **Default wallpaper**: a dark, minimal picture (installed to
  `share/backgrounds/newteller/default.jpg`) while no wallpaper is chosen
  (`desktop.defaultWallpaper`).
- **Idle and power.** The screen turns off after `power.screenOffAfter`
  minutes (swayidle); video players that inhibit idle keep it on.
- **Keyboard layouts.** Pick layouts in Settings → Keyboard (or keep the system
  layout from `/etc/default/keyboard`); `Super+Space` or Alt+Shift (or
  Ctrl+Shift / Caps Lock) switches, and the panel shows the current layout.
  Repeat rate/delay, Num Lock and raw XKB variants/options are there too.
  `keyboard.layoutPerWindow` ("Remember the layout per window") gives every
  window its own layout, as on Windows: switching windows switches back.
- **Mouse & touchpad.** Tap to click, natural scrolling, disable while
  typing, pointer speed and left-handed buttons, applied live through
  libinput; cursor theme and size.
- **Workspaces.** Four virtual desktops by default (`workspaces.count`):
  `Ctrl+Super+Left/Right` or `Super+Page Up/Down` switch (`Ctrl+Super+1…9`
  goes straight to one, `Super+Shift+1…9` takes the focused window there),
  add `Shift` to take the focused window along; dots on the panel show and switch them.
- **Alt+Tab** walks through every window of the current desktop in
  most-recently-used order with a window list overlay and a live preview;
  `Shift+Tab` goes back, `Escape` cancels. A tap of `Super` opens the start menu.
- **Displays.** Display tools that speak wlr-output-management (wdisplays,
  wlr-randr, kanshi) change resolution, refresh rate, scale, rotation and
  arrangement; NewTeller remembers the settings per monitor
  (`~/.config/newteller/outputs.conf`) and starts high-density panels at 200%.
- **Title bars for every window.** Applications that want the window manager
  to draw their frame (Qt/KDE apps, X11 programs, terminals, Chromium with
  "system title bar") get one: title, minimize/maximize/close, double click to
  maximize, drag to move, edges to resize. Buttons left or right, height,
  border, font and colours (or the theme's) in Settings → Windows.
- **Window menu**: right-click a title bar (NewTeller's own or an
  application's header bar) or press `Alt+Space`: minimize, maximize, full
  screen, **always on top**, show on every desktop, move to another desktop
  or (with several monitors) the next screen, close. Settings →
  Windows → Window rules keeps chosen applications on top or opens them
  maximized, full screen, minimised or on a given desktop
  (`windowRules.alwaysOnTop`, `maximized`, `fullscreen`, `minimized`,
  `workspaces` as `firefox=2, thunderbird=3`).
- **Hot corners** (Settings → Windows): push the pointer into a screen corner
  to open the overview, show the desktop, open the start menu or the
  notifications, or lock (`hotCorners.topLeft` … `bottomRight`; off by
  default, never during a drag, a full-screen window or a game).
- **Application bells** (xdg-system-bell, wlroots 0.19+): a terminal's or an
  application's bell plays the sound theme's bell (`sounds.bell`), not under
  do-not-disturb.
- **Caps Lock / Num Lock on screen** for keyboards without indicator lights
  (`keyboard.lockKeysOsd`).
- **Overview** (`Super+Tab`): every window of a desktop as a live thumbnail,
  desktops along the top; click to switch, close, or move a window to
  another desktop. Alt+Tab shows thumbnails too.
- **Displays page**: resolution, refresh rate, scale, rotation, position and
  on/off for each screen, with a 15-second "keep these settings?" revert.
- **Bluetooth** in quick settings: power, connect, disconnect, pair, forget.
- **Window behaviour and shortcuts**: focus follows mouse, placement of new
  windows, edge snapping, Alt+Tab across desktops, wrapping desktops, and
  your own key combinations for any command; every built-in shortcut can be
  moved to other keys or turned off.
- **Start menu look**: list or icon grid (with its own icon size), search
  field at the top or bottom, its own background, text, accent and search
  match colours, surface material, opacity and corner radius, and the power
  buttons shown or hidden.
- **Panel style and length**: the dock pill alone (default) or a bar along
  the edge in the panel surface and colours, with its own corner radius; a
  length below 100% makes a floating, centred panel.
- **A panel per screen**: with several monitors each screen can put its panel
  on another edge or go without one (Settings → Displays).
- **Named desktops**: give desktops names (`workspaces.names`), show them on
  the panel as dots, numbers or names, and on screen when switching.
- **Profiles**: save every setting under a name (Settings → Appearance →
  Profiles), switch between looks in one click, update or delete them, and
  import an exported settings file or someone else's profile. Recent
  applications, histories, notes and icon places stay out of profiles.
- **Accent, font and rounding over any theme**: an accent colour (nine
  presets or any colour), the interface font, its size and the corner
  rounding stay when you switch themes (`theme.accent`, `theme.font`,
  `theme.fontSize`, `theme.radius`).
- **Day and night themes**: two themes switched at set times or together
  with the night light, with applications following (light by day, dark at
  night).
- **Desktop labels**: colour, outline, size and number of lines of the names
  under desktop icons, independent of the icon tint.
- **Application look**: dark/light preference, GTK theme, icon theme,
  application font and text size, cursor theme; lock screen colour.
- **Wi-Fi networks** from quick settings: scan, join (with the password asked
  inline for new secured networks; WPA2/WPA3), join a hidden network by its
  name, disconnect; NetworkManager
  keeps them. The shell talks to NetworkManager over D-Bus and, when Wi-Fi
  cannot work, says why: NetworkManager not running, no adapter (driver or
  firmware missing), adapter unmanaged, switched off or blocked. The
  installer adds NetworkManager and, on real hardware, the common Wi-Fi
  firmware packages, and starts NetworkManager.
- **Clipboard history** on `Super+V` (wl-clipboard): the texts copied in the
  session, newest first; click one to copy it again. Pin the snippets you
  reuse (they stay across restarts and are never pushed out) and type to
  search; Enter takes the first match. Kept in memory unless
  `clipboard.persistHistory` is on. Passwords that a password manager marks as secret
  (KeePassXC, Bitwarden and others) are never recorded.
- **Power profiles** (power-profiles-daemon) in quick settings and low-battery
  notifications at 10% and 5%; at 20% on battery the power saver profile
  switches on until the machine is plugged in (`power.batterySaver`).
- **Night light** (wlsunset) on a schedule or always, from quick settings or
  Settings → Displays.
- **Desktop services.** Screen sharing and screenshots through
  xdg-desktop-portal-wlr, a polkit agent for administrator prompts, and
  `Ctrl+Alt+F1…F12` to switch virtual terminals.
- Screenshots: `Print` captures the whole screen, `Shift+Print` or `Super+Shift+S` an
  area picked with the mouse (slurp), `Alt+Print` the focused window with its frame; the PNG lands in `Screenshots` inside the pictures folder (`~/Pictures`, or its
  localized name from `user-dirs.dirs`), is
  copied to the clipboard (wl-copy) and announced through the notification daemon
- **Screen recording** (wf-recorder): quick settings records the whole screen
  or an area picked with the mouse (slurp), optionally with the microphone
  (`recorder.audio`); `Super+Alt+R` starts and stops it. A red pill on the
  panel shows the time and stops the recording; the MP4 lands in
  `Screencasts` inside the videos folder and a notification offers Play and
  Show in folder.
- Multi-monitor: one desktop and one panel per screen; cards opened from the
  keyboard (`Super`, `Super+A`, `Super+N`, `Super+V`) open on the screen the
  pointer is on (`event output NAME` on the control socket)
- Rust core (application search index, metrics sampler, config, watcher) with a
  C++/Qt fallback, plus the `newteller-ctl` command line tool

## Build (Debian 13 / Ubuntu 26.04 / Arch Linux / Fedora 42+)

Release builds support Debian 13, Ubuntu 26.04 LTS, Arch Linux (and Arch-based
systems such as EndeavourOS) and Fedora 42 or newer, with Qt 6 and wlroots development
packages, not just the runtime libraries. Debian 12 / Ubuntu 24.04 ship Qt5 LayerShellQt
and an older wlroots and are not supported by the release scripts.

`newteller-compositor` builds against wlroots 0.18, 0.19 or 0.20: `libwlroots-0.18-dev` on
Debian 13, `libwlroots-0.19-dev` on Ubuntu 26.04, whichever of 0.20/0.19/0.18 Arch currently
packages (`wlroots0.20` today), `pkgconfig(wlroots-0.19)` (or 0.18) on Fedora. `scripts/install-build-deps.sh` uses
apt, pacman or dnf and picks the right packages for the detected distribution.

Each distribution family has its own GitHub Actions workflow:
`build-release.yml` (Debian 13, Ubuntu 26.04), `build-arch.yml` (Arch Linux,
`archlinux:latest`) and `build-fedora.yml` (`fedora:latest`). Every one runs the Rust
tests, the QML smoke test, the release build and the headless session test, and on
`main` attaches its archive (`arch-x86_64`, `fedora<N>-x86_64`) to the commit release.

From the repository root:

```sh
sudo bash scripts/install-build-deps.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j"$(nproc)"
sudo cmake --install build
```

The build uses the Rust core when `cargo` is found. Control it with
`-DNEWTELLER_WITH_RUST=ON|OFF|AUTO` (default `AUTO`: enabled if `cargo` is
installed). `ON` fails the configuration when `cargo` is missing; `OFF` builds
the pure C++/Qt fallback with no Rust toolchain. The Rust build produces the
static library `libnewteller_core.a` and the `newteller-ctl` CLI, both installed
with the shell. Rust sources live in `rust/` (`cargo test --workspace` there).

If CMake reports `Qt6Config.cmake`, wayland-client or xcb is missing, run the dependency
installer above. It installs `qt6-base-dev`, `qt6-declarative-dev`, `qt6-svg-dev`, the Qt6
LayerShellQt development package, the distribution's wlroots development package,
`libwayland-dev`, `libxcb1-dev`, XWayland, build tools, and runtime QML modules.
For a custom Qt SDK (6.4 or newer), export `CMAKE_PREFIX_PATH` pointing to its
installation prefix, or `Qt6_DIR` pointing to the directory containing
`Qt6Config.cmake`. LayerShellQt must also be built against that Qt version.

IconKit is optional: clone https://github.com/fourtexec/iconkit to `~/iconkit`
for additional icons, or use the system icon theme.

### Log in to NewTeller

The installer registers `newteller.desktop` in `/usr/share/wayland-sessions`. Log out,
choose **NewTeller** in the display manager's session menu, and log in. The compositor
owns the screen from that point; the shell is started automatically inside it.

To try it from a spare virtual console, run the same entry point as the greeter would:

```sh
newteller-session
```

Do not start `newteller-session` from an existing graphical session, because the DRM
device is already in use.

### Run it as a window for development

These modes do not take over the screen and are what the automated tests use:

```sh
# A 1280x800 NewTeller session inside the current Wayland/X11 session.
newteller-compositor --nested=1280x800 --shell ./build/newteller

# No display at all; useful with grim/Wayland clients in CI.
newteller-compositor --headless=1280x800 --shell ./build/newteller
```

`--no-xwayland` skips XWayland, `--no-shell` starts only the compositor, and
`--socket <name>` selects an explicit `WAYLAND_DISPLAY`. `--nested` needs a working
parent Wayland compositor.

### Session modes

| Mode | What receives the graphics |
| --- | --- |
| Login session (`newteller-session`) | The NewTeller compositor owns DRM/KMS and libinput directly. Wayland applications are native clients; X11 applications go through XWayland. |
| Nested (`--nested`) | The compositor is one window on the current desktop. Used for development and screenshots. |
| Headless (`--headless`) | No output window. Used for automated tests and CI. |
| Existing session (optional) | Running plain `newteller` still installs the shell as a layer-shell client on KDE Plasma, sway, Hyprland, labwc, Wayfire or niri, and through XWayland on GNOME. This is the development fallback. |

Setting `QT_QPA_PLATFORM` (or passing `-platform`) overrides the shell's automatic choice
when it runs inside another session. The compositor always selects its own backend.

The login greeter is the system display manager; NewTeller does not ship its own greeter.
The lock screen is gtklock or swaylock, driven by the compositor through
`ext-session-lock-v1` (`newteller-lock`); the installer adds them together with
swayidle, wlsunset, NetworkManager, the portals and a polkit agent when the
distribution has them.

The compositor reads `~/.config/newteller/compositor.conf`, which the shell
writes from its settings, and listens on a control socket (`$NEWTELLER_IPC`):
`newteller-compositor --msg reload|lock|locked|dpms on|dpms off|layout next|workspace N|outputs|windows|kill ID|exit`.
Window commands take the id from `windows`: `minimize ID [on|off]`,
`maximize ID [on|off|toggle]`, `fullscreen ID [on|off|toggle]`,
`always-on-top ID [on|off|toggle]`, `sticky ID [on|off|toggle]` (every
desktop), `move-to-output ID [next|prev]` and `window-menu [ID [X Y]]` (opens the
window menu; watchers get `event window-menu ID X Y DESKTOP FLAGS TITLE`).
`windows` flags: `f` focused, `m` minimized, `x` maximized, `F` full screen,
`t` always on top, `s` on every desktop, `h` not responding (`-` for none).
`windows` marks frozen windows with the `h` flag and reports each window's
process id; `kill ID` force-quits one (never the compositor or the shell).
`idle-inhibit on|off` keeps the screen awake while the connection that sent
it stays open (the shell uses it for `org.freedesktop.ScreenSaver`); without
an argument it reports whether anything inhibits idle (`ok 1` / `ok 0`).
Watchers get `event hung ID`, `event responsive ID`, `event closed ID` and
`event click X Y` (any button press, so open shell cards can close).

Windows use client-side decorations (title bars drawn by the toolkit), so every application
gets a usable frame without a separate window-decorator process. The compositor handles
these shortcuts globally, even when an application has focus:
`Alt+F2` (run dialog), `Super+Return` (terminal), `Super+S` (settings), `Super+Q`
(close the focused window), `Alt+F4` (close), `Super+Left`/`Super+Right` (snap to a half),
`Super+Up`/`Super+Down` (maximise/restore), `Super+Shift+Left`/`Super+Shift+Right`
(move to another output), `Super+D` (show desktop), `Super+M` (maximise),
`Super+F` (fullscreen), `Super+L` (lock), `Super+Space` (next keyboard layout),
`Super+V` (clipboard history), `Super+Alt+R` (start/stop screen recording), `` Alt+` `` (the next window of the same application), `Super+1…9` (the Nth dock application: start
it, bring it up, or cycle its windows), `Alt+Space` (window menu), `Super+N` (notifications), `Super+A` (quick settings), `Super+R` (run), `Super+E` (home
folder), `Super+I` (settings), `Super+Shift+S` (area screenshot),
`Ctrl+Super+Left/Right` (switch desktop, with `Shift` to move the window),
`Ctrl+Shift+Esc` or `Ctrl+Alt+Esc` (force quit),
`Ctrl+Alt+F1…F12` (virtual terminals), `Print` (screenshot), a tap of `Super` on its own (open or close
the start menu), and `Alt+Tab` (switch windows: keep Alt held and press Tab again to walk
through every window, most recently used first, with the candidate raised as a preview;
`Shift+Tab` steps back, `Escape` cancels and releasing Alt switches). Shortcuts
match the Latin key whatever keyboard layout is active, so `Super+L` also locks
with a Russian layout. **Settings → Hotkeys** moves any of the built-in
shortcuts above to other keys (click the shortcut, press the new keys) or turns
it off, gives keys to actions that have none by default (minimise, keep on top,
log out, sleep, restart, shut down), and can switch every built-in shortcut off
at once (`hotkeys.*` in shell.json, `hotkey.<name>` in compositor.conf). The running shell
exports `org.newteller.Shell` on the session bus;
scripts can call `showStartMenu`, `toggleStartMenu`, `showRunDialog`, `openSettings`, `lock`, `logout`
and the other session methods with `gdbus call`.

## Installation images (ISO)

`iso/build-iso.sh` builds a bootable live ISO for Debian 13, Ubuntu 26.04,
Arch Linux or Fedora with NewTeller and a graphical installer that starts on
its own and installs the system with NewTeller as the only desktop:
`sudo iso/build-iso.sh debian|ubuntu|arch|fedora`. See [iso/README.md](iso/README.md).

## Automated builds and commit releases

`.github/workflows/build-release.yml` builds separate native x86_64 archives on
Debian 13 (trixie) and Ubuntu 26.04 LTS. Pull requests build and upload an artifact without publishing a release.
Every push to `main` publishes a prerelease titled `commit <full SHA>` with the
matching tag `commit-<full SHA>`. It can also be run manually on `main` through
**Actions → Build and release NewTeller → Run workflow**. Rerunning a build replaces
its release assets. The workflow uses `GITHUB_TOKEN`; no additional secrets are needed.

Each release contains:

- `newteller-commit-<full SHA>-debian13-x86_64.zip`;
- `newteller-commit-<full SHA>-ubuntu26.04-x86_64.zip`;
- a `.zip.sha256` checksum file.

The ZIP contains `usr/bin/newteller`, `usr/bin/newteller-compositor`,
`usr/bin/newteller-session`, `usr/bin/newteller-screenshot`, `usr/bin/newteller-ctl`,
the example plugins, application and
`wayland-sessions/newteller.desktop` entries, this README, `BUILDINFO` with the
full commit SHA, and installation scripts. QML and the default/light themes are
embedded in the executable. Qt6, LayerShellQt, wlroots and XWayland are **system
dependencies**, installed by the included installer; this is not a portable
archive for Debian 12 or Ubuntu 24.04. IconKit is optional (system icons are used
if it is absent).

Download the ZIP **matching your distribution** and its checksum into the same
directory. The installer rejects archives built for a different distribution.
For example, on Debian 13 x86_64:

```sh
sha256sum -c newteller-commit-<full SHA>-debian13-x86_64.zip.sha256
unzip newteller-commit-<full SHA>-debian13-x86_64.zip
cd newteller-commit-<full SHA>-debian13-x86_64
sudo bash install.sh
# Log out, then choose "NewTeller" in the display manager's session menu.
```

The installer needs an internet connection to install dependencies with `apt`.
It does not create an autostart entry: NewTeller is a session, not an
application that should start on top of another desktop.

On Ubuntu 26.04, use the same commands with `ubuntu26.04` in place of `debian13`
in the archive and directory names.

To create an archive locally on Debian 13 or Ubuntu 26.04 x86_64, from the repository root run:

```sh
sudo bash scripts/install-build-deps.sh
bash scripts/build-release.sh
```

Output is written to `dist/` (ignored by Git), named for the detected distribution.
Build on the target distribution; Debian and Ubuntu binaries are not interchangeable.

## Configuration

`~/.config/newteller/shell.json` is a flat JSON object with dotted keys. The file is
watched, so edits apply without a restart:

```json
{
  "panel.height": 48,
  "panel.position": "bottom",
  "panel.autoHide": false,
  "panel.clockFormat": "HH:mm",
  "panel.showDate": true,
  "panel.pinned": ["org.gnome.Terminal.desktop", "firefox-esr.desktop"],
  "theme.name": "default",
  "desktop.wallpaper": "/home/you/Pictures/wall.jpg",
  "icons.path": "",
  "icons.set": "lucide",
  "system.interval": 2000
}
```

All keys, with their defaults:

| Key | Default | Meaning |
| --- | --- | --- |
| `panel.height` | `48` | panel thickness in pixels |
| `panel.position` | `"bottom"` | `bottom`, `top`, `left` or `right` |
| `panel.margin` | `0` | gap between the panel and the screen edge |
| `panel.centered` | `false` | reserved |
| `panel.width` | `0` | reserved (0 = full edge) |
| `panel.autoHide` | `false` | collapse the panel to a hover strip at the edge |
| `panel.autoHideDelay` | `400` | delay in ms before an auto-hidden panel slides away |
| `panel.clockFormat` | `"HH:mm"` | `Qt.formatDateTime` time format |
| `panel.dateFormat` | `"ddd d MMM"` | date format, shown before the time |
| `panel.showDate` | `false` | show the date next to the clock |
| `panel.showRunning` | `true` | dot badge for running applications |
| `panel.pinned` | `[]` | desktop file IDs pinned to the panel |
| `panel.material` | `"theme"` | `theme`, `opaque`, `translucent`, `frosted` or `liquid` panel surface |
| `dock.showWindows` | `true` | show running applications/windows in the dock |
| `dock.group` | `false` | merge all windows of one application into one dock button (off: one button per window, windows of one application side by side) |
| `dock.iconSize` | `28` | dock button icon size in pixels |
| `dock.material` | `"translucent"` | floating dock surface: `none`, `opaque` (non-transparent), `translucent` (half transparent), `frosted` or `liquid` |
| `dock.backgroundColor` / `dock.backgroundOpacity` | `""` / `0.5` | dock surface tint, and an opacity bias around the material preset (`0.5` = the material's own transparency, lower is clearer, higher is more solid) |
| `dock.thumbnails` | `true` | live window previews in the dock menu (X11/XWayland) |
| `dock.multiRow` / `dock.rows` | `false` / `1` | wrap the dock into up to three rows (horizontal panels) |
| `dock.scrollable` | `true` | let the dock scroll when its buttons do not fit |
| `dock.iconSpacing` | `4` | gap between dock buttons in pixels |
| `dock.position` / `panel.centerDock` | `"left"` / `false` | align the dock at the start, centre or end of the bar |
| `panel.spacing` / `panel.leftSpacing` / `panel.rightSpacing` / `panel.zoneSpacing` | `8` | bar padding and gaps between the panel zones |
| `windows.material` | `"theme"` | surface for shell popups: `theme`, `opaque`, `translucent`, `frosted` or `liquid` |
| `panel.indicators` | `true` | bell (notification centre) and quick settings button at the end of the bar |
| `notifications.material` | `"theme"` | notification popup and centre surface material |
| `notifications.opacityPercent` | `0` | notification surface opacity; `0` uses the material preset |
| `notifications.cornerRadius` | `-1` | notification card radius; `-1` follows the window radius |
| `tray.enabled` | `true` | act as the session's StatusNotifierWatcher and show the registered tray icons |
| `tray.menus` | `true` | render tray item DBusMenus on right click |
| `startmenu.width` / `startmenu.height` | `440` / `580` | menu size |
| `startmenu.limit` | `200` | maximum search hits |
| `startmenu.fuzzy` | `true` | typo-tolerant search |
| `startmenu.highlight` | `"#ffffff"` | hit highlight colour |
| `startmenu.showRecent` | `true` | recently used row |
| `theme.name` | `"default"` | theme file name |
| `theme.autoReload` | `true` | re-apply when the theme file changes |
| `desktop.wallpaper` | `""` | image path (empty = solid colour) |
| `desktop.showIcons` | `true` | show `~/Desktop` icons |
| `desktop.iconSize` | `48` | icon size in pixels |
| `desktop.cellWidth` / `desktop.cellHeight` | `96` / `110` | icon grid cell |
| `desktop.contextMenu` | `true` | desktop right-click menu |
| `desktop.gridPositions` | `{}` | saved grid cells: `"icon:<file name>"` / `"widget:<plugin id>"` -> `{col, row}` |
| `icons.path` / `icons.set` | `""` / `"lucide"` | extra IconKit checkout overriding the bundled icons / set folder inside it |
| `icons.cache` | `true` | cache resolved icons |
| `plugins.disabled` | `[]` | plugin IDs to disable |
| `plugins.autoRescan` | `true` | rescan when the plugin directories change |
| `apps.recent` / `apps.recentLimit` | `[]` / `8` | usage history and row length |
| `apps.usage` | `{}` | launch counts (drives the recent row and ranking) |
| `apps.terminalCommand` | `"newteller-terminal"` | prefix that runs a command in a terminal (`kitty -e`, `gnome-terminal --`); `newteller-terminal`, also used when the named program is missing, picks the installed terminal (`x-terminal-emulator` on Debian, else foot, kitty, alacritty, wezterm, konsole, gnome-terminal, …) |
| `apps.maxApps` | `0` | cap on the catalogue (0 = unlimited) |
| `system.interval` | `2000` | metrics sampling interval in ms |
| `system.perCore` | `false` | per-core CPU metrics |
| `system.battery` / `system.thermal` / `system.disks` | `true` | metric groups to sample |
| `watch.debounce` | `250` | file-watch debounce in ms |
| `run.history` | `[]` | run dialog history |
| `run.historyLimit` | `25` | how many commands the run dialog remembers |

- `panel.pinned` takes desktop file IDs (see `/usr/share/applications`).
- Icons come from [IconKit](https://github.com/fourtexec/iconkit): a small core
  set is compiled into the executable, and the release ships the full kit under
  `/usr/share/newteller/icons`. `icons.path` points at a local IconKit checkout
  (`~/iconkit/icons` after `git clone https://github.com/fourtexec/iconkit`) to
  override the bundled sets; names missing everywhere fall back to the system
  icon theme. The glyph icons of the shell chrome are recoloured to the theme
  foreground, so they are white on dark themes and dark on light themes;
  colourful application logos keep their own colours.

`newteller-ctl schema` prints the same table in machine-readable form.

### Surface materials

The panel, the dock buttons, every floating shell window (start menu, run
dialog, clock, dock menu) and the notifications each take a `*material` key:

| Value | Look |
| --- | --- |
| `theme` | the theme background at the theme opacity (default) |
| `opaque` | fully opaque theme/background colour |
| `translucent` | half-transparent |
| `frosted` | milk-glass: a matte haze with a broad soft highlight and a light rim |
| `liquid` | clear, glossy glass: a bright top rim, a diagonal sheen and a lit foot |
| `none` | no surface at all (dock only) |

The dock's four looks follow the classic macOS dock comparison — *half
transparent*, *non transparent*, *frosted glass* and *liquid glass* — and are
drawn by `qml/DockSurface.qml`: a tinted body with a top-to-bottom sheen, a
dark outer hairline plus a lit inner rim, slightly darker "thicker glass" ends
and a soft drop shadow. The frosted pill is milked towards white with no sharp
specular, and the liquid pill is the most transparent one, carrying a diagonal
gloss band, an inner refraction ring and an accent-tinted foot. The settings
window shows all four as clickable swatches (Appearance → Dock styling →
Material preview).

`windows.material` covers all shell popups; notifications can be customised
separately with `notifications.material`, `notifications.opacityPercent` and
`notifications.cornerRadius`, and the panel and dock have their own keys. All of
them are live: the settings window and `newteller-ctl set` apply them without a
restart.

The materials are drawn by the Qt Quick scene graph with a handful of static
rectangles, so they cost about as much as a plain background — no layered blur
pass and no browser engine. Real backdrop blur is compositor specific (KWin can
blur layer surfaces through a window rule); NewTeller does not require it, so
the same look works on X11, GNOME/XWayland, sway, Hyprland and KDE.

## Dropdowns

All six floating surfaces share `qml/DropdownWindow.qml`, and the placement
maths lives in `qml/Anchoring.js`:

- **`panelButtonRect()`** turns a button's rect in panel coordinates into a
  global one. Panel coordinates are used on purpose: a layer-shell surface does
  not know where it is on Wayland, but the panel always spans its screen edge,
  so the edge, the bar thickness, the panel's margin and the screen rect place
  the button exactly.
- **`place()`** puts the card next to that rect: centred on the button and
  clamped into the screen, above the bar for a bottom panel, below it for a top
  one, beside it for a vertical one. When the card would not fit on the
  preferred side it flips to the other side of the bar. It also reports which
  card edge faces the button and where along that edge the pointer belongs.
- **`layerMargins()`** converts the placement into the two layer-shell anchors
  and margins each edge needs (left+bottom for a bottom bar, left+top for a top
  bar, and so on).

A panel passes the rect to the dropdown it toggles (`Panel.toggleDropdown()`),
and the dropdown keeps following it: a new anchor — another screen's button, a
dock that reordered, a bar that moved — slides the open card there. Panels
without a button (the `newteller-ctl` / D-Bus entry points, a keyboard
shortcut) fall back to a fixed corner, and the start menu keeps its old
bottom-left position.

The pointer is drawn by `qml/ShellFrame.qml` (`nubEdge` / `nubOffset`): the
frame keeps a small transparent strip around the card for it, so the card can
point at its button without clipping. A card that loses the keyboard focus
closes after a short delay, and Escape closes it from anywhere inside.

## Settings application

The start menu, the desktop right-click menu and `newteller --settings` all open
the same settings window:

```sh
newteller --settings
```

It manages the whole configuration of the shell:

- **Appearance** — pick one of the themes (with a live preview) and edit it:
  the six colours, panel opacity, corner radius, font size and family are saved
  as a user theme file in `~/.config/newteller/themes/` and applied immediately.
  A bullet marks themes with a custom copy; “Discard custom copy” removes it.
- **Panel & dock** — position, thickness, edge gap, auto-hide with delay, running
  badge, clock/date formats; dock behaviour (show running applications, group
  windows of one application, icon size) and the pinned applications list.
- **Desktop** — wallpaper browser over the pictures folder (`~/Pictures` or its localized
  XDG name) and `/usr/share/backgrounds`
  (with a random pick), icon visibility and grid sizes, right-click menu toggle.
  "Choose from file manager" opens the file chooser that is already installed in
  the system (the freedesktop FileChooser portal — the GTK dialog on GNOME, the
  KDE dialog on Plasma); the integrated browser is only a fallback for systems
  without the portal service, and it opens automatically when the portal cannot
  be used (not running, no permission, no answer after two minutes).
  Both pickers start in the folder of the current wallpaper, silently fall back
  to `~/Pictures` or the home directory when that folder is gone, and only
  accept a path that is a readable image. Set `NEWTELLER_NO_PORTAL=1` to skip
  a broken portal and always use the integrated picker. — the desktop never receives a file it
  cannot draw. The integrated picker is an ordinary dialog: it stays open when
  it loses the keyboard focus and closes on Esc, Cancel or the title-bar button.
  The colour palette of the theme editor opens as a full-height panel on the
  left edge of the screen.
- **Start menu** — size, typo-tolerant search, recent row and limits.
- **Applications** — terminal command, catalogue cap, clear the usage history.
- **System** — metrics sampling interval and the per-core/battery/thermal/disk
  groups, config-file watching debounce.
- **Plugins** — enable or disable every discovered plugin, rescan, auto-rescan.
- **About** — version, machine, session, window-tracking backend, catalogue
  size, memory/uptime; open/export the config JSON; the same validation report as
  `newteller-ctl validate`; reset all settings (with confirmation).

Every row has a small reset button that restores the key's default. Changes are
written to `shell.json` (debounced, atomic) and apply live — no restart needed.

Because the settings QML ships inside the executable through the Qt resource
system, the settings are always available: they are not plugins, not scripts and
not separate files a user could delete. Editing the raw JSON ("Open JSON" on the
About page) still works the same as before.

## Command line: newteller-ctl

The Rust build installs `newteller-ctl`, a standalone tool for inspecting and
configuring the shell (it does not need a running session):

```sh
newteller-ctl list                    # the effective configuration
newteller-ctl set panel.position top  # write one setting (type-coerced)
newteller-ctl validate                # problems in shell.json
newteller-ctl search terminal         # ranked, highlighted application search
newteller-ctl running                 # applications that are currently running
newteller-ctl metrics                 # one sample of cpu, memory, network, battery, disks
newteller-ctl doctor                  # diagnostics for a bug report
```

Groups: `paths`, `schema`, `list`, `get`, `set`, `unset`, `validate`, `init`,
`apps`, `search`, `categories`, `running`, `exec`, `bench`, `metrics`, `watch`,
`doctor`. `--json` gives machine-readable output everywhere; `newteller-ctl
--help` documents every flag. The `newteller` shell itself also accepts
`--help`, `--version`, `--print-config`, `--diagnostics`, `--theme <name>`,
`--settings` (opens the built-in settings window on startup) and
`--reset`.

## Themes

Built-in themes: `default`, `light`, `nord`, `ocean`, `sunrise`, `highcontrast`.
A custom theme is
a JSON file in `~/.config/newteller/themes/<name>.json`. Missing keys inherit
from `default`:

```json
{
  "background": "#101820",
  "foreground": "#e6edf3",
  "accent": "#ff7a00",
  "surface": "#1a2533",
  "border": "#2c3e50",
  "muted": "#7f8c8d",
  "opacity": 0.9,
  "radius": 12,
  "fontSize": 10,
  "fontFamily": "Inter"
}
```

Colors are hex (`#RRGGBB`). Select a theme with `"theme.name": "<name>"`.

## Plugins

A plugin is a directory with a `manifest.json` and a QML entry file. It is searched for in:

1. `$NEWTELLER_PLUGIN_PATH` (colon-separated)
2. `~/.config/newteller/plugins/`
3. `/usr/share/newteller/plugins/`

```json
{
  "id": "desktop-clock",
  "name": "Desktop Clock",
  "version": "1.0.0",
  "type": "desktop",
  "entry": "Widget.qml",
  "x": 60,
  "y": 60,
  "order": 10
}
```

- `type`: `desktop` (placed at `x`, `y` on the desktop) or `panel` (added to the panel's right side).
- Disable a plugin by adding its `id` to `"plugins.disabled"` in `shell.json`.

Bundled plugins:

| Plugin | Type | Shows |
| --- | --- | --- |
| `desktop-clock` | desktop | large clock and date |
| `desktop-sysinfo` | desktop | host, kernel, uptime, load, battery |
| `desktop-weather` | desktop | weather and a 5-day forecast from Open-Meteo (no key) for the city set with its gear button (`weather.location`, `weather.units`, `weather.showForecast`); nothing is fetched until a city is set |
| `panel-system-monitor` | panel | CPU and memory bars, network rates |

Plugins have access to the global objects:

| Object | Purpose |
|---|---|
| `Theme` | colors (`background`, `foreground`, `accent`, `surface`, `border`, `muted`), `opacity`, `radius`, `fontSize`, `fontFamily` |
| `Settings` | `values` (reactive map), `value(key)`, `setValue(key, v)` |
| `Apps` | `search(text, n)`, `searchDetailed(text, n)` (with `highlight` markup), `byId(id)`, `categories`, `recent(n)`, `isRunning(id)`, `launch(id)` |
| `Shell` | `now` (shared ticker), `iconUrl(name)`, `openUrl(url)`, `openPath(path)`, `runShell(cmd)`, `terminal(cmd)`, `clipboardText()`, `saveTextFile(path, text)`, `openSettings()`, `openConfig()`, `lock()`, `logout()`, `suspend()`, `reboot()`, `powerOff()`, `version`, `home`, `desktopUrl`, `hostname`, `distro`, `kernelVersion` |
| `System` | metrics on a shared ticker: `cpuPercent`, `cpuCount`, `load1/5/15`, `memoryPercent`, `memoryText`, `networkDownText`, `networkUpText`, `uptimeText`, `processes`, `battery*`, `temperatureText`, `disks`, `refresh()` |
| `Plugins` | `plugins` (list of all discovered plugins), `ofType(type)`, `at(id)`, `setEnabled(id, bool)`, `rescan()` |
| `Windows` | running top-level windows on a shared model: `windows` (list of `{id, title, appId, active, minimized, maximized}`), `count`, `backend`, `activate(id)`, `closeWindow(id)`, `minimize(id)`, `toggleMaximized(id)`, `activeWindow()`, `windowsOfApp(appId)` |
| `Apps` (dock helpers) | `matchWindowApp(appId, title)` maps a window to the catalogue |

The bundled `plugins/desktop-clock` is a complete example.

## Tests

```sh
bash tests/platform.sh            # distribution detection used by the scripts
cd rust && cargo test --workspace # the Rust core and CLI
pip install PySide6-Essentials
python3 tests/qml_smoke.py        # loads the QML offscreen, no compositor needed
bash tests/session_smoke.sh       # starts a real headless compositor and captures a frame
```

`tests/session_smoke.sh` starts `newteller-compositor --headless`, waits for the shell's
desktop and panel layers, opens the start menu with a virtual pointer (`wlrctl`), launches
a native Wayland test window, captures the output through the compositor's own screencopy
implementation (using `grim`), and checks that the frames contain the panel, the menu and
the window. It also runs `newteller-screenshot` and checks that a PNG was written.
`scripts/install-build-deps.sh` installs `grim` and `wlrctl`; set
`WLR_RENDERER=pixman` for a GPU-less CI container.

`tests/qml_smoke.py` loads `qrc:/qml/Main.qml` from `resources.qrc` on two
simulated screens, with stand-ins for the C++ objects and the layer-shell and
ShellWindow attached types, and fails on any QML warning. It checks the window
geometry and layer-shell settings of every window, the start menu search
(highlighting, filtering, launching), the dock (windows grouped into dock
entries, activation through the window list, one dock menu per panel), the
embedded settings window (opening through `Shell.settingsRequested`, page
switching), the run dialog and the
desktop right-click menu, and loads the bundled plugins. It does not build or
test the C++ code; the C++ and QML together are built by CI on every push.

## Layout

```
src/            C++ core (settings, theme, appmodel, shellcontroller, plugins, systemmonitor,
                windowlist, rustbridge, windowsystem, shellwindow, main)
src/session/    newteller-compositor: wlroots session, outputs, layer-shell, xdg-shell,
                XWayland, input, and screencopy
protocols/      wlr-layer-shell and xdg-shell XML used to generate compositor headers
qml/            Main.qml, Panel.qml, DockMenu.qml, StartMenu.qml, Desktop.qml, TaskButton.qml,
                RunDialog.qml, SettingsApp.qml and the Settings* row controls
rust/           Rust workspace: newteller-core (static lib + C ABI) and newteller-ctl
tests/          QML, shell and headless compositor smoke tests
themes/         built-in theme JSON files (compiled into the binary)
plugins/        example plugins installed to /usr/share/newteller/plugins
data/           .desktop files (session entry, optional shell entry and autostart)
```

## License

MIT
