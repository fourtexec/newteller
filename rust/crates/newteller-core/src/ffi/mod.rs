//! The C ABI consumed by `src/rustbridge.cpp`.
//!
//! Rules that every function here follows:
//!
//! * **No panic escapes.** Each entry point runs inside `catch_unwind`; a panic
//!   is recorded in the thread local last-error slot and reported as a null
//!   pointer or a negative code, never as unwinding through C++ frames.
//! * **Rust owns every string it returns.** The caller must release it with
//!   [`nt_free`], which is what the C++ `RustString` helper does.
//! * **Pointers may be null.** A null argument is an invalid-input error, not a
//!   crash.
//! * **Handles are opaque.** `NtIndex`, `NtMetrics` and `NtWatcher` are only
//!   ever created by this module and released by the matching `_free` function.
//! * **Raw pointers are turned into references before any closure runs**, so no
//!   closure body needs an `unsafe` block of its own.
//!
//! The header that mirrors these declarations lives in
//! `crates/newteller-core/include/newteller_core.h`. It is hand written, so the
//! `tests/ffi.rs` integration test checks that both sides agree on the ABI
//! version and on the shape of the returned JSON.

use std::cell::RefCell;
use std::collections::{BTreeMap, HashMap};
use std::ffi::{c_char, CStr, CString};
use std::panic::AssertUnwindSafe;
use std::path::Path;

use serde::{Deserialize, Serialize};

use crate::config;
use crate::desktop::exec::ExecContext;
use crate::error::{Error, Result};
use crate::search::{SearchIndex, SearchOptions};
use crate::system::{Monitor, SnapshotOptions};
use crate::util;
use crate::watch::Watcher;

/// Opaque handle to a [`SearchIndex`].
#[derive(Debug)]
pub struct NtIndex(pub(crate) SearchIndex);

/// Opaque handle to a [`Monitor`].
#[derive(Debug)]
pub struct NtMetrics(pub(crate) Monitor);

/// Opaque handle to a [`Watcher`].
#[derive(Debug)]
pub struct NtWatcher(pub(crate) Watcher);

/// Search: allow typos and scattered characters.
pub const SEARCH_FUZZY: u32 = 1;
/// Search: build the highlighted (Qt StyledText) name.
pub const SEARCH_HIGHLIGHT: u32 = 1 << 1;
/// Search: also match `Keywords`.
pub const SEARCH_KEYWORDS: u32 = 1 << 2;
/// Search: also match `GenericName`, `Comment` and `Categories`.
pub const SEARCH_META: u32 = 1 << 3;

/// Write: keep a `.bak` copy of the previous content.
pub const WRITE_BACKUP: u32 = 1;
/// Write: `fsync` the file and its directory.
pub const WRITE_FSYNC: u32 = 1 << 1;
/// Write: create missing parent directories.
pub const WRITE_CREATE_PARENTS: u32 = 1 << 2;

thread_local! {
    static LAST_ERROR: RefCell<String> = RefCell::new(String::new());
}

/// Records the reason of the last failure on this thread.
pub fn set_last_error(message: &str) {
    LAST_ERROR.with(|slot| {
        *slot.borrow_mut() = message.to_string();
    });
}

/// Clears the recorded error.
pub fn clear_last_error() {
    LAST_ERROR.with(|slot| slot.borrow_mut().clear());
}

macro_rules! nt_string {
    ($body:expr) => {{
        let action = $body;
        match std::panic::catch_unwind(AssertUnwindSafe(action)) {
            Ok(Ok(text)) => into_raw(text),
            Ok(Err(error)) => {
                set_last_error(&error.to_string());
                std::ptr::null_mut()
            }
            Err(_) => {
                set_last_error("the Rust core panicked");
                std::ptr::null_mut()
            }
        }
    }};
}

macro_rules! nt_code {
    ($body:expr) => {{
        let action = $body;
        match std::panic::catch_unwind(AssertUnwindSafe(action)) {
            Ok(Ok(())) => 0,
            Ok(Err(error)) => {
                set_last_error(&error.to_string());
                error.code()
            }
            Err(_) => {
                set_last_error("the Rust core panicked");
                -11
            }
        }
    }};
}

