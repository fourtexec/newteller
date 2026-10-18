//! Process listing from `/proc/<pid>`.
//!
//! Two flavours are provided because they have very different costs:
//!
//! * [`list_light`] reads `comm`, `cmdline` and the owner of `/proc/<pid>` -
//!   three cheap operations per process, run in parallel. This is what the
//!   "is this pinned application running?" indicator uses, every couple of
//!   seconds.
//! * [`list_processes`] additionally parses `stat` and resolves `exe`, which is
//!   what a task manager needs.
//!
//! Matching on `comm` alone is not enough: the kernel truncates it to 15
//! characters (`gnome-terminal-` for `gnome-terminal-server`), so the first
//! argument of `cmdline` is used as the authoritative program name.

use std::path::Path;

use rayon::prelude::*;
use serde::{Deserialize, Serialize};

use crate::util;

/// Full information about one process.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
#[derive(Default)]
pub struct ProcessInfo {
    /// Process id.
    pub pid: i32,
    /// Parent process id.
    pub ppid: i32,
    /// `comm`, truncated to 15 characters by the kernel.
    pub name: String,
    /// Arguments, as parsed from `cmdline`. Empty for kernel threads.
    pub cmdline: Vec<String>,
    /// Target of `/proc/<pid>/exe`, when readable.
    pub exe: Option<String>,
    /// Effective user id.
    pub uid: u32,
    /// Single letter process state (`R`, `S`, `Z`, ...).
    pub state: String,
    /// Number of threads.
    pub threads: u32,
    /// Resident set size in bytes.
    pub rss_bytes: u64,
    /// Virtual size in bytes.
    pub vsize_bytes: u64,
    /// Start time in clock ticks since boot.
    pub start_ticks: u64,
    /// User time in clock ticks.
    pub utime_ticks: u64,
    /// System time in clock ticks.
    pub stime_ticks: u64,
}


impl ProcessInfo {
    /// The program that was started: `cmdline[0]` without its directory, or
    /// `comm` for kernel threads.
    pub fn program(&self) -> String {
        match self.cmdline.first() {
            Some(first) => util::basename(first).to_string(),
            None => self.name.clone(),
        }
    }
}

/// The cheap subset used for running-application detection.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
#[derive(Default)]
pub struct LightProcess {
    /// Process id.
    pub pid: i32,
    /// `comm`, truncated to 15 characters by the kernel.
    pub name: String,
    /// `cmdline[0]` without its directory; empty for kernel threads.
    pub program: String,
    /// Owner of `/proc/<pid>`.
    pub uid: u32,
}


/// Fields parsed out of `/proc/<pid>/stat`.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct StatFields {
    /// Process id.
    pub pid: i32,
    /// Command name between the parentheses, may contain spaces.
    pub name: String,
    /// Process state letter.
    pub state: String,
    /// Parent process id.
    pub ppid: i32,
    /// User time in ticks.
    pub utime: u64,
    /// System time in ticks.
    pub stime: u64,
    /// Number of threads.
    pub threads: u32,
    /// Start time in ticks since boot.
    pub start_time: u64,
    /// Virtual size in bytes.
    pub vsize: u64,
    /// Resident set size in pages.
    pub rss_pages: u64,
}

/// Parses `/proc/<pid>/stat`.
///
/// The `comm` field is wrapped in parentheses and may itself contain spaces and
/// parentheses, so everything is anchored on the *last* `)`.
pub fn parse_stat(text: &str) -> Option<StatFields> {
    let open = text.find('(')?;
    let close = text.rfind(')')?;
    if close <= open + 1 {
        return None;
    }
    let pid = text[..open].trim().parse::<i32>().ok()?;
    let name = text[open + 1..close].to_string();
    // After the closing parenthesis the fields continue with `state`, which is
    // field 3 of the specification, so index 0 here is field 3.
    let rest: Vec<&str> = text[close + 1..].split_whitespace().collect();
    let field = |index: usize| rest.get(index).copied().unwrap_or("0");

    Some(StatFields {
        pid,
        name,
        state: field(0).to_string(),
        ppid: field(1).parse().unwrap_or(0),
        utime: field(11).parse().unwrap_or(0),
        stime: field(12).parse().unwrap_or(0),
        threads: field(17).parse().unwrap_or(0),
        start_time: field(19).parse().unwrap_or(0),
        vsize: field(20).parse().unwrap_or(0),
        rss_pages: field(21).parse().unwrap_or(0),
    })
}

