//! Parallel scan of the XDG application directories.
//!
//! The old C++ implementation walked every directory and parsed every file on
//! the GUI thread with `QSettings`. Here the candidate list is collected first
//! (cheap: only `readdir` and file types) and the parsing is then spread over
//! all cores with rayon, which is where the wall-clock time of a cold start
//! goes.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::time::Instant;

use rayon::prelude::*;
use serde::{Deserialize, Serialize};

use crate::desktop::{desktop_env_from_env, locale_from_env, parse_file, EntryOptions};
use crate::util;

/// Default recursion depth inside an applications directory. `kde4/`,
/// `flatpak/` and similar sub-directories are common.
pub const DEFAULT_MAX_DEPTH: usize = 3;

/// Never report more problems than this; a broken directory can hold hundreds.
pub const MAX_REPORTED_ERRORS: usize = 64;

/// What to scan and how to interpret the files.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct ScanRequest {
    /// Application directories, most important first. The first directory that
    /// contains an id wins, matching the XDG basedir precedence.
    pub dirs: Vec<String>,
    /// `LANG` style locale, for example `de_DE.UTF-8`. `None` reads the
    /// environment.
    pub locale: Option<String>,
    /// `XDG_CURRENT_DESKTOP` value. `None` reads the environment.
    pub desktop_env: Option<String>,
    /// Stop after this many applications; `0` means no limit.
    pub max_apps: usize,
    /// Recursion depth inside each applications directory.
    pub max_depth: usize,
    /// Keep `NoDisplay=true` entries, for diagnostics.
    pub include_hidden: bool,
    /// Expand a leading `~/` in `dirs`.
    pub expand_home: bool,
}

impl Default for ScanRequest {
    fn default() -> Self {
        ScanRequest {
            dirs: Vec::new(),
            locale: None,
            desktop_env: None,
            max_apps: 0,
            max_depth: DEFAULT_MAX_DEPTH,
            include_hidden: false,
            expand_home: true,
        }
    }
}

/// A file that will be parsed.
#[derive(Debug, Clone)]
struct Candidate {
    id: String,
    path: PathBuf,
    priority: u32,
}

/// Result of a scan.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct ScanReport {
    /// Applications, sorted by accent folded name and then by id.
    pub apps: Vec<crate::desktop::DesktopEntry>,
    /// Files that were read.
    pub scanned: usize,
    /// Files that were rejected (not an application, hidden, wrong desktop).
    pub skipped: usize,
    /// Wall clock time of the whole scan.
    pub elapsed_ms: f64,
    /// Problems worth showing to the user or in a bug report.
    pub errors: Vec<ScanError>,
    /// Locale chain that was applied.
    pub locales: Vec<String>,
    /// Desktop environments that were applied.
    pub desktop_env: Vec<String>,
}

/// One rejected or unreadable file.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct ScanError {
    /// Absolute path.
    pub path: String,
    /// Why the file was not turned into an application.
    pub reason: String,
}

impl Default for ScanError {
    fn default() -> Self {
        ScanError {
            path: String::new(),
            reason: String::new(),
        }
    }
}

/// Scans the requested directories and returns the report.
pub fn scan_dirs(request: &ScanRequest) -> ScanReport {
    let started = Instant::now();

    let locales = match request.locale.as_deref() {
        Some(locale) if !locale.trim().is_empty() => {
            let candidates = crate::desktop::locale_candidates(locale);
            if candidates.is_empty() {
                locale_from_env()
            } else {
                candidates
            }
        }
        _ => locale_from_env(),
    };
    let desktop_env = match request.desktop_env.as_deref() {
        Some(env) if !env.trim().is_empty() => util::split_env_list(env),
        _ => desktop_env_from_env(),
    };

    let mut candidates: Vec<Candidate> = Vec::new();
    let mut ids: HashMap<String, usize> = HashMap::new();
    let mut errors: Vec<ScanError> = Vec::new();
    let mut scanned = 0usize;

    for (priority, dir) in request.dirs.iter().enumerate() {
        let dir = dir.trim();
        if dir.is_empty() {
            continue;
        }
        let root = if request.expand_home {
            util::expand_home(dir)
        } else {
            PathBuf::from(dir)
        };
        collect_dir(
            &root,
            &root,
            priority as u32,
            0,
            request.max_depth,
            &mut candidates,
            &mut ids,
            &mut scanned,
            &mut errors,
        );
    }

    // Parsing is the expensive part: spread it over every core.
    let parsed: Vec<Result<crate::desktop::DesktopEntry, ScanError>> = candidates
        .par_iter()
        .map(|candidate| {
            let options = EntryOptions {
                id: &candidate.id,
                path: &candidate.path.to_string_lossy(),
                locales: &locales,
                desktop_env: &desktop_env,
                priority: candidate.priority,
                include_hidden: request.include_hidden,
            };
            match parse_file(&candidate.path, &options) {
                Ok(entry) => Ok(entry),
                Err(error) => Err(ScanError {
                    path: candidate.path.to_string_lossy().into_owned(),
                    reason: error.to_string(),
                }),
            }
        })
        .collect();

    let mut apps: Vec<crate::desktop::DesktopEntry> = Vec::with_capacity(parsed.len());
    let mut skipped = 0usize;
    for result in parsed {
        match result {
            Ok(entry) => apps.push(entry),
            Err(problem) => {
                skipped += 1;
                if errors.len() < MAX_REPORTED_ERRORS {
                    errors.push(problem);
                }
            }
        }
    }

    // `sort_key` was folded at parse time, so this comparison does not allocate.
    apps.sort_by(|a, b| a.sort_key.cmp(&b.sort_key).then_with(|| a.id.cmp(&b.id)));
    if request.max_apps > 0 && apps.len() > request.max_apps {
        apps.truncate(request.max_apps);
    }

    ScanReport {
        apps,
        scanned,
        skipped,
        elapsed_ms: started.elapsed().as_secs_f64() * 1000.0,
        errors,
        locales,
        desktop_env,
    }
}

