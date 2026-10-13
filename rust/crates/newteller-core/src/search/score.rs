//! Query classification and scoring.
//!
//! The start menu used to sort by "name starts with query", "name contains
//! query", "comment contains query". That order is kept - it is what users
//! expect - but each tier is now a numeric score with position, contiguity and
//! usage adjustments, and two more tiers were added: acronym matches (`gimp`
//! for "GNU Image Manipulation Program" style names) and typo tolerant matches.

use crate::search::normalize::Normalized;
use crate::util;

/// Which field of an application produced the match. Later fields can never
/// outrank earlier ones, because their weight is lower.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Field {
    /// The application name.
    Name,
    /// `Keywords`.
    Keywords,
    /// `GenericName`, `Comment` and `Categories`.
    Meta,
}

impl Field {
    /// Percentage applied to the raw score.
    pub fn weight(self) -> i32 {
        match self {
            Field::Name => 100,
            Field::Keywords => 60,
            Field::Meta => 35,
        }
    }

    /// Stable name used in the JSON payload and in QML.
    pub fn as_str(self) -> &'static str {
        match self {
            Field::Name => "name",
            Field::Keywords => "keywords",
            Field::Meta => "meta",
        }
    }
}

/// Why an application matched.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MatchKind {
    /// Folded name equals the query.
    Exact,
    /// Folded name starts with the query.
    Prefix,
    /// A word of the name starts with the query.
    Word,
    /// The query matches the first letters of the words.
    Acronym,
    /// The query appears inside the name.
    Substring,
    /// The query appears in the characters of the name, with gaps.
    Fuzzy,
    /// The query is within the edit distance limit of a word.
    Typo,
    /// Nothing matched.
    None,
}

impl MatchKind {
    /// Stable name used in the JSON payload.
    pub fn as_str(self) -> &'static str {
        match self {
            MatchKind::Exact => "exact",
            MatchKind::Prefix => "prefix",
            MatchKind::Word => "word",
            MatchKind::Acronym => "acronym",
            MatchKind::Substring => "substring",
            MatchKind::Fuzzy => "fuzzy",
            MatchKind::Typo => "typo",
            MatchKind::None => "none",
        }
    }

    /// Raw score of the tier, before [`Field`] weighting and adjustments.
    pub fn base(self) -> i32 {
        match self {
            MatchKind::Exact => 100_000,
            MatchKind::Prefix => 80_000,
            MatchKind::Word => 70_000,
            MatchKind::Acronym => 65_000,
            MatchKind::Substring => 50_000,
            MatchKind::Fuzzy => 20_000,
            MatchKind::Typo => 10_000,
            MatchKind::None => 0,
        }
    }
}

/// A parsed query.
#[derive(Debug, Clone)]
pub struct Query {
    /// Original query text, trimmed.
    pub text: String,
    /// Folded characters of the query.
    pub fold: Vec<char>,
    /// Whether typo tolerance is enabled.
    pub fuzzy: bool,
    /// Maximum edit distance for [`MatchKind::Typo`].
    pub max_typo: usize,
}

impl Query {
    /// Parses a query. `fuzzy` enables typo tolerance for queries of at least
    /// three characters; the allowed distance grows with the query length.
    pub fn new(text: &str, fuzzy: bool) -> Query {
        let trimmed = text.trim();
        let fold: Vec<char> = trimmed.chars().map(util::fold_char).collect();
        let max_typo = if fold.len() >= 8 {
            2
        } else if fold.len() >= 4 {
            1
        } else {
            0
        };
        Query {
            text: trimmed.to_string(),
            fold,
            fuzzy: fuzzy && max_typo > 0,
            max_typo,
        }
    }

    /// True when nothing was typed.
    pub fn is_empty(&self) -> bool {
        self.fold.is_empty()
    }

    /// Number of characters in the folded query.
    pub fn len(&self) -> usize {
        self.fold.len()
    }
}

