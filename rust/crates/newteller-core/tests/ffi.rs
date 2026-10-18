//! Integration tests for the C ABI.
//!
//! They call the `extern "C"` functions exactly the way `src/rustbridge.cpp`
//! does, so a signature or JSON change that would break the shell fails here
//! first.

use std::ffi::{c_char, CStr, CString};
use std::ptr;

use newteller_core::ffi::*;

/// Copies a returned string and releases it, like the C++ `RustString` does.
unsafe fn take(pointer: *mut c_char) -> String {
    if pointer.is_null() {
        return String::new();
    }
    let text = CStr::from_ptr(pointer).to_string_lossy().into_owned();
    nt_free(pointer);
    text
}

fn cstr(text: &str) -> CString {
    CString::new(text).expect("no interior NUL")
}

const APPS: &str = r#"[
  {
    "id": "firefox-esr.desktop",
    "path": "/usr/share/applications/firefox-esr.desktop",
    "name": "Firefox ESR",
    "genericName": "Web Browser",
    "comment": "Browse the World Wide Web",
    "exec": "/usr/lib/firefox-esr/firefox-esr %u",
    "icon": "firefox-esr",
    "terminal": false,
    "categories": ["Network", "WebBrowser"],
    "keywords": ["web", "browser", "internet"],
    "startupWMClass": "Firefox",
    "sortKey": "firefox esr"
  },
  {
    "id": "org.gnome.Terminal.desktop",
    "path": "/usr/share/applications/org.gnome.Terminal.desktop",
    "name": "Terminal",
    "comment": "Use the command line",
    "exec": "gnome-terminal",
    "icon": "utilities-terminal",
    "terminal": false,
    "categories": ["System", "TerminalEmulator"],
    "keywords": ["shell", "console", "command"],
    "sortKey": "terminal"
  }
]"#;

#[test]
fn abi_version_matches_the_header() {
    assert_eq!(nt_abi_version(), newteller_core::ABI_VERSION);
    assert_eq!(nt_abi_version(), 1);
}

#[test]
fn version_is_a_releasable_c_string() {
    unsafe {
        let version = take(nt_version());
        assert!(!version.is_empty());
        assert!(version.contains('.'), "{version}");
        // Taking the same pointer twice would be a double free; the wrapper
        // above already released it, so only check that an error slot exists.
        let error = take(nt_last_error());
        let _ = error;
    }
}

#[test]
fn null_arguments_never_crash() {
    unsafe {
        assert!(nt_index_new(ptr::null()).is_null());
        // An empty request scans nothing and yields an empty, valid index.
        let empty_index = nt_index_from_scan(ptr::null());
        assert!(!empty_index.is_null());
        assert_eq!(nt_index_len(empty_index), 0);
        nt_index_free(empty_index);
        assert!(nt_index_apps(ptr::null()).is_null());
        assert!(nt_index_search(ptr::null(), ptr::null(), 10, 0, ptr::null()).is_null());
        assert!(nt_index_running(ptr::null()).is_null());
        assert!(nt_index_categories(ptr::null()).is_null());
        assert!(nt_index_stats(ptr::null()).is_null());
        assert!(nt_index_scan_report(ptr::null()).is_null());
        assert_eq!(nt_index_len(ptr::null()), -1);
        assert!(nt_index_set_usage(ptr::null_mut(), ptr::null()) < 0);
        assert_eq!(nt_watcher_fd(ptr::null()), -1);
        assert_eq!(nt_watcher_pending_in_ms(ptr::null()), -1);
        assert!(nt_watcher_add(ptr::null_mut(), ptr::null(), false) < 0);
        assert!(nt_watcher_remove(ptr::null_mut(), ptr::null()) < 0);
        assert!(nt_watcher_poll(ptr::null_mut()).is_null());
        assert!(nt_metrics_snapshot(ptr::null_mut(), 0).is_null());
        assert!(nt_write_file_atomic(ptr::null(), ptr::null(), 0, 0) < 0);
        assert!(nt_read_file(ptr::null()).is_null());
        assert_eq!(nt_fuzzy_match(ptr::null(), ptr::null(), 0), 0);
        // Releasing null handles is a no-op.
        nt_index_free(ptr::null_mut());
        nt_metrics_free(ptr::null_mut());
        nt_watcher_free(ptr::null_mut());
        nt_free(ptr::null_mut());
    }
}

#[test]
fn invalid_utf8_is_reported_instead_of_crashing() {
    unsafe {
        let bytes: Vec<u8> = vec![0xff, 0xfe, 0x00];
        assert!(nt_index_new(bytes.as_ptr() as *const c_char).is_null());
        let message = take(nt_last_error());
        assert!(!message.is_empty(), "an error must be recorded");
    }
}

