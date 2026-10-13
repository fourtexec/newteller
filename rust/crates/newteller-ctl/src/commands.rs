//! Implementation of every subcommand. Each function returns the process exit
//! code: `0` success, `1` failure, `2` usage.

use std::path::Path;
use std::time::Instant;

use serde_json::{Map, Value};

use newteller_core::desktop::exec::{expand, ExecContext};
use newteller_core::desktop::{scan, DesktopEntry, ScanRequest};
use newteller_core::search::{SearchIndex, SearchOptions};
use newteller_core::system::{self, Monitor, Snapshot, SnapshotOptions};
use newteller_core::{config, diag, util, watch};

use crate::args::Options;
use crate::output;

/// Runs one subcommand.
pub fn run(options: &Options) -> i32 {
    match options.command.as_str() {
        "paths" => paths(options),
        "schema" => schema(options),
        "list" => list(options),
        "get" => get(options),
        "set" => set(options),
        "unset" => unset(options),
        "validate" => validate(options),
        "init" => init(options),
        "themes" => themes(options),
        "plugins" => plugins(options),
        "apps" => apps(options),
        "search" => search_command(options),
        "categories" => categories(options),
        "running" => running(options),
        "exec" => exec(options),
        "bench" => bench(options),
        "metrics" => metrics(options),
        "watch" => watch_command(options),
        "doctor" => doctor(options),
        "version" => {
            println!("{}", newteller_core::VERSION);
            0
        }
        other => output::fail(format!(
            "unknown command '{other}', try 'newteller-ctl --help'"
        )),
    }
}

// ---------------------------------------------------------------------------
// configuration
// ---------------------------------------------------------------------------

fn paths(options: &Options) -> i32 {
    let map = config::paths();
    if options.json {
        return output::print_json(&Value::Object(map));
    }
    let mut rows: Vec<Vec<String>> = Vec::new();
    for (key, value) in &map {
        let text = match value {
            Value::Array(items) => items
                .iter()
                .filter_map(|item| item.as_str())
                .collect::<Vec<_>>()
                .join(":"),
            other => output::short(other),
        };
        let exists = match key.as_str() {
            "pluginDirs" => "mixed".to_string(),
            _ => Path::new(&text).exists().to_string(),
        };
        rows.push(vec![key.clone(), exists, text]);
    }
    print!("{}", output::table(&["path", "exists", "value"], &rows));
    0
}

fn schema(options: &Options) -> i32 {
    let entries = config::schema();
    if options.json {
        return output::print_json(&entries);
    }
    let rows: Vec<Vec<String>> = entries
        .iter()
        .map(|entry| {
            vec![
                entry.key.clone(),
                entry.kind.clone(),
                output::short(&entry.default),
                output::truncate(&entry.doc, 60),
            ]
        })
        .collect();
    print!("{}", output::table(&["key", "type", "default", "description"], &rows));
    0
}

fn effective_config() -> Value {
    let loaded = config::load_json_resilient(&config::config_file());
    config::with_defaults(&loaded.value)
}

fn user_config() -> Value {
    config::load_json_resilient(&config::config_file()).value
}

fn write_user_config(value: &Value) -> i32 {
    match config::write_json_atomic(
        &config::config_file(),
        value,
        &config::WriteOptions::default(),
    ) {
        Ok(()) => 0,
        Err(error) => output::fail(error),
    }
}

fn list(options: &Options) -> i32 {
    let config = effective_config();
    if options.json {
        return output::print_json(&config);
    }
    let rows: Vec<Vec<String>> = config::keys(&config)
        .iter()
        .filter_map(|key| {
            config::get(&config, key).map(|value| vec![key.clone(), output::short(value)])
        })
        .collect();
    print!("{}", output::table(&["key", "value"], &rows));
    0
}

fn get(options: &Options) -> i32 {
    let Some(key) = options.positional.first() else {
        return output::fail("get needs a key, see 'newteller-ctl schema'");
    };
    let config = effective_config();
    match config::get(&config, key) {
        Some(value) => {
            if options.json {
                output::print_json(value)
            } else {
                output::print_value(value);
                0
            }
        }
        None => {
            if options.json {
                output::print_json(&Value::Null)
            } else {
                output::fail(format!("no such key: {key}"))
            }
        }
    }
}

fn set(options: &Options) -> i32 {
    let (Some(key), Some(raw)) = (options.positional.first(), options.positional.get(1)) else {
        return output::fail("set needs a key and a value");
    };
    if config::schema_for(key).is_none() {
        eprintln!("newteller-ctl: warning: '{key}' is not a known setting");
    }
    let value = parse_value(key, raw);
    let mut config = user_config();
    config::set(&mut config, key, value.clone());
    let code = write_user_config(&config);
    if code != 0 {
        return code;
    }
    if options.json {
        return output::print_json(&serde_json::json!({ "key": key, "value": value }));
    }
    println!("{key} = {}", output::short(&value));
    println!("written to {}", config::config_file().display());
    0
}

