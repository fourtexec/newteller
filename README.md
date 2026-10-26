# NewTeller

A complete Wayland desktop session for Debian 13 and Ubuntu 26.04. NewTeller owns the
display: `newteller-compositor` takes over DRM/KMS (or a nested window for development),
starts the QML desktop as its first client, and runs Wayland and X11 applications inside
it. No GNOME, KDE or other desktop environment is needed.

The desktop itself is the NewTeller shell: a Windows-style bottom panel, start menu,
desktop icons and widgets, with themes and plugins for deep customization. It still runs
inside another Wayland or X11 session for development, but that is now the fallback, not
the product.

- **Session:** a small wlroots 0.18/0.19 compositor (`newteller-compositor`)
  with layer-shell, xdg-shell, XWayland, foreign-toplevel, screenshots, clipboard,
  idle-inhibit and the usual input protocols. `newteller-session` is the login-session
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
  menu shows a live preview of every X11/XWayland window (`dock.thumbnails`),
  and pinned buttons can be dragged to reorder them.
- Start menu: highlighted fuzzy search, category chips, recently used row,
  Lock / Log out / Sleep / Restart / Shut down, Settings; right-click an
  application to pin or unpin it on the dock
- Desktop icons can be dragged to any position; the layout is saved in
  `desktop.iconPositions` and restored on the next start (right-click the
  desktop to auto-arrange or reset it)
- **Notification centre: the bell at the end of the panel.** The history of the
  built-in notification daemon, newest first, with per-entry removal, a clear-all
  button and the do-not-disturb switch; the bell counts what arrived while it
  was closed. Per-row removal is `Notifications.removeHistory()` on the daemon.
- **Settings application: a full settings GUI built into the shell.** Every key of
  `shell.json`, a theme editor, plugin management, diagnostics and config export —
  see [Settings application](#settings-application). The QML is compiled into the
  `newteller` executable, so it cannot be deleted or damaged without replacing the
  binary itself.
- Run dialog (`Alt+F2`): run a shell command or search applications; bare
  commands are remembered (`run.history`, capped by `run.historyLimit`) and
  Up/Down walks the history
- **Tooltips.** Panel buttons (dock icons, tray icons, bell, indicators, clock)
  describe themselves in a small layer surface above the hovered button; buttons
  inside the menus (start menu session actions, quick settings, notification
  centre) use an in-window bubble.
- Desktop: wallpaper, icons from `~/Desktop` (double-click to open), desktop
  plugins, right-click menu (change wallpaper, paste to a file, open a terminal,
  settings)
- System monitor (`System` object, on a shared ticker) with bundled panel and
  desktop widgets
- **Surface materials:** configure panel, dock buttons, floating shell windows and notifications independently as theme, opaque, half-transparent, frosted glass or liquid glass. These are lightweight Qt Quick layers; real backdrop blur is compositor-specific and is not required.
- Themes (JSON, including `nord`, `ocean` and `highcontrast`) and a settings file
  with live reload
- Plugin system (manifest + QML) with enable/disable and auto-rescan
- Window management: snap a window to the left/right half with `Super+Left`/`Super+Right`,
  maximise with `Super+Up`, restore with `Super+Down`, drag a window to an edge to snap it,
  move it between outputs with `Super+Shift+Left`/`Super+Shift+Right`, and show/hide the
  desktop with `Super+D`
- Screenshots: `Print` captures the whole output to `~/Pictures/Screenshots` and announces
  the saved path through the notification daemon
- Multi-monitor: one desktop and one panel per screen
- Rust core (application search index, metrics sampler, config, watcher) with a
  C++/Qt fallback, plus the `newteller-ctl` command line tool

## Build (Debian 13 / Ubuntu 26.04 LTS)

Release builds support Debian 13 and Ubuntu 26.04 LTS, with Qt 6 and wlroots development
packages, not just the runtime libraries. Debian 12 / Ubuntu 24.04 ship Qt5 LayerShellQt
and an older wlroots and are not supported by the release scripts.

`newteller-compositor` needs `libwlroots-0.18-dev` on Debian 13 or
`libwlroots-0.19-dev` on Ubuntu 26.04. `scripts/install-build-deps.sh` picks the right
package for the detected distribution.

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
A dedicated lock screen is not included yet, so the Lock action relies on a session
locker installed separately.

Windows use client-side decorations (title bars drawn by the toolkit), so every application
gets a usable frame without a separate window-decorator process. The compositor handles
these shortcuts globally, even when an application has focus:
`Alt+F2` (run dialog), `Super+Return` (terminal), `Super+S` (settings), `Super+Q`
(close the focused window), `Alt+F4` (close), `Super+Left`/`Super+Right` (snap to a half),
`Super+Up`/`Super+Down` (maximise/restore), `Super+Shift+Left`/`Super+Shift+Right`
(move to another output), `Super+D` (show desktop), `Super+M` (maximise),
`Super+F` (fullscreen), `Print` (screenshot), and `Alt+Tab` (switch windows). The running shell exports `org.newteller.Shell` on the session bus;
scripts can call `showStartMenu`, `showRunDialog`, `openSettings`, `lock`, `logout`
and the other session methods with `gdbus call`.

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
| `dock.group` | `true` | merge all windows of one application into one dock button |
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
| `desktop.iconPositions` | `{}` | saved drag-and-drop positions, keyed by file name |
| `icons.path` / `icons.set` | `""` / `"lucide"` | extra IconKit checkout overriding the bundled icons / set folder inside it |
| `icons.cache` | `true` | cache resolved icons |
| `plugins.disabled` | `[]` | plugin IDs to disable |
| `plugins.autoRescan` | `true` | rescan when the plugin directories change |
| `apps.recent` / `apps.recentLimit` | `[]` / `8` | usage history and row length |
| `apps.usage` | `{}` | launch counts (drives the recent row and ranking) |
| `apps.terminalCommand` | `""` | terminal used by "Open terminal" (auto-detected when empty) |
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
- **Desktop** — wallpaper browser over `~/Pictures` and `/usr/share/backgrounds`
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
