---
name: newteller-development
description: How to change NewTeller safely - the wlroots compositor (src/session), the Qt/QML shell (src, qml), settings, tests and the GitHub Actions checks. Use before editing anything in this repository, when adding a feature, a setting, a keybinding, a control-socket command, a QML window or a compositor protocol, or when CI fails.
---

# Working on NewTeller

NewTeller is a complete Wayland desktop session for Debian 13 and Ubuntu 26.04:
a small wlroots compositor (C) plus a Qt 6 / QML shell (panel, start menu,
quick settings, settings app) that runs as the compositor's first client.
Read this before changing anything; it lists the contracts between the parts
and the checks that will fail if one is broken.

## Ground rules

- **Do not build locally.** The environment usually lacks the Debian/Ubuntu
  packages (Qt 6.8+, wlroots 0.18/0.19, LayerShellQt). Write code that is
  correct by inspection and verify it on GitHub Actions (see "Verifying").
  Two things do run locally and save CI rounds: the QML smoke test
  (`pip install PySide6-Essentials` in a venv, plus `libegl1`) and
  `cargo test --workspace` in `rust/`. For wlroots APIs, read the real
  headers: the release tarballs of 0.18, 0.19 and 0.20 from
  gitlab.freedesktop.org/wlroots/wlroots show what each version offers.
- Support **wlroots 0.18 (Debian 13), 0.19 (Ubuntu 26.04, Fedora) and 0.20
  (Arch)**. Their APIs differ (see "wlroots pitfalls"); guard with
  `#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR >= 19` (0.20 takes the
  0.19 branches; add `>= 20` guards when 0.20 needs its own).
- Supported systems: Debian 13, Ubuntu 26.04 (apt), Arch Linux and
  Arch-based (pacman), Fedora 42+ (dnf). `scripts/platform.sh` sets
  `NEWTELLER_TARGET` and `NEWTELLER_FAMILY`; the install scripts branch on
  the family. Arch renames wlroots packages over time, so
  `newteller_arch_wlroots` picks the package by version; Fedora packages are
  requested by `pkgconfig(...)` / soname provides.
- Never use a wlroots struct field that 0.19 moves into `WLR_PRIVATE`.
- C code compiles as C11 with GCC 14: an implicit function declaration is an
  **error**. Every function used before its definition needs a forward
  declaration near the top of `src/session/main.c`.
- Keep user-facing text in English in the code; comments explain *why*.
  Wrap QML texts in `qsTr("...")` and C++ messages shown in the UI in
  `tr("...")` (use `.arg()` for values, never split a
  sentence with `+`) and add the Russian text to `i18n/ru.json`
  (`src/translator.cpp` reads it; the QML smoke test rejects stale entries
  and lost `%1` placeholders). `shell.language` picks the language.
- Commit with clear messages; never skip or weaken a test to get green.

## Layout

