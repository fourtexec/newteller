//! NewTeller - a Windows-like, customizable shell layer.
//!
//! It runs on top of an existing desktop session (GNOME, KDE Plasma, Openbox, ...)
//! as a set of GTK4 windows: a panel (taskbar + tray + clock) and a start menu.
//! On Wayland compositors with wlr-layer-shell the panel reserves screen space;
//! elsewhere it falls back to a normal dock-type window.

mod config;
mod icons;
mod panel;
mod start_menu;
mod theme;

use gtk4::prelude::*;
use gtk4::Application;

const APP_ID: &str = "org.newteller.Shell";

fn main() {
    let app = Application::builder().application_id(APP_ID).build();

    app.connect_activate(|app| {
        let cfg = config::Config::load();
        theme::apply(&cfg.theme);

        let menu = start_menu::StartMenu::new(app, &cfg);
        panel::Panel::new(app, &cfg, menu);
    });

    // Don't hand GTK our CLI args: NewTeller has its own subcommands below.
    let args: Vec<String> = std::env::args().collect();
    if args.iter().any(|a| a == "--print-config-path") {
        println!("{}", config::Config::path().display());
        return;
    }
    if args.iter().any(|a| a == "--version") {
        println!("NewTeller {}", env!("CARGO_PKG_VERSION"));
        return;
    }

    app.run_with_args::<&str>(&[]);
}