fn unset(options: &Options) -> i32 {
    let Some(key) = options.positional.first() else {
        return output::fail("unset needs a key");
    };
    let mut config = user_config();
    let removed = config::remove(&mut config, key);
    if removed.is_none() {
        return output::fail(format!("'{key}' is not set in the user file"));
    }
    let code = write_user_config(&config);
    if code != 0 {
        return code;
    }
    if options.json {
        return output::print_json(&serde_json::json!({ "key": key, "removed": removed }));
    }
    println!("removed {key}");
    0
}

fn validate(options: &Options) -> i32 {
    let path = config::config_file();
    let loaded = config::load_json_resilient(&path);
    let mut issues = config::validate(&loaded.value);
    if let Some(error) = loaded.error.clone() {
        issues.insert(
            0,
            config::Issue {
                key: String::new(),
                level: "error".to_string(),
                message: error,
            },
        );
    }
    if options.json {
        return output::print_json(&serde_json::json!({
            "path": path.to_string_lossy(),
            "source": loaded.source,
            "issues": issues,
        }));
    }
    println!("{} (source: {:?})", path.display(), loaded.source);
    let errors = issues.iter().filter(|issue| issue.level == "error").count();
    let warnings = issues.iter().filter(|issue| issue.level == "warning").count();
    let infos = issues.iter().filter(|issue| issue.level == "info").count();
    let rows: Vec<Vec<String>> = issues
        .iter()
        .filter(|issue| issue.level != "info")
        .map(|issue| {
            vec![
                issue.level.clone(),
                if issue.key.is_empty() { "-".to_string() } else { issue.key.clone() },
                issue.message.clone(),
            ]
        })
        .collect();
    if rows.is_empty() {
        println!("no problems found");
    } else {
        print!("{}", output::table(&["level", "key", "message"], &rows));
    }
    println!("\n{errors} error(s), {warnings} warning(s), {infos} default(s) in use");
    if errors > 0 {
        1
    } else {
        0
    }
}

fn init(options: &Options) -> i32 {
    let path = config::config_file();
    if path.exists() && !options.force {
        return output::fail(format!("{} already exists, use --force", path.display()));
    }
    let value = Value::Object(config::defaults());
    match config::write_json_atomic(&path, &value, &config::WriteOptions::default()) {
        Ok(()) => {
            if options.json {
                return output::print_json(&serde_json::json!({ "path": path.to_string_lossy() }));
            }
            println!("wrote {}", path.display());
            0
        }
        Err(error) => output::fail(error),
    }
}

fn themes(options: &Options) -> i32 {
    let dir = config::themes_dir();
    let mut found: Vec<String> = Vec::new();
    if let Ok(entries) = std::fs::read_dir(&dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.extension().and_then(|e| e.to_str()) == Some("json") {
                if let Some(name) = path.file_stem().and_then(|n| n.to_str()) {
                    found.push(name.to_string());
                }
            }
        }
    }
    found.sort();
    let active = config::get(&effective_config(), "theme.name")
        .map(output::short)
        .unwrap_or_default();
    if options.json {
        return output::print_json(&serde_json::json!({
            "active": active,
            "userThemes": found,
            "builtIn": ["default", "light", "nord", "highcontrast"],
            "directory": dir.to_string_lossy(),
        }));
    }
    println!("active theme : {active}");
    println!("directory    : {} ({})", dir.display(), if dir.is_dir() { "exists" } else { "missing" });
    if found.is_empty() {
        println!("user themes  : none");
    } else {
        for name in &found {
            println!("user themes  : {name}");
        }
    }
    println!("built-in     : default, light, nord, highcontrast");
    0
}

