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

/// Where a release archive (or an installation image) writes down which
/// build it is. The updater compares this with what fourtexec.site offers.
pub const BUILD_INFO_FILE: &str = "/usr/share/newteller/BUILDINFO";

/// The installed build, from `BUILDINFO` (`key=value` lines).
#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct BuildInfo {
    /// Full commit hash; empty for a build from source.
    pub commit: String,
    /// Release target, e.g. `debian13-x86_64`; empty when not recorded.
    pub target: String,
    /// Commit time in seconds since the epoch; 0 when not recorded.
    pub date: u64,
}

/// Parses the text of a `BUILDINFO` file. Unknown keys are ignored, and a
/// commit that is not a full hexadecimal hash counts as "unknown" rather
/// than being passed on.
pub fn parse_build_info(text: &str) -> BuildInfo {
    let mut info = BuildInfo::default();
    for line in text.lines() {
        let line = line.trim();
        if line.starts_with('#') {
            continue;
        }
        let Some((key, value)) = line.split_once('=') else {
            continue;
        };
        let value = value.trim().trim_matches(|c| c == '"' || c == '\'');
        match key.trim() {
            "commit" => {
                if value.len() == 40 && value.bytes().all(|b| matches!(b, b'0'..=b'9' | b'a'..=b'f')) {
                    info.commit = value.to_string();
                }
            }
            "target" => info.target = value.to_string(),
            "date" => info.date = value.parse().unwrap_or(0),
            _ => {}
        }
    }
    info
}

/// The installed build; `NEWTELLER_BUILDINFO` names another file (tests).
pub fn read_build_info() -> BuildInfo {
    let path = std::env::var("NEWTELLER_BUILDINFO").unwrap_or_else(|_| BUILD_INFO_FILE.to_string());
    std::fs::read_to_string(path)
        .map(|text| parse_build_info(&text))
        .unwrap_or_default()
}

/// Everything `newteller-ctl doctor` prints.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Diagnostics {
    /// Crate version.
    pub version: String,
    /// C ABI version.
    pub abi_version: u32,
    /// The installed build (release archive), if it is recorded.
    pub build: BuildInfo,
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
        build: read_build_info(),
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn build_info_is_read_from_key_value_lines() {
        let commit = "0123456789abcdef0123456789abcdef01234567";
        let info = parse_build_info(&format!(
            "# written by build-release.sh\ncommit={commit}\ntarget=debian13-x86_64\ndate=1793943937\nextra=1\n"
        ));
        assert_eq!(info.commit, commit);
        assert_eq!(info.target, "debian13-x86_64");
        assert_eq!(info.date, 1_793_943_937);
    }

    #[test]
    fn build_info_without_a_real_commit_is_unknown() {
        // An installation image built outside git writes no commit; a short
        // or mistyped one must not be compared with real ones either.
        for text in ["", "date=\ntarget=\n", "commit=abc123\n", "commit=XYZ\ndate=soon\n", "no equals sign"] {
            let info = parse_build_info(text);
            assert_eq!(info.commit, "", "{text:?}");
            assert_eq!(info.date, 0, "{text:?}");
        }
        assert_eq!(parse_build_info("target = \"arch-x86_64\"").target, "arch-x86_64");
    }
}
