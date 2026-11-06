# NewTeller - New Settings Reference

This document provides a complete reference for all new settings added to NewTeller, organized by category.

---

## 📖 How to Use This Reference

Each setting is documented with:
- **Key**: The setting key (e.g., `panel.height`)
- **Type**: Data type (boolean, string, integer, color, etc.)
- **Default**: Default value
- **Description**: What the setting controls
- **Page**: Where to find it in Settings application

---

## 🎨 Appearance & Theming

### Panel Styling
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `panel.material` | string | `"theme"` | Surface: theme, opaque, translucent (50%), frosted or liquid glass | Appearance |
| `panel.customColors` | boolean | `false` | Enable custom panel colors independent from theme | Appearance |
| `panel.backgroundColor` | color | `""` | Custom panel background color (hex) | Appearance |
| `panel.backgroundOpacity` | number | `0.92` | Panel background opacity (0.3-1.0) | Appearance |
| `panel.foregroundColor` | color | `""` | Custom panel text/foreground color (hex) | Appearance |

### Dock Styling
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `dock.material` | string | `"none"` | Floating dock pill: none, opaque (non transparent), translucent (half transparent), frosted or liquid glass | Appearance |
| `dock.backgroundColor` | color | `""` | Custom dock pill tint as hex (empty follows the theme) | Appearance |
| `dock.backgroundOpacity` | number | `0.5` | Bias around the material's preset dock opacity: 0.5 keeps the preset, lower is clearer, higher is more solid | Appearance |
| `dock.hoverOpacity` | number | `0.15` | Dock button hover opacity | Appearance |
| `dock.activeOpacity` | number | `0.30` | Dock button active opacity | Appearance |
| `dock.showLabels` | boolean | `false` | Show application names below dock icons | Appearance |
| `dock.labelPosition` | string | `"bottom"` | Label position: bottom, right, top, left | Appearance |
| `dock.iconColorMode` | string | `"theme"` | Icon color mode: theme, custom, original | Appearance |
| `dock.customIconColor` | color | `"#ffffff"` | Custom icon color (hex) | Appearance |

### Start Menu Styling
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `startmenu.backgroundColor` | color | `""` | Custom start menu background color | Appearance |
| `startmenu.foregroundColor` | color | `""` | Custom start menu text color | Appearance |
| `startmenu.accentColor` | color | `""` | Custom start menu accent color | Appearance |

### Desktop Styling
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `desktop.backgroundColor` | color | `""` | Custom desktop background color | Appearance |
| `desktop.backgroundGradient` | boolean | `true` | Use gradient background when no wallpaper | Appearance |

### Window Styling
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `windows.material` | string | `"theme"` | Shell popup material: theme, opaque, translucent (50%), frosted or liquid glass | Appearance |
| `windows.style` | string | `"card"` | Popup frame: card, glass, flat or outline | Appearance |
| `windows.radius` | integer | `-1` | Corner radius in pixels; -1 follows the theme | Appearance |
| `windows.opacityPercent` | integer | `0` | Opacity override; 0 uses the selected material preset | Appearance |
| `windows.borderWidth` | integer | `-1` | Border width in pixels; -1 follows the theme | Appearance |
| `windows.borderColor` | color | `""` | Border color; empty follows the theme | Appearance |
| `windows.shadow` | integer | `1` | Shadow strength from 0 (off) to 3 | Appearance |

### Theme Variants
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|

---

## 📊 Panel & Dock Improvements

### Panel Layout
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `panel.spacing` | integer | `8` | General panel spacing (0-64px) | Panel & Dock |
| `panel.leftSpacing` | integer | `8` | Left edge spacing (0-64px) | Panel & Dock |
| `panel.rightSpacing` | integer | `8` | Right edge spacing (0-64px) | Panel & Dock |
| `panel.centerDock` | boolean | `false` | Center dock buttons in panel | Panel & Dock |
| `dock.position` | string | `"left"` | Dock position in panel: left, center, right | Panel & Dock |
| `panel.zoneSpacing` | integer | `8` | Spacing between panel zones (0-24px) | Panel & Dock |

