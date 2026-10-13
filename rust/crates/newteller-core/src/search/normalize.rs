//! Text normalisation shared by the search index.
//!
//! Every comparison the index makes happens on accent folded, lower-cased
//! `char` vectors. Working on `Vec<char>` (instead of `&str`) keeps every
//! offset a *character* offset, which is what the QML highlighter needs - a byte
//! offset would be wrong for any non-ASCII application name.

use crate::util;

/// One word of a normalised string, in character offsets.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Token {
    /// Offset of the first character in [`Normalized::chars`].
    pub start: usize,
    /// Number of characters.
    pub len: usize,
    /// Folded text of the token.
    pub text: String,
}

impl Token {
    /// Folded characters of the token.
    pub fn chars(&self) -> Vec<char> {
        self.text.chars().collect()
    }
}

/// Normalised form of one searchable string.
#[derive(Debug, Clone, Default)]
pub struct Normalized {
    /// The original text.
    pub original: String,
    /// Characters of the original text.
    pub chars: Vec<char>,
    /// Folded characters, index aligned with [`Normalized::chars`].
    pub fold: Vec<char>,
    /// `fold` as a `String`, for the cheap operations that do not need offsets.
    pub folded: String,
    /// Words, split on non-alphanumerics and camel case boundaries.
    pub tokens: Vec<Token>,
    /// First folded character of every token.
    pub initials: Vec<char>,
    /// Character offset of every initial, index aligned with
    /// [`Normalized::initials`].
    pub initial_at: Vec<usize>,
    /// Folded trigrams of every token, for the inverted index.
    pub trigrams: Vec<String>,
}

impl Normalized {
    /// An empty normalised string.
    pub fn empty() -> Normalized {
        Normalized::default()
    }

    /// True when there is nothing to match against.
    pub fn is_empty(&self) -> bool {
        self.fold.is_empty()
    }

    /// True when the folded text starts with the folded needle.
    pub fn starts_with(&self, needle: &[char]) -> bool {
        util::starts_with_chars(&self.fold, needle)
    }

    /// Character offset of the folded needle, if present.
    pub fn find(&self, needle: &[char]) -> Option<usize> {
        util::find_subslice(&self.fold, needle)
    }

    /// Approximate heap footprint in bytes, reported by `nt_index_stats`.
    pub fn memory(&self) -> usize {
        self.original.len()
            + (self.chars.len() + self.fold.len()) * std::mem::size_of::<char>()
            + self.folded.len()
            + self.tokens.iter().map(|t| t.text.len() + std::mem::size_of::<Token>()).sum::<usize>()
            + self.initials.len() * std::mem::size_of::<char>()
            + self.initial_at.len() * std::mem::size_of::<usize>()
            + self.trigrams.iter().map(|t| t.len()).sum::<usize>()
    }
}

/// Builds the normalised form of `input`.
pub fn normalize(input: &str) -> Normalized {
    let chars: Vec<char> = input.chars().collect();
    let fold: Vec<char> = chars.iter().map(|c| util::fold_char(*c)).collect();
    let folded: String = fold.iter().collect();

    let mut tokens: Vec<Token> = Vec::new();
    let mut start: Option<usize> = None;
    for i in 0..chars.len() {
        if !fold[i].is_alphanumeric() {
            if let Some(begin) = start.take() {
                tokens.push(make_token(&fold, begin, i));
            }
            continue;
        }
        // "FirefoxReader" -> "Firefox", "Reader"; "HTTPServer" stays one token
        // because an all-caps run is treated as a single acronym.
        let camel_break = i > 0 && chars[i].is_uppercase() && !chars[i - 1].is_uppercase();
        match start {
            Some(_) if !camel_break => {}
            Some(begin) => {
                tokens.push(make_token(&fold, begin, i));
                start = Some(i);
            }
            None => start = Some(i),
        }
    }
    if let Some(begin) = start {
        tokens.push(make_token(&fold, begin, chars.len()));
    }

    let mut initials = Vec::with_capacity(tokens.len());
    let mut initial_at = Vec::with_capacity(tokens.len());
    let mut trigrams = Vec::with_capacity(tokens.len() * 2);
    for token in &tokens {
        let token_chars: Vec<char> = token.text.chars().collect();
        if let Some(&first) = token_chars.first() {
            initials.push(first);
            initial_at.push(token.start);
        }
        if token_chars.len() >= 3 {
            for window in token_chars.windows(3) {
                let gram: String = window.iter().collect();
                if !trigrams.contains(&gram) {
                    trigrams.push(gram);
                }
            }
        } else if !token.text.is_empty() && !trigrams.contains(&token.text) {
            // Short tokens are indexed as-is so that 1 and 2 character queries
            // still get candidates from the inverted index.
            trigrams.push(token.text.clone());
        }
    }

    Normalized {
        original: input.to_string(),
        chars,
        fold,
        folded,
        tokens,
        initials,
        initial_at,
        trigrams,
    }
}

fn make_token(fold: &[char], begin: usize, end: usize) -> Token {
    Token {
        start: begin,
        len: end - begin,
        text: fold[begin..end].iter().collect(),
    }
}

/// Joins several optional parts into one searchable string.
pub fn join_parts(parts: &[&str]) -> String {
    let mut out = String::new();
    for part in parts {
        let part = part.trim();
        if part.is_empty() {
            continue;
        }
        if !out.is_empty() {
            out.push(' ');
        }
        out.push_str(part);
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn folds_accents_and_keeps_offsets_aligned() {
        let n = normalize("Éditeur");
        assert_eq!(n.folded, "editeur");
        assert_eq!(n.chars.len(), n.fold.len());
        assert_eq!(n.chars.len(), "Éditeur".chars().count());
    }

    #[test]
    fn splits_on_punctuation_and_camel_case() {
        let n = normalize("GIMP Image Editor");
        assert_eq!(n.tokens.iter().map(|t| t.text.as_str()).collect::<Vec<_>>(),
                   vec!["gimp", "image", "editor"]);
        assert_eq!(n.initials.iter().collect::<String>(), "gie");

        let camel = normalize("FirefoxReader");
        assert_eq!(camel.tokens.iter().map(|t| t.text.as_str()).collect::<Vec<_>>(),
                   vec!["firefox", "reader"]);
        assert_eq!(camel.tokens[1].start, 7);
    }

    #[test]
    fn builds_trigrams() {
        let n = normalize("terminal");
        assert!(n.trigrams.contains(&"ter".to_string()));
        assert!(n.trigrams.contains(&"nal".to_string()));
        let short = normalize("ab cd");
        assert!(short.trigrams.contains(&"ab".to_string()));
    }

    #[test]
    fn find_returns_char_offsets() {
        let n = normalize("Öffner");
        assert_eq!(n.find(&['f', 'n']), Some(2));
        assert!(n.starts_with(&['o', 'f']));
    }

    #[test]
    fn empty_input_is_empty() {
        assert!(normalize("").is_empty());
        assert!(Normalized::empty().is_empty());
        assert_eq!(join_parts(&["", " a ", "b"]), "a b");
    }
}
