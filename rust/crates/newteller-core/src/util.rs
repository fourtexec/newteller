//! Small shared helpers: file reads, path expansion, accent folding and the
//! bounded edit distance used for typo tolerance.
//!
//! Nothing in here allocates a global cache or spawns a thread, so every
//! function is trivially callable from the FFI layer.

use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

/// Reads a file into a `String`, replacing invalid UTF-8 instead of failing.
///
/// `/proc` and `/sys` files report a size of zero but still read fine, so this
/// never pre-allocates from `metadata().len()`.
pub fn read_to_string_lossy(path: &Path) -> Option<String> {
    match std::fs::read(path) {
        Ok(bytes) => Some(String::from_utf8_lossy(&bytes).into_owned()),
        Err(_) => None,
    }
}

/// Like [`read_to_string_lossy`] but caps the amount of data read, so a
/// pathological file (for example `/dev/zero` behind a symlink) cannot exhaust
/// memory. Returns `None` when the file cannot be opened.
pub fn read_capped(path: &Path, max_bytes: usize) -> Option<String> {
    use std::io::Read;
    let mut file = std::fs::File::open(path).ok()?;
    let mut buf = Vec::with_capacity(max_bytes.min(64 * 1024));
    let mut chunk = [0u8; 8192];
    while buf.len() < max_bytes {
        let want = chunk.len().min(max_bytes - buf.len());
        match file.read(&mut chunk[..want]) {
            Ok(0) => break,
            Ok(n) => buf.extend_from_slice(&chunk[..n]),
            Err(ref e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(_) => break,
        }
    }
    Some(String::from_utf8_lossy(&buf).into_owned())
}

/// `$HOME`, without the deprecated `std::env::home_dir`.
pub fn home_dir() -> Option<PathBuf> {
    std::env::var_os("HOME").map(PathBuf::from).filter(|p| !p.as_os_str().is_empty())
}

/// Expands a leading `~/` (or a bare `~`) against [`home_dir`].
pub fn expand_home(input: &str) -> PathBuf {
    if input == "~" {
        return home_dir().unwrap_or_else(|| PathBuf::from("~"));
    }
    if let Some(rest) = input.strip_prefix("~/") {
        if let Some(home) = home_dir() {
            return home.join(rest);
        }
    }
    PathBuf::from(input)
}

/// Milliseconds since the Unix epoch; `0` if the system clock is before 1970.
pub fn now_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as u64)
        .unwrap_or(0)
}

/// Lower-cases a single character and strips its diacritics, always producing
/// exactly one character so that indices stay valid on the original string.
///
/// This is deliberately a small hand written table: the shell runs on Latin
/// scripts in practice and pulling in the whole Unicode decomposition database
/// would cost more than it saves. Characters outside the table fall back to
/// `char::to_lowercase`.
pub fn fold_char(c: char) -> char {
    // `to_lowercase` can yield several scalars (for example 'İ'); take the first
    // one so the mapping stays 1:1 and highlight ranges keep matching.
    let lower = c.to_lowercase().next().unwrap_or(c);
    match lower {
        'á' | 'à' | 'â' | 'ä' | 'ã' | 'å' | 'ā' | 'ă' | 'ą' | 'ǎ' | 'ǻ' => 'a',
        'æ' => 'a',
        'ß' => 's',
        'ç' | 'ć' | 'č' | 'ĉ' | 'ċ' => 'c',
        'ď' | 'đ' | 'ð' => 'd',
        'é' | 'è' | 'ê' | 'ë' | 'ē' | 'ĕ' | 'ė' | 'ę' | 'ě' => 'e',
        'ĝ' | 'ğ' | 'ġ' | 'ģ' | 'ǧ' => 'g',
        'ĥ' | 'ħ' => 'h',
        'í' | 'ì' | 'î' | 'ï' | 'ĩ' | 'ī' | 'ĭ' | 'į' | 'ı' | 'ǐ' => 'i',
        'ĵ' | 'ǰ' => 'j',
        'ķ' | 'ǩ' => 'k',
        'ĺ' | 'ļ' | 'ľ' | 'ŀ' | 'ł' => 'l',
        'ñ' | 'ń' | 'ņ' | 'ň' | 'ŉ' | 'ŋ' => 'n',
        'ó' | 'ò' | 'ô' | 'ö' | 'õ' | 'ø' | 'ō' | 'ŏ' | 'ő' | 'ǒ' | 'ǫ' | 'ǭ' | 'œ' => 'o',
        'ŕ' | 'ŗ' | 'ř' => 'r',
        'ś' | 'ŝ' | 'ş' | 'š' | 'ș' => 's',
        'ť' | 'ţ' | 'ț' | 'ŧ' | 'þ' => 't',
        'ú' | 'ù' | 'û' | 'ü' | 'ũ' | 'ū' | 'ŭ' | 'ů' | 'ű' | 'ų' | 'ǔ' | 'ǖ' | 'ǘ' | 'ǚ'
        | 'ǜ' => 'u',
        'ŵ' => 'w',
        'ý' | 'ÿ' | 'ŷ' => 'y',
        'ź' | 'ż' | 'ž' => 'z',
        'ǆ' | 'ǳ' => 'z',
        '·' | '・' => '-',
        _ => lower,
    }
}

/// Folds a whole string with [`fold_char`].
pub fn fold_str(input: &str) -> String {
    input.chars().map(fold_char).collect()
}

/// Returns true when every character of the string is ASCII.
pub fn is_ascii(input: &str) -> bool {
    input.is_ascii()
}

