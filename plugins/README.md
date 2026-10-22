# NewTeller Plugins

This directory contains plugin widgets for NewTeller that extend its functionality.

## Plugin Types

NewTeller supports two types of plugins:

1. **Desktop Plugins** - Widgets that appear on the desktop
2. **Panel Plugins** - Widgets that appear in the panel

## Bundled Plugins

### Desktop Widgets

| Plugin | ID | Description |
|--------|-----|-------------|
| Desktop Clock | `desktop-clock` | Large clock and date display on the desktop |
| Desktop System Info | `desktop-sysinfo` | System information widget showing host, kernel, uptime, etc. |
| **Desktop Weather** | `desktop-weather` | Weather widget with current conditions, temperature, and forecast |
| **Desktop Sticky Notes** | `desktop-sticky-notes` | Create and manage sticky notes on your desktop |
| **Desktop Calendar** | `desktop-calendar` | Monthly calendar widget with current date highlighting |

### Panel Widgets

| Plugin | ID | Description |
|--------|-----|-------------|
| Panel System Monitor | `panel-system-monitor` | CPU and memory usage bars in the panel |
| **Panel CPU Monitor** | `panel-cpu-monitor` | CPU usage bar graph with customizable colors |
| **Panel Memory Monitor** | `panel-memory-monitor` | Memory usage bar graph with customizable colors |

## Plugin Structure

Each plugin is a directory containing:

```
plugin-name/
├── manifest.json    # Plugin metadata and configuration
└── Widget.qml      # Main QML widget file
```

### manifest.json Format

```json
{
  "id": "unique-plugin-id",
  "name": "Display Name",
  "version": "1.0.0",
  "type": "desktop" or "panel",
  "entry": "Widget.qml",
  "x": 100,
  "y": 100,
  "order": 10,
  "description": "Plugin description",
  "settings": {
    "customSetting": "defaultValue"
  }
}
```

### Fields

- **id** (required): Unique identifier for the plugin
- **name** (required): Human-readable display name
- **version** (required): Plugin version (semantic versioning recommended)
- **type** (required): Either "desktop" or "panel"
- **entry** (required): Path to the main QML file (usually "Widget.qml")
- **x**: Initial X position for desktop plugins
- **y**: Initial Y position for desktop plugins
- **order**: Sorting order (lower = earlier in lists)
- **description**: Short description shown in the plugins list
- **settings**: Default settings for the plugin (optional)

## Creating Custom Plugins

### Basic Desktop Widget

1. Create a directory in `~/.config/newteller/plugins/` or `/usr/share/newteller/plugins/`
2. Create a `manifest.json` file
3. Create a `Widget.qml` file

Example `manifest.json`:
```json
{
  "id": "my-widget",
  "name": "My Widget",
  "version": "1.0.0",
  "type": "desktop",
  "entry": "Widget.qml",
  "x": 100,
  "y": 100,
  "order": 50,
  "description": "My custom desktop widget"
}
```

Example `Widget.qml`:
```qml
import QtQuick
import NewTeller 1.0

Rectangle {
    id: widget
    width: 200
    height: 100
    color: Theme.surface
    border.width: 1
    border.color: Theme.border
    radius: Theme.radius

    Text {
        anchors.centerIn: parent
        text: "Hello, World!"
        color: Theme.foreground
        font.family: Theme.fontFamily
        font.pointSize: Theme.fontSize
    }

    // Make widget draggable
    MouseArea {
        anchors.fill: parent
        drag.target: widget
        cursorShape: Qt.OpenHandCursor
        onPressed: cursorShape = Qt.ClosedHandCursor
        onReleased: cursorShape = Qt.OpenHandCursor
    }
}
```

### Accessing Plugin Settings

Plugin settings defined in `manifest.json` are available through the `manifest.settings` object:

```qml
property string mySetting: manifest.settings ? manifest.settings.mySetting : "default"
```

### Available Global Objects

Plugins have access to the following global objects:

- **Theme**: Theme colors and styling
  - `Theme.background`, `Theme.foreground`, `Theme.accent`, etc.
  - `Theme.opacity`, `Theme.radius`, `Theme.fontSize`, `Theme.fontFamily`

- **Settings**: Shell configuration
  - `Settings.values` - All settings as a map
  - `Settings.value(key)` - Get a specific setting
  - `Settings.setValue(key, value)` - Set a setting

