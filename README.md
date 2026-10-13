# NewTeller

A customizable desktop shell layer for Debian-based systems. NewTeller runs **on top of** an
existing session (GNOME, KDE Plasma, Openbox, ...) and provides a Windows-style desktop: a
bottom panel, a start menu, desktop icons and widgets, with themes and plugins for deep
customization. All UI text is in English.

- **Core:** C++20 + Qt 6 (settings, themes, application catalogue, session actions, plugin loader)
- **UI:** QML with JavaScript logic (panel, start menu, desktop, widgets, plugins)
- **Layer:** wlr-layer-shell via LayerShellQt on Wayland (the panel reserves screen space)
- **Icons:** [IconKit](https://github.com/fourtexec/iconkit), with the system icon theme as fallback

## Status: early alpha

Working in the code, not yet verified on a real system (the first build has not been run):

- Bottom panel: Start button, pinned apps, plugin slots, clock
- Start menu: search, all desktop applications, Lock / Log out / Sleep / Restart / Shut down, Settings
- Desktop: wallpaper, icons from `~/Desktop` (double-click to open), desktop plugins
- Themes (JSON) and a settings file with live reload
- Plugin system (manifest + QML) with enable/disable
- Multi-monitor: one desktop and one panel per screen

Not implemented yet (planned, in this order):

1. Running-window list / taskbar (wlr-foreign-toplevel on Wayland, EWMH on X11)
2. System tray (StatusNotifierItem over D-Bus)
3. Notifications (`org.freedesktop.Notifications`)
4. Panel position options (top/left/right), auto-hide, centered mode
5. Settings GUI (currently edit `shell.json`)
6. Desktop right-click menu, drag-and-drop icon layout

## Build (Debian 12 / Ubuntu 24.04)

```sh
sudo apt install build-essential cmake git \
    qt6-base-dev qt6-declarative-dev qt6-svg-dev \
    qml6-module-qt-labs-folderlistmodel \
    qt6-image-formats-plugins
# Layer-shell bindings for Qt6. Find the exact name on your release:
apt search layershell | grep -i qt6
# e.g.: sudo apt install liblayershellqtinterface-dev

git clone https://github.com/fourtexec/iconkit ~/iconkit

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo cmake --install build
```

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
> don't overlap. On Openbox the panel is a normal window and does not reserve space yet.

## Configuration

`~/.config/newteller/shell.json` is a flat JSON object with dotted keys. The file is
watched, so edits apply without a restart:

```json
{
  "panel.height": 48,
  "panel.clockFormat": "HH:mm",
  "panel.pinned": ["org.gnome.Terminal.desktop", "firefox-esr.desktop"],
  "theme.name": "default",
  "desktop.wallpaper": "/home/you/Pictures/wall.jpg",
  "icons.path": "~/iconkit/icons",
  "icons.set": "lucide"
}
```

- `panel.pinned` takes desktop file IDs (see `/usr/share/applications`).
- `icons.set` is a folder inside IconKit's `icons/` directory, e.g. `lucide`, `tabler/outline`.
  Icon names not found there fall back to the system icon theme.

## Themes

Built-in themes: `default`, `light`. A custom theme is a JSON file in
`~/.config/newteller/themes/<name>.json`. Missing keys inherit from `default`:

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

Plugins have access to the global objects:

| Object | Purpose |
|---|---|
| `Theme` | colors (`background`, `foreground`, `accent`, `surface`, `border`, `muted`), `opacity`, `radius`, `fontSize`, `fontFamily` |
| `Settings` | `values` (reactive map), `value(key)`, `setValue(key, v)` |
| `Apps` | `search(text, n)`, `byId(id)`, `recent(n)`, `launch(id)` |
| `Shell` | `iconUrl(name)`, `openUrl(url)`, `runShell(cmd)`, `lock()`, `logout()`, `suspend()`, `reboot()`, `powerOff()` |
| `Plugins` | `plugins` (list of all discovered plugins) |

The bundled `plugins/desktop-clock` is a complete example.

## Layout

```
src/            C++ core (settings, theme, appmodel, shellcontroller, plugins, main)
qml/            Main.qml, Panel.qml, StartMenu.qml, Desktop.qml, TaskButton.qml
themes/         built-in theme JSON files (compiled into the binary)
plugins/        example plugins installed to /usr/share/newteller/plugins
data/           .desktop files (application entry and autostart)
```

## License

MIT
