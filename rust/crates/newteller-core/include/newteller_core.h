/*
 * C interface of the NewTeller Rust core (crate `newteller-core`).
 *
 * This header is hand written and mirrors `src/ffi/mod.rs`. `tests/ffi.rs` in
 * the crate checks that the ABI version and the JSON shapes still agree; if you
 * change a signature here, change it there too and bump NT_ABI_VERSION.
 *
 * Ownership rules
 * ---------------
 *  - Every `char *` returned here was allocated by Rust. Release it with
 *    `nt_free()`. Never pass it to `free()` or to a Qt container that would
 *    take ownership.
 *  - `NtIndex`, `NtMetrics` and `NtWatcher` are opaque handles. Create them with
 *    the matching `_new`/`_from_scan` function and release them with `_free`.
 *    They are not thread safe: call them from one thread only (the shell uses
 *    the GUI thread).
 *  - Functions that return `int32_t` return 0 on success and a negative
 *    errno-style code on failure. `nt_last_error()` explains the last failure.
 *  - NULL string arguments are allowed and treated as empty, except where a
 *    function documents otherwise.
 */

#ifndef NEWTELLER_CORE_H
#define NEWTELLER_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped whenever a signature or a JSON payload below changes. */
#define NT_ABI_VERSION 1u

/* Flags for nt_index_search(). */
#define NT_SEARCH_FUZZY 1u      /* allow typos and scattered characters */
#define NT_SEARCH_HIGHLIGHT 2u  /* build the StyledText name (needs a colour) */
#define NT_SEARCH_KEYWORDS 4u   /* also match the Keywords field */
#define NT_SEARCH_META 8u       /* also match GenericName, Comment, Categories */

/* Flags for nt_metrics_snapshot(). */
#define NT_METRICS_DISKS 1u     /* statvfs every real mount (expensive) */
#define NT_METRICS_THERMAL 2u   /* read the thermal zones */
#define NT_METRICS_BATTERY 4u   /* read the power supplies */
#define NT_METRICS_PROCESSES 8u /* count processes */
#define NT_METRICS_UNAME 16u    /* include uname(2) */
#define NT_METRICS_PER_CORE 32u /* include one utilisation value per core */

/* Flags for nt_write_file_atomic(); 0 selects the safe default. */
#define NT_WRITE_BACKUP 1u          /* keep <path>.bak */
#define NT_WRITE_FSYNC 2u           /* fsync the file and its directory */
#define NT_WRITE_CREATE_PARENTS 4u  /* mkdir -p */

typedef struct NtIndex NtIndex;
typedef struct NtMetrics NtMetrics;
typedef struct NtWatcher NtWatcher;

/* ---------------------------------------------------------------- version -- */

uint32_t nt_abi_version(void);
char *nt_version(void);
/* Reason of the last failure on this thread; "" when there was none. */
char *nt_last_error(void);
void nt_free(char *pointer);

/* -------------------------------------------------- catalogue and search -- */

/*
 * ScanRequest as JSON:
 *   { "dirs": ["/usr/share/applications", ...],
 *     "locale": "en_GB.UTF-8", "desktopEnv": "GNOME",
 *     "maxApps": 0, "maxDepth": 3, "includeHidden": false,
 *     "expandHome": true }
 * An empty or NULL request uses the environment and defaults.
 *
 * ScanReport as JSON:
 *   { "apps": [DesktopEntry...], "scanned": 312, "skipped": 40,
 *     "elapsedMs": 12.4, "errors": [{"path": "...", "reason": "..."}],
 *     "locales": ["en_GB", "en"], "desktopEnv": ["GNOME"] }
 *
 * DesktopEntry as JSON (camelCase):
 *   { "id", "path", "name", "genericName", "comment", "exec", "icon",
 *     "terminal", "noDisplay", "hidden", "startupNotify", "startupWMClass",
 *     "categories", "keywords", "mimeTypes", "onlyShowIn", "notShowIn",
 *     "tryExec", "dbusActivatable", "priority", "sortKey",
 *     "actions": [{"id","name","exec","icon"}] }
 */
char *nt_scan_desktop(const char *request_json);

/* Scans and indexes in one step. NULL on failure. */
NtIndex *nt_index_from_scan(const char *request_json);
/* Indexes a JSON array of DesktopEntry objects. NULL on failure. */
NtIndex *nt_index_new(const char *apps_json);
void nt_index_free(NtIndex *index);
intptr_t nt_index_len(const NtIndex *index);
char *nt_index_apps(const NtIndex *index);

/*
 * SearchResult as JSON:
 *   { "hits": [{"id","name","icon","comment","category","score","kind",
 *               "field","highlight","usage"}],
 *     "matched": 12, "considered": 40, "scannedAll": false, "elapsedMs": 0.2 }
 * "kind" is one of exact, prefix, word, acronym, substring, fuzzy, typo.
 * "field" is name, keywords or meta.
 */
