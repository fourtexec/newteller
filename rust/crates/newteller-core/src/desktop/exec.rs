//! `Exec` line handling: quoting, field codes and terminal wrapping.
//!
//! `QProcess::splitCommand` understands double quotes but none of the desktop
//! entry specification's field codes, and it cannot produce the argv needed for
//! "open with" (a file argument has to be inserted at `%f`/`%u`, not appended).
//! This module implements both, plus the `PATH` lookup used for `TryExec`.

use std::path::{Path, PathBuf};

use crate::util;

/// Context for [`expand`].
#[derive(Debug, Clone)]
pub struct ExecContext<'a> {
    /// Value of the `Icon` key, used for `%i`.
    pub icon: &'a str,
    /// Localised name, used for `%c`.
    pub name: &'a str,
    /// Path of the desktop file, used for `%k`.
    pub desktop_file: &'a str,
    /// Files or URLs to open, used for `%f` `%F` `%u` `%U`.
    pub files: &'a [String],
    /// Whether the application wants a terminal.
    pub terminal: bool,
    /// Command used to start a terminal application; `%s` is replaced by the
    /// program and the remaining arguments are appended.
    pub terminal_command: &'a str,
    /// Wrap the command in `env VAR=...` pairs (used by plugins).
    pub extra_env: &'a [(String, String)],
}

// Hand written so the crate does not depend on `Default` impls for references.
impl<'a> Default for ExecContext<'a> {
    fn default() -> Self {
        ExecContext {
            icon: "",
            name: "",
            desktop_file: "",
            files: &[],
            terminal: false,
            terminal_command: DEFAULT_TERMINAL_COMMAND,
            extra_env: &[],
        }
    }
}

impl<'a> ExecContext<'a> {
    /// Context that just runs the command with no files.
    pub fn plain() -> Self {
        ExecContext::default()
    }
}

/// Fallback used when the user has not configured a terminal emulator.
pub const DEFAULT_TERMINAL_COMMAND: &str = "x-terminal-emulator -e";

/// Deprecated field codes that must be dropped silently.
const DEPRECATED_CODES: [&str; 6] = ["%d", "%D", "%n", "%N", "%v", "%m"];

/// Splits a command line into arguments using the quoting rules of the desktop
/// entry specification.
///
/// * `"..."` - `\\`, `` \` ``, `\$` and `\"` are escapes, everything else is
///   literal (including spaces).
/// * `'...'` - fully literal until the closing quote.
/// * Outside quotes - whitespace separates, `\\`, `` \` ``, `\$`, `\"`, `\'`
///   are escapes.
pub fn split_command(line: &str) -> Vec<String> {
    let mut args: Vec<String> = Vec::new();
    let mut current = String::new();
    // Tracks whether an argument was started, so `a "" b` yields three
    // arguments instead of two.
    let mut started = false;
    let mut chars = line.chars().peekable();

    while let Some(c) = chars.next() {
        match c {
            '"' => {
                started = true;
                while let Some(&inner) = chars.peek() {
                    if inner == '"' {
                        chars.next();
                        break;
                    }
                    if inner == '\\' {
                        chars.next();
                        push_escape(&mut current, chars.next());
                        continue;
                    }
                    chars.next();
                    current.push(inner);
                }
            }
            '\'' => {
                started = true;
                while let Some(&inner) = chars.peek() {
                    chars.next();
                    if inner == '\'' {
                        break;
                    }
                    current.push(inner);
                }
            }
            '\\' => {
                started = true;
                push_escape(&mut current, chars.next());
            }
            c if c.is_whitespace() => {
                if started {
                    args.push(std::mem::take(&mut current));
                    started = false;
                }
            }
            other => {
                started = true;
                current.push(other);
            }
        }
    }
    if started {
        args.push(current);
    }
    args
}

fn push_escape(out: &mut String, escaped: Option<char>) {
    match escaped {
        // The specification lists \\, \`, \$, \" and \'. A backslash before a
        // space is accepted as well: it matches what shells do and what users
        // type into `Exec` lines, and the value level `\s` escape has already
        // been resolved before we get here.
        Some(c) if matches!(c, '\\' | '`' | '$' | '"' | '\'' | ' ') => out.push(c),
        Some(other) => {
            out.push('\\');
            out.push(other);
        }
        None => out.push('\\'),
    }
}