/// The outcome of comparing one normalised field with a query.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Scored {
    /// Why it matched.
    pub kind: MatchKind,
    /// Final score, already weighted by [`Field`].
    pub score: i32,
    /// Matched character ranges, for highlighting.
    pub ranges: Vec<(usize, usize)>,
}

/// Scores `norm` against `query` for one field.
///
/// Tiers are tried in the order users expect: an exact name beats a prefix,
/// a prefix beats a word start, and so on. Returns `None` when nothing matched.
pub fn score_in(norm: &Normalized, query: &Query, field: Field) -> Option<Scored> {
    if query.is_empty() || norm.is_empty() {
        return None;
    }
    let hay = &norm.fold;
    let needle = &query.fold;

    // 1. exact
    if hay.len() == needle.len() && hay == needle {
        return Some(finalize(MatchKind::Exact, 0, vec![(0, hay.len())], field));
    }

    // 2. prefix
    if util::starts_with_chars(hay, needle) {
        let penalty = ((hay.len() - needle.len()) * 20) as i32;
        return Some(finalize(MatchKind::Prefix, -penalty, vec![(0, needle.len())], field));
    }

    // 3. a word of the field starts with the query
    for (position, token) in norm.tokens.iter().enumerate() {
        if token.len < needle.len() {
            continue;
        }
        let token_chars: Vec<char> = token.text.chars().collect();
        if util::starts_with_chars(&token_chars, needle) {
            let penalty = (position * 120) as i32 + ((token.len - needle.len()) * 15) as i32;
            let range = (token.start, token.start + needle.len());
            return Some(finalize(MatchKind::Word, -penalty, vec![range], field));
        }
    }

    // 4. acronym: the query is a prefix of the word initials
    if needle.len() >= 2
        && needle.len() <= norm.initials.len()
        && norm.initials[..needle.len()] == *needle
    {
        let ranges: Vec<(usize, usize)> = norm.initial_at[..needle.len()]
            .iter()
            .map(|&at| (at, at + 1))
            .collect();
        let penalty = ((norm.initials.len() - needle.len()) * 60) as i32;
        return Some(finalize(MatchKind::Acronym, -penalty, ranges, field));
    }

    // 5. substring anywhere
    if let Some(at) = util::find_subslice(hay, needle) {
        let penalty = (at * 30) as i32 + ((hay.len() - needle.len()) * 10) as i32;
        return Some(finalize(
            MatchKind::Substring,
            -penalty,
            vec![(at, at + needle.len())],
            field,
        ));
    }

    // 6. subsequence with gaps
    if needle.len() >= 2 {
        if let Some((positions, bonus)) = subsequence(hay, needle) {
            let ranges = merge_positions(&positions);
            return Some(finalize(MatchKind::Fuzzy, bonus, ranges, field));
        }
    }

    // 7. typo tolerance, per word
    if query.fuzzy {
        let mut best: Option<(usize, usize)> = None;
        for token in &norm.tokens {
            if token.len < 3 {
                continue;
            }
            let token_chars: Vec<char> = token.text.chars().collect();
            if let Some(distance) =
                util::edit_distance_limited(&token_chars, needle, query.max_typo)
            {
                if best.map(|(d, _)| distance < d).unwrap_or(true) {
                    best = Some((distance, token.start));
                }
            }
        }
        if let Some((distance, start)) = best {
            let needle_len = needle.len().min(
                norm.chars.len().saturating_sub(start),
            );
            let penalty = (distance as i32) * 3_000;
            return Some(finalize(
                MatchKind::Typo,
                -penalty,
                vec![(start, start + needle_len)],
                field,
            ));
        }
    }

    None
}

fn finalize(kind: MatchKind, adjustment: i32, ranges: Vec<(usize, usize)>, field: Field) -> Scored {
    let raw = kind.base() + adjustment;
    let weighted = (raw as i64 * field.weight() as i64 / 100).max(1) as i32;
    Scored {
        kind,
        score: weighted,
        ranges,
    }
}