/// Turns a Rust string into a caller-owned C string.
fn into_raw(text: String) -> *mut c_char {
    // A NUL inside the payload would silently truncate it. JSON never contains
    // one, but a file name or an Exec line might.
    let cleaned = if text.contains('\0') {
        text.replace('\0', "")
    } else {
        text
    };
    match CString::new(cleaned) {
        Ok(value) => value.into_raw(),
        Err(_) => {
            set_last_error("cannot represent the result as a C string");
            std::ptr::null_mut()
        }
    }
}

/// Serialises a value as JSON, or records the error and returns null.
fn json<T: Serialize>(value: &T) -> *mut c_char {
    match serde_json::to_string(value) {
        Ok(text) => into_raw(text),
        Err(error) => {
            set_last_error(&format!("cannot serialise the result: {error}"));
            std::ptr::null_mut()
        }
    }
}

fn null_pointer<T>() -> *mut T {
    std::ptr::null_mut()
}

/// Borrows a C string as a `&str`.
///
/// # Safety
///
/// `pointer` must be null or point to a NUL terminated UTF-8 string that stays
/// valid for `'a`.
pub unsafe fn borrow<'a>(pointer: *const c_char) -> Result<&'a str> {
    if pointer.is_null() {
        return Err(Error::invalid("null pointer argument"));
    }
    match CStr::from_ptr(pointer).to_str() {
        Ok(text) => Ok(text),
        Err(error) => Err(Error::from(error)),
    }
}

/// Borrows a C string, recording the reason in the last-error slot and falling
/// back to `default` when it is null or not valid UTF-8.
///
/// # Safety
///
/// Same requirements as [`borrow`].
pub unsafe fn borrow_lenient<'a>(pointer: *const c_char, default: &'a str) -> &'a str {
    match borrow(pointer) {
        Ok(text) => text,
        Err(error) => {
            set_last_error(&error.to_string());
            default
        }
    }
}

/// Parses a JSON request, or returns the default when it is null or empty.
fn parse_request<T: serde::de::DeserializeOwned + Default>(text: &str) -> Result<T> {
    if text.trim().is_empty() {
        return Ok(T::default());
    }
    Ok(serde_json::from_str(text)?)
}

/// Request payload of [`nt_exec_argv`].
#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase", default)]
struct ExecRequest {
    exec: String,
    icon: String,
    name: String,
    desktop_file: String,
    files: Vec<String>,
    terminal: bool,
    terminal_command: String,
    extra_env: BTreeMap<String, String>,
}

impl Default for ExecRequest {
    fn default() -> Self {
        ExecRequest {
            exec: String::new(),
            icon: String::new(),
            name: String::new(),
            desktop_file: String::new(),
            files: Vec::new(),
            terminal: false,
            terminal_command: crate::desktop::exec::DEFAULT_TERMINAL_COMMAND.to_string(),
            extra_env: BTreeMap::new(),
        }
    }
}

// ---------------------------------------------------------------------------
// Version and errors
// ---------------------------------------------------------------------------

/// Version of the C ABI implemented by this library.
#[no_mangle]
pub extern "C" fn nt_abi_version() -> u32 {
    crate::ABI_VERSION
}

/// Version of the Rust core.
#[no_mangle]
pub extern "C" fn nt_version() -> *mut c_char {
    nt_string!(|| -> Result<String> { Ok(crate::VERSION.to_string()) })
}

/// Reason of the last failure on this thread, or an empty string.
///
/// The returned string is owned by the caller and must be released with
/// [`nt_free`].
#[no_mangle]
pub extern "C" fn nt_last_error() -> *mut c_char {
    let message = LAST_ERROR.with(|slot| slot.borrow().clone());
    into_raw(message)
}

/// Releases a string returned by this library.
///
/// # Safety
///
/// `pointer` must be null or a pointer returned by one of the `nt_*` functions
/// that return `char *`, and must not be freed twice.
#[no_mangle]
pub unsafe extern "C" fn nt_free(pointer: *mut c_char) {
    if pointer.is_null() {
        return;
    }
    let _ = std::panic::catch_unwind(AssertUnwindSafe(|| unsafe {
        drop(CString::from_raw(pointer));
    }));
}