/// Expands field codes and produces the argv to hand to `execvp`.
///
/// Files are inserted where the field code appears (not appended), `%i` becomes
/// `-i <icon>`, `%c` the localised name and `%k` the desktop file path. The
/// deprecated codes are dropped. When [`ExecContext::terminal`] is set the whole
/// command is wrapped in the terminal emulator.
pub fn expand(line: &str, context: &ExecContext) -> Vec<String> {
    let mut argv: Vec<String> = Vec::new();

    for token in split_command(line) {
        if DEPRECATED_CODES.contains(&token.as_str()) {
            continue;
        }
        match token.as_str() {
            "%f" => {
                if let Some(file) = context.files.first() {
                    argv.push(file.clone());
                }
            }
            "%F" => argv.extend(context.files.iter().cloned()),
            "%u" => {
                if let Some(file) = context.files.first() {
                    argv.push(to_url(file));
                }
            }
            "%U" => argv.extend(context.files.iter().map(|file| to_url(file))),
            "%i" => {
                if !context.icon.is_empty() {
                    argv.push("-i".to_string());
                    argv.push(context.icon.to_string());
                }
            }
            "%c" => {
                if !context.name.is_empty() {
                    argv.push(context.name.to_string());
                }
            }
            "%k" => {
                if !context.desktop_file.is_empty() {
                    argv.push(context.desktop_file.to_string());
                }
            }
            _ => argv.push(token.replace("%%", "%")),
        }
    }

    if argv.is_empty() {
        return argv;
    }

    if context.terminal {
        let wrapper = split_command(if context.terminal_command.is_empty() {
            DEFAULT_TERMINAL_COMMAND
        } else {
            context.terminal_command
        });
        let mut wrapped: Vec<String> = Vec::with_capacity(wrapper.len() + argv.len() + 1);
        let mut inserted_program = false;
        for part in wrapper {
            if part == "%s" {
                wrapped.extend(argv.iter().cloned());
                inserted_program = true;
            } else {
                wrapped.push(part);
            }
        }
        if !inserted_program {
            wrapped.extend(argv.iter().cloned());
        }
        argv = wrapped;
    }

    if !context.extra_env.is_empty() {
        let mut with_env = Vec::with_capacity(argv.len() + context.extra_env.len() * 2 + 1);
        with_env.push("env".to_string());
        for (key, value) in context.extra_env {
            with_env.push(format!("{key}={value}"));
        }
        with_env.extend(argv);
        argv = with_env;
    }

    argv
}

/// The program name that `Exec` will start: the first token, without directory,
/// before any field code. Used to match running processes to applications.
pub fn program_of(line: &str) -> String {
    for token in split_command(line) {
        if token.starts_with('%') && token.len() == 2 {
            continue;
        }
        // Skip environment assignments such as `LANG=C make`.
        if token.contains('=') && !token.starts_with('/') && !token.contains(' ') {
            let before_eq = token.split('=').next().unwrap_or("");
            if !before_eq.is_empty()
                && before_eq
                    .chars()
                    .all(|c| c.is_ascii_alphanumeric() || c == '_')
            {
                continue;
            }
        }
        if token == "env" || token == "sudo" || token == "pkexec" || token == "flatpak-spawn" {
            continue;
        }
        return util::basename(&token).to_string();
    }
    String::new()
}

/// Converts a local path to a `file://` URL. Values that already carry a scheme
/// are returned unchanged.
pub fn to_url(value: &str) -> String {
    let trimmed = value.trim();
    if trimmed.is_empty() {
        return String::new();
    }
    // "http://x", "file:///x", "mailto:x" ... - a scheme before any '/'.
    if let Some(scheme_end) = trimmed.find("://") {
        let head = &trimmed[..scheme_end];
        if !head.is_empty()
            && head
                .chars()
                .all(|c| c.is_ascii_alphanumeric() || c == '+' || c == '-' || c == '.')
        {
            return trimmed.to_string();
        }
    }
    if trimmed.starts_with("mailto:") || trimmed.starts_with("tel:") {
        return trimmed.to_string();
    }
    if trimmed.starts_with('/') {
        return format!("file://{}", percent_encode_path(trimmed));
    }
    // Relative path: make it absolute against the current directory.
    match std::env::current_dir() {
        Ok(cwd) => format!("file://{}", percent_encode_path(&cwd.join(trimmed).to_string_lossy())),
        Err(_) => format!("file://{}", percent_encode_path(trimmed)),
    }
}

/// Percent-encodes a path, keeping the characters that are safe in a URL path.
pub fn percent_encode_path(path: &str) -> String {
    let mut out = String::with_capacity(path.len() + 8);
    for byte in path.as_bytes() {
        let c = *byte as char;
        let safe = c.is_ascii_alphanumeric()
            || matches!(c, '-' | '_' | '.' | '~' | '/' | ':' | '@' | '!' | '$' | '&' | '\''
                | '(' | ')' | '*' | '+' | ',' | ';' | '=');
        if safe {
            out.push(c);
        } else {
            out.push('%');
            out.push_str(&format!("{byte:02X}"));
        }
    }
    out
}

