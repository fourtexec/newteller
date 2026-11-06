#define _POSIX_C_SOURCE 200809L

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

const char *const ntHotkeyNames[NT_HOTKEY_COUNT] = {
    "startMenu", "runDialog", "terminal", "fileManager", "settings", "quickSettings",
    "notificationCenter", "clipboard", "lockScreen", "closeWindow", "maximize",
    "fullscreen", "showDesktop", "overview", "screenRecording", "forceQuit",
    "screenshot", "screenshotArea", "screenshotWindow", "nextLayout", "windowMenu",
    "minimize", "alwaysOnTop", "logout", "sleep", "reboot", "shutdown",
};

static void copyText(char *target, size_t size, const char *value)
{
    snprintf(target, size, "%s", value ? value : "");
}

static char *trim(char *text)
{
    while (*text && isspace((unsigned char)*text))
        ++text;
    size_t length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1]))
        text[--length] = '\0';
    return text;
}

// Strips one level of matching single or double quotes.
static char *unquote(char *text)
{
    size_t length = strlen(text);
    if (length >= 2 && (text[0] == '"' || text[0] == '\'')
        && text[length - 1] == text[0]) {
        text[length - 1] = '\0';
        return text + 1;
    }
    return text;
}

static bool parseBool(const char *value, bool fallback)
{
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0
        || strcmp(value, "yes") == 0 || strcmp(value, "on") == 0)
        return true;
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0
        || strcmp(value, "no") == 0 || strcmp(value, "off") == 0)
        return false;
    return fallback;
}

static int parseInt(const char *value, int fallback, int min, int max)
{
    char *end = NULL;
    errno = 0;
    long number = strtol(value, &end, 10);
    if (errno != 0 || end == value)
        return fallback;
    if (number < min)
        number = min;
    if (number > max)
        number = max;
    return (int)number;
}

static double parseDouble(const char *value, double fallback, double min, double max)
{
    char *end = NULL;
    errno = 0;
    double number = strtod(value, &end);
    if (errno != 0 || end == value)
        return fallback;
    if (number < min)
        number = min;
    if (number > max)
        number = max;
    return number;
}

void ntConfigDefaults(struct CompositorConfig *config)
{
    memset(config, 0, sizeof(*config));
    config->repeatRate = 25;
    config->repeatDelay = 600;
    config->numlock = false;
    config->layoutPerWindow = false;
    config->touchpadTap = true;
    config->touchpadNaturalScroll = true;
    config->touchpadDisableWhileTyping = true;
    config->touchpadSpeed = 0.0;
    config->mouseNaturalScroll = false;
    config->mouseSpeed = 0.0;
    config->leftHanded = false;
    config->gestures = true;
    config->cursorSize = 24;
    config->workspaceCount = 4;
    config->workspaceWrap = false;

    config->decorationsServer = true;
    config->titleHeight = 30;
    config->borderWidth = 1;
    config->buttonsLeft = false;
    config->titleCentered = true;
    config->showMinimize = true;
    config->showMaximize = true;
    snprintf(config->titleFont, sizeof(config->titleFont), "%s", "Sans Bold 10");
    ntConfigParseColor("#262626", config->activeBackground);
    ntConfigParseColor("#f2f2f5", config->activeForeground);
    ntConfigParseColor("#1a1a1a", config->inactiveBackground);
    ntConfigParseColor("#8e8e93", config->inactiveForeground);
    ntConfigParseColor("#e5484d", config->closeHover);

    config->focusFollowsMouse = false;
    config->snapping = true;
    config->switcherAllWorkspaces = false;
    snprintf(config->placement, sizeof(config->placement), "%s", "center");
    config->rulesAlwaysOnTop[0] = '\0';
    config->rulesMaximized[0] = '\0';
    for (int i = 0; i < 4; ++i)
        snprintf(config->hotCorners[i], sizeof(config->hotCorners[i]), "%s", "none");
    config->bindingCount = 0;
    config->builtinShortcuts = true;

    config->effects = true;
    config->cornerRadius = 10;
    config->blur = true;
    config->blurStrength = 2;
    config->animations = true;
    config->animationSpeed = 1.0;
}

