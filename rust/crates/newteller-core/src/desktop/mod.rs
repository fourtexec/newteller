//! XDG desktop entry files: parsing, localisation and visibility rules.
//!
//! Implements the freedesktop.org "Desktop Entry Specification" parts that a
//! launcher needs. Qt's `QSettings` in INI mode was used before; it silently
//! mangles several constructs this parser handles correctly:
//!
//! * localised keys (`Name[de_DE]`) and the full locale fallback chain,
//! * escaped characters (`\s`, `\n`, `\;`, `\\`) inside values and lists,
//! * quoted values, continuation lines and UTF-8 BOMs,
//! * `OnlyShowIn` / `NotShowIn` / `TryExec` / `DBusActivatable`,
//! * `[Desktop Action *]` groups, which back the right-click menu.

pub mod exec;
pub mod scan;

use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};

use crate::error::{Error, Result};
use crate::util;

pub use scan::{scan_dirs, scan_json, ScanError, ScanReport, ScanRequest};

/// One `[Desktop Action ...]` group.
#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct DesktopAction {
    /// Action identifier, the `X` in `[Desktop Action X]`.
    pub id: String,
    /// Localised `Name` of the action.
    pub name: String,
    /// Raw `Exec` line; expand it with [`exec::expand`] before running.
    pub exec: String,
    /// Icon name or path.
    pub icon: String,
}

/// A launchable application.
///
/// Serialised to JSON with camelCase keys; the C++ side reads exactly these
/// names in `AppModel::loadFromRust`.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
#[derive(Default)]
pub struct DesktopEntry {
    /// Desktop file id, for example `firefox-esr.desktop`. Sub-directories are
    /// folded in the way the specification requires: `kde4-okular.desktop`.
    pub id: String,
    /// Absolute path of the file this entry was parsed from.
    pub path: String,
    /// Localised `Name`.
    pub name: String,
    /// Localised `GenericName`.
    pub generic_name: String,
    /// Localised `Comment`.
    pub comment: String,
    /// Raw `Exec` line, field codes included.
    pub exec: String,
    /// `Icon` key: a theme name or an absolute path.
    pub icon: String,
    /// `Terminal=true`.
    pub terminal: bool,
    /// `NoDisplay=true`; the entry is kept only for diagnostics.
    pub no_display: bool,
    /// `Hidden=true`.
    pub hidden: bool,
    /// `StartupNotify=true`.
    pub startup_notify: bool,
    /// `StartupWMClass`, used later to match windows to applications.
    #[serde(rename = "startupWMClass")]
    pub startup_wm_class: String,
    /// `Categories`, split on `;`.
    pub categories: Vec<String>,
    /// `Keywords`, split on `;`.
    pub keywords: Vec<String>,
    /// `MimeType`, split on `;` - drives "open with".
    pub mime_types: Vec<String>,
    /// `OnlyShowIn`.
    pub only_show_in: Vec<String>,
    /// `NotShowIn`.
    pub not_show_in: Vec<String>,
    /// `TryExec`.
    pub try_exec: String,
    /// `DBusActivatable=true`.
    pub dbus_activatable: bool,
    /// Parsed `[Desktop Action *]` groups.
    pub actions: Vec<DesktopAction>,
    /// Index of the applications directory this entry came from; lower wins
    /// when the same id exists in several directories.
    pub priority: u32,
    /// Accent folded, lower-cased `name`. Precomputed so that sorting and
    /// searching never re-lower-case the same string.
    pub sort_key: String,
}


impl DesktopEntry {
    /// Program that will actually be started: the first token of `Exec`,
    /// without its directory. Used to match running processes back to apps.
    pub fn program(&self) -> String {
        exec::program_of(&self.exec)
    }

    /// True when the entry should appear in a launcher for `desktop_env`.
    ///
    /// An empty `desktop_env` means "unknown session", and in that case
    /// `OnlyShowIn` / `NotShowIn` are not applied - hiding everything on a
    /// system without `XDG_CURRENT_DESKTOP` would be worse than showing too
    /// much.
    pub fn is_visible(&self, desktop_env: &[String]) -> bool {
        if self.hidden || self.no_display {
            return false;
        }
        self.matches_desktop(desktop_env)
    }

