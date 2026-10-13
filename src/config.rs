//! User configuration for NewTeller.
//!
//! Loaded from `$XDG_CONFIG_HOME/newteller/config.toml`
//! (usually `~/.config/newteller/config.toml`). Missing keys fall back to defaults.

use serde::Deserialize;
use std::path::PathBuf;

#[derive(Debug, Clone, Deserialize)]
#[serde(default)]
pub struct Config {
    pub theme: Theme,
    pub panel: PanelConfig,
    pub start_menu: StartMenuConfig,
    pub icons: IconConfig,
    pub pinned: Vec<PinnedApp>,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(default)]
pub struct Theme {
    /// CSS color for panel background (any CSS color value).
    pub panel_background: String,
    pub panel_foreground: String,
    pub accent: String,
    pub font: String,
    /// Corner radius of panel and menus in pixels.
    pub radius: u32,
    /// Path to a custom CSS file applied after the built-in theme.
    pub custom_css: Option<PathBuf>,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(default)]
pub struct PanelConfig {
    /// One of: "bottom", "top", "left", "right".
    pub position: String,
    /// Panel thickness in pixels.
    pub height: i32,
    /// Show clock in the tray area.
    pub show_clock: bool,
    /// strftime format for the clock.
    pub clock_format: String,
    /// Centered taskbar (Windows 11 style) or left-aligned (Windows 10 style).
    pub centered: bool,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(default)]
pub struct StartMenuConfig {
    pub width: i32,
    pub height: i32,
    /// Max number of applications shown in the "recent" section.
    pub recent_count: usize,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(default)]
pub struct IconConfig {
    /// Directory of an icon set. Defaults to the IconKit checkout location.
    /// Icons are looked up as `<dir>/<set>/<name>.svg`, e.g. `icons/lucide/rocket.svg`.
    pub path: PathBuf,
    /// Icon set name used for shell UI icons (e.g. "lucide", "tabler/outline").
    pub set: String,
    pub size: i32,
}

#[derive(Debug, Clone, Deserialize)]
pub struct PinnedApp {
    pub name: String,
    pub command: String,
    /// Icon name inside the configured icon set, or an absolute path to a file.
    pub icon: Option<String>,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            theme: Theme::default(),
            panel: PanelConfig::default(),
            start_menu: StartMenuConfig::default(),
            icons: IconConfig::default(),
            pinned: vec![
                PinnedApp {
                    name: "Terminal".into(),
                    command: "x-terminal-emulator".into(),
                    icon: Some("terminal".into()),
                },
                PinnedApp {
                    name: "Files".into(),
                    command: "xdg-open ~".into(),
                    icon: Some("folder".into()),
                },
                PinnedApp {
                    name: "Browser".into(),
                    command: "x-www-browser".into(),
                    icon: Some("globe".into()),
                },
            ],
        }
    }
}

impl Default for Theme {
    fn default() -> Self {
        Self {
            panel_background: "rgba(24, 26, 32, 0.92)".into(),
            panel_foreground: "#f2f4f8".into(),
            accent: "#2f80ed".into(),
            font: "Cantarell 10".into(),
            radius: 8,
            custom_css: None,
        }
    }
}

impl Default for PanelConfig {
    fn default() -> Self {
        Self {
            position: "bottom".into(),
            height: 48,
            show_clock: true,
            clock_format: "%H:%M\n%d.%m.%Y".into(),
            centered: false,
        }
    }
}

impl Default for StartMenuConfig {
    fn default() -> Self {
        Self {
            width: 420,
            height: 560,
            recent_count: 8,
        }
    }
}

impl Default for IconConfig {
    fn default() -> Self {
        let path = dirs::home_dir()
            .map(|h| h.join("iconkit/icons"))
            .unwrap_or_else(|| PathBuf::from("/usr/share/newteller/icons"));
        Self {
            path,
            set: "lucide".into(),
            size: 24,
        }
    }
}

impl Config {
    /// Path of the user config file.
    pub fn path() -> PathBuf {
        dirs::config_dir()
            .unwrap_or_else(|| PathBuf::from("~/.config"))
            .join("newteller")
            .join("config.toml")
    }

    /// Load the config; on any error log it and return defaults.
    pub fn load() -> Self {
        let path = Self::path();
        match std::fs::read_to_string(&path) {
            Ok(text) => match toml::from_str::<Config>(&text) {
                Ok(cfg) => cfg,
                Err(e) => {
                    eprintln!("newteller: invalid config {}: {e}", path.display());
                    Config::default()
                }
            },
            Err(_) => Config::default(),
        }
    }
}