// ---------------------------------------------------------------------------
// Application catalogue and search
// ---------------------------------------------------------------------------

/// Scans the XDG application directories and returns the report as JSON.
///
/// Prefer [`nt_index_from_scan`], which keeps the result in a handle and avoids
/// a second copy.
///
/// # Safety
///
/// `request_json` must be null or a valid NUL terminated UTF-8 string holding a
/// `ScanRequest` object.
#[no_mangle]
pub unsafe extern "C" fn nt_scan_desktop(request_json: *const c_char) -> *mut c_char {
    let text = borrow_lenient(request_json, "");
    nt_string!(|| -> Result<String> {
        let request = parse_request::<crate::desktop::ScanRequest>(text)?;
        let report = crate::desktop::scan_dirs(&request);
        Ok(serde_json::to_string(&report)?)
    })
}

/// Scans the application directories and builds a search index in one step.
///
/// # Safety
///
/// `request_json` must be null or a valid NUL terminated UTF-8 string holding a
/// `ScanRequest` object. Returns null on failure, see [`nt_last_error`].
#[no_mangle]
pub unsafe extern "C" fn nt_index_from_scan(request_json: *const c_char) -> *mut NtIndex {
    let text = borrow_lenient(request_json, "");
    let outcome = std::panic::catch_unwind(AssertUnwindSafe(|| -> Result<SearchIndex> {
        let request = parse_request::<crate::desktop::ScanRequest>(text)?;
        Ok(SearchIndex::from_scan(&request))
    }));
    match outcome {
        Ok(Ok(index)) => Box::into_raw(Box::new(NtIndex(index))),
        Ok(Err(error)) => {
            set_last_error(&error.to_string());
            null_pointer()
        }
        Err(_) => {
            set_last_error("the Rust core panicked");
            null_pointer()
        }
    }
}

/// Builds an index from a JSON array of desktop entries.
///
/// # Safety
///
/// `apps_json` must be null or a valid NUL terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn nt_index_new(apps_json: *const c_char) -> *mut NtIndex {
    let text = borrow_lenient(apps_json, "");
    let outcome = std::panic::catch_unwind(AssertUnwindSafe(|| -> Result<SearchIndex> {
        SearchIndex::from_json(text)
    }));
    match outcome {
        Ok(Ok(index)) => Box::into_raw(Box::new(NtIndex(index))),
        Ok(Err(error)) => {
            set_last_error(&error.to_string());
            null_pointer()
        }
        Err(_) => {
            set_last_error("the Rust core panicked");
            null_pointer()
        }
    }
}

/// Releases an index.
///
/// # Safety
///
/// `handle` must be null or a pointer returned by [`nt_index_new`] or
/// [`nt_index_from_scan`], and must not be freed twice.
#[no_mangle]
pub unsafe extern "C" fn nt_index_free(handle: *mut NtIndex) {
    if handle.is_null() {
        return;
    }
    drop(Box::from_raw(handle));
}

/// Number of applications in the index, or `-1` for a null handle.
///
/// # Safety
///
/// `handle` must be null or a live index.
#[no_mangle]
pub unsafe extern "C" fn nt_index_len(handle: *const NtIndex) -> isize {
    if handle.is_null() {
        return -1;
    }
    let index = &(*handle).0;
    index.len() as isize
}

/// Every indexed application, as a JSON array of desktop entries.
///
/// # Safety
///
/// `handle` must be null or a live index.
#[no_mangle]
pub unsafe extern "C" fn nt_index_apps(handle: *const NtIndex) -> *mut c_char {
    if handle.is_null() {
        set_last_error("null index handle");
        return null_pointer();
    }
    let index = &(*handle).0;
    json(index.entries())
}

