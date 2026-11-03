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

struct CompositorConfig {
    char keyboardLayout[NT_CONFIG_TEXT];   // "us,ru"
    char keyboardVariant[NT_CONFIG_TEXT];  // ",phonetic"
    char keyboardOptions[NT_CONFIG_TEXT];  // "grp:alt_shift_toggle"
    char keyboardModel[NT_CONFIG_TEXT];    // "pc105"
    int repeatRate;                        // keys per second
    int repeatDelay;                       // milliseconds
    bool numlock;

    bool touchpadTap;
    bool touchpadNaturalScroll;
    bool touchpadDisableWhileTyping;
    double touchpadSpeed;                  // -1 .. 1
    bool mouseNaturalScroll;
    double mouseSpeed;                     // -1 .. 1
    bool leftHanded;

    char cursorTheme[NT_CONFIG_TEXT];
    int cursorSize;
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

// Splits the comma separated layout list; returns how many were written.
int ntConfigSplitList(const char *list, char out[][32], int max);

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