| Path | What it is |
| --- | --- |
| `src/session/main.c` | The compositor (~6.7k lines, one file, sections marked by `// ----` banners): views (xdg + XWayland), layer shell, input, keybindings, Alt+Tab, workspaces, session lock, outputs, decorations, thumbnails, effects renderer, control socket, CLI. |
| `src/session/config.c/.h` | `compositor.conf` parser (`key = value`), `/etc/default/keyboard`, `outputs.conf`. Pure C, no wlroots. |
| `src/main.cpp` | Shell entry point; creates the C++ services and exposes them to QML as context properties. |
| `src/settings.cpp/.h` | `Settings` (shell.json). **Every key needs a default here.** |
| `src/sessionservices.cpp/.h` | `Session`: talks to the compositor over the control socket, writes `compositor.conf` / `lock.conf`, runs swayidle, wlsunset, polkit agent, XDG autostart, Wi-Fi (NetworkManager over D-Bus, with `wifiStatus` diagnostics), overview/display queries, blur regions, hung windows / force quit, `pointerPressed` (clicks anywhere, closes cards). |
| `src/shellcontroller.cpp/.h` | `Shell`: D-Bus object `org.newteller.Shell` (the compositor calls it with `gdbus` for Super shortcuts), icons, lock/logout. |
| `src/quicksettings.*` `bluetooth.*` `clipboardhistory.*` `tray.*` `notifications.*` `windowlist.*` `systemmonitor.*` `theme.*` `appmodel.*` `plugins.*` | `Quick`, `Bluetooth`, `Clipboard`, `Tray`, `Notifications`, `Windows`, `System`, `Theme`, `Apps`, `Plugins`, `Polkit` (`polkitagent.*`: the built-in polkit agent; GLib/polkit headers must be included before any Qt header there). |
| `src/updater.*`, `scripts/newteller-update-install` | `Updater`: reads `https://fourtexec.site/newteller/update.json` (generated in `fourtexec/fourtexec.github.io` from the commit releases), downloads the archive it links to, checks the SHA-256 and runs the root helper through pkexec. `/usr/share/newteller/BUILDINFO` says what is installed; `newteller --check-update` / `--install-update` and `NEWTELLER_UPDATE_URL=file://...` test it without a session (the session smoke test installs a stand-in archive that way; `NEWTELLER_BUILDINFO` and `NEWTELLER_UPDATE_STORE` keep it out of `/usr` and `/var/lib/newteller`). The helper keeps the replaced archive for `--rollback`. New `updates.*` keys also go into the `Settings` mock of `tests/qml_smoke.py`: it holds only the values listed there, so a `=== true` binding is false without one. |
| `qml/Main.qml` | Root object: instantiates every window and wires D-Bus signals (`Connections { target: Shell }`). |
| `qml/Panel.qml` | Bar, dock, tray, indicators, workspace dots, layout indicator. |
| `qml/DropdownWindow.qml` + `ShellFrame.qml` | Base for every popup card (start menu, quick settings, Wi-Fi, Bluetooth, clipboard...). |
| `qml/SettingsApp.qml` | Settings application. `pages` array order **must match** the order of `Item`s in its `StackLayout`. |
| `qml/Settings*.qml` | Controls that read/write a `settingKey` directly (Switch, Slider, Stepper, Choice, TextField, Row, Section). |
| `rust/` | Optional Rust core (`newteller-core`, `newteller-ctl`); `cargo test --workspace` runs in CI. |
| `resources.qrc` | Every QML file and every `icons/*.svg` must be listed or it is not compiled in. |
| `scripts/` | `install-build-deps.sh`, `install-runtime-deps.sh` (optional desktop services are installed only if `apt-cache show` finds them), `build-release.sh`, `newteller-session`, `newteller-lock`, `newteller-screenshot`. |
| `tests/qml_smoke.py` | Loads all QML with Python mocks of every context property; fails on **any** QML warning. |
| `tests/session_smoke.sh` | Starts a real headless compositor + shell in CI and drives it (screenshots, control socket, lock, crash recovery). |

## How the parts talk

- **Shell → compositor**: the control socket `$NEWTELLER_IPC`
  (`$XDG_RUNTIME_DIR/newteller-<WAYLAND_DISPLAY>.sock`). One command per line,
  one reply line (`ok ...` / `error ...`). `watch` subscribes to `event ...`
  lines. CLI: `newteller-compositor --msg <command>`. Commands include
  `ping reload lock locked dpms on|off layout [next|prev|N] workspace N
  move-to-workspace N windows thumbnail activate close move-window kill outputs
  outputs-detail output-set blur blur-clear effects minimize maximize
  fullscreen always-on-top sticky move-to-output window-menu exit`. Events:
  `layout`, `workspace`, `switcher`, `locked`, `key` (media keys,
  `caps-on|off`, `num-on|off`), `click X Y` (every button press while
  unlocked), `hung ID` / `responsive ID` / `closed ID` (ping timeouts),
  `window-menu ID X Y DESKTOP FLAGS TITLE`, `output NAME` (output under the
  pointer), `fullscreen 1|0`, `bell`. Window state letters come from
  `viewFlags()`. Watchers get the current `output`/`fullscreen` state when
  they subscribe. Never block the shell's UI thread on D-Bus or the socket
  for long: the tray once froze the desktop for minutes with synchronous
  scans; use async calls (`asyncCall` + `QDBusPendingCallWatcher`).
  Fields inside one reply are separated by `\x1e`, records by `\x1f`;
  sanitize titles with `ipcField()`.
