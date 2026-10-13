//! Debounced inotify watching for the configuration file, the theme directory
//! and the plugin directories.
//!
//! `QFileSystemWatcher` stops working when an editor replaces a file (write to a
//! temporary file, then rename over it), which is what most editors do; the old
//! `Settings` class had to re-add the path by hand after every change. Watching
//! the *directory* with inotify fixes that for good, and debouncing means one
//! burst of editor writes produces one reload instead of five.
//!
//! The watcher is non-blocking and exposes its file descriptor, so the C++ side
//! drives it from a `QSocketNotifier` inside the Qt event loop. No thread, no
//! polling, no timer.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use serde::Serialize;

use crate::error::{Error, Result};

/// inotify mask bits.
///
/// Kernel ABI, stable since 2.6.13. They are mirrored here instead of taken from
/// `libc` so that the decoding and the tests also compile on hosts where the
/// `libc` dependency is not linked in.
pub mod mask {
    /// `IN_MODIFY`
    pub const MODIFY: u32 = 0x0000_0002;
    /// `IN_ATTRIB`
    pub const ATTRIB: u32 = 0x0000_0004;
    /// `IN_CLOSE_WRITE`
    pub const CLOSE_WRITE: u32 = 0x0000_0008;
    /// `IN_MOVED_FROM`
    pub const MOVED_FROM: u32 = 0x0000_0040;
    /// `IN_MOVED_TO`
    pub const MOVED_TO: u32 = 0x0000_0080;
    /// `IN_CREATE`
    pub const CREATE: u32 = 0x0000_0100;
    /// `IN_DELETE`
    pub const DELETE: u32 = 0x0000_0200;
    /// `IN_DELETE_SELF`
    pub const DELETE_SELF: u32 = 0x0000_0400;
    /// `IN_MOVE_SELF`
    pub const MOVE_SELF: u32 = 0x0000_0800;
    /// `IN_Q_OVERFLOW`
    pub const QUEUE_OVERFLOW: u32 = 0x0000_4000;
    /// `IN_IGNORED`
    pub const IGNORED: u32 = 0x0000_8000;
    /// `IN_ISDIR`
    pub const IS_DIR: u32 = 0x4000_0000;

    /// Everything the watcher subscribes to.
    pub const WATCHED: u32 = MODIFY
        | ATTRIB
        | CLOSE_WRITE
        | MOVED_FROM
        | MOVED_TO
        | CREATE
        | DELETE
        | DELETE_SELF
        | MOVE_SELF;

    /// The watched path itself disappeared or was moved.
    pub const SELF: u32 = DELETE_SELF | MOVE_SELF;
    /// A new entry appeared.
    pub const CREATED: u32 = CREATE | MOVED_TO;
    /// An entry disappeared.
    pub const REMOVED: u32 = DELETE | MOVED_FROM;
    /// The content of an entry changed.
    pub const WRITTEN: u32 = MODIFY | CLOSE_WRITE;
}

/// Size of the buffer used for one `read` of the inotify descriptor.
const READ_BUFFER: usize = 16 * 1024;

/// Never keep more pending paths than this; a runaway directory would otherwise
/// grow the map without bound.
const MAX_PENDING: usize = 512;

/// Deepest sub-directory level followed by a recursive watch.
const MAX_RECURSION: usize = 4;

/// Size of the fixed part of an inotify event.
const EVENT_HEADER: usize = 16;

/// What happened to a path.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WatchEvent {
    /// Absolute path of the file or directory that changed.
    pub path: String,
    /// Coarse kind: `write`, `create`, `delete`, `attribute`, `self`, `ignored`.
    pub kind: String,
    /// True when the path is a directory.
    pub is_dir: bool,
    /// Raw inotify mask, for callers that need the exact event.
    pub mask: u32,
}

/// A non-blocking, debounced inotify watcher.
#[derive(Debug)]
pub struct Watcher {
    fd: i32,
    debounce: Duration,
    /// inotify watch descriptor -> watched directory or file.
    watched: BTreeMap<i32, PathBuf>,
    /// Watched root -> whether sub-directories are followed.
    recursive: BTreeMap<PathBuf, bool>,
    /// Pending events waiting for their debounce window to expire.
    pending: BTreeMap<PathBuf, Pending>,
    buffer: Vec<u8>,
}

#[derive(Debug, Clone)]
struct Pending {
    mask: u32,
    is_dir: bool,
    last: Instant,
}

