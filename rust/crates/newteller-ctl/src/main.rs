//! `newteller-ctl` - the command line front end of the NewTeller Rust core.
//!
//! Everything the shell does with the core can be inspected here without a
//! running session, which makes it the debugging tool for the two things that
//! are hardest to see from the outside: what the application scan produced and
//! how a query is ranked.
//!
//! ```text
//! newteller-ctl doctor              # machine, paths, config problems
//! newteller-ctl search fire --json  # ranking of one query
//! newteller-ctl bench --apps 4000   # index build and query timings
//! newteller-ctl metrics --watch 5   # five samples, two seconds apart
//! newteller-ctl set panel.height 52 # typed, atomic, with a backup
//! ```

mod args;
mod commands;
mod output;

use std::process::ExitCode;

use args::Parsed;

const HELP: &str = "\
newteller-ctl - inspect and configure the NewTeller shell

USAGE
    newteller-ctl [--json] <command> [arguments] [--flag value]

CONFIGURATION
    paths                 where NewTeller reads and writes
    schema                every known shell.json key, with its type and default
    list                  the effective configuration (defaults merged in)
    get <key>             one setting, as JSON
    set <key> <value>     write one setting, coerced to its declared type
    unset <key>           drop one setting, restoring the default
    validate              problems in shell.json (unknown keys, wrong types)
    init [--force]        write a default shell.json if there is none

CATALOGUE AND SEARCH
    apps                  applications found by the XDG scan
    search <query>        ranked results, with the tier each one matched in
    categories            applications per category
    running               applications that are currently running
    exec <id> [file...]   the argv that launching <id> would produce
    bench                 index build and query timings

SYSTEM
    metrics               one sample of cpu, memory, network, battery, disks
    watch <path>...       inotify events, debounced (Ctrl-C to stop)
    doctor                diagnostics for a bug report

GLOBAL FLAGS
    --json                machine readable output
    --limit <n>           maximum number of results (default 50)
    --dirs <a:b:c>        application directories, colon separated
    --no-fuzzy            disable typo tolerance
    --colour <hex>        highlight colour for search results
    --interval <ms>       sampling interval for metrics --watch
    --count <n>           number of samples for metrics --watch
    --apps <n>            synthetic catalogue size for bench
    --queries <n>         number of queries for bench
    --debounce <ms>       debounce window for watch
    -h, --help            this text
    -V, --version         version of the tool and of the core

Exit codes: 0 success, 1 command failed, 2 bad usage.
";

fn main() -> ExitCode {
    let argv: Vec<String> = std::env::args().skip(1).collect();
    match args::parse(&argv) {
        Parsed::Help => {
            print!("{HELP}");
            ExitCode::SUCCESS
        }
        Parsed::Version => {
            println!(
                "newteller-ctl {} (core {}, abi {}, {})",
                env!("CARGO_PKG_VERSION"),
                newteller_core::VERSION,
                newteller_core::ABI_VERSION,
                if cfg!(debug_assertions) { "debug" } else { "release" }
            );
            ExitCode::SUCCESS
        }
        Parsed::Error(message) => {
            eprintln!("newteller-ctl: {message}");
            eprintln!("Try 'newteller-ctl --help'.");
            ExitCode::from(2)
        }
        Parsed::Run(options) => match commands::run(&options) {
            0 => ExitCode::SUCCESS,
            1 => ExitCode::FAILURE,
            other => ExitCode::from(other.clamp(2, 255) as u8),
        },
    }
}
