//! Configuration storage: XDG paths, crash safe writes and the schema of
//! `~/.config/newteller/shell.json`.
//!
//! The file format is a **flat** JSON object with dotted keys, for example
//! `{"panel.height": 48}`. That shape is kept - QML bindings such as
//! `Settings.values["panel.height"]` depend on it - so every helper here works
//! on top level keys only and refuses to invent nesting.
//!
//! Writing goes through a temporary file in the same directory, an `fsync` of
//! the file, an atomic `rename` and an `fsync` of the directory. A crash or a
//! full disk can therefore never leave a half written `shell.json` behind, and
//! the previous content is copied to `shell.json.bak` first.

use std::path::{Path, PathBuf};

use serde::Serialize;
use serde_json::{Map, Value};

use crate::error::{Error, Result};
use crate::util;

/// Extension of the backup written before a config file is replaced.
pub const BACKUP_SUFFIX: &str = "bak";

/// How to write a file.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct WriteOptions {
    /// Copy the current content to `<path>.bak` before replacing it.
    pub backup: bool,
    /// `fsync` the file and its directory; costs a few milliseconds but makes
    /// the rename durable across a power cut.
    pub fsync: bool,
    /// Create missing parent directories.
    pub create_parents: bool,
    /// POSIX mode of the new file.
    pub mode: u32,
}

impl Default for WriteOptions {
    fn default() -> Self {
        WriteOptions {
            backup: true,
            fsync: true,
            create_parents: true,
            mode: 0o644,
        }
    }
}

impl WriteOptions {
    /// Fast variant: no backup, no `fsync`. Used for cache files.
    pub fn fast() -> WriteOptions {
        WriteOptions {
            backup: false,
            fsync: false,
            create_parents: true,
            mode: 0o644,
        }
    }
}

/// Writes `data` to `path` atomically.
pub fn write_atomic(path: &Path, data: &[u8], options: &WriteOptions) -> Result<()> {
    let parent = path.parent().map(Path::to_path_buf).unwrap_or_else(|| PathBuf::from("."));
    if options.create_parents && !parent.as_os_str().is_empty() {
        std::fs::create_dir_all(&parent)?;
    }
    if options.backup && path.exists() {
        // Best effort: a missing backup must not stop the write.
        let _ = std::fs::copy(path, backup_path(path));
    }

    let temp = temp_path(path);
    let written = (|| -> Result<()> {
        {
            use std::io::Write;
            let mut file = std::fs::OpenOptions::new()
                .write(true)
                .create(true)
                .truncate(true)
                .open(&temp)?;
            file.write_all(data)?;
            file.flush()?;
            if options.fsync {
                file.sync_all()?;
            }
        }
        set_mode(&temp, options.mode);
        std::fs::rename(&temp, path)?;
        if options.fsync {
            fsync_dir(&parent);
        }
        Ok(())
    })();

    if written.is_err() {
        let _ = std::fs::remove_file(&temp);
    }
    written
}

/// Serialises `value` with sorted keys and writes it atomically.
///
/// `serde_json` stores object keys in a `BTreeMap` unless the `preserve_order`
/// feature is enabled, so the output is stable and diff friendly.
pub fn write_json_atomic(path: &Path, value: &Value, options: &WriteOptions) -> Result<()> {
    let mut text = serde_json::to_vec_pretty(value)?;
    text.push(b'\n');
    write_atomic(path, &text, options)
}

/// Reads a whole file.
pub fn read(path: &Path) -> Result<Vec<u8>> {
    Ok(std::fs::read(path)?)
}

/// Reads and parses a JSON file.
pub fn read_json(path: &Path) -> Result<Value> {
    let bytes = read(path)?;
    Ok(serde_json::from_slice(&bytes)?)
}

/// Where a value came from when loading a configuration file.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum LoadSource {
    /// The file itself.
    File,
    /// The `.bak` copy, because the file was missing or corrupt.
    Backup,
    /// Neither; the caller should start from the defaults.
    Defaults,
}

/// Outcome of [`load_json_resilient`].
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Loaded {
    /// The parsed object, empty when nothing could be loaded.
    pub value: Value,
    /// Where it came from.
    pub source: LoadSource,
    /// What went wrong with the file, if anything.
    pub error: Option<String>,
}

/// Loads a JSON object, falling back to the backup when the file is missing or
/// corrupt.
///
/// A truncated `shell.json` used to mean "all settings are gone"; with the
/// backup written by [`write_atomic`] the previous state is recovered instead.
pub fn load_json_resilient(path: &Path) -> Loaded {
    match std::fs::read(path) {
        Ok(bytes) => match serde_json::from_slice::<Value>(&bytes) {
            Ok(value) if value.is_object() => Loaded {
                value,
                source: LoadSource::File,
                error: None,
            },
            Ok(_) => Loaded {
                value: Value::Object(Map::new()),
                source: LoadSource::Defaults,
                error: Some("the file does not contain a JSON object".to_string()),
            },
            Err(error) => recover_from_backup(path, Some(format!("invalid json: {error}"))),
        },
        Err(error) => {
            if error.kind() == std::io::ErrorKind::NotFound {
                recover_from_backup(path, None)
            } else {
                recover_from_backup(path, Some(format!("cannot read: {error}")))
            }
        }
    }
}

fn recover_from_backup(path: &Path, error: Option<String>) -> Loaded {
    let backup = backup_path(path);
    if backup.exists() {
        if let Ok(value) = read_json(&backup) {
            if value.is_object() {
                return Loaded {
                    value,
                    source: LoadSource::Backup,
                    error,
                };
            }
        }
    }
    Loaded {
        value: Value::Object(Map::new()),
        source: LoadSource::Defaults,
        error,
    }
}

/// The `.bak` path of a file.
pub fn backup_path(path: &Path) -> PathBuf {
    let mut name = path
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_else(|| "config".to_string());
    name.push('.');
    name.push_str(BACKUP_SUFFIX);
    match path.parent() {
        Some(parent) if !parent.as_os_str().is_empty() => parent.join(name),
        _ => PathBuf::from(name),
    }
}

/// A temporary path next to `path`, unique per process and call.
pub fn temp_path(path: &Path) -> PathBuf {
    let name = path
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_else(|| "file".to_string());
    let nanos = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.subsec_nanos())
        .unwrap_or(0);
    let unique = format!(".{name}.{}.{}.tmp", std::process::id(), nanos);
    match path.parent() {
        Some(parent) if !parent.as_os_str().is_empty() => parent.join(unique),
        _ => PathBuf::from(unique),
    }
}

#[cfg(unix)]
fn set_mode(path: &Path, mode: u32) {
    use std::os::unix::fs::PermissionsExt;
    let _ = std::fs::set_permissions(path, std::fs::Permissions::from_mode(mode));
}

#[cfg(not(unix))]
fn set_mode(_path: &Path, _mode: u32) {}

fn fsync_dir(dir: &Path) {
    if dir.as_os_str().is_empty() {
        return;
    }
    if let Ok(handle) = std::fs::File::open(dir) {
        let _ = handle.sync_all();
    }
}

// ---------------------------------------------------------------------------
// Flat dotted keys
// ---------------------------------------------------------------------------

/// Reads a top level key from a flat configuration object.
pub fn get<'a>(config: &'a Value, key: &str) -> Option<&'a Value> {
    config.as_object()?.get(key)
}

/// Reads a key with a fallback, converting JSON types to the requested one.
pub fn get_or(config: &Value, key: &str, fallback: Value) -> Value {
    match get(config, key) {
        Some(value) if !value.is_null() => coerce(key, value.clone()),
        _ => fallback,
    }
}

