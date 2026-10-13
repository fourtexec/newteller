//! The application search index.
//!
//! Replaces the linear `QString::contains` scan of the old `AppModel::search`.
//! Two things make it faster:
//!
//! * every searchable string is normalised **once** at build time (accent
//!   folded, lower-cased, tokenised, trigrammed) instead of on every keystroke;
//! * queries of three characters or more are answered from an inverted index
//!   (trigram intersection plus token-prefix range), so only a handful of
//!   applications are scored. When that cheap path finds almost nothing - which
//!   is exactly what happens for typos - the index falls back to a full scan.
//!
//! The full scan is still the safety net for correctness, and it stays well
//! under a millisecond for a few thousand applications.

pub mod normalize;
pub mod score;

use std::collections::{BTreeMap, HashMap, HashSet};
use std::time::Instant;

use rayon::prelude::*;
use serde::{Deserialize, Serialize};

use crate::desktop::{scan_dirs, DesktopEntry, ScanReport, ScanRequest};
use crate::system;

use normalize::{join_parts, normalize, Normalized};
use score::{escape_html, highlight, score_in, Field, MatchKind, Query, Scored};

/// Default number of results when the caller does not ask for a limit.
pub const DEFAULT_LIMIT: usize = 50;

/// Below this many hits the cheap candidate path is retried as a full scan.
const FALLBACK_THRESHOLD: usize = 3;

/// How a search should behave.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct SearchOptions {
    /// Maximum number of hits; `0` means [`DEFAULT_LIMIT`].
    pub limit: usize,
    /// Enable typo tolerance and subsequence matching.
    pub fuzzy: bool,
    /// Colour used to emphasise the matched characters. `None` disables
    /// highlighting, which saves building an extra string per hit.
    pub highlight: Option<String>,
    /// Also match `Keywords`.
    pub match_keywords: bool,
    /// Also match `GenericName`, `Comment` and `Categories`.
    pub match_meta: bool,
}

impl Default for SearchOptions {
    fn default() -> Self {
        SearchOptions {
            limit: DEFAULT_LIMIT,
            fuzzy: true,
            highlight: None,
            match_keywords: true,
            match_meta: true,
        }
    }
}

/// One search result.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct Hit {
    /// Desktop file id.
    pub id: String,
    /// Localised name.
    pub name: String,
    /// Icon name or path.
    pub icon: String,
    /// Localised comment, shown as a subtitle.
    pub comment: String,
    /// Main category, for grouping.
    pub category: String,
    /// Final score; higher is better.
    pub score: i32,
    /// [`MatchKind::as_str`].
    pub kind: String,
    /// [`Field::as_str`] - which part of the entry matched.
    pub field: String,
    /// Qt StyledText version of the name with the match emphasised.
    pub highlight: Option<String>,
    /// How often the application was launched.
    pub usage: u32,
}

impl Default for Hit {
    fn default() -> Self {
        Hit {
            id: String::new(),
            name: String::new(),
            icon: String::new(),
            comment: String::new(),
            category: String::new(),
            score: 0,
            kind: "none".to_string(),
            field: "name".to_string(),
            highlight: None,
            usage: 0,
        }
    }
}

/// What a search did, for profiling and for `newteller-ctl bench`.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct SearchResult {
    /// Ordered hits, at most `limit`.
    pub hits: Vec<Hit>,
    /// Matches before truncation.
    pub matched: usize,
    /// Applications that were scored.
    pub considered: usize,
    /// Whether the expensive full scan path was used.
    pub scanned_all: bool,
    /// Wall clock time of the query.
    pub elapsed_ms: f64,
}

/// A category and how many applications use it.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct CategoryCount {
    /// Category name, for example `Network`.
    pub name: String,
    /// Number of applications.
    pub count: usize,
}

impl Default for CategoryCount {
    fn default() -> Self {
        CategoryCount {
            name: String::new(),
            count: 0,
        }
    }
}

