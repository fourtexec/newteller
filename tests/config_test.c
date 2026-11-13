// Unit test for the compositor's settings parser (src/session/config.c).
// It needs nothing but a C compiler; tests/config_test.sh builds and runs it.
#define _POSIX_C_SOURCE 200809L

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
static char directory[128];

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static const char *writeFile(const char *name, const char *text)
{
    static char path[256];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE *file = fopen(path, "w");
    if (!file) {
        perror(path);
        exit(2);
    }
    fputs(text, file);
    fclose(file);
    return path;
}

static bool near(double a, double b)
{
    return fabs(a - b) < 0.002;
}

static void testDefaults(void)
{
    struct CompositorConfig config;
    ntConfigDefaults(&config);
    CHECK(config.keyboardLayout[0] == '\0');
    CHECK(config.repeatRate == 25);
    CHECK(config.workspaceCount == 4);
    CHECK(config.bindingCount == 0);
    CHECK(strcmp(config.placement, "center") == 0);
    CHECK(strcmp(config.hotCorners[3], "none") == 0);
    CHECK(near(config.closeHover[0], 0xe5 / 255.0));
    CHECK(!ntConfigLoadFile(&config, "/nonexistent/compositor.conf"));
}

static void testLoadFile(void)
{
    struct CompositorConfig config;
    ntConfigDefaults(&config);
    const char *path = writeFile("compositor.conf",
        "# a comment\n"
        "\n"
        "keyboard.layout = us,ru\n"
        "  keyboard.options=\"grp:alt_shift_toggle\"  \n"
        "keyboard.repeatRate = 500\n"         // clamped
        "keyboard.repeatDelay = soon\n"       // not a number: default stays
        "keyboard.numlock = yes\n"
        "touchpad.tapToClick = off\n"
        "touchpad.speed = -3\n"
        "mouse.speed = nan\n"
        "workspaces.count = 0\n"
        "decorations.activeBackground = #80102030\n"
        "decorations.closeHover = #12345g\n"  // invalid: default stays
        "decorations.font = \n"               // empty: default stays
        "effects.animationSpeed = 2.5\n"
        "effects.cornerRadius = 12px\n"
        "windows.placement = mouse\n"
        "hotCorners.bottomLeft = overview\n"
        "unknown.key = 1\n"
        "no equals sign here\n"
        "bind.0 = Super+Shift+T | foot --title=a=b\n"
        "bind.1 = |nothing\n"                 // no keys
        "bind.2 = Super+X|\n"                 // no command
        "bind.3 = Super+Y\n");                // no separator
    CHECK(ntConfigLoadFile(&config, path));
    CHECK(strcmp(config.keyboardLayout, "us,ru") == 0);
    CHECK(strcmp(config.keyboardOptions, "grp:alt_shift_toggle") == 0);
    CHECK(config.repeatRate == 100);
    CHECK(config.repeatDelay == 600);
    CHECK(config.numlock);
    CHECK(!config.touchpadTap);
    CHECK(near(config.touchpadSpeed, -1.0));
    CHECK(near(config.mouseSpeed, 0.0));
    CHECK(config.workspaceCount == 1);
    CHECK(near(config.activeBackground[3], 0x80 / 255.0));
    CHECK(near(config.activeBackground[0], 0x10 / 255.0));
    CHECK(near(config.activeBackground[2], 0x30 / 255.0));
    CHECK(near(config.closeHover[0], 0xe5 / 255.0));
    CHECK(strcmp(config.titleFont, "Sans Bold 10") == 0);
    CHECK(near(config.animationSpeed, 2.5));
    CHECK(config.cornerRadius == 12);
    CHECK(strcmp(config.placement, "mouse") == 0);
    CHECK(strcmp(config.hotCorners[2], "overview") == 0);
    CHECK(config.bindingCount == 1);
    CHECK(strcmp(config.bindings[0].keys, "Super+Shift+T") == 0);
    CHECK(strcmp(config.bindings[0].command, "foot --title=a=b") == 0);
}

// A line longer than the buffer is dropped whole: its tail must not be read
// as a setting of its own.
static void testLongLines(void)
{
    char text[4096];
    size_t used = (size_t)snprintf(text, sizeof(text), "bind.0 = Super+T|echo ");
    while (used < 1019)
        text[used++] = 'a';
    // The 1023 byte buffer ends right before this.
    used += (size_t)snprintf(text + used, sizeof(text) - used,
                             "aaaa\nworkspaces.count = 7\nkeyboard.layout = de");
    // Only the newline separates the long line from the next one, so the
    // tail "aaaa" is harmless, but this one is not:
    char evil[4096];
    size_t n = (size_t)snprintf(evil, sizeof(evil), "bind.1 = Super+U|echo ");
    while (n < 1023)
        evil[n++] = 'b';
    snprintf(evil + n, sizeof(evil) - n, "workspaces.wrap = true\n%s", text);

    struct CompositorConfig config;
    ntConfigDefaults(&config);
    CHECK(ntConfigLoadFile(&config, writeFile("long.conf", evil)));
    CHECK(!config.workspaceWrap);
    CHECK(config.bindingCount == 0);
    CHECK(config.workspaceCount == 7);
    // The last line has no newline.
    CHECK(strcmp(config.keyboardLayout, "de") == 0);
}