### Dock Layout
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `dock.multiRow` | boolean | `false` | Enable multi-row dock | Panel & Dock |
| `dock.rows` | integer | `1` | Number of dock rows (1-3) | Panel & Dock |
| `dock.scrollable` | boolean | `true` | Make dock scrollable when overflowed | Panel & Dock |
| `dock.iconSpacing` | integer | `4` | Spacing between dock icons (0-24px) | Panel & Dock |
| `dock.shrinkIcons` | boolean | `true` | Shrink icons on overflow | Panel & Dock |
| `dock.separateBackground` | boolean | `true` | Separate background/minimized apps | Panel & Dock |
| `dock.thumbnails` | boolean | `true` | Live window previews in the dock menu | Panel & Dock |

### System Tray
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `tray.enabled` | boolean | `true` | Act as the session's StatusNotifierWatcher and show tray icons | Panel & Dock |
| `tray.menus` | boolean | `true` | Render the item's DBusMenu on right click instead of its own popup | Panel & Dock |

---

## 🖥️ Desktop Enhancements

### Wallpaper
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `desktop.wallpaperMode` | string | `"fill"` | Wallpaper display mode: fill, fit, stretch, tile, center | Desktop |
| `desktop.wallpaperBlur` | integer | `0` | Apply blur effect to wallpaper (0-50px) | Desktop |
| `desktop.wallpaperDarken` | integer | `0` | Darken wallpaper (0-100%) | Desktop |
| `desktop.perMonitorWallpaper` | boolean | `false` | Per-monitor wallpapers (experimental) | Desktop |
| `desktop.wallpaperSlideshow` | boolean | `false` | Enable wallpaper slideshow | Desktop |
| `desktop.wallpaperSlideshowInterval` | integer | `60` | Slideshow interval in seconds (10-3600) | Desktop |

### Icons
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `desktop.iconColor` | color | `"#ffffff"` | Custom icon color (hex) | Desktop |
| `desktop.iconShadow` | boolean | `true` | Enable icon drop shadows | Desktop |
| `desktop.iconGlow` | boolean | `false` | Enable icon glow effects | Desktop |
| `desktop.showHiddenFiles` | boolean | `false` | Show hidden files on desktop | Desktop |
| `desktop.autoArrangeIcons` | boolean | `true` | Auto-arrange desktop icons | Desktop |
| `desktop.iconPositions` | object | `{}` | Saved icon positions keyed by file name | Desktop |

### Grid
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `desktop.cellWidth` | integer | `96` | Icon grid cell width (64-160px) | Desktop |
| `desktop.cellHeight` | integer | `110` | Icon grid cell height (64-180px) | Desktop |

---

## 🔔 Notifications

### Appearance
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `notifications.material` | string | `"theme"` | Surface material shared by notification popups and the notification centre | Notifications |
| `notifications.opacityPercent` | integer | `0` | Opacity override from 0 to 100; 0 uses the material preset | Notifications |
| `notifications.cornerRadius` | integer | `-1` | Card radius in pixels; -1 follows the shell window radius | Notifications |
| `notifications.theme` | string | `"system"` | Notification theme: system, light, dark, custom | Notifications |
| `notifications.customBackground` | color | `""` | Custom notification background color | Notifications |
| `notifications.customForeground` | color | `""` | Custom notification text color | Notifications |
| `notifications.groupByApp` | boolean | `false` | Group notifications by application | Notifications |
| `notifications.showAppIcon` | boolean | `true` | Show application icon in notifications | Notifications |

### Sounds
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `notifications.soundEnabled` | boolean | `true` | Enable notification sounds | Notifications |
| `notifications.soundFile` | string | `""` | Custom notification sound file path | Notifications |

### Do Not Disturb Scheduling
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `notifications.doNotDisturbSchedule` | boolean | `false` | Enable scheduled DND | Notifications |
| `notifications.dndStartTime` | string | `"22:00"` | DND start time (HH:mm, 24h format) | Notifications |
| `notifications.dndEndTime` | string | `"08:00"` | DND end time (HH:mm, 24h format) | Notifications |

### Existing (Enhanced UI)
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `notifications.enabled` | boolean | `true` | Enable notifications | Notifications |
| `notifications.doNotDisturb` | boolean | `false` | Do not disturb mode | Notifications |
| `notifications.position` | string | `"top-right"` | Notification position | Notifications |
| `notifications.duration` | integer | `6` | Notification duration in seconds (0 = until clicked) | Notifications |
| `notifications.historyLimit` | integer | `20` | Notification history length | Notifications |

---

## ⌨️ Hotkeys (Keyboard Shortcuts)