/// An application that is currently running.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct RunningApp {
    /// Desktop file id.
    pub id: String,
    /// Number of matching processes.
    pub count: usize,
    /// Matching process ids.
    pub pids: Vec<i32>,
}

impl Default for RunningApp {
    fn default() -> Self {
        RunningApp {
            id: String::new(),
            count: 0,
            pids: Vec::new(),
        }
    }
}

/// Size and shape of the index, for diagnostics.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct IndexStats {
    /// Applications in the index.
    pub apps: usize,
    /// Distinct tokens.
    pub tokens: usize,
    /// Distinct trigrams.
    pub trigrams: usize,
    /// Approximate heap footprint of the normalised text, in bytes.
    pub memory_bytes: usize,
    /// Time spent building the index.
    pub built_in_ms: f64,
    /// Time spent scanning and parsing the desktop files.
    pub scan_ms: f64,
    /// Files read.
    pub scanned: usize,
    /// Files rejected.
    pub skipped: usize,
}

/// Application catalogue plus the structures needed to search it.
#[derive(Debug)]
pub struct SearchIndex {
    entries: Vec<DesktopEntry>,
    names: Vec<Normalized>,
    keywords: Vec<Normalized>,
    metas: Vec<Normalized>,
    by_id: HashMap<String, usize>,
    trigrams: HashMap<String, Vec<u32>>,
    tokens: BTreeMap<String, Vec<u32>>,
    programs: HashMap<String, Vec<u32>>,
    usage: HashMap<String, u32>,
    built_in_ms: f64,
    report: ScanReport,
}

impl SearchIndex {
    /// Scans the XDG directories and builds the index in one step.
    ///
    /// This is the entry point used by the shell: the desktop files are parsed
    /// once, in parallel, and never travel through C++ as intermediate data.
    pub fn from_scan(request: &ScanRequest) -> SearchIndex {
        let mut report = scan_dirs(request);
        let entries = std::mem::take(&mut report.apps);
        let mut index = SearchIndex::from_entries(entries);
        index.report = report;
        index
    }

    /// Builds an index from entries that were parsed elsewhere.
    pub fn from_entries(entries: Vec<DesktopEntry>) -> SearchIndex {
        let started = Instant::now();
        let count = entries.len();

        let norms: Vec<[Normalized; 3]> = entries
            .par_iter()
            .map(|entry| {
                [
                    normalize(&entry.name),
                    normalize(&entry.keywords.join(" ")),
                    normalize(&join_parts(&[
                        entry.generic_name.as_str(),
                        entry.comment.as_str(),
                        entry.categories.join(" ").as_str(),
                    ])),
                ]
            })
            .collect();

        let mut names = Vec::with_capacity(count);
        let mut keywords = Vec::with_capacity(count);
        let mut metas = Vec::with_capacity(count);
        for [name, keyword, meta] in norms {
            names.push(name);
            keywords.push(keyword);
            metas.push(meta);
        }

        let mut by_id = HashMap::with_capacity(count);
        let mut trigrams: HashMap<String, Vec<u32>> = HashMap::new();
        let mut tokens: BTreeMap<String, Vec<u32>> = BTreeMap::new();
        let mut programs: HashMap<String, Vec<u32>> = HashMap::new();

        for (index, entry) in entries.iter().enumerate() {
            let index = index as u32;
            by_id.insert(entry.id.clone(), index as usize);

            for field in [&names[index as usize], &keywords[index as usize], &metas[index as usize]]
            {
                for gram in &field.trigrams {
                    trigrams.entry(gram.clone()).or_default().push(index);
                }
                for token in &field.tokens {
                    tokens.entry(token.text.clone()).or_default().push(index);
                }
            }

            let program = entry.program();
            if !program.is_empty() {
                programs
                    .entry(crate::util::fold_str(&program))
                    .or_default()
                    .push(index);
            }
            if !entry.startup_wm_class.is_empty() {
                programs
                    .entry(crate::util::fold_str(&entry.startup_wm_class))
                    .or_default()
                    .push(index);
            }
        }

        // An application can be listed several times when the same token appears
        // in more than one field; posting lists are deduplicated once, here, so
        // that queries never pay for it.
        for postings in trigrams.values_mut() {
            dedup(postings);
        }
        for postings in tokens.values_mut() {
            dedup(postings);
        }
        for postings in programs.values_mut() {
            dedup(postings);
        }

        let built_in_ms = started.elapsed().as_secs_f64() * 1000.0;
        SearchIndex {
            entries,
            names,
            keywords,
            metas,
            by_id,
            trigrams,
            tokens,
            programs,
            usage: HashMap::new(),
            built_in_ms,
            report: ScanReport::default(),
        }
    }