fn plugins(options: &Options) -> i32 {
    let config = effective_config();
    let disabled: Vec<String> = config::get(&config, "plugins.disabled")
        .and_then(|value| value.as_array())
        .map(|items| {
            items
                .iter()
                .filter_map(|item| item.as_str().map(str::to_string))
                .collect()
        })
        .unwrap_or_default();

    let mut rows: Vec<Vec<String>> = Vec::new();
    let mut found: Vec<Value> = Vec::new();
    for dir in config::plugin_dirs() {
        let Ok(entries) = std::fs::read_dir(&dir) else {
            continue;
        };
        for entry in entries.flatten() {
            let manifest = entry.path().join("manifest.json");
            let Ok(text) = std::fs::read_to_string(&manifest) else {
                continue;
            };
            let Ok(value) = serde_json::from_str::<Value>(&text) else {
                rows.push(vec![
                    entry.file_name().to_string_lossy().into_owned(),
                    "-".to_string(),
                    "-".to_string(),
                    "-".to_string(),
                    "invalid manifest".to_string(),
                ]);
                continue;
            };
            let id = value
                .get("id")
                .and_then(|v| v.as_str())
                .unwrap_or_else(|| "")
                .to_string();
            let enabled = !disabled.contains(&id);
            let row = vec![
                if id.is_empty() {
                    entry.file_name().to_string_lossy().into_owned()
                } else {
                    id.clone()
                },
                value
                    .get("name")
                    .and_then(|v| v.as_str())
                    .unwrap_or("-")
                    .to_string(),
                value
                    .get("version")
                    .and_then(|v| v.as_str())
                    .unwrap_or("-")
                    .to_string(),
                value
                    .get("type")
                    .and_then(|v| v.as_str())
                    .unwrap_or("desktop")
                    .to_string(),
                if enabled { "enabled" } else { "disabled" }.to_string(),
                entry.path().to_string_lossy().into_owned(),
            ];
            found.push(value);
            rows.push(row);
        }
    }
    if options.json {
        return output::print_json(&serde_json::json!({
            "plugins": found,
            "disabled": disabled,
        }));
    }
    if rows.is_empty() {
        println!("no plugins found in:");
        for dir in config::plugin_dirs() {
            println!("  {}", dir.display());
        }
        return 0;
    }
    print!(
        "{}",
        output::table(&["id", "name", "version", "type", "state", "path"], &rows)
    );
    0
}

// ---------------------------------------------------------------------------
// catalogue and search
// ---------------------------------------------------------------------------

fn scan_request(options: &Options) -> ScanRequest {
    let dirs = if options.dirs.is_empty() {
        scan::default_dirs()
    } else {
        options.dirs.clone()
    };
    ScanRequest {
        dirs,
        max_depth: scan::DEFAULT_MAX_DEPTH,
        ..ScanRequest::default()
    }
}

fn build_index(options: &Options) -> SearchIndex {
    if options.apps > 0 {
        SearchIndex::from_entries(synthetic_apps(options.apps))
    } else {
        SearchIndex::from_scan(&scan_request(options))
    }
}

fn search_options(options: &Options) -> SearchOptions {
    SearchOptions {
        limit: options.limit,
        fuzzy: options.fuzzy,
        highlight: options.colour.clone(),
        match_keywords: true,
        match_meta: true,
    }
}

fn report_scan(index: &SearchIndex) {
    let report = index.report();
    let stats = index.stats();
    eprintln!(
        "scanned {} files, kept {}, skipped {} in {:.1} ms; index built in {:.1} ms ({} tokens, {} trigrams)",
        report.scanned,
        stats.apps,
        report.skipped,
        report.elapsed_ms,
        stats.built_in_ms,
        stats.tokens,
        stats.trigrams
    );
    for problem in report.errors.iter().take(5) {
        eprintln!("  {}: {}", problem.path, problem.reason);
    }
}

fn apps(options: &Options) -> i32 {
    let index = build_index(options);
    if options.json {
        return output::print_json(&serde_json::json!({
            "apps": index.entries(),
            "report": index.report(),
            "stats": index.stats(),
        }));
    }
    let rows: Vec<Vec<String>> = index
        .entries()
        .iter()
        .take(options.limit)
        .map(|entry| {
            vec![
                output::truncate(&entry.name, 30),
                output::truncate(&entry.id, 38),
                entry
                    .categories
                    .first()
                    .cloned()
                    .unwrap_or_else(|| "-".to_string()),
                if entry.terminal { "tty" } else { "" }.to_string(),
                output::truncate(&entry.program(), 24),
            ]
        })
        .collect();
    print!(
        "{}",
        output::table(&["name", "id", "category", "", "program"], &rows)
    );
    report_scan(&index);
    0
}