/// Runs a query and returns the result as JSON.
///
/// # Safety
///
/// `handle` must be null or a live index; `query` and `highlight` must be null
/// or valid NUL terminated UTF-8 strings.
#[no_mangle]
pub unsafe extern "C" fn nt_index_search(
    handle: *const NtIndex,
    query: *const c_char,
    limit: u32,
    flags: u32,
    highlight: *const c_char,
) -> *mut c_char {
    if handle.is_null() {
        set_last_error("null index handle");
        return null_pointer();
    }
    let index = &(*handle).0;
    let text = borrow_lenient(query, "");
    let colour = borrow_lenient(highlight, "");
    nt_string!(|| -> Result<String> {
        let options = SearchOptions {
            limit: limit as usize,
            fuzzy: (flags & SEARCH_FUZZY) != 0,
            highlight: if colour.is_empty() || (flags & SEARCH_HIGHLIGHT) == 0 {
                None
            } else {
                Some(colour.to_string())
            },
            match_keywords: (flags & SEARCH_KEYWORDS) != 0,
            match_meta: (flags & SEARCH_META) != 0,
        };
        let result = index.search(text, &options);
        Ok(serde_json::to_string(&result)?)
    })
}

/// Replaces the launch counters used to bias the ranking.
///
/// `usage_json` is a JSON object mapping application ids to counts.
///
/// # Safety
///
/// `handle` must be null or a live index; `usage_json` must be null or valid
/// UTF-8.
#[no_mangle]
pub unsafe extern "C" fn nt_index_set_usage(handle: *mut NtIndex, usage_json: *const c_char) -> i32 {
    if handle.is_null() {
        set_last_error("null index handle");
        return Error::invalid("null index handle").code();
    }
    let index = &mut (*handle).0;
    let text = borrow_lenient(usage_json, "{}");
    nt_code!(|| -> Result<()> {
        let usage: HashMap<String, u32> = parse_request(text)?;
        index.set_usage(usage);
        Ok(())
    })
}

/// Applications that are running right now, as JSON.
///
/// # Safety
///
/// `handle` must be null or a live index.
#[no_mangle]
pub unsafe extern "C" fn nt_index_running(handle: *const NtIndex) -> *mut c_char {
    if handle.is_null() {
        set_last_error("null index handle");
        return null_pointer();
    }
    let index = &(*handle).0;
    nt_string!(|| -> Result<String> { Ok(serde_json::to_string(&index.running())?) })
}

/// Categories and how many applications use them, as JSON.
///
/// # Safety
///
/// `handle` must be null or a live index.
#[no_mangle]
pub unsafe extern "C" fn nt_index_categories(handle: *const NtIndex) -> *mut c_char {
    if handle.is_null() {
        set_last_error("null index handle");
        return null_pointer();
    }
    let index = &(*handle).0;
    nt_string!(|| -> Result<String> { Ok(serde_json::to_string(&index.categories())?) })
}

/// Index size and build cost, as JSON.
///
/// # Safety
///
/// `handle` must be null or a live index.
#[no_mangle]
pub unsafe extern "C" fn nt_index_stats(handle: *const NtIndex) -> *mut c_char {
    if handle.is_null() {
        set_last_error("null index handle");
        return null_pointer();
    }
    let index = &(*handle).0;
    nt_string!(|| -> Result<String> { Ok(serde_json::to_string(&index.stats())?) })
}

/// Report of the scan that built the index, as JSON.
///
/// # Safety
///
/// `handle` must be null or a live index.
#[no_mangle]
pub unsafe extern "C" fn nt_index_scan_report(handle: *const NtIndex) -> *mut c_char {
    if handle.is_null() {
        set_last_error("null index handle");
        return null_pointer();
    }
    let index = &(*handle).0;
    nt_string!(|| -> Result<String> { Ok(serde_json::to_string(index.report())?) })
}

/// Expands an `Exec` line into the argv to start, as a JSON array of strings.
///
/// The request object accepts `exec`, `icon`, `name`, `desktopFile`, `files`,
/// `terminal`, `terminalCommand` and `extraEnv`.
///
/// # Safety
///
/// `request_json` must be null or valid UTF-8.
#[no_mangle]
pub unsafe extern "C" fn nt_exec_argv(request_json: *const c_char) -> *mut c_char {
    let text = borrow_lenient(request_json, "");
    nt_string!(|| -> Result<String> {
        let request: ExecRequest = parse_request(text)?;
        let extra: Vec<(String, String)> = request.extra_env.into_iter().collect();
        let context = ExecContext {
            icon: &request.icon,
            name: &request.name,
            desktop_file: &request.desktop_file,
            files: &request.files,
            terminal: request.terminal,
            terminal_command: &request.terminal_command,
            extra_env: &extra,
        };
        let argv = crate::desktop::exec::expand(&request.exec, &context);
        Ok(serde_json::to_string(&argv)?)
    })
}