impl Watcher {
    /// Creates a watcher. `debounce_ms` of `0` reports every event immediately.
    pub fn new(debounce_ms: u32) -> Result<Watcher> {
        Self::with_capacity(debounce_ms, READ_BUFFER)
    }

    /// Creates a watcher with an explicit read buffer size; used by the tests.
    pub fn with_capacity(debounce_ms: u32, buffer_size: usize) -> Result<Watcher> {
        #[cfg(unix)]
        {
            let fd = unsafe { libc::inotify_init1(libc::IN_NONBLOCK | libc::IN_CLOEXEC) };
            if fd < 0 {
                return Err(Error::Io(std::io::Error::last_os_error()));
            }
            Ok(Watcher {
                fd,
                debounce: Duration::from_millis(debounce_ms as u64),
                watched: BTreeMap::new(),
                recursive: BTreeMap::new(),
                pending: BTreeMap::new(),
                buffer: vec![0u8; buffer_size.max(EVENT_HEADER * 4)],
            })
        }
        #[cfg(not(unix))]
        {
            let _ = (debounce_ms, buffer_size);
            Err(Error::Unsupported("inotify is only available on Linux"))
        }
    }

    /// The file descriptor to hand to `QSocketNotifier`. `-1` when unavailable.
    pub fn fd(&self) -> i32 {
        self.fd
    }

    /// Debounce window.
    pub fn debounce(&self) -> Duration {
        self.debounce
    }

    /// Number of active inotify watches.
    pub fn watch_count(&self) -> usize {
        self.watched.len()
    }

    /// Starts watching `path`.
    ///
    /// Directories are watched for changes to their entries, which is what
    /// survives an editor replacing a file. With `recursive`, sub-directories are
    /// added as well, and new sub-directories are picked up automatically.
    pub fn add(&mut self, path: &Path, recursive: bool) -> Result<()> {
        self.add_one(path)?;
        self.recursive.insert(path.to_path_buf(), recursive);
        if recursive && path.is_dir() {
            self.add_tree(path, 0)?;
        }
        Ok(())
    }

    /// Stops watching `path` and, when it was recursive, its sub-directories.
    pub fn remove(&mut self, path: &Path) -> Result<()> {
        self.recursive.remove(path);
        let doomed: Vec<i32> = self
            .watched
            .iter()
            .filter(|(_, watched)| watched.as_path() == path || watched.starts_with(path))
            .map(|(descriptor, _)| *descriptor)
            .collect();
        for descriptor in doomed {
            self.drop_watch(descriptor);
        }
        Ok(())
    }

    /// Reads every available inotify event and returns the ones whose debounce
    /// window has expired.
    ///
    /// Non-blocking: returns an empty list when nothing is ready.
    pub fn poll(&mut self) -> Vec<WatchEvent> {
        self.read_events();
        self.take_ready()
    }

    /// Returns the events that are already ready, without reading the descriptor.
    pub fn take_ready(&mut self) -> Vec<WatchEvent> {
        let mut ready: Vec<WatchEvent> = Vec::new();
        if self.pending.is_empty() {
            return ready;
        }
        let now = Instant::now();
        let done: Vec<PathBuf> = self
            .pending
            .iter()
            .filter(|(_, pending)| now.duration_since(pending.last) >= self.debounce)
            .map(|(path, _)| path.clone())
            .collect();
        for path in done {
            if let Some(pending) = self.pending.remove(&path) {
                ready.push(WatchEvent {
                    path: path.to_string_lossy().into_owned(),
                    kind: kind_of(pending.mask),
                    is_dir: pending.is_dir,
                    mask: pending.mask,
                });
            }
        }
        ready
    }

    /// True when at least one event is waiting for its debounce window.
    pub fn has_pending(&self) -> bool {
        !self.pending.is_empty()
    }

    /// How long until the next pending event becomes ready.
    pub fn next_ready_in(&self) -> Option<Duration> {
        self.pending
            .values()
            .map(|pending| {
                pending
                    .last
                    .checked_add(self.debounce)
                    .map(|at| at.saturating_duration_since(Instant::now()))
                    .unwrap_or_default()
            })
            .min()
    }

    /// Paths currently watched.
    pub fn watched_paths(&self) -> Vec<String> {
        self.watched
            .values()
            .map(|path| path.to_string_lossy().into_owned())
            .collect()
    }

