# NewTeller Customization Settings and Improvements - Summary

This document summarizes all the new customization settings and improvements added to NewTeller.

## Standalone Wayland session

NewTeller is now a desktop session, not only a shell that runs inside another desktop:

- `newteller-compositor` is a wlroots compositor that owns DRM/KMS, libinput, outputs,
  layer-shell, xdg-shell, XWayland, foreign-toplevel, screencopy, virtual input and the
  usual clipboard/idle protocols.
- `newteller-session` is installed as `/usr/share/wayland-sessions/newteller.desktop`
  and starts the compositor from the display manager.
- The existing QML shell runs unchanged as the compositor's first Wayland client. The
  panel still reserves screen space through layer-shell; the desktop is a background layer.
- X11 applications run through XWayland and are tracked by the existing dock.
- `tests/session_smoke.sh` starts a headless session, opens the start menu with a virtual
  pointer, launches a native Wayland client, and captures/verifies frames through the
  compositor's own screencopy support.
- The compositor adds window snapping (`Super+Left/Right/Up/Down` and drag-to-edge),
  moving windows between outputs (`Super+Shift+Left/Right`), show-desktop (`Super+D`),
  `Alt+F4`, and `Print` screenshots saved to `~/Pictures/Screenshots`.

## Overview

This comprehensive update adds **100+ new settings** across all aspects of NewTeller, including:
- Enhanced theming and appearance controls
- Advanced panel and dock customization
- Desktop improvements with new wallpaper options
- Notification enhancements
- Keyboard shortcuts (hotkeys)
- Animation controls
- System monitoring improvements
- Plugin system enhancements
- New bundled widgets (weather, calendar, sticky notes, CPU/memory monitors)
- And much more!

---

## 🔧 Fixed behaviour

- Choosing a wallpaper through the file-manager dialog no longer crashes. The
  FileChooser portal's image filter uses nested D-Bus structures; those types
  (and their lists) are now registered before the first picker request, so the
  D-Bus call cannot abort the shell while serialising it. `NEWTELLER_NO_PORTAL=1`
  forces the integrated picker for systems with a broken portal.
- Closing a window (including the file picker or the settings window) no longer
  tears the compositor down. Native xdg/XWayland requests and foreign-toplevel
  requests now use separate `wl_listener` objects; sharing one listener between
  two signals corrupted Wayland's listener lists when the view was destroyed.
- Settings → Desktop → “Choose from file manager” no longer takes the shell
  down. The wallpaper image was decoded at the photo's full resolution
  (`sourceSize` was only set when the blur option was on), so a big camera
  photo exceeded the GPU's maximum texture size and the scene graph aborted;
  the desktop and both pickers now decode wallpapers/thumbnails at the size
  they are drawn. The dialog itself also closed the instant it was not the
  active window (which reads as "it crashed the picker"), could be handed a
  `file://file:///…` URL or a folder that no longer exists, and could throw on
  a file name containing `%`; it now normalises every path, falls back to
  `~/Pictures` or home, only accepts readable images, stays open on focus
  changes and closes on Esc/Cancel. On the D-Bus side the FileChooser portal
  request now uses the request object path the portal returns, has a timeout,
  never wedges on a lost answer and always ends in exactly one
  `filePicked`/`pickFailed`.
- `panel.autoHideDelay` is honoured: an auto-hidden panel waits the configured
  delay after the pointer leaves before it slides away (it used to vanish
  immediately, despite the setting and the settings-UI row).
- `panel.showRunning` now controls the running badge on dock buttons.
- `dock.position` and `panel.centerDock` align the dock at the start, centre or
  end of the bar; `panel.spacing`, `panel.leftSpacing`, `panel.rightSpacing` and
  `panel.zoneSpacing` are applied to the bar layout.
- `dock.iconSpacing`, `dock.scrollable`, `dock.multiRow` and `dock.rows` are
  implemented: the dock can wrap into up to three rows and only scrolls when the
  setting allows it. A vertical panel now lays the dock out as a column instead
  of a clipped horizontal row.