    /// Builds an index from a JSON array of desktop entries.
    pub fn from_json(apps: &str) -> crate::Result<SearchIndex> {
        let entries: Vec<DesktopEntry> = serde_json::from_str(apps)?;
        Ok(SearchIndex::from_entries(entries))
    }

    /// Number of applications.
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// True when the index holds no application.
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// All entries, in display order.
    pub fn entries(&self) -> &[DesktopEntry] {
        &self.entries
    }

    /// Looks an entry up by desktop file id.
    pub fn entry(&self, id: &str) -> Option<&DesktopEntry> {
        self.by_id.get(id).map(|&index| &self.entries[index])
    }

    /// The report of the scan that produced the index.
    pub fn report(&self) -> &ScanReport {
        &self.report
    }

    /// Replaces the launch counters used to boost frequently used applications.
    pub fn set_usage(&mut self, usage: HashMap<String, u32>) {
        self.usage = usage;
    }

    /// Launch counter of one application.
    pub fn usage_of_id(&self, id: &str) -> u32 {
        self.usage.get(id).copied().unwrap_or(0)
    }

    /// Searches the index.
    ///
    /// An empty query returns the most used applications, then the whole
    /// catalogue alphabetically - that is what the start menu shows before the
    /// user types anything.
    pub fn search(&self, query: &str, options: &SearchOptions) -> SearchResult {
        let started = Instant::now();
        let limit = if options.limit == 0 { DEFAULT_LIMIT } else { options.limit };
        let parsed = Query::new(query, options.fuzzy);

        if parsed.is_empty() {
            let mut order: Vec<usize> = (0..self.entries.len()).collect();
            order.sort_by(|&a, &b| {
                self.usage_of(b)
                    .cmp(&self.usage_of(a))
                    .then_with(|| self.entries[a].sort_key.cmp(&self.entries[b].sort_key))
            });
            let total = order.len();
            let hits: Vec<Hit> = order
                .into_iter()
                .take(limit)
                .map(|index| {
                    self.make_hit(
                        index,
                        &Scored {
                            kind: MatchKind::Exact,
                            score: self.usage_of(index) as i32,
                            ranges: Vec::new(),
                        },
                        Field::Name,
                        options,
                    )
                })
                .collect();
            return SearchResult {
                matched: total,
                considered: 0,
                scanned_all: false,
                elapsed_ms: started.elapsed().as_secs_f64() * 1000.0,
                hits,
            };
        }

        let mut matched: Vec<(usize, Scored, Field)> = Vec::new();
        let mut considered = 0usize;
        let mut scanned_all = false;

        match self.candidates(&parsed) {
            Some(list) => {
                considered = list.len();
                for index in list {
                    if let Some(hit) = self.score_entry(index, &parsed, options) {
                        matched.push(hit);
                    }
                }
                // Trigrams cannot see a transposed letter, so retry with the
                // full scan when the cheap path came up (almost) empty.
                if matched.len() < FALLBACK_THRESHOLD.min(limit) {
                    scanned_all = true;
                    matched.clear();
                    considered = self.entries.len();
                    self.score_all(&parsed, options, &mut matched);
                }
            }
            None => {
                scanned_all = true;
                considered = self.entries.len();
                self.score_all(&parsed, options, &mut matched);
            }
        }

        let total_matched = matched.len();
        matched.sort_by(|a, b| {
            b.1.score.cmp(&a.1.score).then_with(|| {
                self.entries[a.0]
                    .sort_key
                    .cmp(&self.entries[b.0].sort_key)
                    .then_with(|| self.entries[a.0].id.cmp(&self.entries[b.0].id))
            })
        });

        let hits: Vec<Hit> = matched
            .into_iter()
            .take(limit)
            .map(|(index, scored, field)| self.make_hit(index, &scored, field, options))
            .collect();

        SearchResult {
            hits,
            matched: total_matched,
            considered,
            scanned_all,
            elapsed_ms: started.elapsed().as_secs_f64() * 1000.0,
        }
    }