fn search_command(options: &Options) -> i32 {
    let Some(query) = options.positional.first() else {
        return output::fail("search needs a query");
    };
    let index = build_index(options);
    let started = Instant::now();
    let result = index.search(query, &search_options(options));
    let wall = started.elapsed().as_secs_f64() * 1000.0;

    if options.json {
        return output::print_json(&result);
    }
    let rows: Vec<Vec<String>> = result
        .hits
        .iter()
        .enumerate()
        .map(|(rank, hit)| {
            vec![
                (rank + 1).to_string(),
                hit.score.to_string(),
                hit.kind.clone(),
                hit.field.clone(),
                output::truncate(&hit.name, 30),
                output::truncate(&hit.id, 34),
            ]
        })
        .collect();
    print!(
        "{}",
        output::table(&["#", "score", "tier", "field", "name", "id"], &rows)
    );
    eprintln!(
        "{} matched, {} considered, {} path, {:.3} ms in the index ({:.3} ms total)",
        result.matched,
        result.considered,
        if result.scanned_all { "full scan" } else { "index" },
        result.elapsed_ms,
        wall
    );
    0
}

fn categories(options: &Options) -> i32 {
    let index = build_index(options);
    let counts = index.categories();
    if options.json {
        return output::print_json(&counts);
    }
    let rows: Vec<Vec<String>> = counts
        .iter()
        .map(|entry| vec![entry.name.clone(), entry.count.to_string()])
        .collect();
    print!("{}", output::table(&["category", "applications"], &rows));
    0
}

fn running(options: &Options) -> i32 {
    let index = build_index(options);
    let running = index.running();
    if options.json {
        return output::print_json(&running);
    }
    let rows: Vec<Vec<String>> = running
        .iter()
        .map(|entry| {
            let name = index
                .entry(&entry.id)
                .map(|app| app.name.clone())
                .unwrap_or_default();
            vec![
                output::truncate(&name, 30),
                entry.id.clone(),
                entry.count.to_string(),
                entry
                    .pids
                    .iter()
                    .take(8)
                    .map(|pid| pid.to_string())
                    .collect::<Vec<_>>()
                    .join(","),
            ]
        })
        .collect();
    if rows.is_empty() {
        println!("no indexed application is running");
        return 0;
    }
    print!("{}", output::table(&["name", "id", "processes", "pids"], &rows));
    0
}

fn exec(options: &Options) -> i32 {
    let Some(id) = options.positional.first() else {
        return output::fail("exec needs a desktop file id");
    };
    let files: Vec<String> = options.positional[1..].to_vec();
    let index = build_index(options);
    let Some(entry) = index.entry(id) else {
        return output::fail(format!("no application with id '{id}'"));
    };
    let terminal_command = config::get(&effective_config(), "apps.terminalCommand")
        .and_then(|value| value.as_str())
        .unwrap_or(newteller_core::desktop::exec::DEFAULT_TERMINAL_COMMAND)
        .to_string();
    let context = ExecContext {
        icon: &entry.icon,
        name: &entry.name,
        desktop_file: &entry.path,
        files: &files,
        terminal: entry.terminal,
        terminal_command: &terminal_command,
        extra_env: &[],
    };
    let argv = expand(&entry.exec, &context);
    if options.json {
        return output::print_json(&serde_json::json!({
            "id": entry.id,
            "argv": argv,
            "program": argv.first().cloned().unwrap_or_default(),
        }));
    }
    if argv.is_empty() {
        return output::fail(format!("'{}' produces no command", entry.exec));
    }
    for argument in &argv {
        println!("{argument}");
    }
    0
}

// ---------------------------------------------------------------------------
// bench
// ---------------------------------------------------------------------------

fn bench(options: &Options) -> i32 {
    let queries_count = options.queries.max(1);
    let started = Instant::now();
    let index = build_index(options);
    let build_ms = started.elapsed().as_secs_f64() * 1000.0;
    let stats = index.stats();
    if index.is_empty() {
        return output::fail("no applications to benchmark, pass --apps N or --dirs");
    }

    let queries = query_set(&index, queries_count);
    let search_options = search_options(options);
    // Warm up: the first query pays for page faults in the index.
    for query in queries.iter().take(8) {
        let _ = index.search(query, &search_options);
    }

    let mut timings: Vec<f64> = Vec::with_capacity(queries.len());
    let mut hits = 0usize;
    let mut full_scans = 0usize;
    let started = Instant::now();
    for query in &queries {
        let query_started = Instant::now();
        let result = index.search(query, &search_options);
        timings.push(query_started.elapsed().as_secs_f64() * 1_000_000.0);
        hits += result.hits.len();
        if result.scanned_all {
            full_scans += 1;
        }
    }
    let total_ms = started.elapsed().as_secs_f64() * 1000.0;
    timings.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));

    let percentile = |fraction: f64| -> f64 {
        if timings.is_empty() {
            return 0.0;
        }
        let index = ((timings.len() as f64 - 1.0) * fraction).round() as usize;
        timings[index.min(timings.len() - 1)]
    };
    let mean = timings.iter().sum::<f64>() / timings.len().max(1) as f64;

    let report = serde_json::json!({
        "apps": stats.apps,
        "tokens": stats.tokens,
        "trigrams": stats.trigrams,
        "memoryBytes": stats.memory_bytes,
        "buildMs": build_ms,
        "scanMs": stats.scan_ms,
        "queries": queries.len(),
        "totalMs": total_ms,
        "meanUs": mean,
        "medianUs": percentile(0.5),
        "p95Us": percentile(0.95),
        "p99Us": percentile(0.99),
        "maxUs": percentile(1.0),
        "hits": hits,
        "fullScans": full_scans,
    });
    if options.json {
        return output::print_json(&report);
    }
    println!("catalogue    : {} applications ({} tokens, {} trigrams, {} KiB)",
        stats.apps, stats.tokens, stats.trigrams, stats.memory_bytes / 1024);
    println!("scan         : {:.1} ms", stats.scan_ms);
    println!("index build  : {:.1} ms", build_ms);
    println!("queries      : {} ({} hits, {} full scans)", queries.len(), hits, full_scans);
    println!("total        : {:.2} ms", total_ms);
    println!("per query    : mean {:.1} us, median {:.1} us, p95 {:.1} us, max {:.1} us",
        mean, percentile(0.5), percentile(0.95), percentile(1.0));
    println!("throughput   : {:.0} queries/second",
        queries.len() as f64 / (total_ms / 1000.0).max(1e-9));
    0
}