    /// The `OnlyShowIn` / `NotShowIn` filter alone. `build()` applies this even
    /// when `include_hidden` keeps a `NoDisplay` entry in the catalogue. An
    /// empty `desktop_env` disables the filter, like [`Self::is_visible`].
    pub fn matches_desktop(&self, desktop_env: &[String]) -> bool {
        if desktop_env.is_empty() {
            return true;
        }
        let shown = desktop_env.iter().any(|env| {
            self.only_show_in.iter().any(|allowed| allowed.eq_ignore_ascii_case(env))
        });
        if !self.only_show_in.is_empty() && !shown {
            return false;
        }
        !self.not_show_in.iter().any(|blocked| {
            desktop_env
                .iter()
                .any(|env| blocked.eq_ignore_ascii_case(env))
        })
    }
}

/// Raw, unresolved view of a desktop file: keys are kept exactly as written
/// (localised keys keep their `[locale]` suffix).
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct RawDesktopFile {
    /// Keys of the `[Desktop Entry]` group.
    pub entry: BTreeMap<String, String>,
    /// Keys of every `[Desktop Action <id>]` group.
    pub actions: BTreeMap<String, BTreeMap<String, String>>,
}

impl RawDesktopFile {
    /// Localised lookup: tries `key[locale]` for every candidate and finally
    /// the plain `key`.
    pub fn get(&self, key: &str, locales: &[String]) -> Option<String> {
        for locale in locales {
            let localised = format!("{key}[{locale}]");
            if let Some(value) = self.entry.get(&localised) {
                if !value.trim().is_empty() {
                    return Some(value.clone());
                }
            }
        }
        self.entry.get(key).cloned()
    }

    /// Like [`RawDesktopFile::get`] but resolves the specification escapes
    /// (`\s`, `\n`, `\t`, `\r`, `\\`, `\"`, `\'`).
    ///
    /// List keys must *not* use this: [`split_list`] needs the raw text to tell
    /// an escaped `\;` from a separator.
    pub fn get_unescaped(&self, key: &str, locales: &[String]) -> Option<String> {
        self.get(key, locales).map(|value| unescape(&value))
    }

    /// Unescaped lookup of a non-localised key.
    pub fn plain_unescaped(&self, key: &str) -> String {
        self.entry.get(key).map(|value| unescape(value)).unwrap_or_default()
    }

    /// Localised lookup inside an action group.
    pub fn action_get(
        group: &BTreeMap<String, String>,
        key: &str,
        locales: &[String],
    ) -> Option<String> {
        for locale in locales {
            let localised = format!("{key}[{locale}]");
            if let Some(value) = group.get(&localised) {
                if !value.trim().is_empty() {
                    return Some(value.clone());
                }
            }
        }
        group.get(key).cloned()
    }

    /// True when the file declares `Type=Application`.
    pub fn is_application(&self) -> bool {
        self.entry.get("Type").map(|t| t.trim()) == Some("Application")
    }
}

/// Everything needed to turn a [`RawDesktopFile`] into a [`DesktopEntry`].
#[derive(Debug, Clone)]
#[derive(Default)]
pub struct EntryOptions<'a> {
    /// Desktop file id.
    pub id: &'a str,
    /// Absolute path of the source file.
    pub path: &'a str,
    /// Locale fallback chain, see [`locale_candidates`].
    pub locales: &'a [String],
    /// Members of `XDG_CURRENT_DESKTOP`; empty means unknown.
    pub desktop_env: &'a [String],
    /// Directory index, lower wins.
    pub priority: u32,
    /// Keep entries with `NoDisplay=true` (used by `newteller-ctl doctor`).
    pub include_hidden: bool,
}