    /// Counts applications per category, most populated first.
    pub fn categories(&self) -> Vec<CategoryCount> {
        let mut counts: BTreeMap<&str, usize> = BTreeMap::new();
        for entry in &self.entries {
            if let Some(category) = entry.categories.first() {
                *counts.entry(category.as_str()).or_insert(0) += 1;
            }
        }
        let mut out: Vec<CategoryCount> = counts
            .into_iter()
            .map(|(name, count)| CategoryCount {
                name: name.to_string(),
                count,
            })
            .collect();
        out.sort_by(|a, b| b.count.cmp(&a.count).then_with(|| a.name.cmp(&b.name)));
        out
    }

    /// Matches the current process list against the index.
    ///
    /// The match uses the program name from `Exec` and `StartupWMClass`, which
    /// works on Wayland and on X11 without any windowing protocol binding.
    pub fn running(&self) -> Vec<RunningApp> {
        let processes = system::process::list_processes(0);
        let mut found: HashMap<usize, Vec<i32>> = HashMap::new();
        for process in &processes {
            let mut keys: Vec<String> = Vec::with_capacity(3);
            keys.push(crate::util::fold_str(&process.name));
            if let Some(program) = process.cmdline.first() {
                keys.push(crate::util::fold_str(crate::util::basename(program)));
            }
            if let Some(exe) = process.exe.as_deref() {
                keys.push(crate::util::fold_str(crate::util::basename(exe)));
            }
            let mut seen_here: HashSet<usize> = HashSet::new();
            for key in keys {
                if key.is_empty() {
                    continue;
                }
                if let Some(indices) = self.programs.get(&key) {
                    for &index in indices {
                        if seen_here.insert(index as usize) {
                            found.entry(index as usize).or_default().push(process.pid);
                        }
                    }
                }
            }
        }

        let mut out: Vec<RunningApp> = found
            .into_iter()
            .map(|(index, mut pids)| {
                pids.sort_unstable();
                RunningApp {
                    id: self.entries[index].id.clone(),
                    count: pids.len(),
                    pids,
                }
            })
            .collect();
        out.sort_by(|a, b| a.id.cmp(&b.id));
        out
    }

    /// Size and build cost of the index.
    pub fn stats(&self) -> IndexStats {
        let memory_bytes = self
            .names
            .iter()
            .chain(self.keywords.iter())
            .chain(self.metas.iter())
            .map(Normalized::memory)
            .sum::<usize>()
            + self.entries.iter().map(std::mem::size_of_val).sum::<usize>();
        IndexStats {
            apps: self.entries.len(),
            tokens: self.tokens.len(),
            trigrams: self.trigrams.len(),
            memory_bytes,
            built_in_ms: self.built_in_ms,
            scan_ms: self.report.elapsed_ms,
            scanned: self.report.scanned,
            skipped: self.report.skipped,
        }
    }

    fn usage_of(&self, index: usize) -> u32 {
        self.usage.get(&self.entries[index].id).copied().unwrap_or(0)
    }

    fn score_all(
        &self,
        query: &Query,
        options: &SearchOptions,
        out: &mut Vec<(usize, Scored, Field)>,
    ) {
        for index in 0..self.entries.len() {
            if let Some(hit) = self.score_entry(index, query, options) {
                out.push(hit);
            }
        }
    }

