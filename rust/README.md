# NewTeller: the Rust half

Two crates in one workspace:

| Crate | What it is |
| --- | --- |
| `newteller-core` | The application catalogue (`.desktop` parsing, `Exec` quoting, parallel scan), the fuzzy search index, the system metrics sampler (`/proc`, battery, thermal zones, disks), an inotify watcher with per-path debounce, and atomic configuration files. Built as an `rlib` (for the CLI and the tests) and as a static library with a C ABI (for the Qt shell). |
| `newteller-ctl` | A command line tool on top of the core: inspect and change `shell.json`, search applications, sample metrics, `doctor`. It needs no running session. |

The shell works without any of this: with `-DNEWTELLER_WITH_RUST=OFF` (or when
`cargo` is not installed and the option is `AUTO`) the C++ fallbacks in
`src/appmodel.cpp`, `src/systemmonitor.cpp` and `src/settings.cpp` do the same
work, only slower on large application lists.

## Build and test

```sh
cd rust
cargo test --workspace          # unit tests and the C ABI tests (tests/ffi.rs)
cargo build --release           # target/release/libnewteller_core.a and newteller-ctl
```

The top-level CMake build runs `cargo build --release` itself (into
`<build>/rust-target`) and links the static library into `newteller`; there is
no need to build the crates by hand for a normal build.

Minimum Rust version: 1.75 (`rust-version` in `Cargo.toml`). That rules out
let-chains and `LazyLock`.

### Features

- `no-procfs` (`newteller-core`): the `/proc` and `/sys` readers report
  "unavailable" instead of reading the system. For running the tests in a
  restricted container.

## The C ABI

`crates/newteller-core/include/newteller_core.h` is the contract; the C++ side
is `src/rustbridge.cpp`, which is the only file that calls it.

- Every payload is JSON in a `char *` (camelCase keys). Strings returned by the
  library are freed with `nt_free`, never with `free`.
- No struct crosses the boundary: handles are opaque pointers
  (`nt_index_*`, `nt_metrics_*`, `nt_watcher_*`).
- Every entry point catches panics and turns them into an error; the message
  is in `nt_last_error()` (thread-local).
- Changing a signature means changing the header, `src/ffi/mod.rs` and
  `src/rustbridge.cpp` together, and bumping `NT_ABI_VERSION`. The shell checks
  the version at startup and falls back to C++ when it does not match.

## Things that bite

- `fold_char` in `search/normalize.rs` maps one character to one character.
  Folding `ß` to `ss` would shift the highlight offsets.
- Desktop entry values are kept raw until they are used: unescaping early
  destroys `\;` list separators.
- Inside an `unsafe fn`, closure bodies are not implicitly unsafe: read raw
  pointers before the `nt_string!` / `nt_code!` closures in `src/ffi/mod.rs`.