/// Looks a program up in `$PATH`. Absolute or relative paths are checked as is.
pub fn find_in_path(program: &str) -> Option<PathBuf> {
    if program.is_empty() {
        return None;
    }
    if program.contains('/') {
        let path = PathBuf::from(program);
        return is_executable(&path).then_some(path);
    }
    let path_var = std::env::var_os("PATH")?;
    for dir in std::env::split_paths(&path_var) {
        if dir.as_os_str().is_empty() {
            continue;
        }
        let candidate = dir.join(program);
        if is_executable(&candidate) {
            return Some(candidate);
        }
    }
    None
}

/// True when the path exists and has at least one execute bit set.
pub fn is_executable(path: &Path) -> bool {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        match std::fs::metadata(path) {
            Ok(meta) => meta.is_file() && meta.permissions().mode() & 0o111 != 0,
            Err(_) => false,
        }
    }
    #[cfg(not(unix))]
    {
        path.is_file()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn splits_quoted_command_lines() {
        assert_eq!(split_command("a b"), vec!["a", "b"]);
        assert_eq!(split_command(r#"a "b c" d"#), vec!["a", "b c", "d"]);
        assert_eq!(split_command(r#"a 'b \c' d"#), vec!["a", r"b \c", "d"]);
        assert_eq!(split_command(r#"a "" b"#), vec!["a", "", "b"]);
        assert_eq!(split_command(r#"a b\ c"#), vec!["a", "b c"]);
        assert_eq!(split_command("  \t spaced   out  "), vec!["spaced", "out"]);
        assert_eq!(split_command(r#"env A="1 2" prog"#), vec!["env", "A=1 2", "prog"]);
    }

    #[test]
    fn expands_field_codes_in_place() {
        let files = vec!["/tmp/a b.txt".to_string(), "/tmp/c.txt".to_string()];
        let context = ExecContext {
            icon: "firefox",
            name: "Firefox",
            desktop_file: "/usr/share/applications/firefox.desktop",
            files: &files,
            ..ExecContext::plain()
        };
        assert_eq!(
            expand("/usr/bin/firefox %u --new-tab", &context),
            vec!["/usr/bin/firefox", "file:///tmp/a%20b.txt", "--new-tab"]
        );
        assert_eq!(
            expand("/usr/bin/firefox %U", &context),
            vec![
                "/usr/bin/firefox",
                "file:///tmp/a%20b.txt",
                "file:///tmp/c.txt"
            ]
        );
        assert_eq!(
            expand("prog %i %c %k -x", &context),
            vec!["prog", "-i", "firefox", "Firefox",
                 "/usr/share/applications/firefox.desktop", "-x"]
        );
        // Deprecated codes disappear.
        assert_eq!(expand("prog %d %D %n %N %v %m -x", &context), vec!["prog", "-x"]);
        assert_eq!(expand("prog %%literal", &context), vec!["prog", "%literal"]);
    }

    #[test]
    fn wraps_terminal_applications() {
        let context = ExecContext {
            terminal: true,
            terminal_command: "x-terminal-emulator -e",
            ..ExecContext::default()
        };
        assert_eq!(
            expand("htop -d 10", &context),
            vec!["x-terminal-emulator", "-e", "htop", "-d", "10"]
        );

        let custom = ExecContext {
            terminal: true,
            terminal_command: "kitty --title %s",
            ..ExecContext::default()
        };
        // "%s" inside the wrapper is replaced by the whole command.
        let argv = expand("htop", &custom);
        assert_eq!(argv[0], "kitty");
        assert_eq!(argv[1], "--title");
        assert_eq!(argv[2], "htop");
    }

    #[test]
    fn program_of_skips_env_and_field_codes() {
        assert_eq!(program_of("/usr/lib/firefox-esr/firefox-esr %u"), "firefox-esr");
        assert_eq!(program_of("env LANG=C make -j4"), "make");
        assert_eq!(program_of("%f"), "");
        assert_eq!(program_of("sh -c 'echo hi'"), "sh");
    }

    #[test]
    fn urls_are_encoded_once() {
        assert_eq!(to_url("https://example.org/a b"), "https://example.org/a b");
        assert_eq!(to_url("/tmp/a b.txt"), "file:///tmp/a%20b.txt");
        assert_eq!(to_url("mailto:a@b.c"), "mailto:a@b.c");
        assert_eq!(percent_encode_path("/a+b/c~d"), "/a+b/c~d");
    }

    #[test]
    fn path_lookup_rejects_missing_programs() {
        assert!(find_in_path("").is_none());
        assert!(find_in_path("definitely-not-a-real-program-xyz").is_none());
    }
}