    fn score_entry(
        &self,
        index: usize,
        query: &Query,
        options: &SearchOptions,
    ) -> Option<(usize, Scored, Field)> {
        if let Some(mut scored) = score_in(&self.names[index], query, Field::Name) {
            scored.score += self.usage_boost(index);
            return Some((index, scored, Field::Name));
        }
        if options.match_keywords {
            if let Some(mut scored) = score_in(&self.keywords[index], query, Field::Keywords) {
                scored.score += self.usage_boost(index);
                return Some((index, scored, Field::Keywords));
            }
        }
        if options.match_meta {
            if let Some(mut scored) = score_in(&self.metas[index], query, Field::Meta) {
                scored.score += self.usage_boost(index);
                return Some((index, scored, Field::Meta));
            }
        }
        None
    }

    /// Logarithmic boost so that a heavily used application moves up inside its
    /// tier without ever jumping over a better tier.
    fn usage_boost(&self, index: usize) -> i32 {
        let count = self.usage_of(index);
        if count == 0 {
            return 0;
        }
        ((count as f64 + 1.0).ln() * 1_000.0) as i32
    }

    /// Candidate application indices for a query, or `None` when the query is
    /// too short to prune and everything has to be scored.
    fn candidates(&self, query: &Query) -> Option<Vec<usize>> {
        if query.len() < 3 {
            return None;
        }
        let mut postings: Vec<&Vec<u32>> = Vec::new();
        for window in query.fold.windows(3) {
            let gram: String = window.iter().collect();
            match self.trigrams.get(&gram) {
                Some(list) => postings.push(list),
                // A trigram nobody contains: the cheap path has no candidates.
                None => return Some(Vec::new()),
            }
        }
        // Intersect the smallest list first.
        postings.sort_by_key(|list| list.len());
        let mut result: Vec<u32> = postings[0].clone();
        for list in &postings[1..] {
            let set: HashSet<u32> = list.iter().copied().collect();
            result.retain(|index| set.contains(index));
            if result.is_empty() {
                break;
            }
        }

        let prefix: String = query.fold.iter().collect();
        for (key, list) in self.tokens.range::<str, _>(prefix.as_str()..) {
            if !key.starts_with(prefix.as_str()) {
                break;
            }
            result.extend(list.iter().copied());
        }

        result.sort_unstable();
        result.dedup();
        Some(result.into_iter().map(|index| index as usize).collect())
    }

    fn make_hit(
        &self,
        index: usize,
        scored: &Scored,
        field: Field,
        options: &SearchOptions,
    ) -> Hit {
        let entry = &self.entries[index];
        let highlighted = options.highlight.as_ref().map(|color| {
            if field == Field::Name && !scored.ranges.is_empty() {
                highlight(&entry.name, &scored.ranges, Some(color))
            } else {
                escape_html(&entry.name)
            }
        });
        Hit {
            id: entry.id.clone(),
            name: entry.name.clone(),
            icon: entry.icon.clone(),
            comment: entry.comment.clone(),
            category: entry.categories.first().cloned().unwrap_or_default(),
            score: scored.score,
            kind: scored.kind.as_str().to_string(),
            field: field.as_str().to_string(),
            highlight: highlighted,
            usage: self.usage_of(index),
        }
    }
}

