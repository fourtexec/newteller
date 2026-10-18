# NewTeller

A customizable desktop shell layer for Debian-based systems. NewTeller runs **on top of** an
existing session (GNOME, KDE Plasma, Openbox, ...) and provides a Windows-style desktop: a
bottom panel, a start menu, desktop icons and widgets, with themes and plugins for deep
customization. All UI text is in English.

- **Core:** C++20 + Qt 6 (settings, themes, application catalogue, session actions, plugin loader)
- **UI:** QML with JavaScript logic (panel, start menu, desktop, widgets, plugins)
- **Layer:** wlr-layer-shell via LayerShellQt on Wayland compositors that support it; X11 dock and
  desktop windows elsewhere (X11 sessions, and XWayland on GNOME). The panel reserves screen space
  either way; see [Display servers](#display-servers)
- **Icons:** [IconKit](https://github.com/fourtexec/iconkit), with the system icon theme as fallback

## Status: early alpha

Implemented and covered by the tests (see [Tests](#tests)); not yet verified on a
real desktop:

- Panel: Start button, pinned apps with a running badge, plugin slots, clock with
  optional date, on any edge (`panel.position`: bottom/top/left/right), optional
  auto-hide
- **Dock: every started application appears in the panel.** Real top-level windows
  are tracked on X11 (EWMH) and on Wayland compositors with wlr-foreign-toplevel
  (KDE Plasma, sway, Hyprland, labwc, Wayfire, niri, ...); on other compositors the
  dock falls back to the /proc based application list. Windows can be activated,
  minimised and closed from the dock (click, middle click and a right-click menu),
  and windows of one application can be grouped into a single button.
- Start menu: highlighted fuzzy search, category chips, recently used row,
  Lock / Log out / Sleep / Restart / Shut down, Settings
- **Settings application: a full settings GUI built into the shell.** Every key of
  `shell.json`, a theme editor, plugin management, diagnostics and config export —
  see [Settings application](#settings-application). The QML is compiled into the
  `newteller` executable, so it cannot be deleted or damaged without replacing the
  binary itself.
- Run dialog (`Alt+F2`): run a shell command or search applications
- Desktop: wallpaper, icons from `~/Desktop` (double-click to open), desktop
  plugins, right-click menu (change wallpaper, paste to a file, open a terminal,
  settings)
- System monitor (`System` object, on a shared ticker) with bundled panel and
  desktop widgets
- Themes (JSON, including `nord`, `ocean` and `highcontrast`) and a settings file
  with live reload
- Plugin system (manifest + QML) with enable/disable and auto-rescan
- Multi-monitor: one desktop and one panel per screen
- Rust core (application search index, metrics sampler, config, watcher) with a
  C++/Qt fallback, plus the `newteller-ctl` command line tool

Not implemented yet (planned, in this order):

1. System tray (StatusNotifierItem over D-Bus)
2. Notifications (`org.freedesktop.Notifications`)
3. Drag-and-drop desktop icon layout
4. Per-window thumbnails and drag-to-reorder dock buttons

## Build (Debian 13 / Ubuntu 26.04 LTS)

Release builds support Debian 13 and Ubuntu 26.04 LTS, with Qt 6 development packages, not just the
runtime libraries. Debian 12 / Ubuntu 24.04 ship Qt5 LayerShellQt and are not
supported by the release scripts.

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
LayerShellQt development package, `libwayland-dev`, `libxcb1-dev`, build tools, and runtime
QML modules.
For a custom Qt SDK (6.4 or newer), export `CMAKE_PREFIX_PATH` pointing to its
installation prefix, or `Qt6_DIR` pointing to the directory containing
`Qt6Config.cmake`. LayerShellQt must also be built against that Qt version.

IconKit is optional: clone https://github.com/fourtexec/iconkit to `~/iconkit`
for additional icons, or use the system icon theme.

Run it from a terminal inside your current session:

```sh
newteller
```

To start it on login, copy the autostart entry:

```sh
mkdir -p ~/.config/autostart
cp /usr/share/newteller/newteller-autostart.desktop ~/.config/autostart/newteller.desktop
```

> **Note:** NewTeller draws its own panel. Turn off the panel of your current desktop
> (for example, remove the Plasma panel or disable the GNOME dock extension) so the two
> don't overlap.

### Display servers

NewTeller picks the windowing system when it starts:

| Session | How NewTeller runs |
| --- | --- |
| Wayland with wlr-layer-shell (KDE Plasma, sway, Hyprland, labwc, Wayfire, niri, ...) | Native Wayland. Desktop, panel and start menu are layer surfaces, and the panel reserves its height. |
| Wayland without wlr-layer-shell (GNOME) | Through XWayland, printing `using XWayland instead`. Applications started from NewTeller still use native Wayland. |
| X11 (Openbox, Xfce, ...) | X11. The desktop is a desktop-type window below all others; the panel is a dock that reserves the bottom edge of each screen. |

GNOME Shell's top bar keeps running next to NewTeller. Setting `QT_QPA_PLATFORM` (or passing
`-platform`) overrides the automatic choice; with `QT_QPA_PLATFORM=wayland` on GNOME the shell
windows are ordinary windows.

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

The ZIP contains `usr/bin/newteller`, the example plugins, application/autostart
entries, this README, `BUILDINFO` with the full commit SHA, and installation scripts.
QML and the default/light themes are embedded in the executable. Qt6 and
LayerShellQt libraries/modules are **system dependencies**, installed by the included
installer; this is not a portable archive for Debian 12 or Ubuntu 24.04.
IconKit is optional (system icons are used if it is absent).

Download the ZIP **matching your distribution** and its checksum into the same
directory. The installer rejects archives built for a different distribution.
For example, on Debian 13 x86_64:

```sh
sha256sum -c newteller-commit-<full SHA>-debian13-x86_64.zip.sha256
unzip newteller-commit-<full SHA>-debian13-x86_64.zip
cd newteller-commit-<full SHA>-debian13-x86_64
sudo bash install.sh
newteller # run as your normal user inside a desktop session
```

The installer needs an internet connection to install dependencies with `apt`.
Autostart is opt-in: copy `/usr/share/newteller/newteller-autostart.desktop` to
`~/.config/autostart/newteller.desktop` as described above.

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
  "icons.path": "~/iconkit/icons",
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
| `panel.autoHideDelay` | `400` | delay in ms before auto-hide applies |
| `panel.clockFormat` | `"HH:mm"` | `Qt.formatDateTime` time format |
| `panel.dateFormat` | `"ddd d MMM"` | date format, shown before the time |
| `panel.showDate` | `false` | show the date next to the clock |
| `panel.showRunning` | `true` | dot badge for running applications |
| `panel.pinned` | `[]` | desktop file IDs pinned to the panel |
| `dock.showWindows` | `true` | show running applications/windows in the dock |
| `dock.group` | `true` | merge all windows of one application into one dock button |
| `dock.iconSize` | `28` | dock button icon size in pixels |
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
| `icons.path` / `icons.set` | `"~/iconkit/icons"` / `"lucide"` | IconKit location |
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

- `panel.pinned` takes desktop file IDs (see `/usr/share/applications`).
- `icons.set` is a folder inside IconKit's `icons/` directory, e.g. `lucide`, `tabler/outline`.
  Icon names not found there fall back to the system icon theme.

`newteller-ctl schema` prints the same table in machine-readable form.

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
```

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
qml/            Main.qml, Panel.qml, DockMenu.qml, StartMenu.qml, Desktop.qml, TaskButton.qml,
                RunDialog.qml, SettingsApp.qml and the Settings* row controls
rust/           Rust workspace: newteller-core (static lib + C ABI) and newteller-ctl
tests/          shell and QML smoke tests
themes/         built-in theme JSON files (compiled into the binary)
plugins/        example plugins installed to /usr/share/newteller/plugins
data/           .desktop files (application entry and autostart)
```

## License

MIT