char *nt_index_search(const NtIndex *index,
                      const char *query,
                      uint32_t limit,
                      uint32_t flags,
                      const char *highlight_colour);

/* JSON object of desktop file id -> launch count. */
int32_t nt_index_set_usage(NtIndex *index, const char *usage_json);
/* [{"id","count","pids"}] for the applications that are running. */
char *nt_index_running(const NtIndex *index);
/* [{"name","count"}] ordered by popularity. */
char *nt_index_categories(const NtIndex *index);
/* IndexStats as JSON. */
char *nt_index_stats(const NtIndex *index);
/* ScanReport of the scan that built the index. */
char *nt_index_scan_report(const NtIndex *index);

/*
 * ExecRequest as JSON:
 *   { "exec": "/usr/bin/foo -a %u", "icon": "foo", "name": "Foo",
 *     "desktopFile": "/usr/share/applications/foo.desktop",
 *     "files": ["/tmp/a b.txt"], "terminal": false,
 *     "terminalCommand": "x-terminal-emulator -e", "extraEnv": {} }
 * Returns a JSON array of argv strings.
 */
char *nt_exec_argv(const char *request_json);

/* Score of one candidate, same ranking as the index. 0 means no match. */
int32_t nt_fuzzy_match(const char *query, const char *candidate, uint32_t flags);

/* ------------------------------------------------------- system metrics -- */

NtMetrics *nt_metrics_new(void);
void nt_metrics_free(NtMetrics *metrics);

/*
 * Snapshot as JSON:
 *   { "available", "timestampMs", "intervalMs",
 *     "cpu": {"percent","perCore","count","model","frequencyMhz",
 *             "frequencyMaxMhz","load1","load5","load15","loadPerCore",
 *             "uptimeSeconds","uptimeText"},
 *     "memory": {"total","used","available","free","buffers","cached",
 *                "percent","swapTotal","swapUsed","swapPercent","text"},
 *     "network": {"rxRate","txRate","rxTotal","txTotal",
 *                 "interfaces":[{"name","rxBytes","txBytes","rxRate","txRate",
 *                                "loopback"}]},
 *     "battery": {"present","count","percent","status","charging","onBattery",
 *                 "timeToEmptySeconds","timeToFullSeconds","technology",
 *                 "modelName","capacityLevel","critical"},
 *     "thermal": [{"name","celsius","path"}],
 *     "disks": [{"mount","device","fsType","total","used","available",
 *                "percent","totalText","availableText"}],
 *     "uname": {"sysname","nodename","release","version","machine"},
 *     "processes", "userProcesses", "elapsedMs" }
 */
char *nt_metrics_snapshot(NtMetrics *metrics, uint32_t flags);
/* [ProcessInfo] as JSON; limit 0 means every process. */
char *nt_metrics_processes(const NtMetrics *metrics, uint32_t limit);
/* Forget the previous samples; call after resume from suspend. */
void nt_metrics_reset(NtMetrics *metrics);

/* ----------------------------------------------------------- file watch -- */

/* NULL when inotify is unavailable: fall back to QFileSystemWatcher. */
NtWatcher *nt_watcher_new(uint32_t debounce_ms);
void nt_watcher_free(NtWatcher *watcher);
/* Descriptor for QSocketNotifier; -1 when unavailable. */
int32_t nt_watcher_fd(const NtWatcher *watcher);
int32_t nt_watcher_add(NtWatcher *watcher, const char *path, bool recursive);
int32_t nt_watcher_remove(NtWatcher *watcher, const char *path);
/* [{"path","kind","isDir","mask"}] for the events that are ready. */
char *nt_watcher_poll(NtWatcher *watcher);
/* Milliseconds until the next event is ready, -1 when none is pending. */
int64_t nt_watcher_pending_in_ms(const NtWatcher *watcher);

/* ----------------------------------------------- files and configuration -- */

int32_t nt_write_file_atomic(const char *path, const uint8_t *data, size_t len, uint32_t flags);
char *nt_read_file(const char *path);
/* {"value": {...}, "source": "file"|"backup"|"defaults", "error": null|string} */
char *nt_load_config(const char *path);
/* [{"key","kind","default","doc"}] */
char *nt_config_schema(void);
/* The default configuration object. */
char *nt_config_defaults(void);
/* [{"key","level","message"}] */
char *nt_config_validate(const char *config_json);
/* Pretty printed Diagnostics object. */
char *nt_diagnostics(void);
char *nt_resolve_path(const char *path);

/* Formatting helpers, so C++ and the CLI print numbers identically. */
char *nt_format_bytes(uint64_t bytes);
char *nt_format_rate(double bytes_per_second);
char *nt_format_duration(double seconds);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NEWTELLER_CORE_H */