/// All numeric directory names under `/proc`, sorted ascending.
pub fn pids() -> Vec<i32> {
    let entries = match std::fs::read_dir("/proc") {
        Ok(entries) => entries,
        Err(_) => return Vec::new(),
    };
    let mut pids: Vec<i32> = entries
        .flatten()
        .filter_map(|entry| entry.file_name().to_string_lossy().parse::<i32>().ok())
        .collect();
    pids.sort_unstable();
    pids
}

/// Number of processes, without reading anything but `/proc`.
pub fn count() -> usize {
    match std::fs::read_dir("/proc") {
        Ok(entries) => entries
            .flatten()
            .filter(|entry| {
                entry
                    .file_name()
                    .to_string_lossy()
                    .chars()
                    .next()
                    .map(|c| c.is_ascii_digit())
                    .unwrap_or(false)
            })
            .count(),
        Err(_) => 0,
    }
}

/// Effective user id of this process.
pub fn current_uid() -> u32 {
    #[cfg(unix)]
    {
        unsafe { libc::geteuid() }
    }
    #[cfg(not(unix))]
    {
        0
    }
}

/// Clock ticks per second, used to convert `utime` and `stime`.
pub fn tick_rate() -> f64 {
    #[cfg(unix)]
    {
        let rate = unsafe { libc::sysconf(libc::_SC_CLK_TCK) };
        if rate > 0 {
            return rate as f64;
        }
    }
    100.0
}

/// Page size in bytes, used to convert the `rss` field of `stat`.
pub fn page_size() -> u64 {
    #[cfg(unix)]
    {
        let size = unsafe { libc::sysconf(libc::_SC_PAGESIZE) };
        if size > 0 {
            return size as u64;
        }
    }
    4096
}

/// Cheap process list for the current user.
///
/// Runs in parallel: `/proc` holds hundreds of directories and the reads, not
/// the parsing, dominate. On a non-Linux host this returns an empty list.
pub fn list_light() -> Vec<LightProcess> {
    let uid = current_uid();
    let mut processes: Vec<LightProcess> = pids()
        .par_iter()
        .filter_map(|&pid| read_light(pid))
        .collect();
    // Root sees everything; a normal user only gets its own processes, which is
    // what the panel indicator wants.
    if uid != 0 {
        processes.retain(|process| process.uid == uid || process.uid == 0);
    }
    processes.sort_unstable_by_key(|process| process.pid);
    processes
}

/// Cheap process list without any user filtering, for diagnostics.
pub fn list_light_all() -> Vec<LightProcess> {
    let mut processes: Vec<LightProcess> = pids()
        .par_iter()
        .filter_map(|&pid| read_light(pid))
        .collect();
    processes.sort_unstable_by_key(|process| process.pid);
    processes
}

/// Full process list, sorted by process id. `limit` of `0` means no limit.
pub fn list_processes(limit: usize) -> Vec<ProcessInfo> {
    let mut processes: Vec<ProcessInfo> = pids()
        .par_iter()
        .filter_map(|&pid| read_process(pid))
        .collect();
    processes.sort_unstable_by_key(|process| process.pid);
    if limit > 0 && processes.len() > limit {
        processes.truncate(limit);
    }
    processes
}

/// Reads the cheap subset for one process.
pub fn read_light(pid: i32) -> Option<LightProcess> {
    let dir = format!("/proc/{pid}");
    let mut name = read_trimmed(&format!("{dir}/comm"));
    if name.is_empty() {
        // Zombies and short-lived processes still have a readable `stat`.
        if let Some(text) = util::read_to_string_lossy(Path::new(&format!("{dir}/stat"))) {
            if let Some(fields) = parse_stat(&text) {
                name = fields.name;
            }
        }
    }
    let cmdline = read_cmdline(pid);
    let program = cmdline
        .first()
        .map(|first| util::basename(first).to_string())
        .unwrap_or_default();
    if name.is_empty() && program.is_empty() {
        return None;
    }
    Some(LightProcess {
        pid,
        name,
        program,
        uid: owner_of(&dir),
    })
}