bool ntConfigParseColor(const char *text, float out[4])
{
    if (!text || text[0] != '#')
        return false;
    size_t length = strlen(text + 1);
    unsigned long value = strtoul(text + 1, NULL, 16);
    if (length == 6) {
        out[0] = ((value >> 16) & 0xff) / 255.0f;
        out[1] = ((value >> 8) & 0xff) / 255.0f;
        out[2] = (value & 0xff) / 255.0f;
        out[3] = 1.0f;
        return true;
    }
    if (length == 8) {
        out[3] = ((value >> 24) & 0xff) / 255.0f;
        out[0] = ((value >> 16) & 0xff) / 255.0f;
        out[1] = ((value >> 8) & 0xff) / 255.0f;
        out[2] = (value & 0xff) / 255.0f;
        return true;
    }
    return false;
}

void ntConfigLoadSystemKeyboard(struct CompositorConfig *config, const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return;
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char *text = trim(line);
        if (text[0] == '#' || text[0] == '\0')
            continue;
        char *equals = strchr(text, '=');
        if (!equals)
            continue;
        *equals = '\0';
        char *key = trim(text);
        char *value = unquote(trim(equals + 1));
        if (strcmp(key, "XKBLAYOUT") == 0 && config->keyboardLayout[0] == '\0')
            copyText(config->keyboardLayout, sizeof(config->keyboardLayout), value);
        else if (strcmp(key, "XKBVARIANT") == 0 && config->keyboardVariant[0] == '\0')
            copyText(config->keyboardVariant, sizeof(config->keyboardVariant), value);
        else if (strcmp(key, "XKBOPTIONS") == 0 && config->keyboardOptions[0] == '\0')
            copyText(config->keyboardOptions, sizeof(config->keyboardOptions), value);
        else if (strcmp(key, "XKBMODEL") == 0 && config->keyboardModel[0] == '\0')
            copyText(config->keyboardModel, sizeof(config->keyboardModel), value);
    }
    fclose(file);
}

