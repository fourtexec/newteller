//! Output helpers: aligned text tables, JSON, and the error path.

use serde::Serialize;
use serde_json::Value;

/// Prints a value as pretty JSON. Returns the process exit code.
pub fn print_json<T: Serialize>(value: &T) -> i32 {
    match serde_json::to_string_pretty(value) {
        Ok(text) => {
            println!("{text}");
            0
        }
        Err(error) => fail(format!("cannot serialise the result: {error}")),
    }
}

/// Prints a configuration value: bare strings without quotes, everything else
/// as JSON.
pub fn print_value(value: &Value) {
    match value {
        Value::String(text) => println!("{text}"),
        other => println!("{other}"),
    }
}

/// One line value, for `key = value` output.
pub fn short(value: &Value) -> String {
    match value {
        Value::String(text) => text.clone(),
        Value::Null => "null".to_string(),
        other => other.to_string(),
    }
}

/// Prints a message on stderr and returns exit code 1.
pub fn fail<T: std::fmt::Display>(message: T) -> i32 {
    eprintln!("newteller-ctl: {message}");
    1
}

/// Prints a section heading in text mode.
pub fn heading(text: &str) {
    println!("\n{text}");
    println!("{}", "-".repeat(text.chars().count()));
}

/// Renders an aligned table. Empty input produces an empty string.
pub fn table(headers: &[&str], rows: &[Vec<String>]) -> String {
    let columns = headers.len();
    let mut widths: Vec<usize> = headers.iter().map(|header| display_width(header)).collect();
    for row in rows {
        for (index, cell) in row.iter().take(columns).enumerate() {
            widths[index] = widths[index].max(display_width(cell));
        }
    }
    // Never let a wide column push the rest off the terminal.
    for width in widths.iter_mut() {
        *width = (*width).min(48);
    }

    let mut out = String::new();
    out.push_str(&render_row(
        &headers.iter().map(|h| h.to_string()).collect::<Vec<_>>(),
        &widths,
    ));
    for row in rows {
        out.push_str(&render_row(row, &widths));
    }
    out
}

fn render_row(cells: &[String], widths: &[usize]) -> String {
    let mut line = String::new();
    for (index, cell) in cells.iter().enumerate() {
        let Some(width) = widths.get(index) else { break };
        let text = truncate(cell, *width);
        line.push_str(&text);
        let padding = width.saturating_sub(display_width(&text));
        if index + 1 < cells.len() {
            line.push_str(&" ".repeat(padding + 2));
        } else {
            // Pad the last column too, so every row has the same width.
            line.push_str(&" ".repeat(padding));
        }
    }
    line.push('\n');
    line
}

/// Number of characters, which is close enough to the column width for the
/// scripts a desktop shell deals with.
pub fn display_width(text: &str) -> usize {
    text.chars().count()
}

/// Shortens `text` to `width` characters, adding an ellipsis when it does.
pub fn truncate(text: &str, width: usize) -> String {
    let count = text.chars().count();
    if width == 0 || count <= width {
        return text.to_string();
    }
    if width == 1 {
        return "\u{2026}".to_string();
    }
    let kept: String = text.chars().take(width - 1).collect();
    format!("{kept}\u{2026}")
}

/// Right-aligns a number in a fixed width column.
pub fn right(text: &str, width: usize) -> String {
    let count = display_width(text);
    if count >= width {
        return text.to_string();
    }
    format!("{}{text}", " ".repeat(width - count))
}

/// A bar of `width` cells filled to `percent`, for the text metrics view.
pub fn bar(percent: f64, width: usize) -> String {
    let clamped = if percent.is_finite() { percent.clamp(0.0, 100.0) } else { 0.0 };
    let filled = ((clamped / 100.0) * width as f64).round() as usize;
    let filled = filled.min(width);
    let mut out = String::with_capacity(width + 2);
    out.push('[');
    for index in 0..width {
        out.push(if index < filled { '#' } else { '.' });
    }
    out.push(']');
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tables_are_aligned() {
        let text = table(&["name", "n"], &[vec!["a".to_string(), "1".to_string()], vec!["long".to_string(), "22".to_string()]]);
        let lines: Vec<&str> = text.lines().collect();
        assert_eq!(lines.len(), 3);
        assert!(lines[0].starts_with("name"));
        assert!(lines[2].starts_with("long"));
        assert_eq!(display_width(lines[0]), display_width(lines[2]));
    }

    #[test]
    fn truncation_is_char_aware() {
        assert_eq!(truncate("abc", 5), "abc");
        assert_eq!(truncate("abcdef", 4), "abc\u{2026}");
        assert_eq!(truncate("Grüße", 4), "Grü\u{2026}");
        assert_eq!(display_width("Grüße"), 5);
    }

    #[test]
    fn bars_and_numbers_line_up() {
        assert_eq!(bar(50.0, 4), "[##..]");
        assert_eq!(bar(0.0, 4), "[....]");
        assert_eq!(bar(150.0, 2), "[##]");
        assert_eq!(right("7", 3), "  7");
    }

    #[test]
    fn values_print_readably() {
        assert_eq!(short(&Value::from("text")), "text");
        assert_eq!(short(&Value::from(42)), "42");
        assert_eq!(short(&Value::Null), "null");
    }
}