static void testSystemKeyboard(void)
{
    struct CompositorConfig config;
    ntConfigDefaults(&config);
    const char *path = writeFile("keyboard",
        "# KEYBOARD CONFIGURATION FILE\n"
        "XKBMODEL=\"pc105\"\n"
        "XKBLAYOUT=\"us,ru\"\n"
        "XKBVARIANT=','\n"
        "XKBOPTIONS=\"grp:win_space_toggle\"\n"
        "BACKSPACE=\"guess\"\n");
    ntConfigLoadSystemKeyboard(&config, path);
    CHECK(strcmp(config.keyboardModel, "pc105") == 0);
    CHECK(strcmp(config.keyboardLayout, "us,ru") == 0);
    CHECK(strcmp(config.keyboardVariant, ",") == 0);
    CHECK(strcmp(config.keyboardOptions, "grp:win_space_toggle") == 0);

    // Fields that are already set stay.
    path = writeFile("vconsole.conf", "KEYMAP=de\nXKBLAYOUT=de\nXKBMODEL=pc104\n");
    ntConfigLoadSystemKeyboard(&config, path);
    CHECK(strcmp(config.keyboardLayout, "us,ru") == 0);

    ntConfigDefaults(&config);
    ntConfigLoadSystemKeyboard(&config, path);
    CHECK(strcmp(config.keyboardLayout, "de") == 0);
    CHECK(strcmp(config.keyboardModel, "pc104") == 0);
    ntConfigLoadSystemKeyboard(&config, "/nonexistent/keyboard");
}

static void testXorgKeyboard(void)
{
    struct CompositorConfig config;
    ntConfigDefaults(&config);
    const char *path = writeFile("00-keyboard.conf",
        "# Written by systemd-localed(8), read by systemd-localed and Xorg.\n"
        "Section \"InputClass\"\n"
        "        Identifier \"system-keyboard\"\n"
        "        MatchIsKeyboard \"on\"\n"
        "        Option \"XkbLayout\" \"us,de\"\n"
        "        Option \"XkbModel\" \"pc105\"\n"
        "\tOption\t\"XkbVariant\"\t\",nodeadkeys\"\n"
        "        option \"xkboptions\" \"grp:alt_shift_toggle\"\n"
        "        Option \"Broken\n"
        "        OptionXkbLayout \"fr\" \"fr\"\n"
        "EndSection\n");
    ntConfigLoadXorgKeyboard(&config, path);
    CHECK(strcmp(config.keyboardLayout, "us,de") == 0);
    CHECK(strcmp(config.keyboardModel, "pc105") == 0);
    CHECK(strcmp(config.keyboardVariant, ",nodeadkeys") == 0);
    CHECK(strcmp(config.keyboardOptions, "grp:alt_shift_toggle") == 0);
    ntConfigLoadXorgKeyboard(&config, "/nonexistent/00-keyboard.conf");
}

static void testColors(void)
{
    float color[4] = {9, 9, 9, 9};
    CHECK(ntConfigParseColor("#ff8000", color));
    CHECK(near(color[0], 1.0) && near(color[1], 0x80 / 255.0) && near(color[2], 0.0)
          && near(color[3], 1.0));
    CHECK(ntConfigParseColor("#00FFffFF", color));
    CHECK(near(color[3], 0.0) && near(color[0], 1.0));
    float untouched[4] = {9, 9, 9, 9};
    CHECK(!ntConfigParseColor(NULL, untouched));
    CHECK(!ntConfigParseColor("", untouched));
    CHECK(!ntConfigParseColor("ff8000", untouched));
    CHECK(!ntConfigParseColor("#fff", untouched));
    CHECK(!ntConfigParseColor("#ff80zz", untouched));
    CHECK(!ntConfigParseColor("# ff800", untouched));
    CHECK(!ntConfigParseColor("#-f8000", untouched));
    CHECK(untouched[0] == 9 && untouched[3] == 9);
}