#[test]
fn index_round_trip_through_the_c_abi() {
    unsafe {
        let apps = cstr(APPS);
        let index = nt_index_new(apps.as_ptr());
        assert!(!index.is_null(), "{}", take(nt_last_error()));
        assert_eq!(nt_index_len(index), 2);

        let query = cstr("fire");
        let flags = SEARCH_FUZZY | SEARCH_KEYWORDS | SEARCH_META;
        let result = take(nt_index_search(index, query.as_ptr(), 10, flags, ptr::null()));
        assert!(result.contains("\"id\":\"firefox-esr.desktop\""), "{result}");
        assert!(result.contains("\"kind\":\"prefix\""), "{result}");
        assert!(result.contains("\"elapsedMs\""), "{result}");
        assert!(!result.contains("\"highlight\":\""), "no colour, no highlight");

        // A keyword hit is attributed to the right field.
        let query = cstr("console");
        let result = take(nt_index_search(index, query.as_ptr(), 10, flags, ptr::null()));
        assert!(result.contains("org.gnome.Terminal.desktop"), "{result}");
        assert!(result.contains("\"field\":\"keywords\""), "{result}");

        // Highlighting is only produced when asked for.
        let colour = cstr("#ff7a00");
        let query = cstr("fire");
        let result = take(nt_index_search(
            index,
            query.as_ptr(),
            10,
            flags | SEARCH_HIGHLIGHT,
            colour.as_ptr(),
        ));
        assert!(result.contains("<font color=\\\"#ff7a00\\\">"), "{result}");

        let stats = take(nt_index_stats(index));
        assert!(stats.contains("\"apps\":2"), "{stats}");
        assert!(stats.contains("\"memoryBytes\""), "{stats}");

        let categories = take(nt_index_categories(index));
        assert!(categories.contains("\"name\":\"Network\""), "{categories}");

        let listing = take(nt_index_apps(index));
        assert!(listing.contains("firefox-esr.desktop"), "{listing}");

        let usage = cstr("{\"firefox-esr.desktop\":7}");
        assert_eq!(nt_index_set_usage(index, usage.as_ptr()), 0);
        let empty = take(nt_index_search(index, cstr("").as_ptr(), 5, flags, ptr::null()));
        assert!(empty.contains("\"usage\":7"), "{empty}");

        nt_index_free(index);
    }
}

#[test]
fn exec_argv_handles_field_codes_and_quoting() {
    unsafe {
        let request = cstr(
            r#"{"exec":"/usr/bin/foo --name=\"a b\" %u","icon":"foo","name":"Foo",
                "desktopFile":"/usr/share/applications/foo.desktop",
                "files":["/tmp/a b.txt"],"terminal":false}"#,
        );
        let argv = take(nt_exec_argv(request.as_ptr()));
        assert!(argv.starts_with('['), "{argv}");
        assert!(argv.contains("/usr/bin/foo"), "{argv}");
        assert!(argv.contains("file:///tmp/a%20b.txt"), "{argv}");

        let terminal = cstr(
            r#"{"exec":"htop","terminal":true,"terminalCommand":"x-terminal-emulator -e"}"#,
        );
        let argv = take(nt_exec_argv(terminal.as_ptr()));
        assert_eq!(argv, r#"["x-terminal-emulator","-e","htop"]"#);

        // An empty request is not an error.
        let argv = take(nt_exec_argv(ptr::null()));
        assert_eq!(argv, "[]");
    }
}

#[test]
fn fuzzy_match_is_available_to_plugins() {
    unsafe {
        assert!(nt_fuzzy_match(cstr("firefox").as_ptr(), cstr("Firefox ESR").as_ptr(), 0) > 0);
        assert_eq!(
            nt_fuzzy_match(cstr("zzzzzzz").as_ptr(), cstr("Firefox").as_ptr(), 0),
            0
        );
        // Without the fuzzy flag a typo scores nothing.
        assert_eq!(
            nt_fuzzy_match(cstr("terminak").as_ptr(), cstr("Terminal").as_ptr(), 0),
            0
        );
        assert!(
            nt_fuzzy_match(
                cstr("terminak").as_ptr(),
                cstr("Terminal").as_ptr(),
                SEARCH_FUZZY
            ) > 0
        );
    }
}

#[test]
fn metrics_snapshots_are_json_objects() {
    unsafe {
        let metrics = nt_metrics_new();
        assert!(!metrics.is_null());
        // Two samples so that the rates have something to compare against.
        use newteller_core::system::{FLAG_BATTERY, FLAG_PER_CORE};
        let first = take(nt_metrics_snapshot(metrics, FLAG_PER_CORE | FLAG_BATTERY));
        std::thread::sleep(std::time::Duration::from_millis(30));
        let second = take(nt_metrics_snapshot(metrics, 0));
        assert!(first.starts_with('{'), "{first}");
        assert!(second.starts_with('{'), "{second}");
        assert!(second.contains("\"cpu\""), "{second}");
        assert!(second.contains("\"memory\""), "{second}");
        assert!(second.contains("\"network\""), "{second}");
        assert!(second.contains("\"uptimeText\"") || second.contains("\"intervalMs\""));
        nt_metrics_reset(metrics);
        let processes = take(nt_metrics_processes(metrics, 5));
        assert!(processes.starts_with('['), "{processes}");
        nt_metrics_free(metrics);
    }
}