    /// Drops every pending event, for example after a manual reload.
    pub fn clear_pending(&mut self) {
        self.pending.clear();
    }

    #[cfg(unix)]
    fn add_one(&mut self, path: &Path) -> Result<()> {
        use std::ffi::CString;
        use std::os::unix::ffi::OsStrExt;

        let raw = CString::new(path.as_os_str().as_bytes()).map_err(|_| {
            Error::invalid(format!("path contains a NUL byte: {}", path.display()))
        })?;
        let descriptor = unsafe { libc::inotify_add_watch(self.fd, raw.as_ptr(), mask::WATCHED) };
        if descriptor < 0 {
            return Err(Error::Io(std::io::Error::last_os_error()));
        }
        self.watched.insert(descriptor, path.to_path_buf());
        Ok(())
    }

    #[cfg(not(unix))]
    fn add_one(&mut self, path: &Path) -> Result<()> {
        let _ = path;
        Err(Error::Unsupported("inotify is only available on Linux"))
    }

    #[cfg(unix)]
    fn add_tree(&mut self, root: &Path, depth: usize) -> Result<()> {
        if depth >= MAX_RECURSION {
            return Ok(());
        }
        let entries = match std::fs::read_dir(root) {
            Ok(entries) => entries,
            Err(_) => return Ok(()),
        };
        for entry in entries.flatten() {
            let path = entry.path();
            if !path.is_dir() {
                continue;
            }
            if self.add_one(&path).is_ok() {
                self.add_tree(&path, depth + 1)?;
            }
        }
        Ok(())
    }

    #[cfg(not(unix))]
    fn add_tree(&mut self, root: &Path, depth: usize) -> Result<()> {
        let _ = (root, depth);
        Ok(())
    }

    #[cfg(unix)]
    fn drop_watch(&mut self, descriptor: i32) {
        if let Some(path) = self.watched.remove(&descriptor) {
            self.recursive.remove(&path);
            // Fails harmlessly when the kernel already dropped the watch.
            unsafe {
                libc::inotify_rm_watch(self.fd, descriptor);
            }
        }
    }

    #[cfg(not(unix))]
    fn drop_watch(&mut self, descriptor: i32) {
        let _ = descriptor;
    }

    #[cfg(unix)]
    fn read_events(&mut self) {
        // The buffer is taken out of `self` so that `decode`, which needs
        // `&mut self`, can borrow the bytes without a conflict.
        let mut buffer = std::mem::take(&mut self.buffer);
        if buffer.is_empty() {
            buffer.resize(READ_BUFFER, 0);
        }
        loop {
            let read = unsafe {
                libc::read(
                    self.fd,
                    buffer.as_mut_ptr() as *mut libc::c_void,
                    buffer.len(),
                )
            };
            if read <= 0 {
                // EAGAIN means "queue empty"; anything else is not worth
                // spinning on and the descriptor stays usable.
                break;
            }
            let count = read as usize;
            self.decode(&buffer[..count]);
            if count < buffer.len() {
                // A short read means the queue was drained.
                break;
            }
        }
        self.buffer = buffer;
    }

    #[cfg(not(unix))]
    fn read_events(&mut self) {}

    /// Decodes inotify events without unaligned pointer casts: the fixed part is
    /// 16 bytes and every field is read with `from_ne_bytes`.
    fn decode(&mut self, bytes: &[u8]) {
        let mut offset = 0usize;
        while offset + EVENT_HEADER <= bytes.len() {
            let descriptor = i32::from_ne_bytes(slice4(bytes, offset));
            let event_mask = u32::from_ne_bytes(slice4(bytes, offset + 4));
            // The cookie links IN_MOVED_FROM to IN_MOVED_TO; the debounce map
            // already merges both into one path, so it is not needed here.
            let _cookie = u32::from_ne_bytes(slice4(bytes, offset + 8));
            let name_len = u32::from_ne_bytes(slice4(bytes, offset + 12)) as usize;
            let name_end = (offset + EVENT_HEADER + name_len).min(bytes.len());
            if name_end < offset + EVENT_HEADER {
                return;
            }
            let name = String::from_utf8_lossy(trim_nul(&bytes[offset + EVENT_HEADER..name_end]))
                .into_owned();
            offset = name_end;

            if (event_mask & mask::IGNORED) != 0 {
                self.watched.remove(&descriptor);
                continue;
            }
            if (event_mask & mask::QUEUE_OVERFLOW) != 0 {
                // The kernel dropped events: report every watched path so the
                // shell reloads everything instead of trusting a partial view.
                for path in self.watched.values().cloned().collect::<Vec<_>>() {
                    self.push_pending(path, mask::MODIFY, false);
                }
                continue;
            }
            let watched = match self.watched.get(&descriptor) {
                Some(path) => path.clone(),
                None => continue,
            };
            let is_dir = (event_mask & mask::IS_DIR) != 0;
            let path = if name.is_empty() {
                watched.clone()
            } else {
                watched.join(&name)
            };

            if (event_mask & mask::SELF) != 0 {
                // The watched path itself went away. Drop the dead watch and,
                // for an editor rename, start watching the new file right away.
                self.drop_watch(descriptor);
                if path.exists() {
                    let _ = self.add_one(&path);
                }
            } else if is_dir
                && (event_mask & mask::CREATED) != 0
                && self.is_recursive(&watched)
            {
                let _ = self.add_one(&path);
                let _ = self.add_tree(&path, 0);
            }

            self.push_pending(path, event_mask, is_dir);
        }
    }