/// Writes a top level key. Returns true when the value changed.
pub fn set(config: &mut Value, key: &str, value: Value) -> bool {
    let map = match config {
        Value::Object(map) => map,
        other => {
            *other = Value::Object(Map::new());
            match other {
                Value::Object(map) => map,
                _ => return false,
            }
        }
    };
    let previous = map.get(key).cloned();
    if previous.as_ref() == Some(&value) {
        return false;
    }
    map.insert(key.to_string(), value);
    true
}

/// Removes a top level key, returning the removed value.
pub fn remove(config: &mut Value, key: &str) -> Option<Value> {
    config.as_object_mut()?.remove(key)
}

/// All keys, sorted.
pub fn keys(config: &Value) -> Vec<String> {
    let mut out: Vec<String> = config
        .as_object()
        .map(|map| map.keys().cloned().collect())
        .unwrap_or_default();
    out.sort();
    out
}

/// Merges `overrides` on top of `base`, in place.
pub fn merge(base: &mut Value, overrides: &Value) -> usize {
    let (Some(target), Some(source)) = (base.as_object_mut(), overrides.as_object()) else {
        return 0;
    };
    let mut count = 0;
    for (key, value) in source {
        target.insert(key.clone(), value.clone());
        count += 1;
    }
    count
}

/// Returns the defaults with `overrides` merged on top.
pub fn with_defaults(overrides: &Value) -> Value {
    let mut config = Value::Object(defaults());
    merge(&mut config, overrides);
    config
}

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

/// One known configuration key.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct KeySchema {
    /// Dotted key.
    pub key: &'static str,
    /// Expected JSON type: `int`, `number`, `bool`, `string`, `stringlist`,
    /// `object`.
    pub kind: &'static str,
    /// Default value, as JSON text.
    pub default_json: &'static str,
    /// One line of documentation, shown by `newteller-ctl schema`.
    pub doc: &'static str,
}