- The divider between the running and the minimised dock zone no longer
  collapses to zero pixels (it only had Layout hints, which a Row ignores).
- Panel text and icons follow `panel.foregroundColor` when custom panel colours
  are enabled.
- The QML smoke test now verifies that every `Settings` key used in QML exists
  in the C++ defaults or the Rust schema, so a typo cannot silently turn a
  control into a no-op.

---

## 📋 New Settings Categories

### 1. Appearance & Theming (`settings.cpp`)

#### Surface materials (panel, dock, popups, notifications)
- `panel.material` - Panel surface: theme, opaque, translucent, frosted, liquid
- `dock.material` - Idle dock button surface: none, opaque, translucent, frosted, liquid
- `windows.material` - Surface of the floating shell windows (start menu, run dialog, clock, dock menu)
- `notifications.material` / `notifications.opacityPercent` / `notifications.cornerRadius` -
  notification popups and the notification centre have their own surface settings
- `qml/SurfaceStyle.qml` draws all four looks with static Qt Quick rectangles (no
  Chromium, no per-frame blur shader); `qml/NotificationAppearance.qml` maps the
  notification colour theme onto those surfaces

#### Panel Styling
- `panel.customColors` - Enable custom panel colors
- `panel.backgroundColor` - Custom panel background color
- `panel.backgroundOpacity` - Panel background opacity (0.3-1.0)
- `panel.foregroundColor` - Custom panel text/foreground color

#### Dock Styling
- `dock.backgroundColor` - Custom dock background color
- `dock.backgroundOpacity` - Dock surface opacity (0.5 by default)
- `dock.hoverOpacity` - Dock button hover opacity
- `dock.activeOpacity` - Dock button active opacity
- `dock.showLabels` - Show application names below dock icons
- `dock.labelPosition` - Label position: bottom, right, top, left
- `dock.iconColorMode` - Icon color mode: theme, custom, original
- `dock.customIconColor` - Custom icon color (hex)

#### Start Menu Styling
- `startmenu.backgroundColor` - Custom start menu background
- `startmenu.backgroundOpacity` - Start menu background opacity
- `startmenu.foregroundColor` - Custom start menu text color
- `startmenu.accentColor` - Custom start menu accent color
- `startmenu.highlightColor` - Search result highlight color

#### Desktop Styling
- `desktop.backgroundColor` - Custom desktop background color
- `desktop.backgroundGradient` - Use gradient background

#### Window Styling
- `windows.enableAnimations` - Enable window animations
- `windows.animationSpeed` - Window animation speed multiplier

#### Theme Variants
- `theme.followSystem` - Follow system light/dark theme
- `theme.systemTheme` - System theme preference: auto, light, dark

---

### 2. Panel & Dock Improvements

#### Panel Layout
- `panel.spacing` - General panel spacing (0-64px)
- `panel.leftSpacing` - Left edge spacing (0-64px)
- `panel.rightSpacing` - Right edge spacing (0-64px)
- `panel.centerDock` - Center dock buttons in panel
- `dock.position` - Dock position in panel: left, center, right
- `panel.zoneSpacing` - Spacing between panel zones (0-24px)

#### Dock Layout
- `dock.multiRow` - Enable multi-row dock
- `dock.rows` - Number of dock rows (1-3)
- `dock.scrollable` - Make dock scrollable when overflowed
- `dock.iconSpacing` - Spacing between dock icons (0-24px)
- `dock.shrinkIcons` - Shrink icons on overflow (already existed)
- `dock.separateBackground` - Separate background/minimized apps (already existed)

---

### 3. Desktop Enhancements

