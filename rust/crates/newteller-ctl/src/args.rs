//! Hand written argument parsing.
//!
//! A dependency free parser on purpose: this binary is shipped inside the
//! release archive and has to build with nothing but the workspace crates.

/// Parsed command line.
#[derive(Debug, Clone)]
pub struct Options {
    /// Subcommand, always non-empty for [`Parsed::Run`].
    pub command: String,
    /// Everything that is not a flag, in order.
    pub positional: Vec<String>,
    /// Machine readable output.
    pub json: bool,
    /// Allow overwriting an existing file.
    pub force: bool,
    /// Maximum number of results.
    pub limit: usize,
    /// Typo tolerance on or off.
    pub fuzzy: bool,
    /// Highlight colour for search results.
    pub colour: Option<String>,
    /// Application directories, overriding the XDG defaults.
    pub dirs: Vec<String>,
    /// Sampling interval in milliseconds.
    pub interval_ms: u64,
    /// Number of samples.
    pub count: usize,
    /// Number of events before `watch` stops; `0` means unlimited.
    pub events: usize,
    /// Synthetic catalogue size for `bench`.
    pub apps: usize,
    /// Number of queries for `bench`.
    pub queries: usize,
    /// Debounce window for `watch`.
    pub debounce_ms: u32,
    /// Follow sub-directories in `watch`.
    pub recursive: bool,
    /// Icon name for `notify`.
    pub icon: Option<String>,
    /// Popup timeout in milliseconds for `notify`; 0 keeps it up.
    pub timeout_ms: Option<i64>,
}

impl Default for Options {
    fn default() -> Self {
        Options {
            command: String::new(),
            positional: Vec::new(),
            json: false,
            force: false,
            limit: 50,
            fuzzy: true,
            colour: None,
            dirs: Vec::new(),
            interval_ms: 2_000,
            count: 1,
            events: 0,
            apps: 0,
            queries: 200,
            debounce_ms: 250,
            recursive: false,
            icon: None,
            timeout_ms: None,
        }
    }
}

/// Outcome of parsing.
#[derive(Debug)]
pub enum Parsed {
    /// A command to run.
    Run(Options),
    /// `--help`.
    Help,
    /// `--version`.
    Version,
    /// Usage error, with the message to print.
    Error(String),
}

/// Parses the arguments, without the program name.
pub fn parse(argv: &[String]) -> Parsed {
    let mut options = Options::default();
    let mut index = 0usize;
    let mut only_positional = false;

    while index < argv.len() {
        let token = argv[index].clone();
        index += 1;

        if only_positional || !token.starts_with('-') || token == "-" {
            if options.command.is_empty() {
                options.command = token;
            } else {
                options.positional.push(token);
            }
            continue;
        }
        if token == "--" {
            only_positional = true;
            continue;
        }

        let (flag, inline) = match token.split_once('=') {
            Some((flag, value)) => (flag.to_string(), Some(value.to_string())),
            None => (token.clone(), None),
        };

        macro_rules! value {
            () => {
                match inline.clone() {
                    Some(value) => value,
                    None => {
                        if index >= argv.len() {
                            return Parsed::Error(format!("{flag} needs a value"));
                        }
                        let value = argv[index].clone();
                        index += 1;
                        value
                    }
                }
            };
        }
        macro_rules! number {
            ($kind:ty) => {{
                let raw = value!();
                match raw.parse::<$kind>() {
                    Ok(number) => number,
                    Err(_) => return Parsed::Error(format!("{flag} needs a number, got '{raw}'")),
                }
            }};
        }

        match flag.as_str() {
            "-h" | "--help" => return Parsed::Help,
            "-V" | "--version" => return Parsed::Version,
            "--json" => options.json = true,
            "--force" => options.force = true,
            "--fuzzy" => options.fuzzy = true,
            "--no-fuzzy" => options.fuzzy = false,
            "--recursive" => options.recursive = true,
            "--limit" => options.limit = number!(usize),
            "--count" => options.count = number!(usize),
            "--events" => options.events = number!(usize),
            "--apps" => options.apps = number!(usize),
            "--queries" => options.queries = number!(usize),
            "--interval" => options.interval_ms = number!(u64),
            "--debounce" => options.debounce_ms = number!(u32),
            "--icon" => options.icon = Some(value!()),
            "--timeout" => options.timeout_ms = Some(number!(i64)),
            "--colour" | "--color" => {
                let value = value!();
                options.colour = Some(if value.starts_with('#') {
                    value
                } else {
                    format!("#{value}")
                });
            }
            "--dirs" => {
                let raw = value!();
                options.dirs = raw
                    .split(':')
                    .map(|part| part.trim().to_string())
                    .filter(|part| !part.is_empty())
                    .collect();
            }
            other => return Parsed::Error(format!("unknown flag {other}")),
        }
    }

    if options.command.is_empty() {
        return Parsed::Help;
    }
    Parsed::Run(options)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn argv(items: &[&str]) -> Vec<String> {
        items.iter().map(|item| item.to_string()).collect()
    }

    #[test]
    fn separates_the_command_from_flags_and_arguments() {
        match parse(&argv(&["search", "fire", "--limit", "10", "--json"])) {
            Parsed::Run(options) => {
                assert_eq!(options.command, "search");
                assert_eq!(options.positional, vec!["fire".to_string()]);
                assert_eq!(options.limit, 10);
                assert!(options.json);
                assert!(options.fuzzy);
            }
            other => panic!("unexpected {other:?}"),
        }
    }

    #[test]
    fn accepts_inline_values_and_no_fuzzy() {
        match parse(&argv(&["search", "--limit=5", "--no-fuzzy", "--colour=ff0000"])) {
            Parsed::Run(options) => {
                assert_eq!(options.limit, 5);
                assert!(!options.fuzzy);
                assert_eq!(options.colour.as_deref(), Some("#ff0000"));
            }
            other => panic!("unexpected {other:?}"),
        }
    }

    #[test]
    fn stops_parsing_flags_after_a_double_dash() {
        match parse(&argv(&["set", "panel.pinned", "--", "--json"])) {
            Parsed::Run(options) => {
                assert_eq!(options.command, "set");
                assert_eq!(options.positional, vec!["panel.pinned".to_string(), "--json".to_string()]);
                assert!(!options.json);
            }
            other => panic!("unexpected {other:?}"),
        }
    }

    #[test]
    fn reports_usage_errors() {
        assert!(matches!(parse(&argv(&["search", "--limit"])), Parsed::Error(_)));
        assert!(matches!(parse(&argv(&["search", "--limit", "x"])), Parsed::Error(_)));
        assert!(matches!(parse(&argv(&["search", "--nope"])), Parsed::Error(_)));
        assert!(matches!(parse(&argv(&[])), Parsed::Help));
        assert!(matches!(parse(&argv(&["--help"])), Parsed::Help));
        assert!(matches!(parse(&argv(&["-V"])), Parsed::Version));
    }

    #[test]
    fn splits_directory_lists() {
        match parse(&argv(&["apps", "--dirs", "/a:/b:"])) {
            Parsed::Run(options) => assert_eq!(options.dirs, vec!["/a".to_string(), "/b".to_string()]),
            other => panic!("unexpected {other:?}"),
        }
    }
}