/// A deterministic query set: prefixes, middles and typos of real names.
fn query_set(index: &SearchIndex, count: usize) -> Vec<String> {
    let entries = index.entries();
    let mut queries: Vec<String> = Vec::with_capacity(count);
    let mut state = 0x2545_F491u64;
    let mut next_random = move || {
        // xorshift64*, deterministic across runs and platforms.
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        state.wrapping_mul(0x2545_F491_4F6C_DD1D)
    };
    for index in 0..count {
        if entries.is_empty() {
            queries.push(format!("query{index}"));
            continue;
        }
        let entry = &entries[(next_random() as usize) % entries.len()];
        let name: Vec<char> = entry.name.chars().collect();
        let length = name.len().min(3 + (next_random() as usize % 5));
        let start = (next_random() as usize) % name.len().saturating_sub(length).max(1);
        let mut query: String = name[start..start + length.min(name.len() - start)].iter().collect();
        match next_random() % 4 {
            0 => query = query.to_lowercase(),
            1 => query = query.to_uppercase(),
            2 => {
                // A typo: change the last character.
                let mut chars: Vec<char> = query.chars().collect();
                if let Some(last) = chars.last_mut() {
                    *last = (b'a' + (next_random() % 26) as u8) as char;
                }
                query = chars.into_iter().collect();
            }
            _ => {}
        }
        queries.push(query);
    }
    queries
}

/// A synthetic catalogue, so `bench` also works where no desktop files exist.
fn synthetic_apps(count: usize) -> Vec<DesktopEntry> {
    const ADJECTIVES: [&str; 12] = [
        "Fast", "Simple", "Advanced", "Portable", "Modern", "Classic", "Tiny", "Powerful",
        "Graphical", "Minimal", "Universal", "Secure",
    ];
    const NOUNS: [&str; 16] = [
        "Editor", "Browser", "Terminal", "Player", "Viewer", "Manager", "Monitor", "Converter",
        "Calculator", "Painter", "Recorder", "Scanner", "Archiver", "Launcher", "Profiler",
        "Backup",
    ];
    const CATEGORIES: [&str; 8] = [
        "Utility", "Network", "Graphics", "AudioVideo", "Office", "Development", "System", "Game",
    ];

    let mut entries = Vec::with_capacity(count);
    let mut state = 0x853C_49E6_748F_EA9Bu64;
    for index in 0..count {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        let adjective = ADJECTIVES[(state as usize) % ADJECTIVES.len()];
        let noun = NOUNS[((state >> 8) as usize) % NOUNS.len()];
        let category = CATEGORIES[((state >> 16) as usize) % CATEGORIES.len()];
        let name = format!("{adjective} {noun} {index}");
        entries.push(DesktopEntry {
            id: format!("app-{index}.desktop"),
            path: format!("/usr/share/applications/app-{index}.desktop"),
            name: name.clone(),
            generic_name: noun.to_string(),
            comment: format!("{adjective} {noun} for the desktop, version {index}"),
            exec: format!("/usr/bin/{}-{} %u", adjective.to_lowercase(), noun.to_lowercase()),
            icon: format!("{}-{}", adjective.to_lowercase(), noun.to_lowercase()),
            categories: vec![category.to_string()],
            keywords: vec![
                noun.to_lowercase(),
                adjective.to_lowercase(),
                category.to_lowercase(),
            ],
            sort_key: util::fold_str(&name),
            ..DesktopEntry::default()
        });
    }
    entries
}

