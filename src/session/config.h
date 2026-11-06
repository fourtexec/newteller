// Compositor settings: keyboard, pointer devices and saved output layouts.
//
// The shell mirrors its `input.*` keys from shell.json into
// $XDG_CONFIG_HOME/newteller/compositor.conf (one `key = value` per line) and
// asks the compositor to reload it. The compositor does not parse JSON and
// does not depend on the shell to start: a missing file means the defaults,
// and the keyboard falls back to the system-wide /etc/default/keyboard.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define NT_CONFIG_TEXT 256
#define NT_MAX_BINDINGS 32

// A user shortcut: "Super+Shift+T" -> a shell command.
struct NtBinding {
    char keys[64];      // as written, parsed by the compositor (xkbcommon)
    char command[NT_CONFIG_TEXT];
};

// Built-in shortcuts the user can move or turn off (Settings -> Hotkeys).
// The order matches ntHotkeyNames; the shell writes `hotkey.<name> = keys`.
enum NtHotkey {
    NT_HOTKEY_START_MENU,
    NT_HOTKEY_RUN_DIALOG,
    NT_HOTKEY_TERMINAL,
    NT_HOTKEY_FILE_MANAGER,
    NT_HOTKEY_SETTINGS,
    NT_HOTKEY_QUICK_SETTINGS,
    NT_HOTKEY_NOTIFICATIONS,
    NT_HOTKEY_CLIPBOARD,
    NT_HOTKEY_LOCK,
    NT_HOTKEY_CLOSE,
    NT_HOTKEY_MAXIMIZE,
    NT_HOTKEY_FULLSCREEN,
    NT_HOTKEY_SHOW_DESKTOP,
    NT_HOTKEY_OVERVIEW,
    NT_HOTKEY_RECORD,
    NT_HOTKEY_FORCE_QUIT,
    NT_HOTKEY_SCREENSHOT,
    NT_HOTKEY_SCREENSHOT_AREA,
    NT_HOTKEY_SCREENSHOT_WINDOW,
    NT_HOTKEY_NEXT_LAYOUT,
    NT_HOTKEY_WINDOW_MENU,
    NT_HOTKEY_MINIMIZE,
    NT_HOTKEY_ALWAYS_ON_TOP,
    NT_HOTKEY_LOGOUT,
    NT_HOTKEY_SLEEP,
    NT_HOTKEY_REBOOT,
    NT_HOTKEY_SHUTDOWN,
    NT_HOTKEY_COUNT
};

extern const char *const ntHotkeyNames[NT_HOTKEY_COUNT];

struct CompositorConfig {
    char keyboardLayout[NT_CONFIG_TEXT];   // "us,ru"
    char keyboardVariant[NT_CONFIG_TEXT];  // ",phonetic"
    char keyboardOptions[NT_CONFIG_TEXT];  // "grp:alt_shift_toggle"
    char keyboardModel[NT_CONFIG_TEXT];    // "pc105"
    int repeatRate;                        // keys per second
    int repeatDelay;                       // milliseconds
    bool numlock;
    // Every window keeps its own keyboard layout (switched on focus).
    bool layoutPerWindow;

    bool touchpadTap;
    bool touchpadNaturalScroll;
    bool touchpadDisableWhileTyping;
    double touchpadSpeed;                  // -1 .. 1
    bool mouseNaturalScroll;
    double mouseSpeed;                     // -1 .. 1
    bool leftHanded;
    // Three-finger window drag, four-finger desktop switch.
    bool gestures;

    char cursorTheme[NT_CONFIG_TEXT];
    int cursorSize;

    int workspaceCount;                    // 1 .. 9
    bool workspaceWrap;                    // last -> first when switching

    // Title bars drawn by the compositor (for windows that ask for them).
    bool decorationsServer;                // prefer server-side decorations
    int titleHeight;                       // px
    int borderWidth;                       // px
    bool buttonsLeft;                      // close/min/max on the left (macOS)
    bool titleCentered;
    bool showMinimize;
    bool showMaximize;
    char titleFont[NT_CONFIG_TEXT];        // Pango description, "Sans Bold 10"
    // What a double / middle click on a title bar does: maximize |
    // minimize | alwaysOnTop | none.
    char titleDoubleClick[16];
    char titleMiddleClick[16];
    int doubleClickMs;                     // mouse.doubleClickInterval
    float activeBackground[4];
    float activeForeground[4];
    float inactiveBackground[4];
    float inactiveForeground[4];
    float closeHover[4];