/// Scores one candidate against a query, for plugins that want the same ranking
/// as the start menu. Returns the score, or `0` when there is no match.
///
/// # Safety
///
/// Both pointers must be null or valid UTF-8.
#[no_mangle]
pub unsafe extern "C" fn nt_fuzzy_match(query: *const c_char, candidate: *const c_char, flags: u32) -> i32 {
    let query = borrow_lenient(query, "");
    let candidate = borrow_lenient(candidate, "");
    let fuzzy = (flags & SEARCH_FUZZY) != 0;
    let outcome = std::panic::catch_unwind(AssertUnwindSafe(|| -> i32 {
        let parsed = crate::search::score::Query::new(query, fuzzy);
        let normalized = crate::search::normalize::normalize(candidate);
        match crate::search::score::score_in(
            &normalized,
            &parsed,
            crate::search::score::Field::Name,
        ) {
            Some(scored) => scored.score,
            None => 0,
        }
    }));
    match outcome {
        Ok(score) => score,
        Err(_) => {
            set_last_error("the Rust core panicked");
            0
        }
    }
}

// ---------------------------------------------------------------------------
// System metrics
// ---------------------------------------------------------------------------

/// Creates a metrics monitor. The first snapshot reports zero rates because
/// there is nothing to compare it with yet.
#[no_mangle]
pub extern "C" fn nt_metrics_new() -> *mut NtMetrics {
    Box::into_raw(Box::new(NtMetrics(Monitor::new())))
}

/// Releases a metrics monitor.
///
/// # Safety
///
/// `handle` must be null or a pointer returned by [`nt_metrics_new`].
#[no_mangle]
pub unsafe extern "C" fn nt_metrics_free(handle: *mut NtMetrics) {
    if handle.is_null() {
        return;
    }
    drop(Box::from_raw(handle));
}

/// Takes one sample and returns it as JSON. `flags` is a combination of the
/// `NT_METRICS_*` constants of the header.
///
/// # Safety
///
/// `handle` must be null or a live monitor.
#[no_mangle]
pub unsafe extern "C" fn nt_metrics_snapshot(handle: *mut NtMetrics, flags: u32) -> *mut c_char {
    if handle.is_null() {
        set_last_error("null metrics handle");
        return null_pointer();
    }
    let monitor = &mut (*handle).0;
    nt_string!(|| -> Result<String> {
        let options = SnapshotOptions::from_flags(flags);
        monitor.snapshot_json(&options)
    })
}

/// The process list as JSON. `limit` of `0` means every process.
///
/// # Safety
///
/// `handle` may be null; it is only accepted to keep the API symmetrical.
#[no_mangle]
pub unsafe extern "C" fn nt_metrics_processes(handle: *const NtMetrics, limit: u32) -> *mut c_char {
    let _ = handle;
    let maximum = limit as usize;
    nt_string!(|| -> Result<String> {
        Ok(serde_json::to_string(&crate::system::process::list_processes(maximum))?)
    })
}

/// Forgets the previous samples, so the next snapshot reports zero rates again.
/// Call this after the system wakes up from suspend.
///
/// # Safety
///
/// `handle` must be null or a live monitor.
#[no_mangle]
pub unsafe extern "C" fn nt_metrics_reset(handle: *mut NtMetrics) {
    if handle.is_null() {
        return;
    }
    (*handle).0.reset();
}

// ---------------------------------------------------------------------------
// File watching
// ---------------------------------------------------------------------------

