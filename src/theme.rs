//! Builds a CSS stylesheet from the user's theme config and applies it globally.

use crate::config::Theme;
use gtk4::prelude::*;
use gtk4::{gdk, CssProvider};

pub fn apply(theme: &Theme) {
    let css = build_css(theme);

    let provider = CssProvider::new();
    provider.load_from_string(&css);
    if let Some(display) = gdk::Display::default() {
        gtk4::style_context_add_provider_for_display(
            &display,
            &provider,
            gtk4::STYLE_PROVIDER_PRIORITY_APPLICATION,
        );
    }

    // User CSS is loaded with a higher priority, so it can override anything above.
    if let Some(path) = &theme.custom_css {
        let user = CssProvider::new();
        user.load_from_path(path);
        if let Some(display) = gdk::Display::default() {
            gtk4::style_context_add_provider_for_display(
                &display,
                &user,
                gtk4::STYLE_PROVIDER_PRIORITY_USER,
            );
        }
    }
}

fn build_css(t: &Theme) -> String {
    format!(
        r#"
* {{
    font-family: "{font}";
    color: {fg};
}}

.nt-panel {{
    background: {bg};
    border-radius: {r}px;
    padding: 4px 8px;
}}

.nt-start-button {{
    border-radius: {r}px;
    padding: 4px 10px;
    background: transparent;
}}

.nt-start-button:hover {{
    background: alpha({accent}, 0.25);
}}

.nt-task {{
    border-radius: {r}px;
    padding: 4px 8px;
    background: transparent;
}}

.nt-task:hover {{
    background: alpha({fg}, 0.12);
}}

.nt-task.active {{
    background: alpha({accent}, 0.35);
}}

.nt-clock {{
    font-variant-numeric: tabular-nums;
}}

.nt-menu {{
    background: {bg};
    border-radius: {r}px;
    padding: 12px;
}}

.nt-menu-item {{
    border-radius: {r}px;
    padding: 6px 8px;
    background: transparent;
}}

.nt-menu-item:hover {{
    background: alpha({accent}, 0.25);
}}
"#,
        font = t.font,
        fg = t.panel_foreground,
        bg = t.panel_background,
        accent = t.accent,
        r = t.radius,
    )
}