/// Every key NewTeller understands.
///
/// The shell reads this table to validate the file, to answer
/// `Settings.schema()` for a future settings GUI and to print `--help` style
/// documentation without duplicating it in three places.
pub const SCHEMA: &[KeySchema] = &[
    KeySchema { key: "panel.height", kind: "int", default_json: "48", doc: "Panel thickness in pixels." },
    KeySchema { key: "panel.position", kind: "string", default_json: "\"bottom\"", doc: "bottom, top, left or right screen edge." },
    KeySchema { key: "panel.margin", kind: "int", default_json: "4", doc: "Gap between the panel and the screen edge." },
    KeySchema { key: "panel.style", kind: "string", default_json: "\"pill\"", doc: "pill draws only the dock; bar draws a bar along the edge in panel.material." },
    KeySchema { key: "panel.length", kind: "int", default_json: "100", doc: "Share of the screen edge the panel takes, in percent; less makes a floating, centred panel." },
    KeySchema { key: "panel.cornerRadius", kind: "int", default_json: "-1", doc: "Corner radius of the bar style; -1 rounds only a floating bar." },
    KeySchema { key: "panel.autoHide", kind: "bool", default_json: "false", doc: "Hide the panel until the pointer touches its edge." },
    KeySchema { key: "panel.autoHideDelay", kind: "int", default_json: "400", doc: "Milliseconds before an auto-hidden panel slides away." },
    KeySchema { key: "panel.clockFormat", kind: "string", default_json: "\"HH:mm\"", doc: "Qt format string for the panel clock." },
    KeySchema { key: "panel.dateFormat", kind: "string", default_json: "\"ddd d MMM\"", doc: "Qt format string for the date next to the clock." },
    KeySchema { key: "panel.showDate", kind: "bool", default_json: "false", doc: "Show the date next to the clock." },
    KeySchema { key: "panel.showRunning", kind: "bool", default_json: "true", doc: "Mark running applications on their panel buttons." },
    KeySchema { key: "panel.indicators", kind: "bool", default_json: "true", doc: "Show the bell (notification centre) and the quick settings button at the end of the bar." },
    KeySchema { key: "panel.pinned", kind: "stringlist", default_json: "[\"org.gnome.Terminal.desktop\",\"firefox-esr.desktop\",\"org.gnome.Nautilus.desktop\"]", doc: "Desktop file ids pinned to the panel, in order." },
    KeySchema { key: "dock.showWindows", kind: "bool", default_json: "true", doc: "Show the running windows and applications in the dock." },
    KeySchema { key: "dock.group", kind: "bool", default_json: "true", doc: "Merge every window of one application into a single dock button." },
    KeySchema { key: "dock.iconSize", kind: "int", default_json: "28", doc: "Dock button icon size in pixels." },
    KeySchema { key: "dock.separateBackground", kind: "bool", default_json: "true", doc: "Show applications that only have minimised windows in a separate zone at the end of the dock." },
    KeySchema { key: "dock.shrinkIcons", kind: "bool", default_json: "true", doc: "Shrink dock icons automatically when many applications run at once." },
    KeySchema { key: "dock.thumbnails", kind: "bool", default_json: "true", doc: "Show a live preview of every window in the dock menu (X11; Wayland toplevels have no preview protocol)." },
    KeySchema { key: "dock.material", kind: "string", default_json: "\"translucent\"", doc: "Floating dock surface: none, opaque, translucent, frosted or liquid." },
    KeySchema { key: "dock.backgroundColor", kind: "string", default_json: "\"\"", doc: "Dock button background colour as hex; empty follows the theme." },
    KeySchema { key: "dock.backgroundOpacity", kind: "number", default_json: "0.5", doc: "Dock button surface opacity from 0 to 1." },
    KeySchema { key: "dock.hoverOpacity", kind: "number", default_json: "0.15", doc: "Dock button hover tint opacity." },
    KeySchema { key: "dock.activeOpacity", kind: "number", default_json: "0.30", doc: "Dock button active tint opacity." },
    KeySchema { key: "tray.enabled", kind: "bool", default_json: "true", doc: "Act as the session's StatusNotifierWatcher and show the tray icons of the applications that register with it." },
    KeySchema { key: "tray.menus", kind: "bool", default_json: "true", doc: "Render the DBusMenu of tray items instead of asking the application to pop up its own context menu." },
    KeySchema { key: "panel.material", kind: "string", default_json: "\"theme\"", doc: "Panel surface: theme, opaque, translucent, frosted or liquid." },
    KeySchema { key: "panel.customColors", kind: "bool", default_json: "false", doc: "Use custom panel background and foreground colours." },
    KeySchema { key: "panel.backgroundColor", kind: "string", default_json: "\"\"", doc: "Custom panel background colour as hex." },
    KeySchema { key: "panel.backgroundOpacity", kind: "number", default_json: "0.92", doc: "Panel background opacity when using custom theme colours." },
    KeySchema { key: "panel.foregroundColor", kind: "string", default_json: "\"\"", doc: "Custom panel foreground colour as hex." },
    KeySchema { key: "windows.style", kind: "string", default_json: "\"card\"", doc: "Frame style of the shell windows: card, glass, flat or outline." },
    KeySchema { key: "windows.material", kind: "string", default_json: "\"theme\"", doc: "Shell window surface: theme, opaque, translucent, frosted or liquid." },
    KeySchema { key: "windows.radius", kind: "int", default_json: "-1", doc: "Corner radius of the shell windows in pixels; -1 follows the theme." },
    KeySchema { key: "windows.opacityPercent", kind: "int", default_json: "0", doc: "Window background opacity in percent; 0 uses the selected material preset." },
    KeySchema { key: "windows.borderWidth", kind: "int", default_json: "-1", doc: "Window border width in pixels; -1 follows the theme." },
    KeySchema { key: "windows.borderColor", kind: "string", default_json: "\"\"", doc: "Window border colour as hex; empty follows the theme." },
    KeySchema { key: "windows.shadow", kind: "int", default_json: "1", doc: "Window shadow strength, 0 (none) to 3 (strong)." },
    KeySchema { key: "notifications.enabled", kind: "bool", default_json: "true", doc: "Run the built-in notification daemon (org.freedesktop.Notifications and the newteller-ctl socket)." },
    KeySchema { key: "notifications.doNotDisturb", kind: "bool", default_json: "false", doc: "Keep notifications out of the popups (critical ones still show)." },
    KeySchema { key: "notifications.position", kind: "string", default_json: "\"top-right\"", doc: "Popup corner: top-left, top-center, top-right, bottom-left, bottom-center or bottom-right." },
    KeySchema { key: "notifications.duration", kind: "int", default_json: "6", doc: "Seconds a popup stays up; 0 keeps it until it is clicked." },
    KeySchema { key: "notifications.historyLimit", kind: "int", default_json: "20", doc: "How many recent notifications the daemon keeps." },
    KeySchema { key: "notifications.theme", kind: "string", default_json: "\"system\"", doc: "Notification colours: system, light, dark or custom." },
    KeySchema { key: "notifications.material", kind: "string", default_json: "\"theme\"", doc: "Notification surface: theme, opaque, translucent, frosted or liquid." },
    KeySchema { key: "notifications.opacityPercent", kind: "int", default_json: "0", doc: "Notification opacity in percent; 0 uses the material preset." },
    KeySchema { key: "notifications.cornerRadius", kind: "int", default_json: "-1", doc: "Notification corner radius in pixels; -1 follows the shell window radius." },
    KeySchema { key: "notifications.customBackground", kind: "string", default_json: "\"\"", doc: "Custom notification background colour as hex." },
    KeySchema { key: "notifications.customForeground", kind: "string", default_json: "\"\"", doc: "Custom notification foreground colour as hex." },
    KeySchema { key: "startmenu.width", kind: "int", default_json: "440", doc: "Start menu width." },
    KeySchema { key: "startmenu.height", kind: "int", default_json: "580", doc: "Start menu height." },
    KeySchema { key: "startmenu.limit", kind: "int", default_json: "200", doc: "Maximum number of search results." },
    KeySchema { key: "startmenu.fuzzy", kind: "bool", default_json: "true", doc: "Allow typos and scattered characters in the search." },
    KeySchema { key: "startmenu.highlight", kind: "string", default_json: "\"\"", doc: "Colour of the matched characters in the result list as hex; empty uses the theme accent, \"none\" turns the highlight off." },
    KeySchema { key: "startmenu.showRecent", kind: "bool", default_json: "true", doc: "Show recently used applications before typing." },
    KeySchema { key: "theme.name", kind: "string", default_json: "\"default\"", doc: "Theme name; a file of that name in ~/.config/newteller/themes overrides the built-in one." },
    KeySchema { key: "theme.autoReload", kind: "bool", default_json: "true", doc: "Reload the theme when its file changes." },
    KeySchema { key: "desktop.wallpaper", kind: "string", default_json: "\"\"", doc: "Absolute path or URL of the wallpaper; empty keeps the background colour." },
    KeySchema { key: "desktop.showIcons", kind: "bool", default_json: "true", doc: "Show the contents of ~/Desktop on the desktop." },
    KeySchema { key: "desktop.iconSize", kind: "int", default_json: "40", doc: "Desktop icon size in pixels." },
    KeySchema { key: "desktop.cellWidth", kind: "int", default_json: "96", doc: "Horizontal grid step of the desktop icons." },
    KeySchema { key: "desktop.cellHeight", kind: "int", default_json: "100", doc: "Vertical grid step of the desktop icons." },
    KeySchema { key: "desktop.contextMenu", kind: "bool", default_json: "true", doc: "Enable the right-click menu on the desktop." },
    KeySchema { key: "desktop.iconPositions", kind: "object", default_json: "{}", doc: "Saved desktop icon positions, keyed by file name (written by drag and drop)." },
    KeySchema { key: "icons.path", kind: "string", default_json: "\"\"", doc: "Extra IconKit directory overriding the bundled icons; empty uses the bundled sets." },
    KeySchema { key: "icons.set", kind: "string", default_json: "\"lucide\"", doc: "Icon set folder inside an IconKit installation, e.g. lucide or tabler/outline." },
    KeySchema { key: "icons.cache", kind: "bool", default_json: "true", doc: "Cache icon lookups instead of touching the disk on every binding." },
    KeySchema { key: "plugins.disabled", kind: "stringlist", default_json: "[]", doc: "Plugin ids that are turned off." },
    KeySchema { key: "plugins.autoRescan", kind: "bool", default_json: "true", doc: "Rescan the plugin directories when they change." },
    KeySchema { key: "apps.recent", kind: "stringlist", default_json: "[]", doc: "Most recently launched application ids, newest first." },
    KeySchema { key: "apps.recentLimit", kind: "int", default_json: "8", doc: "How many recent applications to show." },
    KeySchema { key: "apps.usage", kind: "object", default_json: "{}", doc: "Launch counters per application id; they bias the search ranking." },
    KeySchema { key: "apps.terminalCommand", kind: "string", default_json: "\"x-terminal-emulator -e\"", doc: "Command used to start terminal applications; %s is replaced by the program." },
    KeySchema { key: "system.interval", kind: "int", default_json: "2000", doc: "Milliseconds between two system metric samples." },
    KeySchema { key: "system.perCore", kind: "bool", default_json: "true", doc: "Sample the utilisation of every core." },
    KeySchema { key: "system.battery", kind: "bool", default_json: "true", doc: "Sample the battery." },
    KeySchema { key: "system.thermal", kind: "bool", default_json: "false", doc: "Sample the thermal zones." },
    KeySchema { key: "system.disks", kind: "bool", default_json: "false", doc: "Sample every real filesystem, the most expensive part of a snapshot." },
    KeySchema { key: "watch.debounce", kind: "int", default_json: "250", doc: "Milliseconds a watched file must stay quiet before the shell reloads it." },
    KeySchema { key: "run.history", kind: "stringlist", default_json: "[]", doc: "Commands previously typed into the run dialog." },
    KeySchema { key: "run.historyLimit", kind: "int", default_json: "25", doc: "How many commands the run dialog remembers." },
    // Keys of the shell's own settings pages (src/settings.cpp has the same
    // defaults); here so validation and newteller-ctl know them.
    KeySchema { key: "panel.startIcon", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "panel.startLabel", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "panel.showStartLabel", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "panel.showStartButton", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "panel.showClock", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "panel.autoHideMode", kind: "string", default_json: "\"always\"", doc: "With auto-hide: always, or maximized (only over maximised / full-screen windows)" },
    KeySchema { key: "panel.clockSize", kind: "int", default_json: "0", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "panel.clockBold", kind: "bool", default_json: "false", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "clock.firstWeekday", kind: "int", default_json: "-1", doc: "First day of the calendar week: -1 the language's, 0 Sunday … 6 Saturday" },
    KeySchema { key: "clock.weekNumbers", kind: "bool", default_json: "false", doc: "ISO week numbers in the calendars" },
    KeySchema { key: "quick.hidden", kind: "stringlist", default_json: "[]", doc: "Rows taken out of quick settings (media, volume, brightness, network, bluetooth, dnd, nightLight, recorder, powerProfile, battery)" },
    KeySchema { key: "notifications.dndUntil", kind: "int", default_json: "0", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "notifications.quietInFullscreen", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "notifications.width", kind: "int", default_json: "344", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "notifications.maxVisible", kind: "int", default_json: "4", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "notifications.screen", kind: "string", default_json: "\"all\"", doc: "Popups on every screen, the primary one or the active one" },
    KeySchema { key: "startmenu.showPinned", kind: "bool", default_json: "false", doc: "The dock's pinned applications as a row above the recent ones" },
    KeySchema { key: "desktop.defaultWallpaper", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "desktop.gridPositions", kind: "object", default_json: "{}", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "desktop.doubleClick", kind: "string", default_json: "\"none\"", doc: "Double click on the empty desktop: none, startMenu, overview, terminal, files, settings" },
    KeySchema { key: "plugins.settings", kind: "object", default_json: "{}", doc: "Changes to plugins' manifest settings: { plugin id: { key: value } }" },
    KeySchema { key: "apps.maxApps", kind: "int", default_json: "0", doc: "Application catalogue cap (0 = unlimited)" },
    KeySchema { key: "dock.showLabels", kind: "bool", default_json: "false", doc: "Show application names below dock icons" },
    KeySchema { key: "dock.labelPosition", kind: "string", default_json: "\"bottom\"", doc: "Label position: bottom, right, top, left" },
    KeySchema { key: "dock.iconColorMode", kind: "string", default_json: "\"theme\"", doc: "Icon color mode: theme, custom, original" },
    KeySchema { key: "dock.customIconColor", kind: "string", default_json: "\"#ffffff\"", doc: "Custom icon color (hex)" },
    KeySchema { key: "startmenu.backgroundColor", kind: "string", default_json: "\"\"", doc: "Custom start menu background color" },
    KeySchema { key: "startmenu.material", kind: "string", default_json: "\"default\"", doc: "Surface; default follows the other popups" },
    KeySchema { key: "startmenu.opacityPercent", kind: "int", default_json: "0", doc: "Surface opacity; 0 uses the material preset" },
    KeySchema { key: "startmenu.cornerRadius", kind: "int", default_json: "-1", doc: "Corner radius; -1 follows the other popups" },
    KeySchema { key: "startmenu.layout", kind: "string", default_json: "\"list\"", doc: "Applications as a list or an icon grid" },
    KeySchema { key: "startmenu.gridIconSize", kind: "int", default_json: "40", doc: "Icon size of the grid layout" },
    KeySchema { key: "startmenu.searchPosition", kind: "string", default_json: "\"top\"", doc: "Search field at the top or bottom" },
    KeySchema { key: "startmenu.position", kind: "string", default_json: "\"button\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "startmenu.showPowerButtons", kind: "bool", default_json: "true", doc: "Lock, log out, sleep, restart and shut down in the footer" },
    KeySchema { key: "startmenu.foregroundColor", kind: "string", default_json: "\"\"", doc: "Custom start menu text color" },
    KeySchema { key: "startmenu.accentColor", kind: "string", default_json: "\"\"", doc: "Custom start menu accent color" },
    KeySchema { key: "desktop.backgroundColor", kind: "string", default_json: "\"\"", doc: "Custom desktop background color" },
    KeySchema { key: "desktop.backgroundGradient", kind: "bool", default_json: "true", doc: "Use gradient background when no wallpaper" },
    KeySchema { key: "desktop.iconColor", kind: "string", default_json: "\"#ffffff\"", doc: "Custom icon color (hex)" },
    KeySchema { key: "desktop.iconShadow", kind: "bool", default_json: "true", doc: "Enable icon drop shadows" },
    KeySchema { key: "desktop.iconGlow", kind: "bool", default_json: "false", doc: "Enable icon glow effects" },
    KeySchema { key: "theme.schedule", kind: "string", default_json: "\"off\"", doc: "Day/night themes: off, times or nightLight" },
    KeySchema { key: "theme.dayTheme", kind: "string", default_json: "\"light\"", doc: "The two themes" },
    KeySchema { key: "theme.nightTheme", kind: "string", default_json: "\"default\"", doc: "The two themes" },
    KeySchema { key: "theme.dayStart", kind: "string", default_json: "\"07:00\"", doc: "When they switch (times)" },
    KeySchema { key: "theme.nightStart", kind: "string", default_json: "\"20:00\"", doc: "When they switch (times)" },
    KeySchema { key: "theme.scheduleApps", kind: "bool", default_json: "true", doc: "Applications follow: light by day, dark at night" },
    KeySchema { key: "theme.accent", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "theme.font", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "theme.fontSize", kind: "int", default_json: "0", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "theme.radius", kind: "int", default_json: "-1", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "panel.spacing", kind: "int", default_json: "8", doc: "General panel spacing (0-64px)" },
    KeySchema { key: "panel.leftSpacing", kind: "int", default_json: "8", doc: "Left edge spacing (0-64px)" },
    KeySchema { key: "panel.rightSpacing", kind: "int", default_json: "8", doc: "Right edge spacing (0-64px)" },
    KeySchema { key: "panel.centerDock", kind: "bool", default_json: "false", doc: "Center dock buttons in panel" },
    KeySchema { key: "dock.position", kind: "string", default_json: "\"left\"", doc: "Dock position in panel: left, center, right" },
    KeySchema { key: "dock.multiRow", kind: "bool", default_json: "false", doc: "Enable multi-row dock" },
    KeySchema { key: "dock.rows", kind: "int", default_json: "1", doc: "Number of dock rows (1-3)" },
    KeySchema { key: "dock.scrollable", kind: "bool", default_json: "true", doc: "Make dock scrollable when overflowed" },
    KeySchema { key: "dock.iconSpacing", kind: "int", default_json: "4", doc: "Spacing between dock icons (0-24px)" },
    KeySchema { key: "dock.middleClick", kind: "string", default_json: "\"close\"", doc: "Middle click on a dock button: close, new, minimize, none" },
    KeySchema { key: "panel.zoneSpacing", kind: "int", default_json: "8", doc: "Spacing between panel zones (0-24px)" },
    KeySchema { key: "desktop.wallpaperMode", kind: "string", default_json: "\"fill\"", doc: "Wallpaper display mode: fill, fit, stretch, tile, center" },
    KeySchema { key: "desktop.wallpaperBlur", kind: "int", default_json: "0", doc: "Apply blur effect to wallpaper (0-50px)" },
    KeySchema { key: "desktop.wallpaperDarken", kind: "int", default_json: "0", doc: "Darken wallpaper (0-100%)" },
    KeySchema { key: "desktop.perMonitorWallpaper", kind: "bool", default_json: "false", doc: "Per-monitor wallpapers (experimental)" },
    KeySchema { key: "desktop.wallpaperSlideshow", kind: "bool", default_json: "false", doc: "Enable wallpaper slideshow" },
    KeySchema { key: "desktop.wallpaperSlideshowInterval", kind: "int", default_json: "60", doc: "Slideshow interval in seconds (10-3600)" },
    KeySchema { key: "desktop.slideshowFolder", kind: "string", default_json: "\"\"", doc: "Folder of the wallpaper slideshow; empty = Pictures" },
    KeySchema { key: "desktop.screenWallpapers", kind: "object", default_json: "{}", doc: "Per-monitor wallpapers: { screen name: path }" },
    KeySchema { key: "desktop.autoArrangeIcons", kind: "bool", default_json: "true", doc: "Auto-arrange desktop icons" },
    KeySchema { key: "desktop.sortBy", kind: "string", default_json: "\"name\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "desktop.foldersFirst", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "desktop.iconTextColor", kind: "string", default_json: "\"#ffffff\"", doc: "Colour of the names under desktop icons" },
    KeySchema { key: "desktop.iconTextShadow", kind: "bool", default_json: "true", doc: "Dark outline around those names" },
    KeySchema { key: "desktop.iconTextSize", kind: "int", default_json: "0", doc: "Their size in points; 0 uses the theme" },
    KeySchema { key: "desktop.iconTextLines", kind: "int", default_json: "2", doc: "Lines before a name is cut" },
    KeySchema { key: "desktop.showHiddenFiles", kind: "bool", default_json: "false", doc: "Show hidden files on desktop" },
    KeySchema { key: "desktop.widgetPositions", kind: "object", default_json: "{}", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "notifications.groupByApp", kind: "bool", default_json: "true", doc: "Group notifications by application" },
    KeySchema { key: "notifications.mutedApps", kind: "stringlist", default_json: "[]", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "notifications.showAppIcon", kind: "bool", default_json: "true", doc: "Show application icon in notifications" },
    KeySchema { key: "drives.notify", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "weather.location", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "weather.units", kind: "string", default_json: "\"metric\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "weather.showForecast", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "stickyNotes.notes", kind: "list", default_json: "[]", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "notifications.soundEnabled", kind: "bool", default_json: "true", doc: "Enable notification sounds" },
    KeySchema { key: "notifications.soundFile", kind: "string", default_json: "\"\"", doc: "Custom notification sound file path" },
    KeySchema { key: "notifications.doNotDisturbSchedule", kind: "bool", default_json: "false", doc: "Enable scheduled DND" },
    KeySchema { key: "notifications.dndStartTime", kind: "string", default_json: "\"22:00\"", doc: "DND start time (HH:mm, 24h format)" },
    KeySchema { key: "notifications.dndEndTime", kind: "string", default_json: "\"08:00\"", doc: "DND end time (HH:mm, 24h format)" },
    KeySchema { key: "notifications.urgencyLevel", kind: "int", default_json: "2", doc: "Lowest urgency shown while quiet: 1 normal, 2 critical, 3 nothing" },
    KeySchema { key: "hotkeys.enabled", kind: "bool", default_json: "true", doc: "Enable hotkeys globally" },
    KeySchema { key: "hotkeys.startMenu", kind: "string", default_json: "\"\"", doc: "Open start menu" },
    KeySchema { key: "hotkeys.runDialog", kind: "string", default_json: "\"\"", doc: "Open run dialog" },
    KeySchema { key: "hotkeys.terminal", kind: "string", default_json: "\"\"", doc: "Open terminal" },
    KeySchema { key: "hotkeys.fileManager", kind: "string", default_json: "\"\"", doc: "Open file manager" },
    KeySchema { key: "hotkeys.settings", kind: "string", default_json: "\"\"", doc: "Open settings" },
    KeySchema { key: "hotkeys.quickSettings", kind: "string", default_json: "\"\"", doc: "Open quick settings" },
    KeySchema { key: "hotkeys.notificationCenter", kind: "string", default_json: "\"\"", doc: "Open notification centre" },
    KeySchema { key: "hotkeys.clipboard", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotkeys.lockScreen", kind: "string", default_json: "\"\"", doc: "Lock screen" },
    KeySchema { key: "hotkeys.closeWindow", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotkeys.maximize", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotkeys.fullscreen", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotkeys.showDesktop", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotkeys.overview", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotkeys.screenRecording", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotkeys.forceQuit", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotkeys.screenshot", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotkeys.screenshotArea", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotkeys.screenshotWindow", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotkeys.nextLayout", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotkeys.windowMenu", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotkeys.minimize", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotkeys.alwaysOnTop", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotkeys.logout", kind: "string", default_json: "\"\"", doc: "Log out" },
    KeySchema { key: "hotkeys.sleep", kind: "string", default_json: "\"\"", doc: "Sleep system" },
    KeySchema { key: "hotkeys.reboot", kind: "string", default_json: "\"\"", doc: "Reboot system" },
    KeySchema { key: "hotkeys.shutdown", kind: "string", default_json: "\"\"", doc: "Shut down system" },
    KeySchema { key: "startup.apps", kind: "stringlist", default_json: "[]", doc: "List of applications to launch on startup" },
    KeySchema { key: "startup.delay", kind: "int", default_json: "0", doc: "Startup delay in seconds (0-60)" },
    KeySchema { key: "windowRules.alwaysOnTop", kind: "stringlist", default_json: "[]", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "windowRules.maximized", kind: "stringlist", default_json: "[]", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "windowRules.fullscreen", kind: "stringlist", default_json: "[]", doc: "Applications that open full screen / minimised" },
    KeySchema { key: "windowRules.minimized", kind: "stringlist", default_json: "[]", doc: "Applications that open full screen / minimised" },
    KeySchema { key: "windowRules.workspaces", kind: "string", default_json: "\"\"", doc: "firefox=2, thunderbird=3: the desktop an application opens on" },
    KeySchema { key: "windowRules.opacity", kind: "string", default_json: "\"\"", doc: "kitty=85: window opacity in percent per application" },
    KeySchema { key: "startmenu.recentFiles", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotCorners.topLeft", kind: "string", default_json: "\"none\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotCorners.topRight", kind: "string", default_json: "\"none\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotCorners.bottomLeft", kind: "string", default_json: "\"none\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotCorners.bottomRight", kind: "string", default_json: "\"none\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "hotCorners.delay", kind: "int", default_json: "0", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "recorder.audio", kind: "bool", default_json: "false", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "screenshot.folder", kind: "string", default_json: "\"\"", doc: "Folder for screenshots; empty: Screenshots in the pictures folder" },
    KeySchema { key: "screenshot.fileName", kind: "string", default_json: "\"Screenshot_%Y%m%d-%H%M%S\"", doc: "date(1) pattern of a screenshot's file name" },
    KeySchema { key: "screenshot.saveFile", kind: "bool", default_json: "true", doc: "Keep screenshots as files (false: clipboard only)" },
    KeySchema { key: "screenshot.clipboard", kind: "bool", default_json: "true", doc: "Copy screenshots to the clipboard" },
    KeySchema { key: "screenshot.cursor", kind: "bool", default_json: "false", doc: "Draw the pointer in screenshots" },
    KeySchema { key: "screenshot.delay", kind: "int", default_json: "0", doc: "Seconds to wait before a screenshot (0-10)" },
    KeySchema { key: "power.sleepAction", kind: "string", default_json: "\"suspend\"", doc: "suspend, hibernate, hybrid-sleep or suspend-then-hibernate" },
    KeySchema { key: "power.lockOnSleep", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "power.batterySaver", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "power.lockAfter", kind: "int", default_json: "10", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "power.screenOffAfter", kind: "int", default_json: "10", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "power.suspendAfter", kind: "int", default_json: "0", doc: "Sleep after so many idle minutes; 0 = never" },
    KeySchema { key: "nightLight.enabled", kind: "bool", default_json: "false", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "nightLight.temperature", kind: "int", default_json: "4000", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "nightLight.schedule", kind: "string", default_json: "\"custom\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "nightLight.from", kind: "string", default_json: "\"20:00\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "nightLight.to", kind: "string", default_json: "\"07:00\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "keyboard.layouts", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "keyboard.variants", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "keyboard.switchKeys", kind: "string", default_json: "\"grp:alt_shift_toggle\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "keyboard.options", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "keyboard.repeatRate", kind: "int", default_json: "25", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "keyboard.repeatDelay", kind: "int", default_json: "600", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "keyboard.numlock", kind: "bool", default_json: "false", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "keyboard.layoutPerWindow", kind: "bool", default_json: "false", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "sounds.bell", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "keyboard.lockKeysOsd", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "keyboard.layoutOsd", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "keyboard.volumeStep", kind: "int", default_json: "5", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "keyboard.brightnessStep", kind: "int", default_json: "5", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "keyboard.showIndicator", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "keyboard.indicatorStyle", kind: "string", default_json: "\"code\"", doc: "Layout indicator: code (EN), lower (en), name" },
    KeySchema { key: "touchpad.tapToClick", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "touchpad.naturalScroll", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "touchpad.disableWhileTyping", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "touchpad.gestures", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "security.builtinAgent", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "touchpad.speed", kind: "number", default_json: "0.0", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "mouse.naturalScroll", kind: "bool", default_json: "false", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "mouse.speed", kind: "number", default_json: "0.0", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "mouse.leftHanded", kind: "bool", default_json: "false", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "mouse.doubleClickInterval", kind: "int", default_json: "400", doc: "Double-click speed in ms (shell and GTK)" },
    KeySchema { key: "cursor.theme", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "cursor.size", kind: "int", default_json: "24", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "apps.colorScheme", kind: "string", default_json: "\"dark\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "shell.language", kind: "string", default_json: "\"auto\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "workspaces.count", kind: "int", default_json: "4", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "workspaces.showOnPanel", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "workspaces.names", kind: "string", default_json: "\"\"", doc: "Work, Web, Chat" },
    KeySchema { key: "workspaces.indicator", kind: "string", default_json: "\"dots\"", doc: "Panel indicator: dots, numbers or names" },
    KeySchema { key: "workspaces.osd", kind: "bool", default_json: "false", doc: "The desktop's name on screen when switching" },
    KeySchema { key: "osd.enabled", kind: "bool", default_json: "true", doc: "Volume, brightness and media keys pill on screen" },
    KeySchema { key: "osd.position", kind: "string", default_json: "\"bottom\"", doc: "Where the pill shows: bottom, center or top" },
    KeySchema { key: "osd.timeout", kind: "int", default_json: "1500", doc: "How long the pill stays, in milliseconds" },
    KeySchema { key: "osd.screen", kind: "string", default_json: "\"active\"", doc: "The screen of the pill: active (pointer) or primary" },
    KeySchema { key: "switcher.showOverlay", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "switcher.allWorkspaces", kind: "bool", default_json: "false", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "switcher.style", kind: "string", default_json: "\"thumbnails\"", doc: "Alt+Tab list: thumbnails, icons or list" },
    KeySchema { key: "overview.cardSize", kind: "int", default_json: "100", doc: "Overview preview size in percent" },
    KeySchema { key: "workspaces.wrap", kind: "bool", default_json: "false", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.server", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.followTheme", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.titleHeight", kind: "int", default_json: "30", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.border", kind: "int", default_json: "1", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.buttonsLeft", kind: "bool", default_json: "false", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.titleCentered", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.showMinimize", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.showMaximize", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.doubleClick", kind: "string", default_json: "\"maximize\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.middleClick", kind: "string", default_json: "\"minimize\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.font", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.activeBackground", kind: "string", default_json: "\"#262626\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.activeForeground", kind: "string", default_json: "\"#f2f2f5\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.inactiveBackground", kind: "string", default_json: "\"#1a1a1a\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.inactiveForeground", kind: "string", default_json: "\"#8e8e93\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "decorations.closeHover", kind: "string", default_json: "\"#e5484d\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "windows.focusFollowsMouse", kind: "bool", default_json: "false", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "windows.placement", kind: "string", default_json: "\"cascade\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "windows.dragModifier", kind: "string", default_json: "\"super\"", doc: "Key held to move or resize a window from inside it: super, alt or none" },
    KeySchema { key: "windows.snapping", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "windows.gaps", kind: "int", default_json: "0", doc: "Pixels around and between snapped halves and quarters" },
    KeySchema { key: "shortcuts.custom", kind: "list", default_json: "[]", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "apps.gtkTheme", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "apps.iconTheme", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "apps.font", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "apps.textScale", kind: "number", default_json: "1.0", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "effects.enabled", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "effects.cornerRadius", kind: "int", default_json: "10", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "effects.blur", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "effects.blurStrength", kind: "int", default_json: "2", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "effects.animations", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "effects.animationSpeed", kind: "number", default_json: "1.0", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "effects.animationStyle", kind: "string", default_json: "\"zoom\"", doc: "Windows open and close with a zoom, fade or slide" },
    KeySchema { key: "effects.inactiveOpacity", kind: "int", default_json: "100", doc: "Opacity of windows without the focus" },
    KeySchema { key: "lock.color", kind: "string", default_json: "\"#1e1e2e\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "lock.useWallpaper", kind: "bool", default_json: "true", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "lock.dim", kind: "int", default_json: "0", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "lock.clockFormat", kind: "string", default_json: "\"\"", doc: "Shell setting, changed in the settings application." },
    KeySchema { key: "clipboard.historyEnabled", kind: "bool", default_json: "true", doc: "Enable clipboard history" },
    KeySchema { key: "clipboard.historySize", kind: "int", default_json: "100", doc: "Clipboard history size (10-1000)" },
    KeySchema { key: "clipboard.persistHistory", kind: "bool", default_json: "false", doc: "Persist clipboard history across restarts" },
    KeySchema { key: "animations.enabled", kind: "bool", default_json: "true", doc: "Enable animations globally" },
    KeySchema { key: "animations.speed", kind: "number", default_json: "1.0", doc: "Global animation speed multiplier (0.1-3.0)" },
    KeySchema { key: "search.showCategories", kind: "bool", default_json: "true", doc: "Show category chips in search" },
    KeySchema { key: "multiMonitor.panelPositions", kind: "object", default_json: "{}", doc: "Per screen: {'HDMI-A-1': 'top'}, an edge or 'none'" },
    KeySchema { key: "plugins.onlineEnabled", kind: "bool", default_json: "true", doc: "Enable online plugin discovery" },
    KeySchema { key: "plugins.showDescriptions", kind: "bool", default_json: "true", doc: "Show plugin descriptions in list" },
];

/// A schema entry with its default already parsed, for JSON consumers.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SchemaEntry {
    /// Dotted key.
    pub key: String,
    /// Expected type.
    pub kind: String,
    /// Default value.
    pub default: Value,
    /// Documentation.
    pub doc: String,
}

/// One problem found in a configuration file.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Issue {
    /// The key the problem is about, empty for file level problems.
    pub key: String,
    /// `error` or `warning`.
    pub level: String,
    /// What is wrong.
    pub message: String,
}