### Global
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `hotkeys.enabled` | boolean | `true` | Enable hotkeys globally | Hotkeys |

### Shell Actions
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `hotkeys.startMenu` | string | `"Meta"` | Open start menu | Hotkeys |
| `hotkeys.runDialog` | string | `"Alt+F2"` | Open run dialog | Hotkeys |
| `hotkeys.settings` | string | `"Meta+,Meta+S"` | Open settings | Hotkeys |
| `hotkeys.notificationCenter` | string | `"Meta+N"` | Open notification centre | Hotkeys |
| `hotkeys.quickSettings` | string | `"Meta+Q"` | Open quick settings | Hotkeys |

### System Actions
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `hotkeys.lockScreen` | string | `"Meta+L"` | Lock screen | Hotkeys |
| `hotkeys.logout` | string | `"Meta+Shift+Q"` | Log out | Hotkeys |
| `hotkeys.sleep` | string | `"Meta+Shift+S"` | Sleep system | Hotkeys |
| `hotkeys.reboot` | string | `"Meta+Shift+R"` | Reboot system | Hotkeys |
| `hotkeys.shutdown` | string | `"Meta+Shift+P"` | Shut down system | Hotkeys |

### Application Launchers
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `hotkeys.terminal` | string | `"Meta+T"` | Open terminal | Hotkeys |
| `hotkeys.fileManager` | string | `"Meta+E"` | Open file manager | Hotkeys |
| `hotkeys.switcher` | string | `"Alt+Tab"` | Application switcher | Hotkeys |

### Hotkey Format
- **Modifiers**: `Meta` (Windows key), `Ctrl`, `Alt`, `Shift`
- **Combination**: Use `+` (e.g., `Ctrl+Alt+T`)
- **Multiple Shortcuts**: Separate with commas (e.g., `Meta+T,Ctrl+Alt+T`)

---

## ⚡ Animations

### Global
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `animations.enabled` | boolean | `true` | Enable animations globally | Animations |
| `animations.speed` | number | `1.0` | Global animation speed multiplier (0.1-3.0) | Animations |

### Panel
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|

### Windows
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|

---

## 🔧 System & Performance

### Metrics
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `system.interval` | integer | `2000` | Sampling interval in milliseconds | System |
| `system.perCore` | boolean | `false` | Per-core CPU metrics | System |
| `system.battery` | boolean | `true` | Battery metrics | System |
| `system.thermal` | boolean | `true` | Thermal zone metrics | System |
| `system.disks` | boolean | `true` | Disk metrics | System |

### Caching
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|

### Clipboard
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `clipboard.historyEnabled` | boolean | `true` | Enable clipboard history | System |
| `clipboard.historySize` | integer | `100` | Clipboard history size (10-1000) | System |
| `screenshot.folder` | string | `""` | Folder for screenshots; empty: Screenshots in the pictures folder (`~` works) | System |
| `screenshot.fileName` | string | `"Screenshot_%Y%m%d-%H%M%S"` | date(1) pattern of a screenshot's file name | System |
| `screenshot.saveFile` | boolean | `true` | Keep screenshots as files (false: clipboard only, when wl-copy is installed) | System |
| `screenshot.clipboard` | boolean | `true` | Copy screenshots to the clipboard | System |
| `screenshot.cursor` | boolean | `false` | Draw the pointer in screenshots | System |
| `screenshot.delay` | integer | `0` | Seconds to wait before a screenshot (0-10) | System |
| `clipboard.persistHistory` | boolean | `true` | Persist clipboard history across restarts | System |

---

## 📋 Applications

### Startup
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `startup.apps` | string[] | `[]` | List of applications to launch on startup | Applications |
| `startup.delay` | integer | `0` | Startup delay in seconds (0-60) | Applications |

### Search
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `search.showCategories` | boolean | `true` | Show category chips in search | Applications |

### Existing (Enhanced UI)
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `apps.terminalCommand` | string | `"x-terminal-emulator -e"` | Terminal command | Applications |
| `apps.maxApps` | integer | `0` | Application catalogue cap (0 = unlimited) | Applications |

---

## 🔌 Plugin System

### Settings
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `plugins.onlineEnabled` | boolean | `true` | Enable online plugin discovery | Plugins |
| `plugins.showDescriptions` | boolean | `true` | Show plugin descriptions in list | Plugins |
| `plugins.autoRescan` | boolean | `true` | Auto-rescan plugins on change | Plugins |