/// Reads the full information for one process.
pub fn read_process(pid: i32) -> Option<ProcessInfo> {
    let dir = format!("/proc/{pid}");
    let stat = util::read_to_string_lossy(Path::new(&format!("{dir}/stat")))?;
    let fields = parse_stat(&stat)?;
    let cmdline = read_cmdline(pid);
    let exe = std::fs::read_link(format!("{dir}/exe"))
        .ok()
        .map(|path| path.to_string_lossy().into_owned());

    Some(ProcessInfo {
        pid,
        ppid: fields.ppid,
        name: fields.name,
        cmdline,
        exe,
        uid: owner_of(&dir),
        state: fields.state,
        threads: fields.threads,
        rss_bytes: fields.rss_pages.saturating_mul(page_size()),
        vsize_bytes: fields.vsize,
        start_ticks: fields.start_time,
        utime_ticks: fields.utime,
        stime_ticks: fields.stime,
    })
}

/// Reads `/proc/<pid>/cmdline`, splitting on the NUL separators.
pub fn read_cmdline(pid: i32) -> Vec<String> {
    let bytes = match std::fs::read(format!("/proc/{pid}/cmdline")) {
        Ok(bytes) => bytes,
        Err(_) => return Vec::new(),
    };
    bytes
        .split(|&byte| byte == 0)
        .map(|part| String::from_utf8_lossy(part).into_owned())
        .filter(|part| !part.is_empty())
        .collect()
}

fn read_trimmed(path: &str) -> String {
    util::read_to_string_lossy(Path::new(path))
        .map(|text| text.trim().to_string())
        .unwrap_or_default()
}

#[cfg(unix)]
fn owner_of(dir: &str) -> u32 {
    use std::os::unix::fs::MetadataExt;
    std::fs::metadata(dir).map(|meta| meta.uid()).unwrap_or(0)
}

#[cfg(not(unix))]
fn owner_of(_dir: &str) -> u32 {
    0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_stat_with_spaces_and_parentheses_in_comm() {
        let line = "4242 (Web Content) S 1234 4242 4242 0 -1 4194560 100 0 1 0 55 6 0 0 20 0 33 0 99999 123456789 4321 18446744073709551615";
        let fields = parse_stat(line).expect("parses");
        assert_eq!(fields.pid, 4242);
        assert_eq!(fields.name, "Web Content");
        assert_eq!(fields.state, "S");
        assert_eq!(fields.ppid, 1234);
        assert_eq!(fields.utime, 55);
        assert_eq!(fields.stime, 6);
        assert_eq!(fields.threads, 33);
        assert_eq!(fields.start_time, 99999);
        assert_eq!(fields.vsize, 123456789);
        assert_eq!(fields.rss_pages, 4321);
    }

    #[test]
    fn rejects_malformed_stat() {
        assert!(parse_stat("no parentheses here").is_none());
        assert!(parse_stat("1 () S 0").is_none());
    }

    #[test]
    fn process_info_falls_back_to_comm_for_kernel_threads() {
        let mut process = ProcessInfo {
            name: "kworker/0:1".to_string(),
            ..ProcessInfo::default()
        };
        assert_eq!(process.program(), "kworker/0:1");
        process.cmdline = vec!["/usr/lib/firefox-esr/firefox-esr".to_string(), "-P".to_string()];
        assert_eq!(process.program(), "firefox-esr");
    }

    #[test]
    fn helpers_are_sane() {
        assert!(tick_rate() > 0.0);
        assert!(page_size() >= 512);
        // The listing itself only makes sense on Linux; everywhere else it is
        // empty rather than an error.
        let pids = pids();
        assert!(pids.windows(2).all(|pair| pair[0] <= pair[1]));
        let light = list_light();
        assert!(light.iter().all(|p| p.pid > 0));
    }
}
