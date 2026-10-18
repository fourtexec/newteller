# NewTeller — Rust integration handoff

Status snapshot for the next session. Branch: `arena/a2300a50-newteller`.
Constraint from the user: **do not build anything** (no `cmake`, no `cargo`, no
compiles). Code must be written to be correct by inspection.

---

## 1. What already exists (do not rewrite, only extend)

### Rust workspace — `rust/` (27 files, ~9.5k lines, untracked in git)

```
rust/Cargo.toml                          workspace: newteller-core + newteller-ctl, v0.2.0, rust-version 1.75
rust/crates/newteller-core/
  Cargo.toml                             rlib + staticlib, libc only on cfg(unix), feature "no-procfs"
  build.rs                               emits cargo:rustc-env=TARGET
  include/newteller_core.h               the C ABI (NT_ABI_VERSION 1) — read this first
  src/lib.rs error.rs util.rs diag.rs
  src/desktop/{mod,exec,scan}.rs         .desktop parsing, Exec quoting/field codes, parallel scan (rayon)
  src/search/{mod,normalize,score}.rs    trigram + token index, tiered fuzzy ranking, StyledText highlight
  src/system/{mod,procfs,power,disk,process}.rs   /proc metrics, battery, thermal, disks, process list
  src/watch/mod.rs                       inotify wrapper with per-path debounce
  src/config/mod.rs                      43-key schema, atomic write + .bak, resilient load, validate
  src/ffi/mod.rs                         every C entry point, all panic-guarded, thread-local last error
  tests/ffi.rs
rust/crates/newteller-ctl/
  Cargo.toml src/main.rs src/args.rs src/output.rs src/commands.rs   (complete: paths schema list get set
                                                                     unset validate init themes plugins apps
                                                                     search categories running exec bench
                                                                     metrics watch doctor version)
```

Still missing in `rust/`: `rust/README.md` (crate docs, feature flags, ABI rules).

### C ABI (all strings are `char *` freed with `nt_free`; all payloads are JSON)

`nt_abi_version, nt_version, nt_last_error, nt_free, nt_scan_desktop(request_json),
nt_index_from_scan(request_json), nt_index_new(apps_json), nt_index_free, nt_index_len,
nt_index_apps, nt_index_search(idx, query, limit, flags, highlight_colour),
nt_index_set_usage(idx, usage_json), nt_index_running, nt_index_categories, nt_index_stats,
nt_index_scan_report, nt_exec_argv(request_json), nt_fuzzy_match(q, cand, flags),
nt_metrics_new/free/snapshot(m, flags)/processes(m, limit)/reset, nt_watcher_new(debounce_ms)/
free/fd/add/remove/poll/pending_in_ms, nt_write_file_atomic(path, data, len, flags), nt_read_file,
nt_load_config, nt_config_schema, nt_config_defaults, nt_config_validate(json), nt_diagnostics,
nt_resolve_path, nt_format_bytes/rate/duration`.

Flags: `NT_SEARCH_FUZZY 1 | HIGHLIGHT 2 | KEYWORDS 4 | META 8`;
`NT_WRITE_BACKUP 1 | FSYNC 2 | CREATE_PARENTS 4`.
Metrics flags: `DISKS 1 | THERMAL 2 | BATTERY 4 | PROCESSES 8 | UNAME 16 | PER_CORE 32`.
JSON payloads are camelCase; `startupWMClass` is an explicit `serde(rename)`.

### C++ — finished in this session

| File | State |
|---|---|
| `src/appentry.h` | **new**. `AppEntry` value type shared by `AppModel` and the bridge (kept separate to avoid an include cycle). Adds `path, genericName, tryExec, mimeTypes, onlyShowIn, notShowIn, actions, noDisplay, hidden, startupNotify, dbusActivatable, startupWmClass, sortKey, priority` and `program()`. |
| `src/rustbridge.h/.cpp` | **new**, complete. `namespace Rust`: `available() version() lastError()`, `ScanOptions`, `AppAction`, `Index` (scan/build/count/entries/actionsOf/search/setUsage/running/runningIds/categories/stats/report/execArgv), `Metrics` (snapshot/processes/reset + flag constants), `Watcher` (descriptor/add/remove/poll/pendingInMs), `writeFileAtomic readFile loadConfig configSchema configDefaults validateConfig diagnostics resolvePath formatBytes formatRate formatDuration fuzzyMatch`. Everything is `#ifdef NEWTELLER_HAVE_RUST` with a stub path, RAII `RustString`, no struct ABI. |
| `src/appmodel.h/.cpp` | **rewritten**, complete and self-consistent. Dual back end: Rust index when available, Qt parser fallback otherwise. New: cached `roleNames`, `m_indexById` O(1) `byId`/`indexOf`, roles `genericName keywords terminal path program running`, properties `count categories runningIds rustBacked`, invokables `search searchDetailed searchResult byId indexOf recent launchArgs launch launchAction isRunning refreshRunning usageOf forgetUsage forgetRecent stats scanReport`, signals `countChanged categoriesChanged runningChanged reloadFinished launchFailed`, frecency via `apps.usage`, desktop actions, `XDG_DATA_DIRS` + flatpak dirs, priority-based override sorting. |