### Existing (Enhanced UI)
| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `plugins.disabled` | string[] | `[]` | Plugin IDs to disable | Plugins |

---

## 🎯 Multi-Monitor

| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `multiMonitor.panelPositions` | object | `{}` | Per screen: `{"HDMI-A-1": "top"}`, an edge or `"none"` | Displays |

---

## 📊 Complete Settings Count

| Category | Count |
|----------|-------|
| Appearance & Theming | 25 |
| Panel & Dock | 15 |
| Desktop | 18 |
| Notifications | 15 |
| Hotkeys | 16 |
| Animations | 5 |
| System & Performance | 15 |
| Applications | 8 |
| Plugin System | 5 |
| Multi-Monitor | 3 |
| **Total** | **127** |

---

## 🔍 Finding Settings in the UI

### Settings Application Pages

1. **Appearance** (25 settings)
   - Theme selection and editor
   - Panel styling
   - Dock styling
   - Window style
   - Theme variants

2. **Panel & Dock** (15 settings)
   - Panel position, thickness, margins
   - Auto-hide settings
   - Clock and date settings
   - Panel layout
   - Dock layout
   - System tray
   - Pinned applications

3. **Desktop** (18 settings)
   - Wallpaper selection and modes
   - Wallpaper effects (blur, darken)
   - Desktop background
   - Icons (size, color, effects)
   - Grid settings
   - Context menu

4. **Start Menu** (Existing + 3 new)
   - Size settings
   - Search settings
   - Recent apps

5. **Applications** (8 settings)
   - Terminal command
   - Catalogue cap
   - Startup applications
   - Search settings

6. **System** (15 settings)
   - Metrics sampling
   - Per-core CPU, battery, thermal, disks
   - Caching (icons, search)
   - Clipboard history

7. **Notifications** (15 settings)
   - Popups enable/disable
   - Do not disturb
   - Position and duration
   - Appearance (theme, colors)
   - Sounds
   - DND scheduling

8. **Hotkeys** (16 settings)
   - Global enable/disable
   - Shell actions
   - System actions
   - Application launchers
   - Reference guide

9. **Animations** (5 settings)
   - Global controls
   - Panel animations
   - Window animations
   - Preview

10. **Plugins** (5 settings)
    - Auto-rescan
    - Show descriptions
    - Online plugins
    - Auto-update
    - Plugin list with enable/disable

11. **About** (Existing)
    - Version info
    - Configuration
    - Diagnostics

---

## 💡 Tips for Using Settings

### Command Line

You can also manage settings via command line using `newteller-ctl`:

```bash
# List all settings
newteller-ctl list

# Get a specific setting
newteller-ctl get panel.height

# Set a setting
newteller-ctl set panel.height 56

# Reset to default
newteller-ctl unset panel.height

# Validate configuration
newteller-ctl validate
```

### Configuration File

All settings are stored in `~/.config/newteller/shell.json`. You can edit this file directly, and changes will apply automatically (file is watched).

Example:
```json
{
  "panel.height": 56,
  "panel.position": "bottom",
  "dock.showLabels": true,
  "dock.labelPosition": "bottom",
  "hotkeys.terminal": "Ctrl+Alt+T",
  "desktop.wallpaperBlur": 10,
  "animations.enabled": false
}
```

---

## 🎓 Best Practices

1. **Start with defaults** - Most settings have sensible defaults
2. **Change one at a time** - Easier to identify what causes issues
3. **Use the GUI** - Settings application provides validation
4. **Check tooltips** - Many settings have helpful descriptions
5. **Reset if needed** - Use the reset button to restore defaults

---

## 🐛 Troubleshooting

### Setting Not Working?

1. Check if the setting is enabled in the parent category
2. Verify the value type matches what's expected
3. Check for typos in the setting key
4. Restart NewTeller (though most apply live)
5. Check the console output for errors

### Common Issues

- **Hotkeys not working**: Ensure `hotkeys.enabled` is true and no other application is using the same shortcut
- **Plugins not showing**: Ensure the plugin is in the correct directory and has a valid manifest.json
- **Wallpaper effects not working**: Ensure your system supports the required compositing features
- **Animation issues**: Try disabling animations or adjusting the speed

---