/// The schema as JSON serialisable entries.
pub fn schema() -> Vec<SchemaEntry> {
    SCHEMA
        .iter()
        .map(|entry| SchemaEntry {
            key: entry.key.to_string(),
            kind: entry.kind.to_string(),
            default: parse_default(entry),
            doc: entry.doc.to_string(),
        })
        .collect()
}

/// The default configuration object.
pub fn defaults() -> Map<String, Value> {
    let mut map = Map::new();
    for entry in SCHEMA {
        map.insert(entry.key.to_string(), parse_default(entry));
    }
    map
}

fn parse_default(entry: &KeySchema) -> Value {
    serde_json::from_str(entry.default_json).unwrap_or(Value::Null)
}

/// Looks a key up in the schema.
pub fn schema_for(key: &str) -> Option<&'static KeySchema> {
    SCHEMA.iter().find(|entry| entry.key == key)
}

/// Coerces a value to the type the schema expects.
///
/// QML and hand edited files disagree about numbers: a spin box writes `48`,
/// a text editor writes `"48"`. Accepting both is cheaper than explaining the
/// difference in the documentation.
pub fn coerce(key: &str, value: Value) -> Value {
    let Some(entry) = schema_for(key) else {
        return value;
    };
    match entry.kind {
        "int" => match &value {
            Value::Number(_) => Value::Number(
                serde_json::Number::from(value.as_i64().unwrap_or_else(|| value.as_f64().unwrap_or(0.0) as i64)),
            ),
            Value::String(text) => match text.trim().parse::<i64>() {
                Ok(number) => Value::from(number),
                Err(_) => match text.trim().parse::<f64>() {
                    Ok(number) => Value::from(number as i64),
                    Err(_) => value,
                },
            },
            Value::Bool(flag) => Value::from(i64::from(*flag)),
            _ => value,
        },
        "number" => match &value {
            Value::Number(_) => value,
            Value::String(text) => text
                .trim()
                .parse::<f64>()
                .map(Value::from)
                .unwrap_or(value),
            _ => value,
        },
        "bool" => match &value {
            Value::Bool(_) => value,
            Value::String(text) => match text.trim().to_ascii_lowercase().as_str() {
                "true" | "1" | "yes" | "on" => Value::from(true),
                "false" | "0" | "no" | "off" => Value::from(false),
                _ => value,
            },
            Value::Number(number) => Value::from(number.as_f64().unwrap_or(0.0) != 0.0),
            _ => value,
        },
        "string" => match &value {
            Value::String(_) => value,
            Value::Number(_) | Value::Bool(_) => Value::from(value.to_string()),
            _ => value,
        },
        "stringlist" => match value {
            Value::Array(items) => Value::Array(
                items
                    .into_iter()
                    .map(|item| match item {
                        Value::String(text) => Value::String(text),
                        other => Value::String(other.to_string()),
                    })
                    .collect(),
            ),
            Value::String(text) => {
                // A value with list separators is split; a bare scalar stays a
                // string so a typo is visible instead of silently becoming a
                // one-element list.
                if text.contains(',') || text.contains(';') {
                    Value::Array(
                        text.split([',', ';'])
                            .map(|part| part.trim())
                            .filter(|part| !part.is_empty())
                            .map(|part| Value::String(part.to_string()))
                            .collect(),
                    )
                } else {
                    Value::String(text)
                }
            }
            other => other,
        },
        _ => value,
    }
}