fn dedup(list: &mut Vec<u32>) {
    list.sort_unstable();
    list.dedup();
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entry(id: &str, name: &str, keywords: &[&str], comment: &str) -> DesktopEntry {
        DesktopEntry {
            id: id.to_string(),
            name: name.to_string(),
            keywords: keywords.iter().map(|k| k.to_string()).collect(),
            comment: comment.to_string(),
            sort_key: crate::util::fold_str(name),
            ..DesktopEntry::default()
        }
    }

    fn sample_index() -> SearchIndex {
        SearchIndex::from_entries(vec![
            entry("firefox-esr.desktop", "Firefox ESR", &["web", "browser"], "Browse the web"),
            entry("gimp.desktop", "GNU Image Manipulation Program", &["graphics"], "Edit images"),
            entry("org.gnome.Terminal.desktop", "Terminal", &["shell", "console"], "Use the command line"),
            entry("libreoffice-writer.desktop", "LibreOffice Writer", &["office"], "Word processor"),
            entry("thunderbird.desktop", "Thunderbird", &["mail"], "Send e-mail"),
            entry("vlc.desktop", "VLC media player", &["video"], "Play everything"),
        ])
    }

    #[test]
    fn ranks_prefix_over_substring_and_reports_the_tier() {
        let index = sample_index();
        let result = index.search("fire", &SearchOptions::default());
        assert_eq!(result.hits[0].id, "firefox-esr.desktop");
        assert_eq!(result.hits[0].kind, "prefix");
        assert_eq!(result.hits[0].field, "name");
    }

    #[test]
    fn finds_acronyms_and_keywords() {
        let index = sample_index();
        let result = index.search("gimp", &SearchOptions::default());
        assert_eq!(result.hits[0].id, "gimp.desktop");
        assert_eq!(result.hits[0].kind, "acronym");

        let by_keyword = index.search("console", &SearchOptions::default());
        assert_eq!(by_keyword.hits[0].id, "org.gnome.Terminal.desktop");
        assert_eq!(by_keyword.hits[0].field, "keywords");
    }

    #[test]
    fn tolerates_typos_via_the_full_scan_fallback() {
        let index = sample_index();
        let result = index.search("terminak", &SearchOptions::default());
        assert!(!result.hits.is_empty(), "expected a typo match");
        assert_eq!(result.hits[0].id, "org.gnome.Terminal.desktop");
        assert_eq!(result.hits[0].kind, "typo");
        assert!(result.scanned_all, "typos must fall back to the full scan");
    }

    #[test]
    fn empty_query_returns_the_catalogue() {
        let index = sample_index();
        let result = index.search("", &SearchOptions { limit: 3, ..SearchOptions::default() });
        assert_eq!(result.hits.len(), 3);
        assert_eq!(result.matched, 6);
    }

    #[test]
    fn usage_moves_an_app_up_inside_its_tier() {
        let mut index = sample_index();
        let mut usage = HashMap::new();
        usage.insert("thunderbird.desktop".to_string(), 500u32);
        index.set_usage(usage);

        let plain = index.search("t", &SearchOptions { fuzzy: false, ..SearchOptions::default() });
        let first = plain.hits.iter().find(|h| h.id == "thunderbird.desktop").unwrap();
        assert!(first.usage >= 500);
        assert!(first.score > 0);
    }

    #[test]
    fn highlight_is_only_requested_when_a_colour_is_given() {
        let index = sample_index();
        let plain = index.search("fire", &SearchOptions::default());
        assert!(plain.hits[0].highlight.is_none());

        let coloured = index.search(
            "fire",
            &SearchOptions {
                highlight: Some("#ff0000".to_string()),
                ..SearchOptions::default()
            },
        );
        let html = coloured.hits[0].highlight.clone().unwrap();
        assert!(html.starts_with("<font color=\"#ff0000\"><b>Fire</b></font>"), "{html}");
    }

    #[test]
    fn categories_are_counted() {
        let mut index = sample_index();
        index.entries[0].categories = vec!["Network".to_string()];
        index.entries[4].categories = vec!["Network".to_string()];
        let categories = index.categories();
        assert_eq!(categories[0].name, "Network");
        assert_eq!(categories[0].count, 2);
    }

    #[test]
    fn json_entries_can_be_indexed() {
        let entries = vec![entry("a.desktop", "Alpha", &[], "")];
        let json = serde_json::to_string(&entries).unwrap();
        let index = SearchIndex::from_json(&json).unwrap();
        assert_eq!(index.len(), 1);
        assert_eq!(index.search("alp", &SearchOptions::default()).hits[0].id, "a.desktop");
    }

    #[test]
    fn stats_describe_the_index() {
        let index = sample_index();
        let stats = index.stats();
        assert_eq!(stats.apps, 6);
        assert!(stats.trigrams > 0);
        assert!(stats.memory_bytes > 0);
    }
}
