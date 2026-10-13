//! Machine and configuration diagnostics.
//!
//! `newteller --diagnostics` and `newteller-ctl doctor` both print this blob.
//! It answers the questions a bug report needs ("which session, which desktop,
//! which config file, what is wrong with it") without ever including secrets:
//! only session and XDG variables are copied, never the whole environment.

use serde::Serialize;
use serde_json::{Map, Value};

use crate::config;
use crate::system;
use crate::util;

/// Environment variables that are useful when debugging a shell.
pub const REPORTED_VARIABLES: [&str; 18] = [
    "XDG_SESSION_TYPE",
    "XDG_SESSION_DESKTOP",
    "XDG_CURRENT_DESKTOP",
    "XDG_RUNTIME_DIR",
    "XDG_DATA_DIRS",
    "XDG_CONFIG_HOME",
    "XDG_CACHE_HOME",
    "XDG_STATE_HOME",
    "WAYLAND_DISPLAY",
    "DISPLAY",
    "DESKTOP_SESSION",
    "GDMSESSION",
    "QT_QPA_PLATFORM",
    "QT_QUICK_BACKEND",
    "LANG",
    "LC_ALL",
    "NEWTELLER_PLUGIN_PATH",
    "SHELL",
];

/// One plugin search directory.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PluginDirInfo {
    /// Absolute path.
    pub path: String,
    /// Whether it exists.
    pub exists: bool,
    /// How many sub-directories contain a `manifest.json`.
    pub plugins: usize,
}

/// Everything `newteller-ctl doctor` prints.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Diagnostics {
    /// Crate version.
    pub version: String,
    /// C ABI version.
    pub abi_version: u32,
    /// Target triple the crate was compiled for.
    pub target: String,
    /// True for a debug build.
    pub debug_build: bool,
    /// Logical CPUs.
    pub cpu_count: usize,
    /// Total memory in bytes.
    pub memory_total: u64,
    /// Memory in use, in percent.
    pub memory_percent: f64,
    /// `uname`.
    pub uname: system::Uname,
    /// Battery state.
    pub battery: system::power::Battery,
    /// Where NewTeller reads and writes.
    pub paths: Map<String, Value>,
    /// Session and XDG variables.
    pub environment: Map<String, Value>,
    /// Plugin search directories.
    pub plugin_dirs: Vec<PluginDirInfo>,
    /// Problems found in `shell.json`.
    pub config_issues: Vec<config::Issue>,
    /// True when the configuration file exists.
    pub config_exists: bool,
    /// Applications found by a full scan.
    pub applications: usize,
    /// Milliseconds the application scan took.
    pub scan_ms: f64,
    /// Milliseconds since the epoch.
    pub timestamp_ms: u64,
    /// Locale chain used for the scan.
    pub locales: Vec<String>,
    /// Desktop environments used for the scan.
    pub desktop_env: Vec<String>,
}

/// Collects a full diagnostic snapshot.
///
/// This scans the application directories, so it takes a few tens of
/// milliseconds; it is meant for the CLI and for an explicit `--diagnostics`
/// run, not for periodic polling.
pub fn collect() -> Diagnostics {
    let scan = crate::desktop::scan_dirs(&crate::desktop::ScanRequest {
        dirs: crate::desktop::scan::default_dirs(),
        ..crate::desktop::ScanRequest::default()
    });

    let memory = system::procfs::read_meminfo().unwrap_or_default();
    let uname = system::read_uname();
    let cpu_count = std::thread::available_parallelism()
        .map(|count| count.get())
        .unwrap_or(0);

    Diagnostics {
        version: crate::VERSION.to_string(),
        abi_version: crate::ABI_VERSION,
        target: env!("TARGET").to_string(),
        debug_build: cfg!(debug_assertions),
        cpu_count,
        memory_total: memory.total,
        memory_percent: memory.percent(),
        uname,
        battery: system::power::read_battery(),
        paths: config::paths(),
        environment: environment(),
        plugin_dirs: plugin_dirs(),
        config_issues: config_issues(),
        config_exists: config::config_file().exists(),
        applications: scan.apps.len(),
        scan_ms: scan.elapsed_ms,
        timestamp_ms: util::now_ms(),
        locales: scan.locales,
        desktop_env: scan.desktop_env,
    }
}

/// Collects and serialises the diagnostics.
pub fn collect_json() -> crate::Result<String> {
    Ok(serde_json::to_string_pretty(&collect())?)
}

/// The interesting environment variables.
pub fn environment() -> Map<String, Value> {
    let mut map = Map::new();
    for name in REPORTED_VARIABLES {
        match std::env::var(name) {
            Ok(value) => {
                map.insert(name.to_string(), Value::String(value));
            }
            Err(_) => {
                map.insert(name.to_string(), Value::Null);
            }
        }
    }
    map
}

/// Inspects the plugin search paths.
pub fn plugin_dirs() -> Vec<PluginDirInfo> {
    config::plugin_dirs()
        .iter()
        .map(|dir| PluginDirInfo {
            path: dir.to_string_lossy().into_owned(),
            exists: dir.is_dir(),
            plugins: count_plugins(dir),
        })
        .collect()
}

fn count_plugins(dir: &std::path::Path) -> usize {
    let entries = match std::fs::read_dir(dir) {
        Ok(entries) => entries,
        Err(_) => return 0,
    };
    entries
        .flatten()
        .filter(|entry| entry.path().join("manifest.json").is_file())
        .count()
}

fn config_issues() -> Vec<config::Issue> {
    let path = config::config_file();
    match config::load_json_resilient(&path) {
        loaded if loaded.source == config::LoadSource::Defaults && !path.exists() => Vec::new(),
        loaded => {
            let mut issues = config::validate(&loaded.value);
            if let Some(error) = loaded.error {
                issues.insert(
                    0,
                    config::Issue {
                        key: String::new(),
                        level: "error".to_string(),
                        message: error,
                    },
                );
            }
            // Only the real problems; "not set" is normal for a fresh install.
            issues
                .into_iter()
                .filter(|issue| issue.level != "info")
                .collect()
        }
    }
}