New settings keys read by `AppModel` (already in the Rust schema, **not yet in
`src/settings.cpp` defaults**): `apps.usage`, `apps.recentLimit`,
`apps.terminalCommand`, `apps.maxApps`, `startmenu.fuzzy`, `startmenu.highlight`.

---

## 2. What is still to do (in this order)

1. **`src/systemmonitor.h/.cpp`** (new) — `QObject`, context property `System`.
   * Properties (all with NOTIFY): `available, refreshInterval, cpuPercent, cpuCount,
     cpuModel, frequencyMhz, load1, load5, load15, perCore (QVariantList), memoryPercent,
     memoryText, memoryUsedText, memoryTotalText, swapPercent, networkDownText,
     networkUpText, networkDown, networkUp, batteryPresent, batteryPercent, batteryStatus,
     batteryCharging, batteryCritical, batteryTimeText, temperatureText, thermalZones,
     diskPercent, diskFreeText, disks, processes, userProcesses, uptimeText, snapshot
     (QVariantMap)`.
   * Invokables: `refresh()`, `processesList(int limit)`, `reset()`, `formatBytes(qint64)`,
     `formatRate(double)`, `setFlags(quint32)`.
   * Own `QTimer` with `refreshInterval` ms (0 = stopped, default 2000); one shared ticker
     instead of the per-widget timers QML currently creates.
   * Back end: `Rust::Metrics`; if `!Rust::available()`, a small C++ fallback reading
     `/proc/stat`, `/proc/meminfo`, `/proc/net/dev` so the panel plugin still works.
2. **`src/settings.h/.cpp`** upgrade — debounced (≈150 ms `QTimer`, single shot) atomic
   save through `Rust::writeFileAtomic` (`.bak` + fsync) with a `QSaveFile` fallback;
   `loadConfig()` resilient load; defaults from `Rust::configDefaults()` merged over the
   built-in map so new keys appear without a C++ table edit; add `keys() has(key) reset(key)
   setValues(map) reload() defaults() schema() validate() dirty`; watch the config dir with
   `Rust::Watcher` + `QSocketNotifier`, falling back to `QFileSystemWatcher`.
3. **`src/theme.h/.cpp`** — cache parsed `QColor`s in a `QHash<QString,QColor>` (today every
   property read re-parses), add `withAlpha(color, alpha)` / `mix(a, b, t)` invokables,
   `autoReload` through the settings watcher, load `nord.json` / `highcontrast.json`.
4. **`src/plugins.h/.cpp`** — cache the `QVariantList` (rebuild only on rescan), validate
   `manifest.json` (id/type/entry required, warn otherwise), add `at(id)`, `setEnabled(id,
   bool)`, respect `order/x/y`, rescan on inotify events (`plugins.autoRescan`).
5. **`src/shellcontroller.h/.cpp`** — icon existence cache (`QHash<QString,QString>` +
   invalidation on watcher events) instead of `QFile::exists` per binding; add `notify(title,
   body)`, `openPath(path)`, `terminal(command)`, `copyToClipboard(text)`, `hostname`,
   `distro`, `kernelVersion`, `isWayland`, `diagnostics()` (from `Rust::diagnostics()`).
6. **`src/main.cpp`** — register `System`; CLI flags `--version --print-config --diagnostics
   --theme <name> --reset --bench` (bench = `newteller-ctl bench` equivalent, optional);
   `QLockFile` single-instance guard; `QLoggingCategory("newteller")`; pass dropped/opened
   files to `Apps.launch` so `%u/%f` works.
