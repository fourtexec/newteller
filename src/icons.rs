//! Icon lookup. Icons come from IconKit (https://github.com/fourtexec/iconkit)
//! and are referenced by name inside the configured icon set, e.g. `lucide/rocket.svg`.
//! We never vendor the icon set into this repo: point `[icons] path` at a checkout.

use crate::config::IconConfig;
use gtk4::prelude::*;
use gtk4::{gio, Image};
use std::path::PathBuf;

/// Resolve an icon name to a file on disk. Absolute paths are used as-is.
pub fn resolve(cfg: &IconConfig, name: &str) -> Option<PathBuf> {
    let direct = PathBuf::from(name);
    if direct.is_absolute() && direct.exists() {
        return Some(direct);
    }
    let file = if name.ends_with(".svg") {
        name.to_string()
    } else {
        format!("{name}.svg")
    };
    let candidate = cfg.path.join(&cfg.set).join(file);
    candidate.exists().then_some(candidate)
}

/// Build a GTK image for the given icon name, falling back to a generic glyph.
pub fn image(cfg: &IconConfig, name: &str) -> Image {
    let img = match resolve(cfg, name) {
        Some(path) => Image::from_gicon(&gio::FileIcon::new(&gio::File::for_path(path))),
        None => Image::from_icon_name("application-x-executable"),
    };
    img.set_pixel_size(cfg.size);
    img
}