- **Shell → compositor settings**: `SessionServices::compositorConfigText()`
  mirrors shell.json keys into `~/.config/newteller/compositor.conf`, then
  sends `reload`. Add a key in three places: `src/settings.cpp` default,
  `compositorConfigText()`, and `config.h/config.c` (field, default,
  parser). Add its prefix to `SessionServices::settingChanged()` if new.
- **Compositor → shell**: control-socket events, or `shellAction("method")`
  which runs `gdbus call ... org.newteller.Shell.<method>`; add the method as a
  `Q_INVOKABLE` on `ShellController` emitting a signal, handle it in
  `Main.qml`, and add the signal to the `Shell` mock in `tests/qml_smoke.py`.

## Recipes

**New setting shown in the settings app**
1. Default in `src/settings.cpp` (`m_defaults.insert`). The QML smoke test
   fails if QML reads a key without a default, and if a key has a default
   that nothing reads (a `settingKey:` in `SettingsApp.qml` does not count):
   a setting ships together with the code that obeys it, or not at all.
2. A `SettingsRow { settingKey: ...; SettingsSwitch/Slider/... { settingKey: ... } }`
   in the right page of `qml/SettingsApp.qml`.
3. If the compositor needs it: the three places above.

**New settings page**: add `{ id, label, icon }` to `pages` and an `Item`
at the same position in the `StackLayout`; the icon must exist in `icons/`
and `resources.qrc`.

**New QML window**: create the file, list it in `resources.qrc`, instantiate
it in `Main.qml`. Layer-shell windows set `LayerShell.Window.*` (anchors,
layer, keyboardInteractivity, exclusionZone, scope). Popup cards should be a
`DropdownWindow`. Never animate a `Window`'s `opacity` (the offscreen test
platform prints a warning, which fails the smoke test); animate an inner item.

**New context property / C++ service**: construct it in `src/main.cpp`
before the engine, `setContextProperty`, add the `.cpp` to `CMakeLists.txt`,
and add a mock class with every property/slot/signal QML uses to
`tests/qml_smoke.py` (registered in the `setContextProperty` loop there).

**New keybinding**: `keyHandled()` in `src/session/main.c`. Order matters:
VT switching first, then media keys, then the lock check (nothing else works
while locked), user shortcuts (`bind.N`), then the built-ins. Check modifier
combinations that already exist (e.g. Super+S vs Super+Shift+S).

**New control-socket command**: a branch in `ipcHandleCommand()`; reply with
exactly one line; document it in the CLI usage text and README; add a check
to `tests/session_smoke.sh` via its `msg` helper.

**New wlroots protocol**: create the manager in `setupProtocols()`, store
listeners on `Server`, remove them in the teardown part of `run()`. If the
wlroots header includes a generated `*-protocol.h` (layer shell, xdg-shell,
output power management do), the header must be generated with
`wayland-scanner` in `CMakeLists.txt` from an XML in `protocols/` or
`/usr/share/wayland-protocols`; otherwise just include it.