7. **QML** — `Main.qml`: one shared clock `Timer` exposed as a property so Panel and the
   clock plugin stop running separate 1 s timers. `Panel.qml`: `panel.position` (bottom/top/
   left/right), `panel.margin`, `panel.centered`, `panel.autoHide` + delay, running badge from
   `Apps.runningIds`, hoist the plugin filter out of the binding (`readonly property var
   panelPlugins`). `StartMenu.qml`: use `searchDetailed()` + `Text.StyledText` highlight,
   `ListView.cacheBuffer` / `reuseItems: true`, category chips from `Apps.categories`,
   recent row from `Apps.recent()`. `Desktop.qml`: right-click menu (new wallpaper / paste /
   open terminal / settings), `desktop.iconSize|cellWidth|cellHeight`, hoisted plugin filter.
   New `qml/RunDialog.qml` (Alt+F2 style launcher using `Apps.search`).
8. **`resources.qrc`** — add every new QML file and `themes/nord.json`,
   `themes/highcontrast.json`.
9. **New plugins** — `plugins/panel-system-monitor/{manifest.json,Widget.qml}` (CPU/memory/
   net sparkline bound to `System`), `plugins/desktop-sysinfo/{manifest.json,Widget.qml}`.
10. **New themes** — `themes/nord.json`, `themes/highcontrast.json` (same key set as
    `themes/default.json`).
11. **`CMakeLists.txt`** — `option(NEWTELLER_WITH_RUST "..." AUTO)`;
    `find_program(CARGO_EXECUTABLE cargo)`; `file(GLOB_RECURSE ... CONFIGURE_DEPENDS rust/*)`;
    `add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/rust-target/release/libnewteller_core.a
    COMMAND ${CMAKE_CARGO} build --release --manifest-path rust/Cargo.toml -p newteller-core
    -p newteller-ctl` with `CARGO_TARGET_DIR=${CMAKE_BINARY_DIR}/rust-target`); `add_library(
    newteller_core STATIC IMPORTED GLOBAL)`; `target_include_directories(newteller PRIVATE
    rust/crates/newteller-core/include)`; `target_compile_definitions(newteller PRIVATE
    NEWTELLER_HAVE_RUST=1)`; link `pthread dl m`; `FATAL_ERROR` when cargo is missing and the
    option is `ON`; install `newteller-ctl` into `bin`; add `src/rustbridge.cpp`,
    `src/systemmonitor.cpp`, `src/appentry.h`, `src/rustbridge.h` to the target sources.
12. **`scripts/build-release.sh`** — build the Rust crates first, `cargo test`, copy
    `newteller-ctl` into the staged `usr/bin`, extend the staged-file assertions and BUILDINFO
    with the rustc/cargo versions.
13. **`.github/workflows/build-release.yml`** — install the Rust toolchain inside
    `container: debian:13` (`apt-get install rustc cargo` or the rustup script), cache
    `~/.cargo/registry` and the cargo target dir, run `cargo test --workspace`, ship
    `newteller-ctl` in the release ZIP.
14. **`README.md`** — document the Rust core, the CLI (`newteller-ctl …`), the new settings
    keys, `-DNEWTELLER_WITH_RUST=ON/OFF/AUTO`, and the new features; add `rust/README.md`.
15. **`.gitignore`** — already has `target/ build/ dist/`; add `rust/target/` explicitly and
    `Cargo.lock` is **committed** for the binary crate (do not ignore it).
16. **Verify (user side)** — `cd rust && cargo test --workspace && cargo clippy`,
    `cmake -B build -DNEWTELLER_WITH_RUST=ON && cmake --build build`, `qmlformat`/`qmllint`
    on the QML, then run the shell and check the panel badge, start menu highlight, system
    monitor plugin and the desktop right-click menu.
17. **git** — nothing is committed yet (`git status` shows `M src/appmodel.h` and untracked
    `rust/`, `src/appentry.h`, `src/rustbridge.{h,cpp}`). Commit on
    `arena/a2300a50-newteller` only when the user asks; no push was requested.

---

## 3. Gotchas already paid for (do not reintroduce)

* No Rust toolchain, no cmake, no apt/network in this sandbox — nothing here has ever been
  compiled. Unit tests exist on the Rust side so the user's own build catches mistakes.
* `mask & FLAG != 0` is a type error in Rust: always parenthesise.
* Closure bodies inside an `unsafe fn` are **not** implicitly unsafe: dereference raw
  pointers/`CStr` before the `nt_string!`/`nt_code!` closures in `src/ffi/mod.rs`.
* `fold_char` in `search/normalize.rs` must stay 1:1 per char (no `ß`→`ss`), otherwise the
  highlight byte offsets drift.
