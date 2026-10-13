//! Start menu: a popup window with pinned apps, a search box and a power row.

use crate::config::Config;
use crate::icons;
use gtk4::prelude::*;
use gtk4::{
    glib, Application, ApplicationWindow, Box as GtkBox, Button, Entry, Label, Orientation,
    ScrolledWindow,
};
use std::cell::RefCell;
use std::process::Command;
use std::rc::Rc;

#[derive(Clone)]
pub struct StartMenu {
    pub window: ApplicationWindow,
    cfg: Rc<Config>,
    list: GtkBox,
    search: Entry,
    /// Every entry currently in the list, so search can filter it.
    entries: Rc<RefCell<Vec<(String, Button)>>>,
}

impl StartMenu {
    pub fn new(app: &Application, cfg: &Config) -> Self {
        let cfg = Rc::new(cfg.clone());

        let root = GtkBox::new(Orientation::Vertical, 8);
        root.add_css_class("nt-menu");

        let search = Entry::builder().placeholder_text("Type to search...").build();
        root.append(&search);

        let list = GtkBox::new(Orientation::Vertical, 2);
        let scroller = ScrolledWindow::builder()
            .child(&list)
            .vexpand(true)
            .min_content_height(300)
            .build();
        root.append(&scroller);

        let footer = GtkBox::new(Orientation::Horizontal, 6);
        let settings = Button::with_label("Settings");
        settings.connect_clicked(|_| spawn(&format!("xdg-open {}", crate::config::Config::path().display())));
        let logout = Button::with_label("Log out");
        logout.connect_clicked(|_| spawn("loginctl terminate-user $USER"));
        let shutdown = Button::with_label("Shut down");
        shutdown.connect_clicked(|_| spawn("systemctl poweroff"));
        footer.append(&settings);
        footer.append(&logout);
        footer.append(&shutdown);
        root.append(&footer);

        let window = ApplicationWindow::builder()
            .application(app)
            .title("NewTeller Start")
            .default_width(cfg.start_menu.width)
            .default_height(cfg.start_menu.height)
            .decorated(false)
            .resizable(false)
            .child(&root)
            .build();

        let menu = StartMenu {
            window,
            cfg,
            list,
            search: search.clone(),
            entries: Rc::new(RefCell::new(Vec::new())),
        };

        menu.populate();

        let m = menu.clone();
        search.connect_changed(move |s| m.filter(&s.text()));

        // Hide on Escape or when focus leaves the window.
        let key = gtk4::EventControllerKey::new();
        let m = menu.clone();
        key.connect_key_pressed(move |_, keyval, _, _| {
            if keyval == gtk4::gdk::Key::Escape {
                m.hide();
                glib::Propagation::Stop
            } else {
                glib::Propagation::Proceed
            }
        });
        menu.window.add_controller(key);

        let m = menu.clone();
        menu.window.connect_is_active_notify(move |w| {
            if !w.is_active() {
                m.hide();
            }
        });

        menu
    }

    /// Rebuild the app list from pinned apps.
    fn populate(&self) {
        let mut entries = self.entries.borrow_mut();
        for p in &self.cfg.pinned {
            let row = Button::new();
            row.add_css_class("nt-menu-item");
            let inner = GtkBox::new(Orientation::Horizontal, 10);
            let icon_name = p.icon.as_deref().unwrap_or("app");
            inner.append(&icons::image(&self.cfg.icons, icon_name));
            inner.append(&Label::builder().label(&p.name).xalign(0.0).hexpand(true).build());
            row.set_child(Some(&inner));

            let cmd = p.command.clone();
            let win = self.window.clone();
            row.connect_clicked(move |_| {
                spawn(&cmd);
                win.set_visible(false);
            });

            self.list.append(&row);
            entries.push((p.name.to_lowercase(), row));
        }
    }

    fn filter(&self, query: &str) {
        let q = query.trim().to_lowercase();
        for (name, row) in self.entries.borrow().iter() {
            row.set_visible(q.is_empty() || name.contains(&q));
        }
    }

    pub fn toggle(&self) {
        if self.window.is_visible() {
            self.hide();
        } else {
            self.search.set_text("");
            self.window.present();
            self.search.grab_focus();
        }
    }

    pub fn hide(&self) {
        self.window.set_visible(false);
    }
}

/// Run a command line through the user's shell without blocking the UI.
pub fn spawn(cmdline: &str) {
    if let Err(e) = Command::new("sh").arg("-c").arg(cmdline).spawn() {
        eprintln!("newteller: failed to run `{cmdline}`: {e}");
    }
}