/// Parses desktop file text. Never touches the filesystem.
pub fn parse_raw(text: &str) -> Result<RawDesktopFile> {
    let text = text.strip_prefix('\u{feff}').unwrap_or(text);
    let mut out = RawDesktopFile::default();
    let mut group: Option<String> = None;
    let mut lines = text.lines();

    while let Some(raw_line) = lines.next() {
        let line = raw_line.trim();
        if line.is_empty() || line.starts_with('#') || line.starts_with(';') {
            continue;
        }
        if line.starts_with('[') {
            match line.strip_suffix(']') {
                Some(name) => group = Some(name[1..].trim().to_string()),
                // Malformed header: ignore the rest of the line, keep the group.
                None => group = Some(line.trim_start_matches('[').trim().to_string()),
            }
            continue;
        }

        let Some((key, value)) = line.split_once('=') else {
            continue;
        };
        let key = key.trim().to_string();
        if key.is_empty() {
            continue;
        }
        // Continuation lines: an odd number of trailing backslashes escapes the
        // newline, so the next line belongs to the same value.
        let mut value = value.trim().to_string();
        while trailing_backslashes(&value) % 2 == 1 {
            value.pop();
            match lines.next() {
                Some(next) => value.push_str(next.trim()),
                None => break,
            }
        }

        match group.as_deref() {
            Some("Desktop Entry") => {
                out.entry.insert(key, value);
            }
            Some(other) => {
                if let Some(action) = other.strip_prefix("Desktop Action") {
                    let id = action.trim().to_string();
                    if !id.is_empty() {
                        out.actions.entry(id).or_default().insert(key, value);
                    }
                }
                // Unknown groups are ignored on purpose (e.g. "KDE Desktop Entry").
            }
            None => {
                // Keys before the first group header are invalid; ignore them.
            }
        }
    }

    if out.entry.is_empty() {
        return Err(Error::invalid("no [Desktop Entry] group"));
    }
    Ok(out)
}

/// Builds a [`DesktopEntry`] from parsed data, applying localisation and the
/// visibility rules. Rejects files a launcher must not show with a descriptive
/// error.
pub fn build(raw: &RawDesktopFile, options: &EntryOptions) -> Result<DesktopEntry> {
    if !raw.is_application() {
        return Err(Error::invalid("Type is not Application"));
    }

    let mut entry = DesktopEntry {
        id: options.id.to_string(),
        path: options.path.to_string(),
        priority: options.priority,
        ..DesktopEntry::default()
    };

    entry.hidden = parse_bool(raw.entry.get("Hidden"));
    entry.no_display = parse_bool(raw.entry.get("NoDisplay"));
    entry.terminal = parse_bool(raw.entry.get("Terminal"));
    entry.startup_notify = parse_bool(raw.entry.get("StartupNotify"));
    entry.dbus_activatable = parse_bool(raw.entry.get("DBusActivatable"));

    entry.name = raw
        .get_unescaped("Name", options.locales)
        .unwrap_or_default()
        .trim()
        .to_string();
    entry.generic_name = raw
        .get_unescaped("GenericName", options.locales)
        .unwrap_or_default()
        .trim()
        .to_string();
    entry.comment = raw
        .get_unescaped("Comment", options.locales)
        .unwrap_or_default()
        .trim()
        .to_string();
    entry.exec = raw.plain_unescaped("Exec").trim().to_string();
    entry.icon = raw.plain_unescaped("Icon").trim().to_string();
    entry.try_exec = raw.plain_unescaped("TryExec").trim().to_string();
    entry.startup_wm_class = raw.plain_unescaped("StartupWMClass").trim().to_string();

    entry.categories = split_list(raw.entry.get("Categories").map(|s| s.as_str()).unwrap_or(""));
    entry.keywords = raw
        .get("Keywords", options.locales)
        .map(|value| split_list(&value))
        .unwrap_or_default();
    entry.mime_types = split_list(raw.entry.get("MimeType").map(|s| s.as_str()).unwrap_or(""));
    entry.only_show_in = split_list(raw.entry.get("OnlyShowIn").map(|s| s.as_str()).unwrap_or(""));
    entry.not_show_in = split_list(raw.entry.get("NotShowIn").map(|s| s.as_str()).unwrap_or(""));

    for (id, group) in &raw.actions {
        let Some(name) = RawDesktopFile::action_get(group, "Name", options.locales) else {
            continue;
        };
        let action = DesktopAction {
            id: id.clone(),
            name: unescape(&name).trim().to_string(),
            exec: group.get("Exec").map(|v| unescape(v)).unwrap_or_default().trim().to_string(),
            icon: group.get("Icon").map(|v| unescape(v)).unwrap_or_default().trim().to_string(),
        };
        if !action.name.is_empty() {
            entry.actions.push(action);
        }
    }
    entry.actions.sort_by(|a, b| a.name.cmp(&b.name));

    if entry.name.is_empty() {
        return Err(Error::invalid("empty Name"));
    }
    if entry.exec.is_empty() && !entry.dbus_activatable {
        return Err(Error::invalid("empty Exec"));
    }
    if entry.hidden {
        return Err(Error::invalid("Hidden=true"));
    }
    if entry.no_display && !options.include_hidden {
        return Err(Error::invalid("NoDisplay=true"));
    }
    if !entry.matches_desktop(options.desktop_env) {
        return Err(Error::invalid("not shown in this desktop"));
    }
    if !entry.try_exec.is_empty() && exec::find_in_path(&entry.try_exec).is_none() {
        return Err(Error::invalid(format!("TryExec not found: {}", entry.try_exec)));
    }

    entry.sort_key = util::fold_str(&entry.name);
    Ok(entry)
}