- **Shell**: Shell utilities
  - `Shell.now` - Current timestamp
  - `Shell.home` - User's home directory
  - `Shell.iconUrl(name, color)` - Get icon URL
  - `Shell.notify(title, message)` - Show notification
  - `Shell.openUrl(url)` - Open URL
  - `Shell.openPath(path)` - Open file path
  - `Shell.runShell(cmd)` - Run shell command
  - `Shell.terminal(cmd)` - Open terminal with command
  - `Shell.clipboardText()` - Get clipboard text
  - And many more...

- **System**: System metrics
  - `System.cpuPercent`, `System.memoryPercent`
  - `System.perCoreCpu` (if enabled)
  - `System.batteryPercent`, `System.networkDown`, `System.networkUp`
  - And many more...

- **Apps**: Application catalog
  - `Apps.search(query, limit)` - Search applications
  - `Apps.byId(id)` - Get application by ID
  - `Apps.launch(id)` - Launch application

- **Plugins**: Plugin management
  - `Plugins.plugins` - List of all plugins
  - `Plugins.setEnabled(id, enabled)` - Enable/disable plugin
  - `Plugins.rescan()` - Rescan plugins

## Plugin Management

### Enabling/Disabling Plugins

1. Open Settings → Plugins
2. Toggle the switch next to each plugin
3. Changes apply immediately

### Plugin Positions

Desktop plugins can be repositioned by:
1. Dragging the widget on the desktop
2. Editing the `x` and `y` values in the plugin's `manifest.json`

### Plugin Settings

Some plugins have customizable settings. These can be accessed by:
1. Clicking a settings button in the plugin (if provided)
2. Editing the `settings` object in the plugin's `manifest.json`

## Best Practices

1. **Use Theme Colors**: Always use `Theme.*` colors for consistent appearance
2. **Respect User Settings**: Check if features are enabled before using them
3. **Handle Missing Data**: Provide sensible defaults when data is unavailable
4. **Performance**: Avoid expensive operations in frequently-updated widgets
5. **Error Handling**: Gracefully handle errors and missing dependencies
6. **Documentation**: Include a `description` in your manifest

## Examples

### Weather Widget (`desktop-weather`)

The weather widget demonstrates:
- Custom settings (location, units, refresh interval)
- Settings popup
- Mock data for development
- Tooltip support
- Draggable widget

### Sticky Notes (`desktop-sticky-notes`)

The sticky notes widget demonstrates:
- Multiple note management
- Draggable and resizable notes
- Auto-save functionality
- Custom styling

### Calendar Widget (`desktop-calendar`)

The calendar widget demonstrates:
- Date calculations
- Dynamic layout
- Navigation controls
- Month/year display

### CPU Monitor (`panel-cpu-monitor`)

The CPU monitor panel widget demonstrates:
- System metrics access
- Bar graph visualization
- Tooltip with detailed information
- Customizable colors

## Plugin Discovery

NewTeller looks for plugins in the following locations (in order):

1. `$NEWTELLER_PLUGIN_PATH` (colon-separated list of directories)
2. `~/.config/newteller/plugins/`
3. `/usr/share/newteller/plugins/`

## Auto-Rescan

When `plugins.autoRescan` is enabled (default: true), NewTeller will automatically detect new plugins or changes to existing plugins without requiring a restart.

## Troubleshooting

### Plugin Not Showing Up

1. Check that the plugin directory exists
2. Verify the `manifest.json` is valid JSON
3. Ensure the `entry` file exists
4. Check that the `type` matches where you expect to see it (desktop/panel)
5. Enable the plugin in Settings → Plugins

### Plugin Crashes

1. Check the terminal output for errors
2. Verify all required imports are present
3. Ensure all referenced objects exist
4. Test with a minimal example and add features gradually

### Plugin Settings Not Working

1. Verify the settings are defined in `manifest.json`
2. Check that you're accessing them via `manifest.settings`
3. Ensure you're providing defaults for missing settings

## Contributing Plugins

If you create a useful plugin, consider:
1. Sharing it on the NewTeller plugin marketplace (when available)
2. Submitting it as a pull request to the main repository
3. Sharing it on GitHub with the `newteller-plugin` topic

## License

Unless otherwise specified, all bundled plugins are licensed under the same MIT license as NewTeller itself.