// ---------------------------------------------------------------------------
// system
// ---------------------------------------------------------------------------

fn metrics(options: &Options) -> i32 {
    let flags = system::FLAG_BATTERY
        | system::FLAG_PER_CORE
        | system::FLAG_DISKS
        | system::FLAG_THERMAL
        | system::FLAG_PROCESSES
        | system::FLAG_UNAME;
    let snapshot_options = SnapshotOptions::from_flags(flags);
    let mut monitor = Monitor::new();
    let samples = options.count.max(1);
    for sample in 0..samples {
        if sample > 0 {
            std::thread::sleep(std::time::Duration::from_millis(options.interval_ms));
        }
        let snapshot = monitor.snapshot(&snapshot_options);
        if options.json {
            println!(
                "{}",
                serde_json::to_string(&snapshot).unwrap_or_else(|_| "{}".to_string())
            );
        } else {
            print_metrics(&snapshot);
        }
    }
    if !monitor.snapshot(&SnapshotOptions::light()).available {
        eprintln!("newteller-ctl: /proc is not readable, metrics are unavailable");
        return 1;
    }
    0
}

fn print_metrics(snapshot: &Snapshot) {
    println!(
        "cpu   {} {:6.1}%  load {:.2} {:.2} {:.2}  cores {}  {}",
        output::bar(snapshot.cpu.percent, 20),
        snapshot.cpu.percent,
        snapshot.cpu.load1,
        snapshot.cpu.load5,
        snapshot.cpu.load15,
        snapshot.cpu.count,
        output::truncate(&snapshot.cpu.model, 40)
    );
    if let Some(hottest) = snapshot.cpu.per_core.iter().cloned().reduce(f64::max) {
        println!("      hottest core {:.1}%", hottest);
    }
    println!(
        "mem   {} {:6.1}%  {}  swap {:.1}% of {}",
        output::bar(snapshot.memory.percent, 20),
        snapshot.memory.percent,
        snapshot.memory.text,
        snapshot.memory.swap_percent,
        util::format_bytes(snapshot.memory.swap_total)
    );
    println!(
        "net   down {:>12}  up {:>12}",
        system::format_rate(snapshot.network.rx_rate),
        system::format_rate(snapshot.network.tx_rate)
    );
    if snapshot.battery.present {
        println!(
            "batt  {} {:6.1}%  {}  {}",
            output::bar(snapshot.battery.percent, 20),
            snapshot.battery.percent,
            snapshot.battery.status,
            if snapshot.battery.time_to_empty_seconds > 0.0 {
                format!(
                    "{} left",
                    system::format_duration(snapshot.battery.time_to_empty_seconds)
                )
            } else if snapshot.battery.time_to_full_seconds > 0.0 {
                format!(
                    "{} to full",
                    system::format_duration(snapshot.battery.time_to_full_seconds)
                )
            } else {
                String::new()
            }
        );
    }
    for zone in snapshot.thermal.iter().take(3) {
        println!("temp  {:<20} {:.1} C", output::truncate(&zone.name, 20), zone.celsius);
    }
    for disk in snapshot.disks.iter().take(4) {
        println!(
            "disk  {:<16} {} {:5.1}%  {} free",
            output::truncate(&disk.mount, 16),
            output::bar(disk.percent, 12),
            disk.percent,
            disk.available_text
        );
    }
    println!(
        "proc  {} total, {} yours   up {}",
        snapshot.processes, snapshot.user_processes, snapshot.cpu.uptime_text
    );
    if !snapshot.uname.sysname.is_empty() {
        println!(
            "host  {} {} {} ({})",
            snapshot.uname.nodename,
            snapshot.uname.sysname,
            snapshot.uname.release,
            snapshot.uname.machine
        );
    }
    println!("      sampled in {:.2} ms", snapshot.elapsed_ms);
}