/// Reads and builds one desktop file.
pub fn parse_file(path: &std::path::Path, options: &EntryOptions) -> Result<DesktopEntry> {
    let text = std::fs::read_to_string(path)?;
    let raw = parse_raw(&text)?;
    build(&raw, options)
}

/// Locale fallback chain for a `LANG` value such as `de_DE.UTF-8@euro`.
///
/// Produces `de_DE@euro`, `de_DE`, `de@euro`, `de` - the order required by the
/// desktop entry specification.
pub fn locale_candidates(lang: &str) -> Vec<String> {
    let lang = lang.trim();
    if lang.is_empty() || lang == "C" || lang == "POSIX" || lang.eq_ignore_ascii_case("c.utf8") {
        return Vec::new();
    }
    // language[_COUNTRY][.codeset][@modifier] - the modifier sits after the
    // codeset, so it must be split off first or `de_DE.UTF-8@euro` loses it.
    let (without_modifier, modifier) = match lang.split_once('@') {
        Some((before, after)) => (before, Some(after)),
        None => (lang, None),
    };
    let without_encoding = match without_modifier.split_once('.') {
        Some((before, _)) => before,
        None => without_modifier,
    };
    let (base, modifier) = (without_encoding, modifier);
    let (language, country) = match base.split_once(['_', '-']) {
        Some((before, after)) => (before, Some(after)),
        None => (base, None),
    };
    if language.is_empty() {
        return Vec::new();
    }

    let mut out: Vec<String> = Vec::with_capacity(4);
    if let (Some(country), Some(modifier)) = (country, modifier) {
        out.push(format!("{language}_{country}@{modifier}"));
    }
    if let Some(country) = country {
        out.push(format!("{language}_{country}"));
    }
    if let Some(modifier) = modifier {
        out.push(format!("{language}@{modifier}"));
    }
    out.push(language.to_string());
    out.retain(|candidate| !candidate.is_empty());

    let mut unique: Vec<String> = Vec::with_capacity(out.len());
    for candidate in out {
        if !unique.contains(&candidate) {
            unique.push(candidate);
        }
    }
    unique
}

/// Reads the locale fallback chain from the environment (`LC_ALL`, `LC_MESSAGES`,
/// `LANG` - the first one that is set wins).
pub fn locale_from_env() -> Vec<String> {
    for var in ["LC_ALL", "LC_MESSAGES", "LANG"] {
        if let Ok(value) = std::env::var(var) {
            if !value.trim().is_empty() {
                let candidates = locale_candidates(&value);
                if !candidates.is_empty() {
                    return candidates;
                }
            }
        }
    }
    Vec::new()
}