* Desktop values are stored raw and unescaped in `build()`/`split_list()` — unescaping
  earlier destroys `\;` list separators.
* MSRV 1.75: no let-chains, no `LazyLock`, no `binding @ (A|B)` or-patterns, no char-array
  `Pattern`s.
* `Rust::Index` is movable but not copyable; resetting it is `m_index = Rust::Index();`.
* `rustbridge.h` must include `appentry.h`, never `appmodel.h` (include cycle).
* Every `Rust::` call must be guarded by `Rust::available()`; with
  `-DNEWTELLER_WITH_RUST=OFF` the shell must behave exactly as before this branch.

---

## 4. NEXT CHAT: start here

Re-orient in one shot:

```bash
cd /home/user/newteller && git status --short && git log --oneline -3
cat HANDOFF.md                                  # this file
sed -n '56,205p' rust/crates/newteller-core/include/newteller_core.h   # the ABI contract
cat src/appentry.h src/rustbridge.h             # types the next files must use
```

Write the files in this exact order (each one compiles against what precedes it):

### 4.1 `src/systemmonitor.h` — skeleton to fill in

```cpp
#pragma once
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include "rustbridge.h"

class SystemMonitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(int refreshInterval READ refreshInterval WRITE setRefreshInterval NOTIFY refreshIntervalChanged)
    Q_PROPERTY(double cpuPercent READ cpuPercent NOTIFY snapshotUpdated)
    Q_PROPERTY(int cpuCount READ cpuCount NOTIFY snapshotUpdated)
    Q_PROPERTY(QString cpuModel READ cpuModel NOTIFY snapshotUpdated)
    Q_PROPERTY(double load1 READ load1 NOTIFY snapshotUpdated)
    Q_PROPERTY(double load5 READ load5 NOTIFY snapshotUpdated)
    Q_PROPERTY(double load15 READ load15 NOTIFY snapshotUpdated)
    Q_PROPERTY(QVariantList perCore READ perCore NOTIFY snapshotUpdated)
    Q_PROPERTY(double memoryPercent READ memoryPercent NOTIFY snapshotUpdated)
    Q_PROPERTY(QString memoryText READ memoryText NOTIFY snapshotUpdated)
    Q_PROPERTY(double swapPercent READ swapPercent NOTIFY snapshotUpdated)
    Q_PROPERTY(QString networkDownText READ networkDownText NOTIFY snapshotUpdated)
    Q_PROPERTY(QString networkUpText READ networkUpText NOTIFY snapshotUpdated)
    Q_PROPERTY(bool batteryPresent READ batteryPresent NOTIFY snapshotUpdated)
    Q_PROPERTY(double batteryPercent READ batteryPercent NOTIFY snapshotUpdated)
    Q_PROPERTY(QString batteryStatus READ batteryStatus NOTIFY snapshotUpdated)
    Q_PROPERTY(bool batteryCharging READ batteryCharging NOTIFY snapshotUpdated)
    Q_PROPERTY(bool batteryCritical READ batteryCritical NOTIFY snapshotUpdated)
    Q_PROPERTY(QString batteryTimeText READ batteryTimeText NOTIFY snapshotUpdated)
    Q_PROPERTY(QString temperatureText READ temperatureText NOTIFY snapshotUpdated)
    Q_PROPERTY(double diskPercent READ diskPercent NOTIFY snapshotUpdated)
    Q_PROPERTY(QString diskFreeText READ diskFreeText NOTIFY snapshotUpdated)
    Q_PROPERTY(QVariantList disks READ disks NOTIFY snapshotUpdated)
    Q_PROPERTY(int processes READ processes NOTIFY snapshotUpdated)
    Q_PROPERTY(int userProcesses READ userProcesses NOTIFY snapshotUpdated)
    Q_PROPERTY(QString uptimeText READ uptimeText NOTIFY snapshotUpdated)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY snapshotUpdated)
public:
    explicit SystemMonitor(QObject *parent = nullptr);
    // ... getters read from m_snapshot (QVariantMap), never re-parse per call
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantList processesList(int limit);
    Q_INVOKABLE void reset();                       // after resume from suspend
    Q_INVOKABLE QString formatBytes(qint64 bytes) const;
    Q_INVOKABLE QString formatRate(double bytesPerSecond) const;
signals:
    void snapshotUpdated();
    void refreshIntervalChanged(int milliseconds);
private:
    void applySnapshot();                            // QVariantMap -> typed members + emit
    void sampleWithQt();                             // /proc fallback when !Rust::available()
    Rust::Metrics m_metrics;
    QTimer m_timer;                                  // interval 0 = stopped
    QVariantMap m_snapshot;
    int m_refreshInterval = 2000;
    quint32 m_flags = Rust::Metrics::Battery | Rust::Metrics::Disks
                      | Rust::Metrics::Thermal | Rust::Metrics::Uname;
};
```