/// Creates a non-blocking inotify watcher. Returns null where inotify is
/// unavailable, which the caller must treat as "fall back to
/// `QFileSystemWatcher`".
#[no_mangle]
pub extern "C" fn nt_watcher_new(debounce_ms: u32) -> *mut NtWatcher {
    match Watcher::new(debounce_ms) {
        Ok(watcher) => Box::into_raw(Box::new(NtWatcher(watcher))),
        Err(error) => {
            set_last_error(&error.to_string());
            null_pointer()
        }
    }
}

/// Releases a watcher and closes its descriptor.
///
/// # Safety
///
/// `handle` must be null or a pointer returned by [`nt_watcher_new`].
#[no_mangle]
pub unsafe extern "C" fn nt_watcher_free(handle: *mut NtWatcher) {
    if handle.is_null() {
        return;
    }
    drop(Box::from_raw(handle));
}

/// File descriptor to watch for readability, or `-1`.
///
/// # Safety
///
/// `handle` must be null or a live watcher.
#[no_mangle]
pub unsafe extern "C" fn nt_watcher_fd(handle: *const NtWatcher) -> i32 {
    if handle.is_null() {
        return -1;
    }
    (*handle).0.fd()
}

/// Starts watching a path. Returns `0`, or a negative errno style code.
///
/// # Safety
///
/// `handle` must be null or a live watcher; `path` must be null or valid UTF-8.
#[no_mangle]
pub unsafe extern "C" fn nt_watcher_add(handle: *mut NtWatcher, path: *const c_char, recursive: bool) -> i32 {
    if handle.is_null() {
        set_last_error("null watcher handle");
        return Error::invalid("null watcher handle").code();
    }
    let watcher = &mut (*handle).0;
    let text = borrow_lenient(path, "");
    nt_code!(|| -> Result<()> { watcher.add(Path::new(text), recursive) })
}

/// Stops watching a path. Returns `0`, or a negative errno style code.
///
/// # Safety
///
/// `handle` must be null or a live watcher; `path` must be null or valid UTF-8.
#[no_mangle]
pub unsafe extern "C" fn nt_watcher_remove(handle: *mut NtWatcher, path: *const c_char) -> i32 {
    if handle.is_null() {
        set_last_error("null watcher handle");
        return Error::invalid("null watcher handle").code();
    }
    let watcher = &mut (*handle).0;
    let text = borrow_lenient(path, "");
    nt_code!(|| -> Result<()> { watcher.remove(Path::new(text)) })
}

/// Drains the descriptor and returns the events whose debounce window expired,
/// as a JSON array.
///
/// # Safety
///
/// `handle` must be null or a live watcher.
#[no_mangle]
pub unsafe extern "C" fn nt_watcher_poll(handle: *mut NtWatcher) -> *mut c_char {
    if handle.is_null() {
        set_last_error("null watcher handle");
        return null_pointer();
    }
    let watcher = &mut (*handle).0;
    nt_string!(|| -> Result<String> { Ok(serde_json::to_string(&watcher.poll())?) })
}

/// Milliseconds until the next pending event is ready, `-1` when there is none.
///
/// # Safety
///
/// `handle` must be null or a live watcher.
#[no_mangle]
pub unsafe extern "C" fn nt_watcher_pending_in_ms(handle: *const NtWatcher) -> i64 {
    if handle.is_null() {
        return -1;
    }
    match (*handle).0.next_ready_in() {
        Some(duration) => duration.as_millis() as i64,
        None => -1,
    }
}

// ---------------------------------------------------------------------------
// Files and configuration
// ---------------------------------------------------------------------------

/// Writes a file atomically. `flags` is a combination of the `NT_WRITE_*`
/// constants; `0` selects the safe default (backup, fsync, create parents).
///
/// # Safety
///
/// `path` must be null or valid UTF-8; `data` must be null or valid for `len`
/// bytes.
#[no_mangle]
pub unsafe extern "C" fn nt_write_file_atomic(
    path: *const c_char,
    data: *const u8,
    len: usize,
    flags: u32,
) -> i32 {
    let target = borrow_lenient(path, "");
    if target.is_empty() {
        set_last_error("empty path");
        return Error::invalid("empty path").code();
    }
    let bytes: &[u8] = if data.is_null() || len == 0 {
        &[]
    } else {
        std::slice::from_raw_parts(data, len)
    };
    let options = if flags == 0 {
        config::WriteOptions::default()
    } else {
        config::WriteOptions {
            backup: (flags & WRITE_BACKUP) != 0,
            fsync: (flags & WRITE_FSYNC) != 0,
            create_parents: (flags & WRITE_CREATE_PARENTS) != 0,
            mode: 0o600,
        }
    };
    let destination = Path::new(target);
    nt_code!(|| -> Result<()> { config::write_atomic(destination, bytes, &options) })
}