/// Members of `XDG_CURRENT_DESKTOP`.
pub fn desktop_env_from_env() -> Vec<String> {
    std::env::var("XDG_CURRENT_DESKTOP")
        .map(|value| util::split_env_list(&value))
        .unwrap_or_default()
}

/// `true` for `true`/`1`/`yes`, everything else is `false`.
pub fn parse_bool(value: Option<&String>) -> bool {
    matches!(
        value.map(|v| v.trim()),
        Some("true") | Some("True") | Some("1") | Some("yes")
    )
}

/// Splits a `;` separated list, honouring `\;` escapes and unescaping values.
pub fn split_list(raw: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut current = String::new();
    let mut chars = raw.chars().peekable();
    while let Some(c) = chars.next() {
        match c {
            '\\' => match chars.peek() {
                Some(';') => {
                    current.push(';');
                    chars.next();
                }
                Some('\\') => {
                    current.push('\\');
                    chars.next();
                }
                _ => current.push('\\'),
            },
            ';' => {
                push_item(&mut out, &current);
                current.clear();
            }
            other => current.push(other),
        }
    }
    push_item(&mut out, &current);
    out
}

fn push_item(out: &mut Vec<String>, raw: &str) {
    let item = unescape(raw.trim());
    if !item.is_empty() {
        out.push(item);
    }
}

/// Resolves the escapes defined by the specification: `\s` `\n` `\t` `\r` `\\`
/// `\"` `\'`.
pub fn unescape(raw: &str) -> String {
    let mut out = String::with_capacity(raw.len());
    let mut chars = raw.chars();
    while let Some(c) = chars.next() {
        if c != '\\' {
            out.push(c);
            continue;
        }
        match chars.next() {
            Some('s') => out.push(' '),
            Some('n') => out.push('\n'),
            Some('t') => out.push('\t'),
            Some('r') => out.push('\r'),
            Some('\\') => out.push('\\'),
            Some('"') => out.push('"'),
            Some('\'') => out.push('\''),
            Some(other) => {
                out.push('\\');
                out.push(other);
            }
            None => out.push('\\'),
        }
    }
    out
}

fn trailing_backslashes(value: &str) -> usize {
    let mut count = 0;
    for c in value.chars().rev() {
        if c == '\\' {
            count += 1;
        } else {
            break;
        }
    }
    count
}

#[cfg(test)]
mod tests {
    use super::*;

    const FIREFOX: &str = "\u{feff}[Desktop Entry]\n\
        Version=1.0\n\
        Type=Application\n\
        Name=Firefox ESR\n\
        Name[de]=Firefox ESR (Deutsch)\n\
        GenericName=Web Browser\n\
        Comment=Browse the World Wide Web\n\
        Exec=/usr/lib/firefox-esr/firefox-esr %u\n\
        Icon=firefox-esr\n\
        Terminal=false\n\
        Categories=Network;WebBrowser;\n\
        Keywords[en]=web;browser;internet;\n\
        Keywords=web;browser;\n\
        MimeType=text/html;x-scheme-handler/https;\n\
        StartupNotify=true\n\
        StartupWMClass=Firefox\n\
        Actions=new-window;new-private-window;\n\
        \n\
        [Desktop Action new-window]\n\
        Name=New Window\n\
        Exec=/usr/lib/firefox-esr/firefox-esr --new-window %u\n\
        \n\
        [Desktop Action new-private-window]\n\
        Name=New Private Window\n\
        Exec=/usr/lib/firefox-esr/firefox-esr --private-window %u\n";