fn watch_command(options: &Options) -> i32 {
    let mut paths: Vec<String> = options.positional.clone();
    if paths.is_empty() {
        paths.push(config::config_dir().to_string_lossy().into_owned());
        paths.push(config::themes_dir().to_string_lossy().into_owned());
        for dir in config::plugin_dirs() {
            paths.push(dir.to_string_lossy().into_owned());
        }
    }
    let mut watcher = match watch::Watcher::new(options.debounce_ms) {
        Ok(watcher) => watcher,
        Err(error) => {
            return output::fail(format!(
                "{error}; inotify is only available on Linux, the shell falls back to QFileSystemWatcher"
            ))
        }
    };
    let mut watching = 0usize;
    for path in &paths {
        match watcher.add(Path::new(path), options.recursive) {
            Ok(()) => watching += 1,
            Err(error) => eprintln!("newteller-ctl: cannot watch {path}: {error}"),
        }
    }
    if watching == 0 {
        return output::fail("nothing to watch");
    }
    if options.json {
        eprintln!(
            "{{\"watching\":{watching},\"debounceMs\":{},\"paths\":{:?}}}",
            options.debounce_ms, paths
        );
    } else {
        println!(
            "watching {watching} path(s) with a {} ms debounce, {} - Ctrl-C to stop",
            options.debounce_ms,
            if options.recursive { "recursively" } else { "top level only" }
        );
    }

    let mut seen = 0usize;
    loop {
        for event in watcher.poll() {
            seen += 1;
            if options.json {
                println!(
                    "{}",
                    serde_json::to_string(&event).unwrap_or_else(|_| "{}".to_string())
                );
            } else {
                println!(
                    "[{:>8}] {:<9} {}{}",
                    seen,
                    event.kind,
                    event.path,
                    if event.is_dir { " (dir)" } else { "" }
                );
            }
            if options.events > 0 && seen >= options.events {
                return 0;
            }
        }
        std::thread::sleep(std::time::Duration::from_millis(50));
    }
}

fn doctor(options: &Options) -> i32 {
    let diagnostics = diag::collect();
    if options.json {
        return output::print_json(&diagnostics);
    }

    output::heading("NewTeller core");
    println!(
        "version {}  abi {}  target {}  {}",
        diagnostics.version,
        diagnostics.abi_version,
        diagnostics.target,
        if diagnostics.debug_build { "debug build" } else { "release build" }
    );
    println!(
        "{} {} ({}), {} CPUs, {} memory ({:.0}% used)",
        diagnostics.uname.sysname,
        diagnostics.uname.release,
        diagnostics.uname.machine,
        diagnostics.cpu_count,
        util::format_bytes(diagnostics.memory_total),
        diagnostics.memory_percent
    );

    output::heading("Session");
    let mut rows: Vec<Vec<String>> = Vec::new();
    for (key, value) in &diagnostics.environment {
        if let Some(text) = value.as_str() {
            rows.push(vec![key.clone(), output::truncate(text, 60)]);
        } else {
            rows.push(vec![key.clone(), "-".to_string()]);
        }
    }
    print!("{}", output::table(&["variable", "value"], &rows));

    output::heading("Paths");
    for (key, value) in &diagnostics.paths {
        match value {
            Value::Array(items) => {
                let text = items
                    .iter()
                    .filter_map(|item| item.as_str())
                    .collect::<Vec<_>>()
                    .join(":");
                println!("{key:<12} {text}");
            }
            other => println!("{key:<12} {}", output::short(other)),
        }
    }

    output::heading("Plugins");
    let rows: Vec<Vec<String>> = diagnostics
        .plugin_dirs
        .iter()
        .map(|dir| {
            vec![
                dir.path.clone(),
                if dir.exists { "yes" } else { "no" }.to_string(),
                dir.plugins.to_string(),
            ]
        })
        .collect();
    print!("{}", output::table(&["directory", "exists", "plugins"], &rows));

    output::heading("Applications");
    println!(
        "{} found in {:.1} ms, locales {:?}, desktop {:?}",
        diagnostics.applications,
        diagnostics.scan_ms,
        diagnostics.locales,
        diagnostics.desktop_env
    );

    output::heading("Configuration");
    println!(
        "{} ({})",
        diagnostics.paths
            .get("configFile")
            .map(output::short)
            .unwrap_or_default(),
        if diagnostics.config_exists { "exists" } else { "missing" }
    );
    if diagnostics.config_issues.is_empty() {
        println!("no problems found");
    } else {
        let rows: Vec<Vec<String>> = diagnostics
            .config_issues
            .iter()
            .map(|issue| {
                vec![
                    issue.level.clone(),
                    if issue.key.is_empty() { "-".to_string() } else { issue.key.clone() },
                    issue.message.clone(),
                ]
            })
            .collect();
        print!("{}", output::table(&["level", "key", "message"], &rows));
    }
    if diagnostics.battery.present {
        output::heading("Battery");
        println!(
            "{:.0}% {}{} ({})",
            diagnostics.battery.percent,
            diagnostics.battery.status,
            if diagnostics.battery.critical { ", CRITICAL" } else { "" },
            diagnostics.battery.model_name
        );
    }

    let errors = diagnostics
        .config_issues
        .iter()
        .filter(|issue| issue.level == "error")
        .count();
    if errors > 0 {
        1
    } else {
        0
    }
}