/// Greedy subsequence match that prefers contiguous runs and word starts.
///
/// Returns the matched offsets and an adjustment: contiguous characters are
/// worth much more than scattered ones, and a match anchored at the beginning
/// of the field beats one that starts in the middle.
fn subsequence(hay: &[char], needle: &[char]) -> Option<(Vec<usize>, i32)> {
    let mut positions = Vec::with_capacity(needle.len());
    let mut cursor = 0usize;
    let mut bonus = 0i32;
    let mut previous: Option<usize> = None;

    for &want in needle {
        let mut found: Option<usize> = None;
        for index in cursor..hay.len() {
            if hay[index] == want {
                found = Some(index);
                break;
            }
        }
        let index = found?;
        match previous {
            Some(before) if index == before + 1 => bonus += 2_000,
            Some(before) => bonus -= 300 * ((index - before - 1).min(8) as i32),
            None if index == 0 => bonus += 1_500,
            None => bonus -= 200 * (index.min(12) as i32),
        }
        positions.push(index);
        previous = Some(index);
        cursor = index + 1;
    }

    Some((positions, bonus))
}

/// Merges consecutive offsets into `(start, end)` ranges.
fn merge_positions(positions: &[usize]) -> Vec<(usize, usize)> {
    let mut ranges: Vec<(usize, usize)> = Vec::new();
    for &index in positions {
        match ranges.last_mut() {
            Some(last) if last.1 == index => last.1 = index + 1,
            _ => ranges.push((index, index + 1)),
        }
    }
    ranges
}

/// Escapes the characters that would otherwise be interpreted as markup.
pub fn escape_html(input: &str) -> String {
    let mut out = String::with_capacity(input.len());
    for c in input.chars() {
        push_escaped(&mut out, c);
    }
    out
}

fn push_escaped(out: &mut String, c: char) {
    match c {
        '&' => out.push_str("&amp;"),
        '<' => out.push_str("&lt;"),
        '>' => out.push_str("&gt;"),
        '"' => out.push_str("&quot;"),
        _ => out.push(c),
    }
}

/// Renders `original` as Qt StyledText with the matched ranges emphasised.
///
/// `color` may be `None`, in which case only `<b>` is used. Ranges are
/// character offsets into `original`; overlapping or unsorted ranges are merged
/// first so malformed input cannot produce unbalanced tags.
pub fn highlight(original: &str, ranges: &[(usize, usize)], color: Option<&str>) -> String {
    let merged = merge_ranges(ranges);
    let chars: Vec<char> = original.chars().collect();
    if merged.is_empty() {
        return escape_html(original);
    }

    let use_color = color.map(|c| !c.trim().is_empty()).unwrap_or(false);
    let open = if use_color {
        format!("<font color=\"{}\"><b>", escape_html(color.unwrap_or("")))
    } else {
        "<b>".to_string()
    };
    let close = if use_color { "</b></font>" } else { "</b>" };

    let mut out = String::with_capacity(original.len() + merged.len() * 24);
    let mut inside = false;
    let mut next = 0usize;
    for index in 0..chars.len() {
        if !inside && next < merged.len() && index == merged[next].0 {
            out.push_str(&open);
            inside = true;
        }
        if inside && index >= merged[next].1 {
            out.push_str(close);
            inside = false;
            next += 1;
            if next < merged.len() && index == merged[next].0 {
                out.push_str(&open);
                inside = true;
            }
        }
        push_escaped(&mut out, chars[index]);
    }
    if inside {
        out.push_str(close);
    }
    out
}