/// Scans and serialises the report to JSON, as used by the C ABI.
pub fn scan_json(request: &ScanRequest) -> crate::Result<String> {
    let report = scan_dirs(request);
    Ok(serde_json::to_string(&report)?)
}

#[allow(clippy::too_many_arguments)]
fn collect_dir(
    root: &Path,
    dir: &Path,
    priority: u32,
    depth: usize,
    max_depth: usize,
    candidates: &mut Vec<Candidate>,
    ids: &mut HashMap<String, usize>,
    scanned: &mut usize,
    errors: &mut Vec<ScanError>,
) {
    let entries = match std::fs::read_dir(dir) {
        Ok(entries) => entries,
        Err(error) => {
            if errors.len() < MAX_REPORTED_ERRORS {
                errors.push(ScanError {
                    path: dir.to_string_lossy().into_owned(),
                    reason: format!("cannot list directory: {error}"),
                });
            }
            return;
        }
    };

    for entry in entries.flatten() {
        let path = entry.path();
        // `file_type` comes from readdir and needs no extra stat; only symlinks
        // have to be resolved.
        let file_type = match entry.file_type() {
            Ok(t) => t,
            Err(_) => continue,
        };
        if file_type.is_symlink() {
            match std::fs::metadata(&path) {
                Ok(meta) if meta.is_dir() => {
                    if depth < max_depth {
                        collect_dir(
                            root,
                            &path,
                            priority,
                            depth + 1,
                            max_depth,
                            candidates,
                            ids,
                            scanned,
                            errors,
                        );
                    }
                    continue;
                }
                Ok(_) => {}
                Err(_) => continue,
            }
        } else if file_type.is_dir() {
            if depth < max_depth {
                collect_dir(
                    root,
                    &path,
                    priority,
                    depth + 1,
                    max_depth,
                    candidates,
                    ids,
                    scanned,
                    errors,
                );
            }
            continue;
        } else if !file_type.is_file() {
            continue;
        }

        let Some(name) = path.file_name().and_then(|n| n.to_str()) else {
            continue;
        };
        if !name.ends_with(".desktop") {
            continue;
        }
        *scanned += 1;

        // Specification desktop file id: path relative to the applications
        // directory with '/' replaced by '-'.
        let id = match path.strip_prefix(root) {
            Ok(relative) => relative.to_string_lossy().replace('/', "-"),
            Err(_) => name.to_string(),
        };
        // The first directory wins, so an already known id is ignored.
        if ids.contains_key(&id) {
            continue;
        }
        ids.insert(id.clone(), candidates.len());
        candidates.push(Candidate {
            id,
            path,
            priority,
        });
    }
}