### 4.2 `src/settings.cpp` — defaults to add

Merge `Rust::configDefaults()` over the built-in map when `Rust::available()`, and make sure
these keys exist either way (they are already read by `AppModel` and by the Rust schema):

`panel.position` `"bottom"`, `panel.margin` `0`, `panel.centered` `false`, `panel.width` `0`,
`panel.autoHide` `false`, `panel.autoHideDelay` `400`, `panel.dateFormat` `"ddd d MMM"`,
`panel.showDate` `false`, `panel.showRunning` `true`,
`startmenu.limit` `200`, `startmenu.fuzzy` `true`, `startmenu.highlight` `"#ffffff"`,
`startmenu.showRecent` `true`,
`theme.autoReload` `true`,
`desktop.showIcons` `true`, `desktop.iconSize` `48`, `desktop.cellWidth` `96`,
`desktop.cellHeight` `110`, `desktop.contextMenu` `true`,
`icons.cache` `true`, `plugins.autoRescan` `true`,
`apps.recentLimit` `8`, `apps.usage` `{}`, `apps.terminalCommand` `"x-terminal-emulator -e"`,
`apps.maxApps` `0`,
`system.interval` `2000`, `system.perCore` `false`, `system.battery` `true`,
`system.thermal` `true`, `system.disks` `true`,
`watch.debounce` `250`, `run.history` `[]`.

Save path: `QTimer::singleShot(150 ms, this, &Settings::flush)` on every `setValue`, and
`flush()` calls `Rust::writeFileAtomic(configFile(), json, /*backup=*/true)` when available,
otherwise `QSaveFile` + a manual `.bak` copy. Never write on every key change (the current
code does).

### 4.3 `CMakeLists.txt` — Rust block to paste

```cmake
option(NEWTELLER_WITH_RUST "Build the Rust core (ON/OFF/AUTO)" AUTO)
find_program(CARGO_EXECUTABLE cargo)
if(CARGO_EXECUTABLE)
  set(NEWTELLER_RUST_ENABLED TRUE)
elseif(NEWTELLER_WITH_RUST STREQUAL "ON")
  message(FATAL_ERROR "NEWTELLER_WITH_RUST=ON but cargo was not found")
else()
  set(NEWTELLER_RUST_ENABLED FALSE)
endif()

set(NEWTELLER_RUST_LIB ${CMAKE_BINARY_DIR}/rust-target/release/libnewteller_core.a)
if(NEWTELLER_RUST_ENABLED)
  file(GLOB_RECURSE NEWTELLER_RUST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/rust/*)
  add_custom_command(OUTPUT ${NEWTELLER_RUST_LIB}
    COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${CMAKE_BINARY_DIR}/rust-target
            ${CARGO_EXECUTABLE} build --release
            --manifest-path ${CMAKE_SOURCE_DIR}/rust/Cargo.toml
            -p newteller-core -p newteller-ctl
    DEPENDS ${NEWTELLER_RUST_SOURCES}
    COMMENT "Building the Rust core (newteller-core, newteller-ctl)")
  add_custom_target(newteller_rust DEPENDS ${NEWTELLER_RUST_LIB})
  add_library(newteller_core STATIC IMPORTED GLOBAL)
  set_target_properties(newteller_core PROPERTIES IMPORTED_LOCATION ${NEWTELLER_RUST_LIB})
  add_dependencies(newteller_core newteller_rust)
  target_include_directories(newteller PRIVATE ${CMAKE_SOURCE_DIR}/rust/crates/newteller-core/include)
  target_compile_definitions(newteller PRIVATE NEWTELLER_HAVE_RUST=1)
  target_link_libraries(newteller PRIVATE newteller_core pthread dl m)
  install(PROGRAMS ${CMAKE_BINARY_DIR}/rust-target/release/newteller-ctl DESTINATION bin)
endif()
```

Also add to the `newteller` target sources: `src/appentry.h src/rustbridge.h src/rustbridge.cpp
src/systemmonitor.h src/systemmonitor.cpp`.

### 4.4 `resources.qrc` — entries to add