bool ntConfigLoadFile(struct CompositorConfig *config, const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return false;
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        char *text = trim(line);
        if (text[0] == '#' || text[0] == '\0')
            continue;
        char *equals = strchr(text, '=');
        if (!equals)
            continue;
        *equals = '\0';
        char *key = trim(text);
        char *value = unquote(trim(equals + 1));

        if (strcmp(key, "keyboard.layout") == 0)
            copyText(config->keyboardLayout, sizeof(config->keyboardLayout), value);
        else if (strcmp(key, "keyboard.variant") == 0)
            copyText(config->keyboardVariant, sizeof(config->keyboardVariant), value);
        else if (strcmp(key, "keyboard.options") == 0)
            copyText(config->keyboardOptions, sizeof(config->keyboardOptions), value);
        else if (strcmp(key, "keyboard.model") == 0)
            copyText(config->keyboardModel, sizeof(config->keyboardModel), value);
        else if (strcmp(key, "keyboard.repeatRate") == 0)
            config->repeatRate = parseInt(value, config->repeatRate, 1, 100);
        else if (strcmp(key, "keyboard.repeatDelay") == 0)
            config->repeatDelay = parseInt(value, config->repeatDelay, 100, 2000);
        else if (strcmp(key, "keyboard.numlock") == 0)
            config->numlock = parseBool(value, config->numlock);
        else if (strcmp(key, "keyboard.layoutPerWindow") == 0)
            config->layoutPerWindow = parseBool(value, config->layoutPerWindow);
        else if (strcmp(key, "touchpad.tapToClick") == 0)
            config->touchpadTap = parseBool(value, config->touchpadTap);
        else if (strcmp(key, "touchpad.naturalScroll") == 0)
            config->touchpadNaturalScroll = parseBool(value, config->touchpadNaturalScroll);
        else if (strcmp(key, "touchpad.disableWhileTyping") == 0)
            config->touchpadDisableWhileTyping =
                parseBool(value, config->touchpadDisableWhileTyping);
        else if (strcmp(key, "touchpad.speed") == 0)
            config->touchpadSpeed = parseDouble(value, config->touchpadSpeed, -1.0, 1.0);
        else if (strcmp(key, "mouse.naturalScroll") == 0)
            config->mouseNaturalScroll = parseBool(value, config->mouseNaturalScroll);
        else if (strcmp(key, "mouse.speed") == 0)
            config->mouseSpeed = parseDouble(value, config->mouseSpeed, -1.0, 1.0);
        else if (strcmp(key, "touchpad.gestures") == 0)
            config->gestures = parseBool(value, config->gestures);
        else if (strcmp(key, "mouse.leftHanded") == 0)
            config->leftHanded = parseBool(value, config->leftHanded);
        else if (strcmp(key, "cursor.theme") == 0)
            copyText(config->cursorTheme, sizeof(config->cursorTheme), value);
        else if (strcmp(key, "cursor.size") == 0)
            config->cursorSize = parseInt(value, config->cursorSize, 8, 256);
        else if (strcmp(key, "workspaces.count") == 0)
            config->workspaceCount = parseInt(value, config->workspaceCount, 1, 9);
        else if (strcmp(key, "workspaces.wrap") == 0)
            config->workspaceWrap = parseBool(value, config->workspaceWrap);
        else if (strcmp(key, "decorations.server") == 0)
            config->decorationsServer = parseBool(value, config->decorationsServer);
        else if (strcmp(key, "decorations.titleHeight") == 0)
            config->titleHeight = parseInt(value, config->titleHeight, 16, 64);
        else if (strcmp(key, "decorations.border") == 0)
            config->borderWidth = parseInt(value, config->borderWidth, 0, 8);
        else if (strcmp(key, "decorations.buttonsLeft") == 0)
            config->buttonsLeft = parseBool(value, config->buttonsLeft);
        else if (strcmp(key, "decorations.titleCentered") == 0)
            config->titleCentered = parseBool(value, config->titleCentered);
        else if (strcmp(key, "decorations.showMinimize") == 0)
            config->showMinimize = parseBool(value, config->showMinimize);
        else if (strcmp(key, "decorations.showMaximize") == 0)
            config->showMaximize = parseBool(value, config->showMaximize);
        else if (strcmp(key, "decorations.font") == 0 && value[0])
            copyText(config->titleFont, sizeof(config->titleFont), value);
        else if (strcmp(key, "decorations.activeBackground") == 0)
            ntConfigParseColor(value, config->activeBackground);
        else if (strcmp(key, "decorations.activeForeground") == 0)
            ntConfigParseColor(value, config->activeForeground);
        else if (strcmp(key, "decorations.inactiveBackground") == 0)
            ntConfigParseColor(value, config->inactiveBackground);
        else if (strcmp(key, "decorations.inactiveForeground") == 0)
            ntConfigParseColor(value, config->inactiveForeground);
        else if (strcmp(key, "decorations.closeHover") == 0)
            ntConfigParseColor(value, config->closeHover);
        else if (strcmp(key, "windows.focusFollowsMouse") == 0)
            config->focusFollowsMouse = parseBool(value, config->focusFollowsMouse);
        else if (strcmp(key, "windows.snapping") == 0)
            config->snapping = parseBool(value, config->snapping);
        else if (strcmp(key, "switcher.allWorkspaces") == 0)
            config->switcherAllWorkspaces = parseBool(value, config->switcherAllWorkspaces);
        else if (strcmp(key, "effects.enabled") == 0)
            config->effects = parseBool(value, config->effects);
        else if (strcmp(key, "effects.cornerRadius") == 0)
            config->cornerRadius = parseInt(value, config->cornerRadius, 0, 32);
        else if (strcmp(key, "effects.blur") == 0)
            config->blur = parseBool(value, config->blur);
        else if (strcmp(key, "effects.blurStrength") == 0)
            config->blurStrength = parseInt(value, config->blurStrength, 1, 4);
        else if (strcmp(key, "effects.animations") == 0)
            config->animations = parseBool(value, config->animations);
        else if (strcmp(key, "effects.animationSpeed") == 0)
            config->animationSpeed = parseDouble(value, config->animationSpeed, 0.25, 4.0);
        else if (strcmp(key, "windows.placement") == 0)
            copyText(config->placement, sizeof(config->placement), value);
        else if (strcmp(key, "rules.alwaysOnTop") == 0)
            copyText(config->rulesAlwaysOnTop, sizeof(config->rulesAlwaysOnTop), value);
        else if (strcmp(key, "rules.maximized") == 0)
            copyText(config->rulesMaximized, sizeof(config->rulesMaximized), value);
        else if (strcmp(key, "rules.fullscreen") == 0)
            copyText(config->rulesFullscreen, sizeof(config->rulesFullscreen), value);
        else if (strcmp(key, "rules.minimized") == 0)
            copyText(config->rulesMinimized, sizeof(config->rulesMinimized), value);
        else if (strcmp(key, "rules.workspace") == 0)
            copyText(config->rulesWorkspace, sizeof(config->rulesWorkspace), value);
        else if (strcmp(key, "hotCorners.topLeft") == 0)
            copyText(config->hotCorners[0], sizeof(config->hotCorners[0]), value);
        else if (strcmp(key, "hotCorners.topRight") == 0)
            copyText(config->hotCorners[1], sizeof(config->hotCorners[1]), value);
        else if (strcmp(key, "hotCorners.bottomLeft") == 0)
            copyText(config->hotCorners[2], sizeof(config->hotCorners[2]), value);
        else if (strcmp(key, "hotCorners.bottomRight") == 0)
            copyText(config->hotCorners[3], sizeof(config->hotCorners[3]), value);
        else if (strcmp(key, "hotkeys.enabled") == 0)
            config->builtinShortcuts = parseBool(value, config->builtinShortcuts);
        else if (strncmp(key, "hotkey.", 7) == 0) {
            for (int i = 0; i < NT_HOTKEY_COUNT; ++i) {
                if (strcmp(key + 7, ntHotkeyNames[i]) == 0) {
                    copyText(config->hotkeys[i], sizeof(config->hotkeys[i]), value);
                    break;
                }
            }
        }
        else if (strncmp(key, "bind.", 5) == 0 && config->bindingCount < NT_MAX_BINDINGS) {
            // bind.N = Super+Shift+T|command
            char *bar = strchr(value, '|');
            if (bar && bar != value && bar[1] != '\0') {
                *bar = '\0';
                struct NtBinding *binding = &config->bindings[config->bindingCount++];
                copyText(binding->keys, sizeof(binding->keys), trim(value));
                copyText(binding->command, sizeof(binding->command), trim(bar + 1));
            }
        }
    }
    fclose(file);
    return true;
}