/// Checks a configuration object against the schema.
pub fn validate(config: &Value) -> Vec<Issue> {
    let mut issues = Vec::new();
    let Some(map) = config.as_object() else {
        issues.push(Issue {
            key: String::new(),
            level: "error".to_string(),
            message: "the settings file must contain a JSON object".to_string(),
        });
        return issues;
    };

    for (key, value) in map {
        let Some(entry) = schema_for(key) else {
            issues.push(Issue {
                key: key.clone(),
                level: "warning".to_string(),
                message: "unknown key, it is kept but ignored".to_string(),
            });
            continue;
        };
        if !matches_kind(value, entry.kind) {
            issues.push(Issue {
                key: key.clone(),
                level: "error".to_string(),
                message: format!(
                    "expected {}, found {}; it will be coerced or ignored",
                    entry.kind,
                    type_name(value)
                ),
            });
            continue;
        }
        if let Some(message) = range_problem(key, value) {
            issues.push(Issue {
                key: key.clone(),
                level: "warning".to_string(),
                message,
            });
        }
    }

    for entry in SCHEMA {
        if !map.contains_key(entry.key) {
            issues.push(Issue {
                key: entry.key.to_string(),
                level: "info".to_string(),
                message: format!("not set, using the default {}", entry.default_json),
            });
        }
    }
    issues
}