    // Window behaviour.
    bool focusFollowsMouse;
    bool snapping;                         // edge snapping while dragging
    int gaps;                              // px around and between snapped windows
    bool switcherAllWorkspaces;            // Alt+Tab over every desktop
    char placement[16];                    // center | cascade | mouse
    // Window rules: comma separated app ids (or X11 classes), any case.
    char rulesAlwaysOnTop[NT_CONFIG_TEXT];
    char rulesMaximized[NT_CONFIG_TEXT];
    char rulesFullscreen[NT_CONFIG_TEXT];
    char rulesMinimized[NT_CONFIG_TEXT];
    // "firefox=2,telegram-desktop=3": the desktop (1-based) an application
    // opens on.
    char rulesWorkspace[NT_CONFIG_TEXT * 2];
    // Hot corners (top left, top right, bottom left, bottom right): none |
    // overview | desktop | startMenu | notifications | lock.
    char hotCorners[4][16];

    struct NtBinding bindings[NT_MAX_BINDINGS];
    int bindingCount;
    // Per built-in shortcut: "" keeps the default keys, "none" turns it
    // off, anything else replaces the default ("Super+Shift+L").
    char hotkeys[NT_HOTKEY_COUNT][64];
    // hotkeys.enabled = false: only shortcuts given above stay.
    bool builtinShortcuts;

    // Effects renderer: rounded corners, blur behind the shell, animations.
    bool effects;
    int cornerRadius;                      // px, 0 = square
    bool blur;
    int blurStrength;                      // 1 .. 4
    bool animations;
    double animationSpeed;                 // 0.25 .. 4, 1 = normal
};

void ntConfigDefaults(struct CompositorConfig *config);

// Fills the keyboard fields that are still empty from a Debian style
// /etc/default/keyboard (XKBLAYOUT=, XKBVARIANT=, XKBOPTIONS=, XKBMODEL=).
void ntConfigLoadSystemKeyboard(struct CompositorConfig *config, const char *path);

// Reads `key = value` lines over the current values. Returns false when the
// file does not exist or cannot be read; unknown keys are ignored.
bool ntConfigLoadFile(struct CompositorConfig *config, const char *path);

// $XDG_CONFIG_HOME/newteller/<name> (or ~/.config/newteller/<name>).
void ntConfigPath(char *buffer, size_t size, const char *name);

// "#rrggbb" or "#aarrggbb" (Qt style) into premultiplied-free RGBA floats.
bool ntConfigParseColor(const char *text, float out[4]);

// Splits the comma separated layout list; returns how many were written.
int ntConfigSplitList(const char *list, char out[][32], int max);

// True when `name` is one of the comma separated entries of `list`
// (case-insensitive, spaces around entries ignored). Empty names never match.
bool ntConfigListContains(const char *list, const char *name);

// The number after `name=` in a comma separated "name=N" list (names as in
// ntConfigListContains), or -1 when the name is not there.
int ntConfigListNumber(const char *list, const char *name);

// A saved output configuration, keyed by the output's make/model/serial.
struct SavedOutput {
    char key[NT_CONFIG_TEXT];
    bool enabled;
    int width;
    int height;
    int refresh;   // mHz, 0 = any
    int x;
    int y;
    double scale;
    int transform; // enum wl_output_transform
};

// outputs.conf: one output per line,
//   <key>\t<enabled>\t<width>x<height>@<mHz>\t<x>,<y>\t<scale>\t<transform>
// Returns the number of entries read (at most `max`).
int ntOutputsLoad(const char *path, struct SavedOutput *outputs, int max);
// Writes the list atomically (temporary file + rename).
bool ntOutputsSave(const char *path, const struct SavedOutput *outputs, int count);