## 📚 See Also

- [CHANGES_SUMMARY.md](CHANGES_SUMMARY.md) - Comprehensive change overview
- [IMPLEMENTATION_COMPLETE.md](IMPLEMENTATION_COMPLETE.md) - Implementation details
- [plugins/README.md](plugins/README.md) - Plugin development guide

---

## Added later

| Key | Type | Default | Description | Page |
|-----|------|---------|-------------|------|
| `startmenu.layout` | string | `"list"` | Applications as a `list` or an icon `grid` | Start menu |
| `startmenu.gridIconSize` | integer | `40` | Icon size of the grid layout | Start menu |
| `startmenu.searchPosition` | string | `"top"` | Search field at the `top` or `bottom` | Start menu |
| `startmenu.showPowerButtons` | boolean | `true` | Lock, log out, sleep, restart and shut down in the footer | Start menu |
| `startmenu.material` | string | `"default"` | Surface; `default` follows the other popups | Start menu |
| `startmenu.opacityPercent` | integer | `0` | Surface opacity; 0 uses the material preset | Start menu |
| `startmenu.cornerRadius` | integer | `-1` | Corner radius; -1 follows the other popups | Start menu |
| `startmenu.highlight` | color | `""` | Colour of letters matching the search; empty uses the accent, `none` turns it off | Start menu |
| `desktop.iconTextColor` | color | `"#ffffff"` | Colour of the names under desktop icons | Desktop |
| `desktop.iconTextShadow` | boolean | `true` | Dark outline around those names | Desktop |
| `desktop.iconTextSize` | integer | `0` | Their size in points; 0 uses the theme | Desktop |
| `desktop.iconTextLines` | integer | `2` | Lines before a name is cut | Desktop |
| `hotkeys.enabled` | boolean | `true` | Off: only built-in shortcuts given below work | Hotkeys |
| `hotkeys.<action>` | string | `""` | New keys for a built-in shortcut (`Super+Shift+K`), `none` turns it off. Actions: startMenu, runDialog, terminal, fileManager, settings, quickSettings, notificationCenter, clipboard, lockScreen, closeWindow, maximize, minimize, fullscreen, alwaysOnTop, showDesktop, overview, windowMenu, nextLayout, screenshot, screenshotArea, screenshotWindow, screenRecording, forceQuit, logout, sleep, reboot, shutdown | Hotkeys |
| `power.sleepAction` | string | `"suspend"` | suspend, hibernate, hybrid-sleep or suspend-then-hibernate | Power & lock |
| `power.suspendAfter` | integer | `0` | Sleep after so many idle minutes; 0 = never | Power & lock |
| `notifications.groupByApp` | boolean | `true` | Group the notification centre by application | Notifications |
| `notifications.urgencyLevel` | integer | `2` | Lowest urgency shown while quiet: 1 normal, 2 critical, 3 nothing | Notifications |
| `theme.schedule` | string | `"off"` | Day/night themes: `off`, `times` or `nightLight` | Appearance |
| `theme.dayTheme` / `theme.nightTheme` | string | `"light"` / `"default"` | The two themes | Appearance |
| `theme.dayStart` / `theme.nightStart` | string | `"07:00"` / `"20:00"` | When they switch (`times`) | Appearance |
| `theme.scheduleApps` | boolean | `true` | Applications follow: light by day, dark at night | Appearance |
| `panel.style` | string | `"pill"` | `pill` (only the dock) or `bar` (a bar in panel.material) | Panel & dock |
| `panel.length` | integer | `100` | Share of the screen edge in percent; less floats the panel | Panel & dock |
| `panel.cornerRadius` | integer | `-1` | Corner radius of the bar style | Panel & dock |
| `windowRules.fullscreen` / `windowRules.minimized` | list | `[]` | Applications that open full screen / minimised | Windows |
| `windowRules.workspaces` | string | `""` | `firefox=2, thunderbird=3`: the desktop an application opens on | Windows |
| `workspaces.names` | string | `""` | `Work, Web, Chat` | Displays |
| `workspaces.indicator` | string | `"dots"` | Panel indicator: dots, numbers or names | Displays |
| `workspaces.osd` | boolean | `false` | The desktop's name on screen when switching | Displays |
| `osd.enabled` | boolean | `true` | Volume, brightness and media keys pill on screen | Keyboard |
| `osd.position` | string | `"bottom"` | Where the pill shows: bottom, center or top | Keyboard |
| `osd.timeout` | integer | `1500` | How long the pill stays, in milliseconds | Keyboard |
| `osd.screen` | string | `"active"` | The screen of the pill: active (pointer) or primary | Keyboard |
| `mouse.doubleClickInterval` | integer | `400` | Double-click speed in ms (shell and GTK) | Mouse & touchpad |
| `startmenu.showPinned` | boolean | `false` | The dock's pinned applications as a row above the recent ones | Start menu |
| `panel.startIcon / panel.startLabel / panel.showStartLabel` | string / string / boolean | "" / "" / true | The start button's icon (shell icon, theme icon or image), text, and whether the text shows | Panel & dock |
| `panel.autoHideMode` | string | `"always"` | With auto-hide: `always`, or `maximized` (only over maximised / full-screen windows) | Panel & dock |
| `panel.clockSize / panel.clockBold` | integer / boolean | 0 / false | Panel clock size in points (0 = theme) and weight | Panel & dock |
| `clock.firstWeekday` | integer | `-1` | First day of the calendar week: -1 the language's, 0 Sunday … 6 Saturday | Panel & dock |
| `clock.weekNumbers` | boolean | `false` | ISO week numbers in the calendars | Panel & dock |
| `dock.middleClick` | string | `"close"` | Middle click on a dock button: close, new, minimize, none | Panel & dock |
| `dock.hoverOpacity / dock.activeOpacity` | number | 0.15 / 0.30 | How strongly hovered / active dock buttons light up | Appearance |
| `quick.hidden` | list | `[]` | Rows taken out of quick settings (media, volume, brightness, network, bluetooth, dnd, nightLight, recorder, powerProfile, battery) | System |
| `notifications.width / notifications.maxVisible` | integer | 344 / 4 | Popup width and how many popups show at once | Notifications |
| `notifications.screen` | string | `"all"` | Popups on every screen, the `primary` one or the `active` one | Notifications |
| `theme.accent / theme.font / theme.fontSize / theme.radius` | color / string / integer / integer | "" / "" / 0 / -1 | Kept over any theme; empty / 0 / -1 use the theme's own | Appearance |
| `multiMonitor.panelPositions` | object | `{}` | Per screen: an edge or `none` | Displays |
| `desktop.sortBy / desktop.foldersFirst` | string / boolean | "name" / true | Order of auto-arranged icons: name, type, time, size | Desktop |
| `desktop.slideshowFolder` | string | `""` | Folder of the wallpaper slideshow; empty = Pictures | Desktop |
| `desktop.screenWallpapers` | object | `{}` | Per-monitor wallpapers: { screen name: path } | Desktop |
| `desktop.doubleClick` | string | `"none"` | Double click on the empty desktop: none, startMenu, overview, terminal, files, settings | Desktop |
| `windows.gaps` | integer | `0` | Pixels around and between snapped halves and quarters | Windows |
| `windows.dragModifier` | string | `"super"` | Key held to move (left button) or resize (right button) a window from inside it: super, alt or none | Windows |
| `decorations.doubleClick / decorations.middleClick` | string | "maximize" / "minimize" | Title bar clicks: maximize, minimize, alwaysOnTop, none (GTK header bars follow) | Windows |
| `windowRules.opacity` | string | `""` | `kitty=85`: window opacity in percent per application | Windows |
| `effects.animationStyle` | string | `"zoom"` | Windows open and close with a zoom, fade or slide | Animations |
| `effects.inactiveOpacity` | integer | `100` | Opacity of windows without the focus | Animations |
| `switcher.style` | string | `"thumbnails"` | Alt+Tab list: thumbnails, icons or list | Displays |
| `overview.cardSize` | integer | `100` | Overview preview size in percent | Displays |
| `lock.dim / lock.clockFormat` | integer / string | 0 / "" | Lock screen wallpaper veil (percent) and clock format (gtklock) | Power & lock |
| `keyboard.volumeStep / keyboard.brightnessStep` | integer | 5 | Percent per volume / brightness key press | Keyboard |
| `keyboard.indicatorStyle` | string | `"code"` | Layout indicator: code (EN), lower (en), name | Keyboard |
| `plugins.settings` | object | `{}` | Changes to plugins' manifest settings: { plugin id: { key: value } } | Plugins |