/// Sorts ranges and merges the ones that touch or overlap.
pub fn merge_ranges(ranges: &[(usize, usize)]) -> Vec<(usize, usize)> {
    let mut sorted: Vec<(usize, usize)> = ranges
        .iter()
        .filter(|(start, end)| end > start)
        .copied()
        .collect();
    sorted.sort_unstable();
    let mut merged: Vec<(usize, usize)> = Vec::with_capacity(sorted.len());
    for (start, end) in sorted {
        match merged.last_mut() {
            Some(last) if start <= last.1 => {
                if end > last.1 {
                    last.1 = end;
                }
            }
            _ => merged.push((start, end)),
        }
    }
    merged
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::search::normalize::normalize;

    fn name_score(name: &str, query: &str) -> Option<Scored> {
        score_in(&normalize(name), &Query::new(query, true), Field::Name)
    }

    #[test]
    fn tiers_are_ordered_as_documented() {
        let exact = name_score("Firefox", "firefox").unwrap();
        let prefix = name_score("Firefox", "fire").unwrap();
        let word = name_score("GNU Image Manipulation Program", "image").unwrap();
        let acronym = name_score("GNU Image Manipulation Program", "gimp").unwrap();
        let substring = name_score("Firefox", "ref").unwrap();
        let fuzzy = name_score("Firefox", "frfx").unwrap();

        assert_eq!(exact.kind, MatchKind::Exact);
        assert_eq!(prefix.kind, MatchKind::Prefix);
        assert_eq!(word.kind, MatchKind::Word);
        assert_eq!(acronym.kind, MatchKind::Acronym);
        assert_eq!(substring.kind, MatchKind::Substring);
        assert_eq!(fuzzy.kind, MatchKind::Fuzzy);
        assert!(exact.score > prefix.score);
        assert!(prefix.score > word.score);
        assert!(word.score > acronym.score);
        assert!(acronym.score > substring.score);
        assert!(substring.score > fuzzy.score);
    }

    #[test]
    fn matching_is_accent_and_case_insensitive() {
        let scored = name_score("Öffner", "offn").unwrap();
        assert_eq!(scored.kind, MatchKind::Prefix);
        assert_eq!(scored.ranges, vec![(0, 4)]);
        assert_eq!(name_score("Éditeur de texte", "editeur").unwrap().kind, MatchKind::Prefix);
    }

    #[test]
    fn typo_tolerance_is_length_gated() {
        // Three characters: no typo tolerance, but "trm" is a subsequence.
        let short = Query::new("trm", true);
        assert_eq!(short.max_typo, 0);
        assert!(!short.fuzzy);

        let typo = name_score("Terminal", "terminak").unwrap();
        assert_eq!(typo.kind, MatchKind::Typo);

        let far = name_score("Terminal", "xylophon");
        assert!(far.is_none());
    }

    #[test]
    fn fields_are_weighted() {
        let norm = normalize("browser");
        let query = Query::new("browser", false);
        let as_name = score_in(&norm, &query, Field::Name).unwrap();
        let as_keywords = score_in(&norm, &query, Field::Keywords).unwrap();
        let as_meta = score_in(&norm, &query, Field::Meta).unwrap();
        assert!(as_name.score > as_keywords.score);
        assert!(as_keywords.score > as_meta.score);
    }

    #[test]
    fn highlight_produces_balanced_styled_text() {
        assert_eq!(highlight("Firefox", &[(0, 4)], None), "<b>Fire</b>fox");
        assert_eq!(
            highlight("Firefox", &[(0, 4)], Some("#ff0000")),
            "<font color=\"#ff0000\"><b>Fire</b></font>fox"
        );
        assert_eq!(highlight("A & B <x>", &[], None), "A &amp; B &lt;x&gt;");
        // Overlapping ranges are merged instead of nesting.
        assert_eq!(highlight("abcdef", &[(0, 3), (2, 5)], None), "<b>abcde</b>f");
        // Out of range input cannot panic.
        assert_eq!(highlight("ab", &[(0, 99)], None), "<b>ab</b>");
    }

    #[test]
    fn subsequence_prefers_contiguous_runs() {
        let hay: Vec<char> = "gthumb".chars().collect();
        let (positions, bonus) = subsequence(&hay, &['t', 'h']).unwrap();
        assert_eq!(positions, vec![1, 2]);
        assert!(bonus > 0);
    }

    #[test]
    fn empty_queries_never_match() {
        assert!(name_score("Firefox", "").is_none());
        assert!(name_score("", "fire").is_none());
    }
}
