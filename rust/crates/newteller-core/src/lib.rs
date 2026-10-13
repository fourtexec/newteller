//! Rust core of the NewTeller desktop shell.
//!
//! The Qt/C++ half of NewTeller is a thin user-interface layer: everything that
//! is CPU bound, talks to the kernel or has to be exact is implemented here and
//! exported through a small C ABI ([`ffi`], header: `include/newteller_core.h`).
//!
//! Modules
//! -------
//! * [`desktop`] - specification compliant `.desktop` file parsing, `Exec` line
//!   expansion and a parallel scan of the XDG application directories.
//! * [`search`]  - normalisation, scoring and an inverted (token + trigram)
//!   index that answers start-menu queries with typo tolerance and highlight
//!   ranges.
//! * [`system`]  - `/proc` and `/sys` readers: CPU, memory, swap, network
//!   throughput, battery, thermal zones, disks and the process list, plus the
//!   matching of running processes back to application ids.
//! * [`watch`]   - a debounced inotify watcher that survives editors which
//!   replace files instead of writing them in place.
//! * [`config`]  - atomic (crash safe) writes with backups and a schema for the
//!   flat dotted keys used by `~/.config/newteller/shell.json`.
//! * [`diag`]    - a single JSON blob describing the machine, for bug reports.
//! * [`ffi`]     - the `extern "C"` surface consumed by `src/rustbridge.cpp`.
//!
//! Design rules for this crate
//! ---------------------------
//! 1. No panics cross the FFI boundary: every `extern "C"` function in [`ffi`]
//!    is wrapped in `catch_unwind` and reports errors through
//!    [`ffi::nt_last_error`].
//! 2. No Qt types appear here. Data crosses the boundary either as scalars or
//!    as UTF-8 JSON text produced by `serde_json`.
//! 3. Linux-only kernel interfaces are behind `#[cfg(unix)]` so the crate still
//!    builds - and most of the test-suite still runs - on other hosts.
#![warn(missing_docs)]
#![warn(missing_debug_implementations)]
#![allow(clippy::needless_range_loop)]

pub mod config;
pub mod desktop;
pub mod diag;
pub mod error;
pub mod ffi;
pub mod search;
pub mod system;
pub mod util;
pub mod watch;

pub use error::{Error, Result};

/// Crate version, taken from `Cargo.toml` at build time.
pub const VERSION: &str = env!("CARGO_PKG_VERSION");

/// Bumped whenever the C ABI changes in an incompatible way. The C++ side
/// compares this against `NT_ABI_VERSION` from `newteller_core.h` at start-up
/// and disables the Rust fast paths if they disagree.
pub const ABI_VERSION: u32 = 1;