    fn options<'a>(id: &'a str, locales: &'a [String], env: &'a [String]) -> EntryOptions<'a> {
        EntryOptions {
            id,
            path: "/usr/share/applications/firefox-esr.desktop",
            locales,
            desktop_env: env,
            priority: 1,
            include_hidden: false,
        }
    }

    #[test]
    fn parses_localised_keys_and_actions() {
        let raw = parse_raw(FIREFOX).expect("parses");
        let en = locale_candidates("en_GB.UTF-8");
        let de = locale_candidates("de_DE.UTF-8@euro");
        assert_eq!(de, vec!["de_DE@euro", "de_DE", "de@euro", "de"]);

        let entry = build(&raw, &options("firefox-esr.desktop", &en, &[])).expect("builds");
        assert_eq!(entry.name, "Firefox ESR");
        assert_eq!(entry.sort_key, "firefox esr");
        assert_eq!(entry.categories, vec!["Network", "WebBrowser"]);
        assert_eq!(entry.keywords, vec!["web", "browser", "internet"]);
        assert_eq!(entry.mime_types, vec!["text/html", "x-scheme-handler/https"]);
        assert_eq!(entry.startup_wm_class, "Firefox");
        assert!(entry.startup_notify);
        assert_eq!(entry.actions.len(), 2);
        assert_eq!(entry.actions[0].name, "New Private Window");
        assert_eq!(entry.program(), "firefox-esr");

        let german = build(&raw, &options("firefox-esr.desktop", &de, &[])).expect("builds");
        assert_eq!(german.name, "Firefox ESR (Deutsch)");
    }

    #[test]
    fn rejects_non_applications_and_hidden_entries() {
        let link = "[Desktop Entry]\nType=Link\nName=Example\nURL=http://example.org\n";
        assert!(parse_raw(link).and_then(|r| build(
            &r,
            &options("example.desktop", &[], &[])
        )).is_err());

        let hidden = "[Desktop Entry]\nType=Application\nName=X\nExec=x\nHidden=true\n";
        let raw = parse_raw(hidden).unwrap();
        assert!(build(&raw, &options("x.desktop", &[], &[])).is_err());

        let nodisplay = "[Desktop Entry]\nType=Application\nName=X\nExec=x\nNoDisplay=true\n";
        let raw = parse_raw(nodisplay).unwrap();
        let mut opts = options("x.desktop", &[], &[]);
        assert!(build(&raw, &opts).is_err());
        opts.include_hidden = true;
        assert!(build(&raw, &opts).is_ok());
    }

    #[test]
    fn honours_only_show_in_and_not_show_in() {
        let text = "[Desktop Entry]\nType=Application\nName=K\nExec=k\nOnlyShowIn=KDE;\n";
        let raw = parse_raw(text).unwrap();
        let kde = vec!["KDE".to_string()];
        let gnome = vec!["GNOME".to_string()];
        assert!(build(&raw, &options("k.desktop", &[], &kde)).is_ok());
        assert!(build(&raw, &options("k.desktop", &[], &gnome)).is_err());
        // Unknown session: do not filter.
        assert!(build(&raw, &options("k.desktop", &[], &[])).is_ok());
    }

    #[test]
    fn handles_continuation_lines_and_escapes() {
        let text = "[Desktop Entry]\n\
                    Type=Application\n\
                    Name=Odd \\\n\
                    Name\n\
                    Exec=echo \"a\\\\b\\s c\"\n\
                    Keywords=a\\;b;c;\n";
        let raw = parse_raw(text).unwrap();
        assert_eq!(raw.entry.get("Name").unwrap(), "Odd Name");
        assert_eq!(split_list(raw.entry.get("Keywords").unwrap()), vec!["a;b", "c"]);
        let entry = build(&raw, &options("odd.desktop", &[], &[])).unwrap();
        // The general string unescape runs before the quoting rule
        // (`\\` -> `\`, `\s` -> a space), so the quoted argument keeps both
        // spaces: `a\\b\s c` -> `a\b  c`.
        assert_eq!(exec::split_command(&entry.exec), vec!["echo", "a\\b  c"]);
    }

    #[test]
    fn locale_candidates_are_unique() {
        assert_eq!(locale_candidates("C"), Vec::<String>::new());
        assert_eq!(locale_candidates("en_US"), vec!["en_US", "en"]);
        assert_eq!(locale_candidates("en_US.UTF-8"), vec!["en_US", "en"]);
    }
}