#### Wallpaper
- `desktop.wallpaperMode` - Wallpaper display mode: fill, fit, stretch, tile, center
- `desktop.wallpaperBlur` - Apply blur effect to wallpaper (0-50px)
- `desktop.wallpaperDarken` - Darken wallpaper (0-100%)
- `desktop.perMonitorWallpaper` - Per-monitor wallpapers (experimental)
- `desktop.wallpaperSlideshow` - Enable wallpaper slideshow
- `desktop.wallpaperSlideshowInterval` - Slideshow interval in seconds (10-3600)

#### Icons
- `desktop.iconColor` - Custom icon color (hex)
- `desktop.iconShadow` - Enable icon drop shadows
- `desktop.iconGlow` - Enable icon glow effects
- `desktop.showHiddenFiles` - Show hidden files on desktop
- `desktop.autoArrangeIcons` - Auto-arrange desktop icons

#### Grid
- `desktop.cellWidth` - Icon grid cell width (already existed, 64-160px)
- `desktop.cellHeight` - Icon grid cell height (already existed, 64-180px)

---

### 4. Notifications

#### Appearance
- `notifications.theme` - Notification theme: system, light, dark, custom
- `notifications.customBackground` - Custom notification background color
- `notifications.customForeground` - Custom notification text color
- `notifications.groupByApp` - Group notifications by application
- `notifications.showAppIcon` - Show application icon in notifications

#### Sounds
- `notifications.soundEnabled` - Enable notification sounds
- `notifications.soundFile` - Custom notification sound file path

#### Do Not Disturb
- `notifications.doNotDisturbSchedule` - Enable scheduled DND
- `notifications.dndStartTime` - DND start time (HH:mm, 24h format)
- `notifications.dndEndTime` - DND end time (HH:mm, 24h format)

#### Behavior
- `notifications.enabled` - Enable notifications (already existed)
- `notifications.doNotDisturb` - Do not disturb mode (already existed)
- `notifications.position` - Notification position (already existed)
- `notifications.duration` - Notification duration in seconds (already existed)
- `notifications.historyLimit` - History length (already existed)

---

### 5. Hotkeys (Keyboard Shortcuts)

All hotkeys can be customized with modifier combinations:
- Modifiers: `Meta` (Windows key), `Ctrl`, `Alt`, `Shift`
- Combine with `+` (e.g., `Ctrl+Alt+T`)
- Multiple shortcuts can be separated by commas

#### Shell Actions
- `hotkeys.enabled` - Enable hotkeys globally
- `hotkeys.startMenu` - Open start menu (default: `Meta`)
- `hotkeys.runDialog` - Open run dialog (default: `Alt+F2`)
- `hotkeys.settings` - Open settings (default: `Meta+,Meta+S`)
- `hotkeys.notificationCenter` - Open notification centre (default: `Meta+N`)
- `hotkeys.quickSettings` - Open quick settings (default: `Meta+Q`)

#### System Actions
- `hotkeys.lockScreen` - Lock screen (default: `Meta+L`)
- `hotkeys.logout` - Log out (default: `Meta+Shift+Q`)
- `hotkeys.sleep` - Sleep system (default: `Meta+Shift+S`)
- `hotkeys.reboot` - Reboot system (default: `Meta+Shift+R`)
- `hotkeys.shutdown` - Shut down system (default: `Meta+Shift+P`)

#### Application Launchers
- `hotkeys.terminal` - Open terminal (default: `Meta+T`)
- `hotkeys.fileManager` - Open file manager (default: `Meta+E`)
- `hotkeys.switcher` - Application switcher (default: `Alt+Tab`)

---

### 6. Animations

#### Global
- `animations.enabled` - Enable animations globally
- `animations.speed` - Global animation speed multiplier (0.1-3.0)

#### Panel
- `animations.panelHide` - Enable panel hide/show animations

#### Windows
- `windows.enableAnimations` - Enable window animations
- `windows.animationSpeed` - Window animation speed multiplier

---

### 7. System & Performance