/// Reads a file. Returns null when it cannot be read.
///
/// # Safety
///
/// `path` must be null or valid UTF-8.
#[no_mangle]
pub unsafe extern "C" fn nt_read_file(path: *const c_char) -> *mut c_char {
    let source = Path::new(borrow_lenient(path, ""));
    nt_string!(|| -> Result<String> {
        let bytes = std::fs::read(source)?;
        Ok(String::from_utf8_lossy(&bytes).into_owned())
    })
}

/// Loads a configuration file, falling back to its backup when the file is
/// missing or corrupt. Returns `{"value":..., "source":..., "error":...}`.
///
/// # Safety
///
/// `path` must be null or valid UTF-8.
#[no_mangle]
pub unsafe extern "C" fn nt_load_config(path: *const c_char) -> *mut c_char {
    let source = Path::new(borrow_lenient(path, ""));
    nt_string!(|| -> Result<String> {
        let loaded = config::load_json_resilient(source);
        Ok(serde_json::to_string(&loaded)?)
    })
}

/// The configuration schema, as a JSON array.
#[no_mangle]
pub extern "C" fn nt_config_schema() -> *mut c_char {
    nt_string!(|| -> Result<String> { Ok(serde_json::to_string(&config::schema())?) })
}

/// The default configuration object, as JSON.
#[no_mangle]
pub extern "C" fn nt_config_defaults() -> *mut c_char {
    nt_string!(|| -> Result<String> {
        Ok(serde_json::to_string(&serde_json::Value::Object(config::defaults()))?)
    })
}

/// Validates a configuration object and returns the problems as JSON.
///
/// # Safety
///
/// `config_json` must be null or valid UTF-8.
#[no_mangle]
pub unsafe extern "C" fn nt_config_validate(config_json: *const c_char) -> *mut c_char {
    let text = borrow_lenient(config_json, "{}");
    nt_string!(|| -> Result<String> {
        let value: serde_json::Value = serde_json::from_str(text)?;
        Ok(serde_json::to_string(&config::validate(&value))?)
    })
}

/// Machine and configuration diagnostics, as pretty printed JSON.
#[no_mangle]
pub extern "C" fn nt_diagnostics() -> *mut c_char {
    nt_string!(|| -> Result<String> { crate::diag::collect_json() })
}

/// Formats a byte count the way the widgets print it, for example `1.5 GiB`.
#[no_mangle]
pub extern "C" fn nt_format_bytes(bytes: u64) -> *mut c_char {
    nt_string!(|| -> Result<String> { Ok(util::format_bytes(bytes)) })
}

/// Formats a throughput, for example `512.0 KiB/s`.
#[no_mangle]
pub extern "C" fn nt_format_rate(bytes_per_second: f64) -> *mut c_char {
    nt_string!(|| -> Result<String> { Ok(crate::system::format_rate(bytes_per_second)) })
}

/// Formats a duration in seconds, for example `1 d 04:05`.
#[no_mangle]
pub extern "C" fn nt_format_duration(seconds: f64) -> *mut c_char {
    nt_string!(|| -> Result<String> { Ok(crate::system::format_duration(seconds)) })
}

/// Expands `~/` in a path setting and returns the absolute path.
///
/// # Safety
///
/// `path` must be null or valid UTF-8.
#[no_mangle]
pub unsafe extern "C" fn nt_resolve_path(path: *const c_char) -> *mut c_char {
    let text = borrow_lenient(path, "");
    nt_string!(|| -> Result<String> { Ok(config::resolved_path_string(text)) })
}