/// First index of `needle` inside `hay`, comparing whole `char`s.
///
/// The search index works on `Vec<char>` rather than on byte slices so that
/// every offset is a character offset, which is what the QML highlighter needs.
pub fn find_subslice(hay: &[char], needle: &[char]) -> Option<usize> {
    if needle.is_empty() {
        return Some(0);
    }
    if needle.len() > hay.len() {
        return None;
    }
    let last = hay.len() - needle.len();
    // Skip ahead on the first character: names are short, this is enough to keep
    // the scan cheap without pulling in a substring-search dependency.
    let first = needle[0];
    for start in 0..=last {
        if hay[start] != first {
            continue;
        }
        if hay[start..start + needle.len()] == *needle {
            return Some(start);
        }
    }
    None
}

/// True when `hay` starts with `needle`.
pub fn starts_with_chars(hay: &[char], needle: &[char]) -> bool {
    hay.len() >= needle.len() && hay[..needle.len()] == *needle
}

/// File name part of a path or command, without touching the filesystem.
pub fn basename(input: &str) -> &str {
    let trimmed = input.trim_end_matches('/');
    match trimmed.rsplit_once('/') {
        Some((_, last)) => last,
        None => trimmed,
    }
}

/// Splits an environment list such as `XDG_CURRENT_DESKTOP=GNOME:Unity` into
/// its members.
pub fn split_env_list(input: &str) -> Vec<String> {
    input
        .split(|c| c == ':' || c == ';')
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
        .collect()
}

/// Damerau-Levenshtein (optimal string alignment) distance, or `None` when the
/// distance is greater than `max`.
///
/// Used for typo tolerance: start-menu queries are short, so the quadratic scan
/// is bounded and the early exit on the row minimum keeps the common "no match"
/// case cheap.
pub fn edit_distance_limited(a: &[char], b: &[char], max: usize) -> Option<usize> {
    let n = a.len();
    let m = b.len();
    if n.abs_diff(m) > max {
        return None;
    }
    if n == 0 {
        return Some(m);
    }
    if m == 0 {
        return Some(n);
    }

    // Row i-2 is only needed for transpositions; start it far above `max` so
    // that adding a cost can never wrap around.
    let sentinel = max.saturating_add(1).saturating_mul(4).max(64);
    let mut prev2 = vec![sentinel; m + 1];
    let mut prev1: Vec<usize> = (0..=m).collect();
    let mut cur = vec![0usize; m + 1];

    for i in 1..=n {
        cur[0] = i;
        let mut row_min = i;
        for j in 1..=m {
            let cost = usize::from(a[i - 1] != b[j - 1]);
            let mut value = (prev1[j] + 1).min(cur[j - 1] + 1).min(prev1[j - 1] + cost);
            if i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] {
                value = value.min(prev2[j - 2] + cost);
            }
            cur[j] = value;
            if value < row_min {
                row_min = value;
            }
        }
        if row_min > max {
            return None;
        }
        std::mem::swap(&mut prev2, &mut prev1);
        std::mem::swap(&mut prev1, &mut cur);
    }

    let distance = prev1[m];
    if distance <= max {
        Some(distance)
    } else {
        None
    }
}

/// Formats a byte count the way the panel widgets want to print it.
pub fn format_bytes(bytes: u64) -> String {
    const UNITS: [&str; 5] = ["B", "KiB", "MiB", "GiB", "TiB"];
    let mut value = bytes as f64;
    let mut unit = 0;
    while value >= 1024.0 && unit + 1 < UNITS.len() {
        value /= 1024.0;
        unit += 1;
    }
    if unit == 0 {
        format!("{bytes} {}", UNITS[unit])
    } else {
        format!("{value:.1} {}", UNITS[unit])
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn folding_is_length_preserving_and_lowercase() {
        assert_eq!(fold_char('É'), 'e');
        assert_eq!(fold_char('ß'), 's');
        assert_eq!(fold_char('Z'), 'z');
        assert_eq!(fold_str("Święta").chars().count(), "Święta".chars().count());
        assert_eq!(fold_str("Święta"), "swieta");
    }

    #[test]
    fn subslice_search_reports_char_offsets() {
        let hay: Vec<char> = "gömül".chars().collect();
        let needle: Vec<char> = "mul".chars().collect();
        assert_eq!(find_subslice(&hay, &needle), None); // folding happens earlier
        let folded: Vec<char> = fold_str("gömül").chars().collect();
        assert_eq!(find_subslice(&folded, &needle), Some(2));
    }

    #[test]
    fn basename_strips_directories_and_trailing_slash() {
        assert_eq!(basename("/usr/bin/firefox-esr"), "firefox-esr");
        assert_eq!(basename("/usr/lib/xorg/"), "xorg");
        assert_eq!(basename("ls"), "ls");
    }

    #[test]
    fn edit_distance_honours_the_limit_and_transpositions() {
        let a: Vec<char> = "kitten".chars().collect();
        let b: Vec<char> = "sitting".chars().collect();
        assert_eq!(edit_distance_limited(&a, &b, 3), Some(3));
        assert_eq!(edit_distance_limited(&a, &b, 2), None);
        let x: Vec<char> = "ab".chars().collect();
        let y: Vec<char> = "ba".chars().collect();
        assert_eq!(edit_distance_limited(&x, &y, 1), Some(1));
        assert_eq!(edit_distance_limited(&[], &y, 2), Some(2));
    }

    #[test]
    fn byte_formatting_picks_a_unit() {
        assert_eq!(format_bytes(512), "512 B");
        assert_eq!(format_bytes(2048), "2.0 KiB");
    }
}