fn matches_kind(value: &Value, kind: &str) -> bool {
    match kind {
        "int" => value.is_i64() || value.is_u64() || value.is_f64() || value.is_string() || value.is_boolean(),
        "number" => value.is_number() || value.is_string(),
        "bool" => value.is_boolean() || value.is_number() || value.is_string(),
        "string" => value.is_string() || value.is_number() || value.is_boolean(),
        "stringlist" => value.is_array() || value.is_string(),
        "object" => value.is_object(),
        "list" => value.is_array(),
        _ => true,
    }
}

fn type_name(value: &Value) -> &'static str {
    match value {
        Value::Null => "null",
        Value::Bool(_) => "bool",
        Value::Number(_) => "number",
        Value::String(_) => "string",
        Value::Array(_) => "array",
        Value::Object(_) => "object",
    }
}

fn range_problem(key: &str, value: &Value) -> Option<String> {
    let number = value.as_i64()?;
    let range: (i64, i64) = match key {
        "panel.height" => (20, 200),
        "dock.iconSize" => (12, 64),
        "panel.margin" => (0, 64),
        "panel.length" => (30, 100),
        "panel.cornerRadius" => (-1, 32),
        "panel.autoHideDelay" => (0, 10_000),
        "windows.radius" => (-1, 32),
        "windows.opacityPercent" => (0, 100),
        "windows.borderWidth" => (-1, 6),
        "windows.shadow" => (0, 3),
        "notifications.duration" => (0, 3600),
        "notifications.historyLimit" => (5, 200),
        "notifications.opacityPercent" => (0, 100),
        "notifications.cornerRadius" => (-1, 32),
        "run.historyLimit" => (1, 100),
        "startmenu.width" => (200, 4096),
        "startmenu.height" => (200, 4096),
        "startmenu.limit" => (1, 5_000),
        "desktop.iconSize" => (8, 512),
        "desktop.cellWidth" => (32, 1024),
        "desktop.cellHeight" => (32, 1024),
        "apps.recentLimit" => (0, 100),
        "system.interval" => (250, 600_000),
        "watch.debounce" => (0, 10_000),
        _ => return None,
    };
    if number < range.0 || number > range.1 {
        Some(format!("{} is outside {}..{}", number, range.0, range.1))
    } else {
        None
    }
}