static void testLists(void)
{
    char out[8][32];
    CHECK(ntConfigSplitList("us, ru ,de", out, 8) == 3);
    CHECK(strcmp(out[0], "us") == 0 && strcmp(out[1], "ru") == 0
          && strcmp(out[2], "de") == 0);
    CHECK(ntConfigSplitList("", out, 8) == 0);
    CHECK(ntConfigSplitList("a,b,c", out, 2) == 2);
    CHECK(ntConfigSplitList("a-name-that-is-much-longer-than-the-31-bytes-of-an-entry,b",
                            out, 8) == 2);
    CHECK(strlen(out[0]) == 31 && strcmp(out[1], "b") == 0);

    CHECK(ntConfigListContains("foot, Firefox ,org.gnome.Calculator", "firefox"));
    CHECK(ntConfigListContains("foot", "FOOT"));
    CHECK(ntConfigListContains(",,foot,,", "foot"));
    CHECK(!ntConfigListContains("footclient", "foot"));
    CHECK(!ntConfigListContains("foot", "footclient"));
    CHECK(!ntConfigListContains("foot", ""));
    CHECK(!ntConfigListContains("", "foot"));
    CHECK(!ntConfigListContains(NULL, "foot"));
    CHECK(!ntConfigListContains("foot", NULL));
}

static void testOutputs(void)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/deep/er/outputs.conf", directory);
    struct SavedOutput saved[3];
    memset(saved, 0, sizeof(saved));
    snprintf(saved[0].key, sizeof(saved[0].key), "Dell Inc.\tU2720Q\n123");
    saved[0].enabled = true;
    saved[0].width = 3840;
    saved[0].height = 2160;
    saved[0].refresh = 59997;
    saved[0].x = -1920;
    saved[0].y = 0;
    saved[0].scale = 1.5;
    saved[0].transform = 3;
    snprintf(saved[1].key, sizeof(saved[1].key), "eDP-1");
    saved[1].enabled = false;
    saved[1].width = 1920;
    saved[1].height = 1080;
    saved[1].scale = 1.0;
    CHECK(ntOutputsSave(path, saved, 2));

    struct SavedOutput loaded[3];
    CHECK(ntOutputsLoad(path, loaded, 3) == 2);
    // Separators inside a key are written as spaces.
    CHECK(strcmp(loaded[0].key, "Dell Inc. U2720Q 123") == 0);
    CHECK(loaded[0].enabled && loaded[0].width == 3840 && loaded[0].height == 2160);
    CHECK(loaded[0].refresh == 59997 && loaded[0].x == -1920 && loaded[0].y == 0);
    CHECK(near(loaded[0].scale, 1.5) && loaded[0].transform == 3);
    CHECK(strcmp(loaded[1].key, "eDP-1") == 0 && !loaded[1].enabled);
    CHECK(loaded[1].refresh == 0);
    CHECK(ntOutputsLoad(path, loaded, 1) == 1);
    CHECK(ntOutputsLoad("/nonexistent/outputs.conf", loaded, 3) == 0);

    const char *damaged = writeFile("damaged.conf",
        "too\tfew\tfields\n"
        "A\t1\tgarbage\t0,0\t1.000\t0\n"
        "B\t1\t0x0@60000\t0,0\t1.000\t0\n"
        "C\t1\t-800x600@60000\t0,0\t1.000\t0\n"
        "D\t1\t800x600\tnowhere\t1.000\t0\n"
        "E\t1\t800x600@-5\t10,20\t99\t12\n"
        "\n");
    CHECK(ntOutputsLoad(damaged, loaded, 3) == 1);
    CHECK(strcmp(loaded[0].key, "E") == 0);
    CHECK(loaded[0].refresh == 0 && loaded[0].x == 10 && loaded[0].y == 20);
    CHECK(near(loaded[0].scale, 4.0) && loaded[0].transform == 7);
}

static void testPath(void)
{
    char path[256];
    setenv("XDG_CONFIG_HOME", "/tmp/xdg", 1);
    ntConfigPath(path, sizeof(path), "compositor.conf");
    CHECK(strcmp(path, "/tmp/xdg/newteller/compositor.conf") == 0);
    // A relative XDG_CONFIG_HOME is invalid and ignored.
    setenv("XDG_CONFIG_HOME", "relative", 1);
    setenv("HOME", "/home/someone", 1);
    ntConfigPath(path, sizeof(path), "outputs.conf");
    CHECK(strcmp(path, "/home/someone/.config/newteller/outputs.conf") == 0);
}

int main(void)
{
    snprintf(directory, sizeof(directory), "/tmp/newteller-config-test-XXXXXX");
    if (!mkdtemp(directory)) {
        perror("mkdtemp");
        return 2;
    }
    testDefaults();
    testLoadFile();
    testLongLines();
    testSystemKeyboard();
    testXorgKeyboard();
    testColors();
    testLists();
    testOutputs();
    testPath();

    char command[256];
    snprintf(command, sizeof(command), "rm -rf '%s'", directory);
    if (system(command) != 0)
        fprintf(stderr, "could not remove %s\n", directory);
    if (failures) {
        fprintf(stderr, "config test: %d check(s) failed\n", failures);
        return 1;
    }
    printf("config test passed\n");
    return 0;
}