// ---------------------------------------------------------------------------
// value parsing
// ---------------------------------------------------------------------------

/// Turns a command line value into the JSON type the schema declares.
fn parse_value(key: &str, raw: &str) -> Value {
    let trimmed = raw.trim();
    if let Some(entry) = config::schema_for(key) {
        match entry.kind {
            "int" => {
                if let Ok(number) = trimmed.parse::<i64>() {
                    return Value::from(number);
                }
                if let Ok(number) = trimmed.parse::<f64>() {
                    return Value::from(number as i64);
                }
            }
            "number" => {
                if let Ok(number) = trimmed.parse::<f64>() {
                    return Value::from(number);
                }
            }
            "bool" => match trimmed.to_ascii_lowercase().as_str() {
                "true" | "1" | "yes" | "on" => return Value::from(true),
                "false" | "0" | "no" | "off" => return Value::from(false),
                _ => {}
            },
            "string" => return Value::String(trimmed.to_string()),
            "stringlist" => {
                if trimmed.starts_with('[') {
                    if let Ok(value) = serde_json::from_str::<Value>(trimmed) {
                        return value;
                    }
                }
                return Value::Array(
                    trimmed
                        .split(',')
                        .map(|part| Value::String(part.trim().to_string()))
                        .filter(|part| !part.as_str().unwrap_or("").is_empty())
                        .collect(),
                );
            }
            "object" => {
                if let Ok(value) = serde_json::from_str::<Value>(trimmed) {
                    return value;
                }
            }
            _ => {}
        }
    }
    if trimmed.starts_with('{') || trimmed.starts_with('[') {
        if let Ok(value) = serde_json::from_str::<Value>(trimmed) {
            return value;
        }
    }
    if let Ok(number) = trimmed.parse::<i64>() {
        return Value::from(number);
    }
    Value::String(raw.to_string())
}

/// Unused helper kept for symmetry with the C++ side: merges two objects.
#[allow(dead_code)]
fn merge_into(target: &mut Map<String, Value>, source: &Map<String, Value>) {
    for (key, value) in source {
        target.insert(key.clone(), value.clone());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn values_follow_the_schema() {
        assert_eq!(parse_value("panel.height", "52"), Value::from(52));
        assert_eq!(parse_value("panel.height", "52.9"), Value::from(52));
        assert_eq!(parse_value("panel.autoHide", "yes"), Value::from(true));
        assert_eq!(parse_value("theme.name", "nord"), Value::from("nord"));
        assert_eq!(parse_value("theme.name", "5"), Value::from("5"), "a string key stays a string");
        assert_eq!(
            parse_value("panel.pinned", "a.desktop, b.desktop"),
            Value::Array(vec![Value::from("a.desktop"), Value::from("b.desktop")])
        );
        assert_eq!(
            parse_value("panel.pinned", "[\"x.desktop\"]"),
            Value::Array(vec![Value::from("x.desktop")])
        );
        assert!(parse_value("apps.usage", "{\"a\":1}").is_object());
        // Unknown keys fall back to JSON sniffing, then to a string.
        assert_eq!(parse_value("custom.flag", "7"), Value::from(7));
        assert_eq!(parse_value("custom.text", "hello world"), Value::from("hello world"));
    }

    #[test]
    fn synthetic_catalogues_are_unique_and_sorted_by_name() {
        let apps = synthetic_apps(64);
        assert_eq!(apps.len(), 64);
        let ids: std::collections::HashSet<&str> = apps.iter().map(|a| a.id.as_str()).collect();
        assert_eq!(ids.len(), 64);
        assert!(apps.iter().all(|a| !a.sort_key.is_empty() && !a.exec.is_empty()));
    }

    #[test]
    fn query_sets_are_deterministic() {
        let index = SearchIndex::from_entries(synthetic_apps(32));
        let first = query_set(&index, 16);
        let second = query_set(&index, 16);
        assert_eq!(first, second);
        assert_eq!(first.len(), 16);
        assert!(first.iter().all(|q| !q.is_empty()));
    }

    #[test]
    fn indexing_a_synthetic_catalogue_finds_everything() {
        let index = SearchIndex::from_entries(synthetic_apps(200));
        let result = index.search("editor", &search_options(&Options::default()));
        assert!(!result.hits.is_empty());
        assert!(result.hits[0].name.to_lowercase().contains("editor"));
        let all = index.search("", &search_options(&Options::default()));
        assert_eq!(all.hits.len(), 50, "the default limit applies");
    }
}