// ---------------------------------------------------------------------------
// XDG paths
// ---------------------------------------------------------------------------

fn xdg_dir(variable: &str, fallback: &str) -> PathBuf {
    match std::env::var_os(variable) {
        Some(value) if !value.is_empty() => {
            let path = PathBuf::from(value);
            if path.is_absolute() {
                return path;
            }
            util::expand_home(fallback)
        }
        _ => util::expand_home(fallback),
    }
}

/// `~/.config/newteller`, honouring `$XDG_CONFIG_HOME`.
pub fn config_dir() -> PathBuf {
    xdg_dir("XDG_CONFIG_HOME", "~/.config").join("newteller")
}

/// `~/.config/newteller/shell.json`.
pub fn config_file() -> PathBuf {
    config_dir().join("shell.json")
}

/// `~/.config/newteller/themes`.
pub fn themes_dir() -> PathBuf {
    config_dir().join("themes")
}

/// `~/.config/newteller/plugins`.
pub fn config_plugins_dir() -> PathBuf {
    config_dir().join("plugins")
}

/// `~/.local/state/newteller`, honouring `$XDG_STATE_HOME`.
pub fn state_dir() -> PathBuf {
    xdg_dir("XDG_STATE_HOME", "~/.local/state").join("newteller")
}

/// `~/.cache/newteller`, honouring `$XDG_CACHE_HOME`.
pub fn cache_dir() -> PathBuf {
    xdg_dir("XDG_CACHE_HOME", "~/.cache").join("newteller")
}

/// Plugin search paths, in the order the shell uses:
/// `$NEWTELLER_PLUGIN_PATH`, the user directory, then the system directory.
pub fn plugin_dirs() -> Vec<PathBuf> {
    let mut dirs: Vec<PathBuf> = Vec::new();
    if let Some(env) = std::env::var_os("NEWTELLER_PLUGIN_PATH") {
        for part in std::env::split_paths(&env) {
            if !part.as_os_str().is_empty() {
                dirs.push(part);
            }
        }
    }
    dirs.push(config_plugins_dir());
    dirs.push(PathBuf::from("/usr/share/newteller/plugins"));
    let mut unique: Vec<PathBuf> = Vec::with_capacity(dirs.len());
    for dir in dirs {
        if !unique.contains(&dir) {
            unique.push(dir);
        }
    }
    unique
}

/// Every path NewTeller reads or writes, for `newteller-ctl doctor`.
pub fn paths() -> Map<String, Value> {
    let mut map = Map::new();
    let insert = |map: &mut Map<String, Value>, key: &str, path: &Path| {
        map.insert(
            key.to_string(),
            Value::String(path.to_string_lossy().into_owned()),
        );
    };
    insert(&mut map, "configDir", &config_dir());
    insert(&mut map, "configFile", &config_file());
    insert(&mut map, "themesDir", &themes_dir());
    insert(&mut map, "stateDir", &state_dir());
    insert(&mut map, "cacheDir", &cache_dir());
    map.insert(
        "pluginDirs".to_string(),
        Value::Array(
            plugin_dirs()
                .iter()
                .map(|path| Value::String(path.to_string_lossy().into_owned()))
                .collect(),
        ),
    );
    map
}

/// Expands `~/` in a settings value and returns an absolute path.
pub fn resolved_path(value: &str) -> PathBuf {
    util::expand_home(value)
}