#[test]
fn config_schema_and_defaults_are_consistent() {
    unsafe {
        let schema = take(nt_config_schema());
        assert!(schema.contains("\"key\":\"panel.height\""), "{schema}");
        assert!(schema.contains("\"kind\":\"int\""), "{schema}");

        let defaults = take(nt_config_defaults());
        assert!(defaults.contains("\"panel.height\":48"), "{defaults}");
        assert!(defaults.contains("\"theme.name\":\"default\""), "{defaults}");

        let problems = take(nt_config_validate(
            cstr("{\"panel.height\":4000,\"panel.nonsense\":1}").as_ptr(),
        ));
        assert!(problems.contains("panel.height"), "{problems}");
        assert!(problems.contains("panel.nonsense"), "{problems}");
        assert!(problems.contains("outside"), "{problems}");

        let resolved = take(nt_resolve_path(cstr("~/iconkit/icons").as_ptr()));
        assert!(!resolved.contains('~') || resolved == "~/iconkit/icons");
    }
}

#[test]
fn atomic_write_and_resilient_load_work_over_the_abi() {
    let dir = std::env::temp_dir().join(format!("newteller-ffi-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("shell.json");

    unsafe {
        let c_path = cstr(path.to_str().unwrap());
        let body = cstr("{\"panel.height\":52}");
        assert_eq!(
            nt_write_file_atomic(c_path.as_ptr(), body.as_ptr() as *const u8, body.as_bytes().len(), 0),
            0,
            "{}",
            take(nt_last_error())
        );
        assert_eq!(std::fs::read_to_string(&path).unwrap(), "{\"panel.height\":52}");

        let body = cstr("{\"panel.height\":56}");
        assert_eq!(
            nt_write_file_atomic(c_path.as_ptr(), body.as_ptr() as *const u8, body.as_bytes().len(), 0),
            0
        );
        let read_back = take(nt_read_file(c_path.as_ptr()));
        assert!(read_back.contains("56"), "{read_back}");

        let loaded = take(nt_load_config(c_path.as_ptr()));
        assert!(loaded.contains("\"source\":\"file\""), "{loaded}");

        // Corrupt the file and check that the backup is used.
        std::fs::write(&path, b"{\"panel.hei").unwrap();
        let loaded = take(nt_load_config(c_path.as_ptr()));
        assert!(loaded.contains("\"source\":\"backup\""), "{loaded}");
        assert!(loaded.contains("\"panel.height\":52"), "{loaded}");
    }

    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn watcher_reports_changes_or_is_unavailable() {
    unsafe {
        let watcher = nt_watcher_new(0);
        if watcher.is_null() {
            // Not Linux, or inotify is blocked: the shell must fall back.
            assert!(!take(nt_last_error()).is_empty());
            return;
        }
        assert!(nt_watcher_fd(watcher) >= 0);

        let dir = std::env::temp_dir().join(format!("newteller-ffi-watch-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let c_dir = cstr(dir.to_str().unwrap());
        assert_eq!(nt_watcher_add(watcher, c_dir.as_ptr(), false), 0);

        let file = dir.join("shell.json");
        std::fs::write(&file, b"{}").unwrap();
        let mut seen = String::new();
        for _ in 0..200 {
            seen = take(nt_watcher_poll(watcher));
            if seen.contains("shell.json") {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(10));
        }
        assert!(seen.contains("shell.json"), "no event: {seen}");
        assert!(seen.contains("\"kind\""), "{seen}");

        assert_eq!(nt_watcher_remove(watcher, c_dir.as_ptr()), 0);
        nt_watcher_free(watcher);
        let _ = std::fs::remove_dir_all(&dir);
    }
}

#[test]
fn formatting_helpers_match_the_widgets() {
    unsafe {
        assert_eq!(take(nt_format_bytes(1024)), "1.0 KiB");
        assert_eq!(take(nt_format_bytes(0)), "0 B");
        assert_eq!(take(nt_format_rate(2048.0)), "2.0 KiB/s");
        assert_eq!(take(nt_format_duration(3661.0)), "01:01:01");
    }
}

#[test]
fn diagnostics_is_a_json_object() {
    unsafe {
        let diagnostics = take(nt_diagnostics());
        assert!(diagnostics.starts_with('{'), "{diagnostics}");
        assert!(diagnostics.contains("\"abiVersion\""), "{diagnostics}");
        assert!(diagnostics.contains("\"pluginDirs\""), "{diagnostics}");
        assert!(diagnostics.contains("\"target\""), "{diagnostics}");
    }
}