/// Application directories in precedence order, from `$XDG_DATA_DIRS` and the
/// XDG basedir specification. `~/.local/share/applications` comes first so user
/// entries override system ones.
pub fn default_dirs() -> Vec<String> {
    let mut dirs: Vec<String> = Vec::new();

    if let Some(home) = util::home_dir() {
        if let Some(data) = std::env::var_os("XDG_DATA_HOME") {
            let data = PathBuf::from(data);
            if data.is_absolute() {
                dirs.push(data.join("applications").to_string_lossy().into_owned());
            }
        } else {
            dirs.push(home.join(".local/share/applications").to_string_lossy().into_owned());
        }
    }

    let flatpak = util::expand_home("~/.local/share/flatpak/exports/share/applications");
    dirs.push(flatpak.to_string_lossy().into_owned());

    if let Some(data_dirs) = std::env::var_os("XDG_DATA_DIRS") {
        for dir in std::env::split_paths(&data_dirs) {
            dirs.push(dir.join("applications").to_string_lossy().into_owned());
        }
    } else {
        dirs.push("/usr/local/share/applications".to_string());
        dirs.push("/usr/share/applications".to_string());
    }
    dirs.push("/var/lib/flatpak/exports/share/applications".to_string());
    dirs.push("/usr/local/share/flatpak/exports/share/applications".to_string());

    let mut unique: Vec<String> = Vec::with_capacity(dirs.len());
    for dir in dirs {
        if !dir.is_empty() && !unique.contains(&dir) {
            unique.push(dir);
        }
    }
    unique
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    fn write(dir: &Path, name: &str, body: &str) {
        let path = dir.join(name);
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent).unwrap();
        }
        let mut file = std::fs::File::create(path).unwrap();
        file.write_all(body.as_bytes()).unwrap();
    }

    #[test]
    fn user_directory_wins_and_subdirectories_are_found() {
        let root = std::env::temp_dir().join(format!("newteller-scan-{}", std::process::id()));
        let system = root.join("system");
        let user = root.join("user");
        let _ = std::fs::remove_dir_all(&root);
        std::fs::create_dir_all(&system).unwrap();
        std::fs::create_dir_all(&user).unwrap();

        write(
            &system,
            "app.desktop",
            "[Desktop Entry]\nType=Application\nName=System App\nExec=/bin/true\n",
        );
        write(
            &user,
            "app.desktop",
            "[Desktop Entry]\nType=Application\nName=User App\nExec=/bin/true\n",
        );
        write(
            &system,
            "kde4/nested.desktop",
            "[Desktop Entry]\nType=Application\nName=Nested\nExec=/bin/true\n",
        );
        write(
            &system,
            "skip.desktop",
            "[Desktop Entry]\nType=Application\nName=Skipped\nExec=/bin/true\nNoDisplay=true\n",
        );

        let request = ScanRequest {
            dirs: vec![
                user.to_string_lossy().into_owned(),
                system.to_string_lossy().into_owned(),
            ],
            locale: Some("C".to_string()),
            desktop_env: Some("".to_string()),
            ..ScanRequest::default()
        };
        let report = scan_dirs(&request);

        let ids: Vec<&str> = report.apps.iter().map(|a| a.id.as_str()).collect();
        assert!(ids.contains(&"app.desktop"), "{ids:?}");
        assert!(ids.contains(&"kde4-nested.desktop"), "{ids:?}");
        assert_eq!(report.skipped, 1);
        let user_app = report.apps.iter().find(|a| a.id == "app.desktop").unwrap();
        assert_eq!(user_app.name, "User App");
        assert_eq!(user_app.priority, 0);

        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn sorting_ignores_case_and_accents() {
        let root = std::env::temp_dir().join(format!("newteller-sort-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        std::fs::create_dir_all(&root).unwrap();
        write(
            &root,
            "z.desktop",
            "[Desktop Entry]\nType=Application\nName=éléphant\nExec=/bin/true\n",
        );
        write(
            &root,
            "a.desktop",
            "[Desktop Entry]\nType=Application\nName=Zebra\nExec=/bin/true\n",
        );
        write(
            &root,
            "e.desktop",
            "[Desktop Entry]\nType=Application\nName=apple\nExec=/bin/true\n",
        );

        let report = scan_dirs(&ScanRequest {
            dirs: vec![root.to_string_lossy().into_owned()],
            locale: Some("C".to_string()),
            ..ScanRequest::default()
        });
        let names: Vec<&str> = report.apps.iter().map(|a| a.name.as_str()).collect();
        assert_eq!(names, vec!["apple", "éléphant", "Zebra"]);
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn json_round_trip_keeps_camel_case_keys() {
        let report = ScanReport::default();
        let json = serde_json::to_string(&report).unwrap();
        assert!(json.contains("elapsedMs"));
        let back: ScanReport = serde_json::from_str(&json).unwrap();
        assert!(back.apps.is_empty());
    }

    #[test]
    fn default_dirs_starts_with_the_user_directory() {
        let dirs = default_dirs();
        assert!(!dirs.is_empty());
        assert!(dirs.iter().any(|d| d.ends_with("/applications")));
    }
}