    fn push_pending(&mut self, path: PathBuf, event_mask: u32, is_dir: bool) {
        if self.pending.len() >= MAX_PENDING && !self.pending.contains_key(&path) {
            // Drop the oldest pending event instead of growing forever.
            if let Some(oldest) = self
                .pending
                .iter()
                .min_by_key(|(_, pending)| pending.last)
                .map(|(path, _)| path.clone())
            {
                self.pending.remove(&oldest);
            }
        }
        match self.pending.get_mut(&path) {
            Some(pending) => {
                pending.mask |= event_mask;
                pending.is_dir = pending.is_dir || is_dir;
                pending.last = Instant::now();
            }
            None => {
                self.pending.insert(
                    path,
                    Pending {
                        mask: event_mask,
                        is_dir,
                        last: Instant::now(),
                    },
                );
            }
        }
    }

    fn is_recursive(&self, dir: &Path) -> bool {
        self.recursive
            .iter()
            .any(|(root, recursive)| *recursive && dir.starts_with(root))
    }
}

impl Drop for Watcher {
    fn drop(&mut self) {
        #[cfg(unix)]
        {
            if self.fd >= 0 {
                unsafe {
                    libc::close(self.fd);
                }
            }
        }
    }
}

fn slice4(bytes: &[u8], offset: usize) -> [u8; 4] {
    let mut out = [0u8; 4];
    out.copy_from_slice(&bytes[offset..offset + 4]);
    out
}

fn trim_nul(bytes: &[u8]) -> &[u8] {
    match bytes.iter().position(|&byte| byte == 0) {
        Some(index) => &bytes[..index],
        None => bytes,
    }
}