/// Turns a relative or home prefixed setting into an absolute path string.
pub fn resolved_path_string(value: &str) -> String {
    resolved_path(value).to_string_lossy().into_owned()
}

/// Errors when the value is not a JSON object.
pub fn as_object(value: &Value) -> Result<&Map<String, Value>> {
    value.as_object().ok_or_else(|| Error::invalid("expected a JSON object"))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp_file(tag: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("newteller-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        dir.join("shell.json")
    }

    #[test]
    fn atomic_write_creates_parents_and_a_backup() {
        let path = temp_file("atomic");
        write_atomic(&path, b"{\"a\":1}", &WriteOptions::default()).unwrap();
        assert_eq!(std::fs::read(&path).unwrap(), b"{\"a\":1}".to_vec());
        assert!(!backup_path(&path).exists(), "no backup for a new file");

        write_atomic(&path, b"{\"a\":2}", &WriteOptions::default()).unwrap();
        assert_eq!(std::fs::read(&path).unwrap(), b"{\"a\":2}".to_vec());
        assert_eq!(std::fs::read(backup_path(&path)).unwrap(), b"{\"a\":1}".to_vec());

        // No temporary file may be left behind.
        let leftovers: Vec<_> = std::fs::read_dir(path.parent().unwrap())
            .unwrap()
            .flatten()
            .map(|entry| entry.file_name().to_string_lossy().into_owned())
            .filter(|name| name.ends_with(".tmp"))
            .collect();
        assert!(leftovers.is_empty(), "{leftovers:?}");
        let _ = std::fs::remove_dir_all(path.parent().unwrap());
    }

    #[test]
    fn a_corrupt_file_falls_back_to_the_backup() {
        let path = temp_file("corrupt");
        write_atomic(&path, b"{\"panel.height\":60}", &WriteOptions::default()).unwrap();
        write_atomic(&path, b"{\"panel.height\":72}", &WriteOptions::default()).unwrap();
        // Simulate a truncated write by something that does not use this module.
        std::fs::write(&path, b"{\"panel.hei").unwrap();

        let loaded = load_json_resilient(&path);
        assert_eq!(loaded.source, LoadSource::Backup);
        assert!(loaded.error.is_some());
        assert_eq!(get(&loaded.value, "panel.height").unwrap(), &Value::from(60));
        let _ = std::fs::remove_dir_all(path.parent().unwrap());
    }

    #[test]
    fn a_missing_file_uses_the_defaults() {
        let path = std::env::temp_dir().join("newteller-does-not-exist.json");
        let loaded = load_json_resilient(&path);
        assert_eq!(loaded.source, LoadSource::Defaults);
        assert!(loaded.value.as_object().unwrap().is_empty());
    }

    #[test]
    fn flat_keys_are_read_written_and_removed() {
        let mut config = Value::Object(defaults());
        assert_eq!(get(&config, "panel.height"), Some(&Value::from(48)));
        assert!(set(&mut config, "panel.height", Value::from(52)));
        assert!(!set(&mut config, "panel.height", Value::from(52)), "no-op write");
        assert_eq!(get(&config, "panel.height"), Some(&Value::from(52)));
        assert!(remove(&mut config, "panel.height").is_some());
        assert!(get(&config, "panel.height").is_none());
        assert!(keys(&config).contains(&"theme.name".to_string()));
    }

    #[test]
    fn values_are_coerced_to_their_declared_type() {
        assert_eq!(coerce("panel.height", Value::from("64")), Value::from(64));
        assert_eq!(coerce("panel.autoHide", Value::from("yes")), Value::from(true));
        assert_eq!(
            coerce("panel.pinned", Value::from("a.desktop, b.desktop")),
            Value::Array(vec![Value::from("a.desktop"), Value::from("b.desktop")])
        );
        assert_eq!(coerce("panel.pinned", Value::from("x")), Value::from("x"));
        assert_eq!(coerce("unknown.key", Value::from("x")), Value::from("x"));
    }

    #[test]
    fn validation_reports_unknown_and_out_of_range_keys() {
        let mut config = Value::Object(defaults());
        set(&mut config, "panel.nonsense", Value::from(1));
        set(&mut config, "panel.height", Value::from(4000));
        // A key that is missing entirely is reported as informational.
        remove(&mut config, "theme.name");
        let issues = validate(&config);
        assert!(issues.iter().any(|issue| issue.key == "panel.nonsense" && issue.level == "warning"));
        assert!(issues.iter().any(|issue| issue.key == "panel.height" && issue.level == "warning"));
        assert!(issues.iter().any(|issue| issue.level == "info"));
    }

    #[test]
    fn every_schema_default_parses_and_matches_its_kind() {
        for entry in SCHEMA {
            let value = parse_default(entry);
            assert!(!value.is_null(), "{} has an unparsable default", entry.key);
            assert!(
                matches_kind(&value, entry.kind),
                "{} default {:?} does not match {}",
                entry.key,
                value,
                entry.kind
            );
            assert!(!entry.doc.is_empty(), "{} has no documentation", entry.key);
            assert_eq!(coerce(entry.key, value.clone()), value);
        }
        assert!(schema_for("panel.height").is_some());
        assert!(schema_for("nope").is_none());
        assert!(!defaults().is_empty());
    }

    #[test]
    fn surface_material_settings_are_known_and_have_matching_defaults() {
        let default_values = defaults();
        assert_eq!(default_values.get("panel.material").unwrap(), &Value::from("theme"));
        assert_eq!(default_values.get("dock.material").unwrap(), &Value::from("translucent"));
        assert_eq!(default_values.get("windows.material").unwrap(), &Value::from("theme"));
        assert_eq!(default_values.get("notifications.material").unwrap(), &Value::from("theme"));

        let mut config = Map::new();
        config.insert("panel.material".to_string(), Value::from("frosted"));
        config.insert("dock.material".to_string(), Value::from("liquid"));
        config.insert("windows.material".to_string(), Value::from("translucent"));
        config.insert("notifications.material".to_string(), Value::from("opaque"));
        config.insert("notifications.opacityPercent".to_string(), Value::from(50));
        config.insert("notifications.cornerRadius".to_string(), Value::from(12));
        let issues = validate(&Value::Object(config));
        for key in [
            "panel.material",
            "dock.material",
            "windows.material",
            "notifications.material",
            "notifications.opacityPercent",
            "notifications.cornerRadius",
        ] {
            assert!(
                !issues.iter().any(|issue| issue.key == key && issue.level != "info"),
                "{key} should be a valid surface setting"
            );
        }
    }

    #[test]
    fn defaults_merge_with_overrides() {
        let mut overrides = Map::new();
        overrides.insert("theme.name".to_string(), Value::from("light"));
        overrides.insert("panel.custom".to_string(), Value::from(1));
        let merged = with_defaults(&Value::Object(overrides));
        assert_eq!(get(&merged, "theme.name").unwrap(), &Value::from("light"));
        assert_eq!(get(&merged, "panel.height").unwrap(), &Value::from(48));
        assert_eq!(get(&merged, "panel.custom").unwrap(), &Value::from(1));
    }

    #[test]
    fn paths_are_absolute_and_namespaced() {
        let map = paths();
        let config = map.get("configFile").unwrap().as_str().unwrap();
        assert!(config.ends_with("newteller/shell.json"), "{config}");
        assert!(!plugin_dirs().is_empty());
        assert!(plugin_dirs().iter().any(|dir| dir.ends_with("newteller/plugins")));
    }

    #[test]
    fn backup_and_temp_paths_stay_next_to_the_file() {
        let path = Path::new("/home/user/.config/newteller/shell.json");
        assert_eq!(
            backup_path(path),
            PathBuf::from("/home/user/.config/newteller/shell.json.bak")
        );
        let temp = temp_path(path);
        assert_eq!(temp.parent(), path.parent());
        assert!(temp.file_name().unwrap().to_string_lossy().ends_with(".tmp"));
    }
}