#### Metrics
- `system.showCpuTemp` - Show CPU temperature
- `system.showGpu` - Show GPU information
- `system.showFanSpeed` - Show fan speed
- `system.interval` - Sampling interval (already existed)
- `system.perCore` - Per-core CPU metrics (already existed)
- `system.battery` - Battery metrics (already existed)
- `system.thermal` - Thermal zone metrics (already existed)
- `system.disks` - Disk metrics (already existed)

#### Caching
- `cache.iconCache` - Enable icon caching
- `cache.maxIconCacheSize` - Maximum icon cache size (100-5000)
- `cache.searchCache` - Enable search caching
- `cache.maxSearchCacheSize` - Maximum search cache size (100-2000)

#### Clipboard
- `clipboard.historyEnabled` - Enable clipboard history
- `clipboard.historySize` - Clipboard history size (10-1000)
- `clipboard.persistHistory` - Persist clipboard history across restarts

---

### 8. Applications

#### Startup
- `startup.apps` - List of applications to launch on startup
- `startup.delay` - Startup delay in seconds (0-60)

#### Search
- `search.maxResults` - Maximum search results (10-500)
- `search.showCategories` - Show category chips in search
- `search.categoryIcons` - Show category icons

#### General
- `apps.terminalCommand` - Terminal command (already existed)
- `apps.maxApps` - Application catalogue cap (already existed)

---

### 9. Plugin System Improvements

#### Settings
- `plugins.onlineEnabled` - Enable online plugin discovery
- `plugins.autoUpdate` - Auto-update plugins
- `plugins.showDescriptions` - Show plugin descriptions in list
- `plugins.autoRescan` - Auto-rescan plugins on change (already existed)

#### Bundled Plugins (NEW)

**Desktop Widgets:**
1. `desktop-clock` - Large clock and date widget (already existed)
2. `desktop-sysinfo` - System information widget (already existed)
3. **`desktop-weather`** - Weather widget with temperature, conditions, and forecast
4. **`desktop-sticky-notes`** - Create and manage sticky notes on desktop
5. **`desktop-calendar`** - Monthly calendar widget

**Panel Widgets:**
1. `panel-system-monitor` - CPU and memory bars (already existed)
2. **`panel-cpu-monitor`** - CPU usage bar graph with customizable colors
3. **`panel-memory-monitor`** - Memory usage bar graph with customizable colors

---

### 10. Multi-Monitor Support

- `multiMonitor.perMonitorSettings` - Enable per-monitor settings
- `multiMonitor.panelPositions` - Per-monitor panel positions
- `multiMonitor.wallpapers` - Per-monitor wallpapers

---

## 🎯 Settings Application Updates

### New Pages Added
1. **Hotkeys Page** - Manage all keyboard shortcuts
   - Organized by category (Shell actions, System actions, Application launchers)
   - Hotkey reference guide
   - Real-time validation

2. **Animations Page** - Control all animation settings
   - Global animation toggle
   - Speed multipliers
   - Preview/demo section
   - Per-component animation controls

### Enhanced Existing Pages

#### Appearance
- Added Panel styling section with custom colors
- Added Dock styling section with labels and icon colors
- Added Theme variants section for system theme following

#### Panel & Dock
- Added Panel layout section with spacing controls
- Added Dock layout section with multi-row and scrolling options

#### Desktop
- Added Wallpaper mode and effects (blur, darken, slideshow)
- Added Desktop background section with custom colors
- Added Icon customization (color, shadow, glow)
- Added Grid and visibility options

#### Notifications
- Added Appearance section with theme and custom colors
- Added Sounds section
- Added Do Not Disturb scheduling

#### System
- Added Metrics section with new monitoring options
- Added Caching section
- Added Clipboard section

#### Plugins
- Added Plugin marketplace section (experimental)
- Enhanced plugin list with descriptions toggle

#### Applications
- Added Startup applications section
- Added Search settings section

---

## 📁 Files Modified

### C++ Source Files
- `src/settings.cpp` - Added 100+ new default settings keys