/// Maps an inotify mask to a coarse, stable kind name.
pub fn kind_of(event_mask: u32) -> String {
    if (event_mask & mask::IGNORED) != 0 {
        return "ignored".to_string();
    }
    if (event_mask & mask::QUEUE_OVERFLOW) != 0 {
        return "overflow".to_string();
    }
    if (event_mask & mask::SELF) != 0 {
        return "self".to_string();
    }
    if (event_mask & mask::CREATED) != 0 {
        return "create".to_string();
    }
    if (event_mask & mask::REMOVED) != 0 {
        return "delete".to_string();
    }
    if (event_mask & mask::WRITTEN) != 0 {
        return "write".to_string();
    }
    if (event_mask & mask::ATTRIB) != 0 {
        return "attribute".to_string();
    }
    "unknown".to_string()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    fn inotify_available() -> bool {
        Watcher::new(0).is_ok()
    }

    fn temp_root(tag: &str) -> PathBuf {
        let root = std::env::temp_dir().join(format!("newteller-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        std::fs::create_dir_all(&root).unwrap();
        root
    }

    fn wait_for(watcher: &mut Watcher, wanted: &str) -> Vec<WatchEvent> {
        let mut events = Vec::new();
        for _ in 0..200 {
            events.extend(watcher.poll());
            if events.iter().any(|event| event.path.ends_with(wanted)) {
                break;
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        events
    }

    #[test]
    fn kind_names_are_stable() {
        assert_eq!(kind_of(mask::MODIFY), "write");
        assert_eq!(kind_of(mask::CLOSE_WRITE), "write");
        assert_eq!(kind_of(mask::CREATE), "create");
        assert_eq!(kind_of(mask::MOVED_TO), "create");
        assert_eq!(kind_of(mask::DELETE), "delete");
        assert_eq!(kind_of(mask::MOVED_FROM), "delete");
        assert_eq!(kind_of(mask::ATTRIB), "attribute");
        assert_eq!(kind_of(mask::MOVE_SELF), "self");
        assert_eq!(kind_of(mask::IGNORED), "ignored");
        assert_eq!(kind_of(0), "unknown");
    }

    #[test]
    fn mask_values_match_the_kernel_abi() {
        assert_eq!(mask::IS_DIR, 0x4000_0000);
        assert_eq!(mask::SELF, mask::DELETE_SELF | mask::MOVE_SELF);
        assert_eq!(mask::CREATED, mask::CREATE | mask::MOVED_TO);
        assert_ne!(mask::WATCHED & mask::IS_DIR, mask::IS_DIR);
    }

    #[test]
    fn sees_plain_writes_and_editor_style_replacements() {
        if !inotify_available() {
            return;
        }
        let root = temp_root("watch");
        let file = root.join("shell.json");
        std::fs::write(&file, b"{}").unwrap();

        let mut watcher = Watcher::new(0).expect("inotify");
        watcher.add(&root, false).expect("watch");
        assert_eq!(watcher.watch_count(), 1);
        assert!(watcher.fd() >= 0);

        let mut handle = std::fs::OpenOptions::new().append(true).open(&file).unwrap();
        handle.write_all(b"\n").unwrap();
        drop(handle);
        let events = wait_for(&mut watcher, "shell.json");
        assert!(
            events.iter().any(|event| event.path.ends_with("shell.json")),
            "expected an event for shell.json, got {events:?}"
        );

        // Write to a temporary file and rename it over the original: the classic
        // case QFileSystemWatcher misses because the inode it watched is gone.
        watcher.clear_pending();
        let temp = root.join("shell.json.tmp");
        std::fs::write(&temp, b"{\"a\":1}").unwrap();
        std::fs::rename(&temp, &file).unwrap();
        let events = wait_for(&mut watcher, "shell.json");
        assert!(
            events.iter().any(|event| event.path.ends_with("shell.json")),
            "expected an event after the rename, got {events:?}"
        );

        watcher.remove(&root).unwrap();
        assert_eq!(watcher.watch_count(), 0);
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn follows_new_subdirectories_when_recursive() {
        if !inotify_available() {
            return;
        }
        let root = temp_root("watch-recursive");
        let mut watcher = Watcher::new(0).expect("inotify");
        watcher.add(&root, true).expect("watch");

        let nested = root.join("plugins").join("clock");
        std::fs::create_dir_all(&nested).unwrap();
        let _ = wait_for(&mut watcher, "plugins");
        // Two new directories must have been picked up.
        assert!(watcher.watch_count() >= 3, "{:?}", watcher.watched_paths());

        std::fs::write(nested.join("manifest.json"), b"{}").unwrap();
        let events = wait_for(&mut watcher, "manifest.json");
        assert!(
            events.iter().any(|event| event.path.ends_with("manifest.json")),
            "expected an event from the new sub-directory, got {events:?}"
        );
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn debounce_merges_a_burst_of_writes() {
        if !inotify_available() {
            return;
        }
        let root = temp_root("watch-debounce");
        let file = root.join("shell.json");
        std::fs::write(&file, b"{}").unwrap();

        let mut watcher = Watcher::new(300).expect("inotify");
        watcher.add(&root, false).unwrap();
        for index in 0..5 {
            std::fs::write(&file, format!("{{\"a\":{index}}}").as_bytes()).unwrap();
        }

        // The window has not expired yet, so nothing may be reported even though
        // five writes already reached the kernel.
        assert!(watcher.poll().is_empty(), "nothing may be ready yet");
        assert!(watcher.has_pending());
        assert!(watcher.next_ready_in().unwrap_or_default() <= Duration::from_millis(300));

        std::thread::sleep(Duration::from_millis(400));
        let seen = watcher.poll();
        assert_eq!(seen.len(), 1, "one path, one event: {seen:?}");
        assert!(seen[0].path.ends_with("shell.json"));
        assert!(watcher.poll().is_empty(), "the burst must not repeat");
        let _ = std::fs::remove_dir_all(&root);
    }
}