```xml
<file>qml/RunDialog.qml</file>
<file>themes/nord.json</file>
<file>themes/highcontrast.json</file>
```
(plus any other new QML file created in step 7 of section 2).

### 4.5 Definition of done for the next chat

- `-DNEWTELLER_WITH_RUST=OFF` still configures, builds and runs exactly as `main` does today
  (every `Rust::` call is stubbed, every QML binding has a fallback).
- `cargo test --workspace` green; `cargo clippy -- -D warnings` clean.
- `newteller-ctl doctor`, `bench`, `metrics`, `search`, `watch` all produce output.
- Panel shows a running badge, start menu shows highlighted fuzzy hits, the system monitor
  plugin updates on the single shared ticker, desktop right-click menu works.
- README documents the new keys, the CLI and the Rust build option.

## 5. Features added in the dock/settings/notification round (commit after 60e31a2)

### 5.1 Settings additions (all live through `Settings`/`Theme`, stored in shell.json)

- `dock.separateBackground` (bool, true) — applications whose windows are all minimised move
  into a dimmed background zone at the end of the dock strip.
- `dock.shrinkIcons` (bool, true) — dock icons shrink (down to 16 px) while the strip would
  overflow; the strip becomes wheel-scrollable when even that is not enough.
- `windows.style` (card | glass | flat | outline), `windows.radius` (-1 = theme),
  `windows.opacityPercent` (0 = theme), `windows.borderWidth` (-1 = theme),
  `windows.borderColor` ("" = theme), `windows.shadow` (0..3) — the shared frame of every
  floating shell window, implemented once in `qml/ShellFrame.qml` and used by the start menu,
  run dialog, dock menu, clock popup and notification cards. Edited in Settings → Appearance
  → "Window style", with a live preview.
- `notifications.enabled`, `notifications.doNotDisturb`, `notifications.position`,
  `notifications.duration` (seconds, 0 = sticky), `notifications.historyLimit` — see 5.3.
  Settings gained a "Notifications" page (popups config, daemon status, history, test button).

### 5.2 Settings pickers (no new QML module dependencies)

- `qml/SettingsFilePicker.qml` — in-shell file manager dialog (FolderListModel based):
  breadcrumb path field, Up/Home/Pictures/Documents/Downloads shortcuts, images/all-files
  filter, image preview pane. "Desktop → Choose from file manager" writes
  `desktop.wallpaper`.
- `qml/SettingsFontPicker.qml` — dropdown over `Qt.fontFamilies()` with per-entry preview in
  its own face and a filter field; replaces the free-text font family field of the theme
  editor.
- `qml/ColorPaletteWindow.qml` — colour palette (curated swatches + hue bar + saturation/value
  square) opened from every `SettingsColorField` via the new `paletteRequested` signal; the
  host window connects `applied` back to the field that opened it.

### 5.3 Notifications (`src/notifications.h/.cpp`, context property `Notifications`)

- QtDBus daemon that claims `org.freedesktop.Notifications` (Notify / CloseNotification /
  GetCapabilities / GetServerInformation, signals NotificationClosed + ActionInvoked) when no
  other daemon owns the session bus; otherwise it steps aside gracefully.
- Private Unix socket `$XDG_RUNTIME_DIR/newteller/notify.sock` fed by the new
  `newteller-ctl notify <title> [body] [--icon name] [--timeout ms]` command (one JSON line).
- `qml/Notifications.qml` — one popup window per screen (Instantiator in Main.qml), corner
  configurable, DND aware, action buttons, per-card expiry timer, history in the settings.
- `ShellController::notify` now prefers the built-in daemon and falls back to `notify-send`.

### 5.4 Clock popup and dock behaviour

- `qml/ClockPopup.qml` — tapping the panel clock (it is a real button now) opens a card with a
  digital read-out, a Canvas analogue dial and a month calendar; anchored at the panel end,
  opposite the bar.
- Dock click is a toggle for single-window entries (focused → minimise, otherwise activate);
  the dock menu gained per-window minimise/restore; pinned apps keep their slot while
  unpinned fully-minimised apps park in the background zone.

### 5.5 Build notes

- `find_package(Qt6 ... COMPONENTS ... DBus)` — QtDBus is part of qtbase; runtime needs
  `libqt6dbus6` (added to `scripts/install-runtime-deps.sh`).
- New sources: `src/notifications.cpp`; new QML registered in `resources.qrc`.
- `tests/qml_smoke.py` mocks the new `Notifications` context property and asserts one clock
  popup per panel and one notification window per screen.