### QML Files
- `qml/SettingsApp.qml` - Added new pages and expanded existing pages with new settings
- `qml/Panel.qml` - Added support for custom colors, dock labels, and layout options
- `qml/TaskButton.qml` - Added dock label display and custom icon color support
- `qml/Desktop.qml` - Added wallpaper modes, effects, and icon customization

### New Plugin Directories Created
- `plugins/desktop-weather/` - Weather widget
- `plugins/desktop-sticky-notes/` - Sticky notes widget
- `plugins/desktop-calendar/` - Calendar widget
- `plugins/panel-cpu-monitor/` - CPU monitor panel widget
- `plugins/panel-memory-monitor/` - Memory monitor panel widget

---

## 🚀 Usage

### Accessing New Settings

1. Open the Settings application:
   - Click the settings button in the start menu
   - Right-click on the desktop and select "Settings"
   - Run `newteller --settings` from terminal

2. Navigate to the new pages:
   - **Hotkeys** - Configure keyboard shortcuts
   - **Animations** - Control animation behavior

3. Explore enhanced sections in existing pages:
   - Appearance: Custom panel and dock styling
   - Panel & Dock: Advanced layout options
   - Desktop: Wallpaper effects and icon customization
   - Notifications: Themes, sounds, and scheduling
   - System: Additional monitoring options
   - Plugins: New bundled plugins and marketplace

### Using New Plugins

The new plugins will be automatically discovered and can be enabled through:
1. Settings → Plugins page
2. Toggle the switch next to each plugin to enable/disable
3. Customize plugin positions by editing their manifest.json files

### Customizing Hotkeys

1. Go to Settings → Hotkeys
2. Click on any hotkey field
3. Type your desired key combination
4. Common modifiers: `Meta` (Windows key), `Ctrl`, `Alt`, `Shift`
5. Combine with `+` (e.g., `Ctrl+Alt+T`)
6. Multiple shortcuts can be separated by commas

---

## 📊 Statistics

- **New Settings Keys**: 100+
- **New Settings Pages**: 2 (Hotkeys, Animations)
- **Enhanced Settings Pages**: 8 (Appearance, Panel & Dock, Desktop, Notifications, System, Plugins, Applications, About)
- **New Plugins**: 5 (Weather, Sticky Notes, Calendar, CPU Monitor, Memory Monitor)
- **Files Modified**: 5 C++/QML files
- **New Directories**: 5 plugin directories

---

## 🔧 Technical Notes

### Backward Compatibility

All new settings have sensible defaults that maintain backward compatibility with existing configurations. Users with existing `shell.json` files will not experience any issues.

### Performance Considerations

- New monitoring options (CPU temp, GPU, fan speed) are disabled by default
- Caching options help improve performance on lower-end systems
- Animation controls allow users to disable resource-intensive effects

### Theme System

The theme system now supports:
- Custom colors for individual UI elements
- System theme following (light/dark mode)
- Per-component styling overrides

### Plugin Architecture

New plugins follow the existing manifest.json format with added support for:
- Custom settings in manifest
- Plugin descriptions
- Type-specific behavior (desktop vs panel)

---

## 🎨 Design Philosophy

The new customization options follow these principles:

1. **Discoverable**: All settings are accessible through the GUI
2. **Live Updates**: Changes apply immediately without restart
3. **Sensible Defaults**: New features are disabled or use reasonable defaults
4. **Consistent**: Settings follow existing patterns and conventions
5. **Flexible**: Users can customize nearly every aspect of the shell

---

## 📝 Future Enhancements

Potential areas for future improvements:
- Per-application window rules
- Advanced window management (tiling, snapping)
- More system monitoring metrics
- Plugin settings UI for individual plugins
- Theme marketplace/online discovery
- Custom CSS/style overrides for advanced users

---

## 🏆 Summary

This comprehensive update transforms NewTeller from a functional desktop shell into a highly customizable environment that can be tailored to individual preferences. With over 100 new settings, 5 new plugins, and enhanced existing features, users now have unprecedented control over their desktop experience.
