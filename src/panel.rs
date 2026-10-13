//! The panel: start button, pinned/running app buttons, tray area with clock.
//! Uses wlr-layer-shell when available so other windows don't overlap it.

use crate::config::Config;
use crate::icons;
use crate::start_menu::{spawn, StartMenu};
use gtk4::prelude::*;
use gtk4::{glib, Application, ApplicationWindow, Box as GtkBox, Button, Label, Orientation};
use gtk4_layer_shell::{Edge, Layer, LayerShell};

pub struct Panel {
    pub window: ApplicationWindow,
}

impl Panel {
    pub fn new(app: &Application, cfg: &Config, menu: StartMenu) -> Self {
        let root = GtkBox::new(Orientation::Horizontal, 6);
        root.add_css_class("nt-panel");

        // Start button
        let start = Button::with_label("NewTeller");
        start.add_css_class("nt-start-button");
        start.connect_clicked(move |_| menu.toggle());

        // Pinned apps
        let apps = GtkBox::new(Orientation::Horizontal, 4);
        for p in &cfg.pinned {
            let b = Button::new();
            b.add_css_class("nt-task");
            b.set_tooltip_text(Some(&p.name));
            b.set_child(Some(&icons::image(&cfg.icons, p.icon.as_deref().unwrap_or("app"))));
            let cmd = p.command.clone();
            b.connect_clicked(move |_| spawn(&cmd));
            apps.append(&b);
        }

        let spacer = GtkBox::new(Orientation::Horizontal, 0);
        spacer.set_hexpand(true);

        root.append(&start);
        root.append(&apps);
        root.append(&spacer);

        // Tray: clock
        if cfg.panel.show_clock {
            let clock = Label::new(None);
            clock.add_css_class("nt-clock");
            let fmt = cfg.panel.clock_format.clone();
            let update = move |l: &Label| {
                if let Ok(now) = glib::DateTime::now_local() {
                    if let Ok(s) = now.format(&fmt) {
                        l.set_text(&s);
                    }
                }
            };
            update(&clock);
            let c = clock.clone();
            glib::timeout_add_seconds_local(10, move || {
                update(&c);
                glib::ControlFlow::Continue
            });
            root.append(&clock);
        }

        let window = ApplicationWindow::builder()
            .application(app)
            .title("NewTeller Panel")
            .child(&root)
            .decorated(false)
            .build();

        configure_layer_shell(&window, cfg);

        window.present();
        Panel { window }
    }
}

/// Reserve space on screen for the panel (Wayland + wlr-layer-shell).
/// On X11/other compositors this is a no-op and the window behaves like a normal dock.
fn configure_layer_shell(window: &ApplicationWindow, cfg: &Config) {
    if !gtk4_layer_shell::is_supported() {
        window.set_default_size(0, cfg.panel.height);
        return;
    }

    window.init_layer_shell();
    window.set_layer(Layer::Top);
    window.auto_exclusive_zone_enable();

    let edge = match cfg.panel.position.as_str() {
        "top" => Edge::Top,
        "left" => Edge::Left,
        "right" => Edge::Right,
        _ => Edge::Bottom,
    };
    // Anchor to the chosen edge and stretch along it: horizontal panels span
    // Left+Right, vertical panels span Top+Bottom.
    let horizontal = matches!(edge, Edge::Top | Edge::Bottom);
    window.set_anchor(edge, true);
    window.set_anchor(if horizontal { Edge::Left } else { Edge::Top }, true);
    window.set_anchor(if horizontal { Edge::Right } else { Edge::Bottom }, true);
    if horizontal {
        window.set_default_size(0, cfg.panel.height);
    } else {
        window.set_default_size(cfg.panel.height, 0);
    }
}
