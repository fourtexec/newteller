# NewTeller

A Windows-like, highly customizable desktop shell layer for Debian-based systems.
NewTeller runs **on top of** an existing session (GNOME, KDE Plasma, Openbox, ...)
and provides a taskbar-style panel, a start menu, and theming.

Written in Rust with GTK4. All UI is in English.

## Status

Early scaffold (v0.1.0). Implemented so far:

- Panel with start button, pinned app launchers, clock, and screen-edge placement
  (wlr-layer-shell on Wayland, dock-style fallback elsewhere)
- Start menu with search, pinned apps, Settings / Log out / Shut down
- Configuration file (TOML) and custom CSS theming
- Icons loaded from [IconKit](https://github.com/fourtexec/iconkit)

Planned: running-window list and window activation, system tray (StatusNotifier),
multi-monitor, widgets, plugin system.

## Dependencies (Debian 12 / Ubuntu 24.04+)

```sh
sudo apt install build-essential pkg-config libgtk-4-dev libgtk4-layer-shell-dev
curl https://sh.rustup.rs -sSf | sh   # Rust toolchain
```

## Icons

```sh
git clone https://github.com/fourtexec/iconkit ~/iconkit
```

Then point `[icons] path` in the config at `~/iconkit/icons` (this is the default).

## Build and run

```sh
cargo build --release
./target/release/newteller
```

Helpful flags: `--version`, `--print-config-path`.

## Configuration

`~/.config/newteller/config.toml` — see [`config.example.toml`](config.example.toml).

## License

MIT