**New runtime dependency**: required ones go into
`scripts/install-runtime-deps.sh` `packages`; optional desktop services into
the `optional` array (installed only when the distribution has them). Build
dependencies go into `scripts/install-build-deps.sh` and a
`pkg_check_modules` + `target_link_libraries` entry. Link every library you
call directly (pixman had to be added explicitly: "DSO missing from command
line").

## Shell pitfalls already paid for

- **No blocking D-Bus or process calls on the UI thread.** A slow or hung
  service (an app's tray menu, MPRIS player, NetworkManager, wpctl) froze
  the whole shell. Use `asyncCall` + `QDBusPendingCallWatcher` or an
  asynchronous `QProcess`, and test with a fake service that never answers.
- **QML lists and objects reach a `QVariant` parameter as a `QJSValue`**,
  which `QJsonDocument` writes as `null`. `Settings::setValue` converts it
  (`plainValue`); any other C++ method that stores what QML hands it should
  take `QVariantList` / `QVariantMap` or convert the same way. A list read
  back from `Settings.values` is array-like, not always a JS `Array`: copy it
  by `length`, not with `Array.isArray`.
- **Read D-Bus signals as a raw `QDBusMessage`** in slots connected to other
  services: a typed slot makes Qt abort the shell on a malformed signal.
  `Properties.Get` replies hold a `QDBusVariant`; read a `QDBusArgument` only
  through a `const` reference (the non-const one writes).
- **Never fail silently.** A missing helper (terminal, xdg-open, screen
  locker, pkexec) or a launch that fails gets a notification; important ones
  (critical battery, pairing codes) go through `Shell.notifyUrgent` /
  `sendWithActions`, which do-not-disturb does not hide.
- **Nothing assumes Debian or English.** `x-terminal-emulator` exists only on
  Debian (`newteller-terminal` picks a terminal), `~/Pictures` is localized
  (use the XDG folders: `Shell.picturesPath`), and every visible text goes
  through `qsTr()` / `tr()` - the smoke test lints `qml/` and `plugins/` for
  bare English labels.
- **Plugins keep their state in the shell settings** (`weather.*`,
  `stickyNotes.notes`) with defaults in `src/settings.cpp`; nothing from the
  network until the user turns it on (the weather widget waits for a city).

## Dock and panel layout

- Only the active dock layout exists: `DockStrip` loads a Row, a Column
  (vertical bar) or a Flow (`dock.multiRow`) through a Loader. Never size a
  button from a container that sizes itself from the buttons (binding loop,
  CPU load, clipped dock); `DockApps.extent` is passed down explicitly.
- `barThickness` = max(`panel.height`, what the icon size / rows need), so
  `dock.iconSize` and `dock.rows` grow the bar (and its exclusive zone).
- Position surfaces on whole pixels (`Math.round`) - fractional centring
  made the dock look off-centre.
- Popup cards close on `Session.pointerPressed` outside them (layer
  surfaces do not always get keyboard focus, so focus loss alone is not
  enough); tooltips hide on their own after a few seconds.

## Compositor invariants

- `View` = one toplevel (xdg or XWayland). `view->x/y` is the **content**
  origin (window geometry); a compositor-drawn frame sits outside it
  (`viewFrame`, `frameContentBox`). Use `viewSetPosition`/`viewSetSize`.
- Visibility goes through `viewSyncVisible()` (mapped, not minimized, on the
  current workspace). Focus goes through `focusView()` / `restoreFocus()`.
- `server->views` is most-recently-used first (`focusView` raises).
- **Lock screen** (`ext-session-lock-v1`): while `server->locked`, only lock
  surfaces get input and focus; `focusView`/`focusLayer` refuse; an opaque
  backdrop in `lockTree` covers everything. A dead locker never unlocks.
  Any new input or focus path must respect `server->locked`.
- Always-on-top windows stay in `toplevelTree`; anything that raises a view
  must end with `raisePinnedViews()` (`viewRaise` does). Dialogs follow their
  parent (`viewParent`, `raiseWithChildren`).
- Sandboxed clients (security-context) are filtered in `globalFilter()`:
  add any new privileged global's interface name there.
- Hit testing follows the scene's stacking (toplevelTree children, top-most
  last) and skips hidden layer trees; keep new input paths consistent with
  what is drawn.
- With effects on, `renderEffectsFrame` draws the trees explicitly (background,
  bottom, views, snap preview, top, overlay, input popups, drag icon, lock): a
  new scene tree must be added there too.
- The shell: NetworkManager calls go through async `QDBusPendingCallWatcher`
  chains (join, list, disconnect); keep new ones asynchronous.
- Graphics tablets: over a surface that accepts tablet-v2 the pen is sent as
  a pen (`handleTabletAxis/Tip/Button/Proximity`, one `TabletTool` per
  `wlr_tablet_tool`), everywhere else it goes through the pointer path
  (`tabletEmulateButton` -> `cursorButton`). A press on a surface runs
  `pressFocus()`, shared with mouse clicks. `wlr_surface_accepts_tablet_v2`
  swapped its arguments in 0.19.
- Pointers stored on `Server` that refer to a `View` (`focusedView`,
  `previousView`, `grabbedView`, `switchTarget`, `hoverView`,
  `lastTitleView`) must be cleared in `destroyView()`.
- Spawn helpers with `spawnDetached()` (double fork, clean signal mask) and
  `setChildEnvironment()` first.
- **Effects renderer** (`renderEffectsFrame`): with `effects.enabled` the
  compositor draws frames itself (rounded clip regions, blur, animations,
  ghosts of closed windows) and falls back to `wlr_scene_output_commit`
  when it cannot. New scene content must be drawable by `renderNode()`
  (trees, rects, buffers). Animations must call `scheduleAllFrames()` and
  stop on their own. The session test asserts the renderer drew frames.
- Title bars (`viewUpdateDecoration`) are cheap to call often; they redraw
  only when size, focus, title, hover, scale or state changed.

## wlroots pitfalls (0.18 vs 0.19)

- `wlr_output_begin_render_pass`: 4 arguments in 0.18, 3 in 0.19.
- Many struct fields are `WLR_PRIVATE` in 0.19 (e.g. `wlr_scene_buffer.texture`);
  use public APIs (`wlr_surface_get_texture`, `wlr_texture_from_buffer`).
- `wlr_keyboard_notify_modifiers` is in `<wlr/interfaces/wlr_keyboard.h>`;
  `wlr_output_transform_invert/compose` in `<wlr/util/transform.h>`;
  idle notifier header is `wlr_idle_notify_v1.h`.
- `wlr_xdg_toplevel_decoration_v1_set_mode` and size configures only after
  the xdg surface is initialized (answer the initial commit).
- With pixman, a texture created from a buffer holds it: destroy the texture
  before rendering into that buffer again.

**When unsure about an API**, do not guess: push a temporary workflow
`.github/workflows/probe-headers.yml` triggered on `push` to your branch with
`paths: ['.github/workflows/probe-headers.yml']`, running in `debian:13` and
`ubuntu:26.04` containers, that installs `libwlroots-0.18-dev` /
`libwlroots-0.19-dev` and greps/prints the headers you need. Read the job
logs, then delete the workflow in your next commit.

## Verifying (GitHub Actions)

Three workflows run on pull requests, pushes to `main` and
`workflow_dispatch`: `build-release.yml` (Debian 13, Ubuntu 26.04),
`build-arch.yml` (`archlinux:latest`) and `build-fedora.yml`
(`fedora:latest`). A new workflow file can only be dispatched once it is on
`main`; test it through a pull request. All of them use PySide6 from PyPI in
a venv for the QML smoke test: a system PySide6 sees the installed
`org.kde.layershell` module, which collides with the test's stand-in.
Per target (Debian 13, Ubuntu 26.04) it runs, in order: dependency install,
`cargo test`, **QML smoke (newest Qt)**, **QML smoke (Qt 6.8 parser)**, build
and package, **session smoke test**. A failing QML smoke stops the job before
the C/C++ build, so fix it first.

Typical loop:
1. Push the branch; dispatch the workflow (`actions_run_trigger` with
   `workflow_id: build-release.yml`, `ref: <branch>`) or open the PR.
2. Wait for completion, then read failures from the job logs
   (`grep -E "error:|undefined reference|FAIL"`). Compiler errors list all
   problems in one go; QML smoke prints each warning before
   `FAIL: no QML warnings`.
3. Successful session runs print `session smoke: ...` lines (thumbnail size,
   effects frames, crash recovery, lock checks); check they ran, not skipped.

QML smoke test rules: every QML-visible C++ API needs a mock; no QML
warnings at all (missing signals on mocks, unknown properties, binding
loops, window opacity changes); literal `iconName`/`icon:` values need an
`icons/<name>.svg` listed in `resources.qrc`; QML must parse on Qt 6.8
(no newer syntax). Avoid simulating hover in tests (tooltips fade a window's
opacity).

Session smoke test: `tests/wayland_globals.py` lists the advertised globals
(add a new protocol's interface name to the list in `session_smoke.sh`).
It runs `newteller-compositor --headless=1280x800` with the
shell, a PySide test window, `grim` screenshots, `wlr-randr`, `swaylock`,
and the `msg` helper for the control socket. Add a check there for every
compositor behaviour you add.

## Before you finish

- [ ] New keys have defaults; new QML files/icons are in `resources.qrc`.
- [ ] Mocks in `tests/qml_smoke.py` cover new properties, slots and signals.
- [ ] Forward declarations for new C functions used earlier in `main.c`.
- [ ] Works on both wlroots versions; no private fields.
- [ ] Locked session cannot be bypassed by the new code.
- [ ] README (feature list / shortcuts / control socket) updated.
- [ ] GitHub Actions green on both targets, session smoke lines present.