void ntConfigPath(char *buffer, size_t size, const char *name)
{
    const char *config = getenv("XDG_CONFIG_HOME");
    if (config && config[0] == '/') {
        snprintf(buffer, size, "%s/newteller/%s", config, name);
        return;
    }
    const char *home = getenv("HOME");
    snprintf(buffer, size, "%s/.config/newteller/%s", home ? home : "", name);
}

int ntConfigSplitList(const char *list, char out[][32], int max)
{
    int count = 0;
    const char *start = list;
    while (start && count < max) {
        const char *comma = strchr(start, ',');
        size_t length = comma ? (size_t)(comma - start) : strlen(start);
        if (length > 31)
            length = 31;
        memcpy(out[count], start, length);
        out[count][length] = '\0';
        char *item = trim(out[count]);
        if (item != out[count])
            memmove(out[count], item, strlen(item) + 1);
        ++count;
        if (!comma)
            break;
        start = comma + 1;
    }
    // A single empty entry means "no list".
    if (count == 1 && out[0][0] == '\0')
        return 0;
    return count;
}

int ntOutputsLoad(const char *path, struct SavedOutput *outputs, int max)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return 0;
    int count = 0;
    char line[1024];
    while (count < max && fgets(line, sizeof(line), file)) {
        char *fields[6] = {0};
        int n = 0;
        char *cursor = line;
        while (n < 6) {
            fields[n++] = cursor;
            char *tab = strchr(cursor, '\t');
            if (!tab)
                break;
            *tab = '\0';
            cursor = tab + 1;
        }
        if (n != 6)
            continue;
        struct SavedOutput *out = &outputs[count];
        memset(out, 0, sizeof(*out));
        copyText(out->key, sizeof(out->key), fields[0]);
        out->enabled = parseBool(trim(fields[1]), true);
        if (sscanf(fields[2], "%dx%d@%d", &out->width, &out->height, &out->refresh) < 2)
            continue;
        if (sscanf(fields[3], "%d,%d", &out->x, &out->y) != 2)
            continue;
        out->scale = parseDouble(trim(fields[4]), 1.0, 0.25, 4.0);
        out->transform = parseInt(trim(fields[5]), 0, 0, 7);
        ++count;
    }
    fclose(file);
    return count;
}

static void makeParents(const char *path)
{
    char directory[1024];
    snprintf(directory, sizeof(directory), "%s", path);
    for (char *p = directory + 1; *p; ++p) {
        if (*p != '/')
            continue;
        *p = '\0';
        mkdir(directory, 0700);
        *p = '/';
    }
}

bool ntOutputsSave(const char *path, const struct SavedOutput *outputs, int count)
{
    makeParents(path);
    char temporary[1100];
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    FILE *file = fopen(temporary, "w");
    if (!file)
        return false;
    for (int i = 0; i < count; ++i) {
        const struct SavedOutput *out = &outputs[i];
        char key[NT_CONFIG_TEXT];
        copyText(key, sizeof(key), out->key);
        for (char *p = key; *p; ++p) {
            if (*p == '\t' || *p == '\n' || *p == '\r')
                *p = ' ';
        }
        fprintf(file, "%s\t%d\t%dx%d@%d\t%d,%d\t%.3f\t%d\n", key,
                out->enabled ? 1 : 0, out->width, out->height, out->refresh,
                out->x, out->y, out->scale, out->transform);
    }
    if (fclose(file) != 0) {
        unlink(temporary);
        return false;
    }
    return rename(temporary, path) == 0;
}

bool ntConfigListContains(const char *list, const char *name)
{
    if (!list || !name || name[0] == '\0')
        return false;
    size_t length = strlen(name);
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',')
            ++p;
        const char *start = p;
        while (*p && *p != ',')
            ++p;
        const char *end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
            --end;
        if ((size_t)(end - start) == length && strncasecmp(start, name, length) == 0)
            return true;
    }
    return false;
}

int ntConfigListNumber(const char *list, const char *name)
{
    if (!list || !name || name[0] == '\0')
        return -1;
    size_t length = strlen(name);
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',')
            ++p;
        const char *start = p;
        while (*p && *p != ',')
            ++p;
        const char *equals = memchr(start, '=', (size_t)(p - start));
        if (!equals)
            continue;
        const char *end = equals;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
            --end;
        if ((size_t)(end - start) == length && strncasecmp(start, name, length) == 0) {
            char *after = NULL;
            long number = strtol(equals + 1, &after, 10);
            if (after != equals + 1 && number >= 0 && number < 1000)
                return (int)number;
            return -1;
        }
    }
    return -1;
}
