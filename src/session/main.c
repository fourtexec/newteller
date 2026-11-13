// NewTeller session compositor.
//
// This is what makes NewTeller a desktop environment instead of a panel that
// borrows another desktop's display server.  It owns the DRM/KMS outputs (or a
// nested/headless output for testing), implements the Wayland protocols the
// shell and normal applications need, runs the NewTeller QML shell as its
// first client, and starts XWayland for X11 applications.
//
// The compositor deliberately stays small: it is a floating-stack compositor
// with layer-shell support, not a tiling window manager.  The desktop itself
// is the existing NewTeller shell, running unmodified as a native Wayland
// client on the layer surfaces it already creates.

#define _GNU_SOURCE

#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/libinput.h>
#include <wlr/backend/session.h>
#include <wlr/backend/wayland.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/render/allocator.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/pass.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_security_context_v1.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_foreign_registry.h>
#include <wlr/types/wlr_xdg_foreign_v1.h>
#include <wlr/types/wlr_xdg_foreign_v2.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/util/transform.h>
#include <wlr/version.h>
// Version-dependent headers go after wlr/version.h, which defines the macros.
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR >= 19
#include <wlr/types/wlr_xdg_system_bell_v1.h>
#endif
#include <wlr/xwayland/xwayland.h>

#include <xkbcommon/xkbcommon.h>

#include <libinput.h>

#include <cairo.h>
#include <pango/pangocairo.h>

#include <errno.h>
#include <math.h>
#include <linux/input-event-codes.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

// DRM fourcc codes (drm_fourcc.h), for reading window textures.
#ifndef DRM_FORMAT_ARGB8888
#define DRM_FORMAT_ARGB8888 0x34325241
#endif
#ifndef DRM_FORMAT_ABGR8888
#define DRM_FORMAT_ABGR8888 0x34324241
#endif
#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID 0x00ffffffffffffffULL
#endif
#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0ULL
#endif

enum GhostKind {
    GHOST_CLOSE,
    GHOST_MINIMIZE,
};

// The last picture of a window that closed or minimized, drawn while it
// fades away.
struct Ghost {
    struct wl_list link;
    struct wlr_buffer *buffer;          // locked client buffer
    struct wlr_client_buffer *client;
    struct wlr_buffer *titleBuffer;     // locked title bar, or NULL
    struct wlr_box box;                 // surface, layout coordinates
    struct wlr_box titleBox;
    struct wlr_box frame;               // whole window, for rounding
    int radius;
    int workspace;
    int kind;
    uint64_t start;
    int duration;
};

// Where the shell wants the backdrop blurred behind one of its surfaces
// (layer namespace + output), in surface coordinates.
struct BlurRegion {
    struct wl_list link;
    char ns[64];
    char output[64];
    struct wlr_box box;
    int radius;
};

// The parts of a compositor-drawn window frame under the pointer.
enum FramePart {
    FRAME_NONE = 0,
    FRAME_TITLE,
    FRAME_CLOSE,
    FRAME_MAXIMIZE,
    FRAME_MINIMIZE,
    FRAME_EDGE,
};

// A user shortcut compiled from compositor.conf.
struct Binding {
    uint32_t modifiers;
    xkb_keysym_t sym;
    char command[NT_CONFIG_TEXT];
};

struct Server;
struct Output;
struct View;
struct Layer;

static void spawnDetached(const char *program, const char *const argv[]);
static void maybeStartShell(struct Server *server);
static void ipcBroadcast(struct Server *server, const char *line);
static bool ipcHasWatchers(struct Server *server);
static void broadcastLayout(struct Server *server);
static void lockFocusSurface(struct Server *server, struct wlr_surface *surface);
static void spawnLocker(struct Server *server);
static void setLayout(struct Server *server, int index);
static void switchWorkspace(struct Server *server, int index);
static void broadcastWorkspaces(struct Server *server);
static void broadcastSwitcher(struct Server *server);
static void switcherCancel(struct Server *server);
static void refreshPointerFocus(struct Server *server);
static void viewUpdateDecoration(struct View *view);
static void applyDecorationMode(struct View *view);
static void compileBindings(struct Server *server);
static bool captureThumbnail(struct View *view, int maxSize);
static void thumbnailPath(struct View *view, char *path, size_t size);
static bool x11WantsFrame(struct View *view);
static void createGhost(struct View *view, int kind);
static void viewAnimate(struct View *view, float fromScale, float fromAlpha, int baseMs);
static void startWorkspaceSlide(struct Server *server, int from, int to, int direction);
static void scheduleAllFrames(struct Server *server);
static void setBlurRegion(struct Server *server, const char *ns, const char *output,
                          struct wlr_box box, int radius);
static void clearBlurRegion(struct Server *server, const char *ns, const char *output);
static void destroyGhost(struct Ghost *ghost);
static void viewRestoreGeometry(struct View *view, int x, int y, int width, int height);
static pid_t viewPid(struct View *view);
static void updateIdleInhibited(struct Server *server);
static bool surfaceLayoutPosition(struct Server *server, struct wlr_surface *surface, int *x, int *y);
static bool shortcutsInhibited(struct Server *server);
static void raisePinnedViews(struct Server *server);
static void toggleShowDesktop(struct Server *server);
static void shellAction(const char *method);
static void checkHotCorner(struct Server *server);
static void arrangeLockSurfaces(struct Server *server);
static void updateFullscreenLayers(struct Server *server);
static struct wlr_output *viewNearestOutput(struct View *view);
static void resizeOffsetsFrom(struct Server *server, double x, double y);
static void broadcastFullscreen(struct Server *server, bool force);
static struct wlr_box viewOuterBox(struct View *view);
static uint32_t dragSnapEdges(struct Server *server, struct View *view);
static void updateSnapPreview(struct Server *server, bool dragging);
static void viewSetMaximized(struct View *view, bool maximized);
static void requestWindowMenu(struct View *view, int x, int y);
struct Keyboard;
static struct wlr_input_method_keyboard_grab_v2 *keyboardImGrab(struct Keyboard *keyboard);

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static void *xcalloc(size_t count, size_t size)
{
    void *pointer = calloc(count, size);
    if (!pointer) {
        fprintf(stderr, "newteller-compositor: out of memory\n");
        exit(1);
    }
    return pointer;
}

// A line that grows: replies and events whose length depends on how many
// windows or monitors there are. A fixed buffer used to cut the list (and
// with it the selected window of Alt+Tab) once enough windows were open.
struct Text {
    char *data;
    size_t length;
    size_t capacity;
};

__attribute__((format(printf, 2, 3)))
static void textAppend(struct Text *text, const char *format, ...)
{
    for (;;) {
        va_list arguments;
        va_start(arguments, format);
        int written = vsnprintf(text->data ? text->data + text->length : NULL,
                                text->capacity - text->length, format, arguments);
        va_end(arguments);
        if (written < 0)
            return;
        if ((size_t)written < text->capacity - text->length) {
            text->length += (size_t)written;
            return;
        }
        size_t capacity = text->capacity ? text->capacity * 2 : 1024;
        while (capacity <= text->length + (size_t)written)
            capacity *= 2;
        char *data = realloc(text->data, capacity);
        if (!data) {
            fprintf(stderr, "newteller-compositor: out of memory\n");
            exit(1);
        }
        text->data = data;
        text->capacity = capacity;
        text->data[text->length] = '\0';
    }
}

static void textFree(struct Text *text)
{
    free(text->data);
    *text = (struct Text){0};
}


// A layer-shell surface plus the scene node that draws it.
struct Layer {
    struct wl_list link;
    struct Server *server;
    struct wlr_layer_surface_v1 *layer;
    struct wlr_scene_layer_surface_v1 *scene;
    struct Output *output;
    bool arrangePending;
    bool hasLastState;
    struct wlr_layer_surface_v1_state lastState;

    struct wl_listener commit;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener newPopup;
};

struct View {
    struct wl_list link; // top-most first
    struct Server *server;
    // keyboard.layoutPerWindow: the layout this window was last used with
    // (0, the first layout, for a new window).
    int layoutIndex;

    // Exactly one of xdg/x11 is set.
    struct wlr_xdg_toplevel *xdg;
    struct wlr_xwayland_surface *x11;

    struct wlr_scene_tree *tree;
    struct wlr_foreign_toplevel_handle_v1 *handle;
    // ext-foreign-toplevel-list: the read-only window list (portals, tools).
    struct wlr_ext_foreign_toplevel_handle_v1 *extHandle;

    bool mapped;
    bool minimized;
    bool maximized;
    bool fullscreen;
    bool overrideRedirect;
    bool positioned;
    bool hiddenByShowDesktop;
    // Stays above ordinary windows (window menu, rules.alwaysOnTop).
    bool alwaysOnTop;
    // Shown on every desktop (window menu).
    bool sticky;
    // Told it is suspended (xdg-shell v6) while hidden.
    bool suspended;
    // rules.* were applied at the first map.
    bool rulesApplied;
    // The virtual desktop the window lives on.
    int workspace;
    // A stable number for the control socket (thumbnails, overview).
    uint32_t id;

    // Server-side decoration: title bar and border drawn by the compositor
    // for windows that ask for one (xdg-decoration, X11 without Motif hints).
    bool ssd;
    struct wlr_scene_tree *decoTree;
    struct wlr_scene_buffer *decoTitle;
    struct wlr_scene_rect *decoBorder[3];
    int decoWidth;
    int decoHeight;
    bool decoFocused;
    bool decoMaximized;
    float decoScale;
    int decoHover;
    int decoRenderedHover;
    char decoTitleText[160];
    struct wlr_xdg_toplevel_decoration_v1 *xdgDecoration;
    struct wl_listener xdgDecorationMode;
    struct wl_listener xdgDecorationDestroy;
    struct wl_listener x11SetDecorations;

    // Geometry before fullscreen (kept apart from the maximize one).
    int fullSavedX;
    int fullSavedY;
    int fullSavedWidth;
    int fullSavedHeight;
    // The client did not answer an xdg ping in time.
    bool hung;
    // The thumbnail on disk: the surface commit it shows and its size, so
    // a window that did not draw since is not read back and encoded again.
    bool thumbValid;
    uint32_t thumbSeq;
    int thumbSize;
    // Counts the commits of the window's main surface (xdg windows).
    uint32_t commits;
    struct wl_listener pingTimeout;

    // Open/restore animation (effects renderer).
    bool animating;
    uint64_t animStart;
    int animDuration;
    float animFromScale;
    float animFromAlpha;
    int snapEdges;
    int snapSavedX;
    int snapSavedY;
    int snapSavedWidth;
    int snapSavedHeight;

    int x;
    int y;
    int width;
    int height;
    int savedX;
    int savedY;
    int savedWidth;
    int savedHeight;

    struct wl_listener commit;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener requestMove;
    struct wl_listener requestConfigure;
    struct wl_listener requestResize;
    struct wl_listener requestMaximize;
    struct wl_listener requestFullscreen;
    struct wl_listener requestMinimize;
    struct wl_listener requestShowMenu;
    struct wl_listener foreignClose;
    struct wl_listener foreignActivate;
    struct wl_listener foreignMinimize;
    struct wl_listener foreignMaximize;
    struct wl_listener foreignFullscreen;
    struct wl_listener x11RequestClose;
    struct wl_listener x11RequestActivate;
    struct wl_listener setTitle;
    struct wl_listener setAppId;
    struct wl_listener associate;
    struct wl_listener dissociate;
};

struct Keyboard {
    struct wl_list link;
    struct Server *server;
    struct wlr_keyboard *keyboard;
    // Virtual keyboards that bring their own keymap keep it on reload.
    bool ownKeymap;
    // Caps Lock (1) and Num Lock (2) as last seen; the shell shows changes.
    int lockKeys;
    bool lockKeysKnown;
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

// A text field client (zwp_text_input_v3). The relay below passes its state
// to the input method (IBus, fcitx5) and the composed text back.
struct TextInput {
    struct wl_list link;
    struct Server *server;
    struct wlr_text_input_v3 *input;
    // The surface it gets once an input method exists: without one, GTK
    // must not see the focus, or it stops handling dead keys itself.
    struct wlr_surface *pendingFocus;
    struct wl_listener pendingFocusDestroy;
    struct wl_listener enable;
    struct wl_listener commit;
    struct wl_listener disable;
    struct wl_listener destroy;
};

// The input method's candidate window, shown under the text cursor.
struct InputPopup {
    struct wl_list link;
    struct Server *server;
    struct wlr_input_popup_surface_v2 *popup;
    struct wlr_scene_tree *tree;
    struct wlr_box sentRectangle;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct Output {
    struct wl_list link;
    struct Server *server;
    struct wlr_output *output;
    struct wlr_scene_output *sceneOutput;
    struct wlr_box usable;
    // The screen and its usable area as maximized, snapped and fullscreen
    // windows were last fitted to (refitViews).
    struct wlr_box fitFull;
    struct wlr_box fitUsable;
    // Turned off by the user (output management), as opposed to blanked.
    bool disabled;
    // Blanked by `dpms off` (screen saver); comes back with `dpms on`.
    bool poweredOff;
    bool gammaPending;
    // Offscreen buffers for the blurred backdrop.
    struct wlr_buffer *blurBuffers[2];

    struct wl_listener frame;
    struct wl_listener requestState;
    struct wl_listener destroy;
    struct wl_listener sceneDestroy;
};

struct Popup {
    struct wlr_xdg_popup *popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};

// A pointer device, kept so libinput settings can be re-applied on reload.
struct Pointer {
    struct wl_list link;
    struct Server *server;
    struct wlr_input_device *device;
    struct wl_listener destroy;
};

// A touchscreen.
struct TouchDevice {
    struct wl_list link;
    struct Server *server;
    struct wl_listener destroy;
};

// One ext-session-lock surface (the locker's window on one output).
// A KDE server-decoration object: GTK 3 drops its own title bar when the
// compositor offers to draw one this way (it does not speak xdg-decoration).
struct KdeDecoration {
    struct Server *server;
    struct wlr_server_decoration *decoration;
    struct wl_listener mode;
    struct wl_listener destroy;
    struct wl_list link;
};

struct LockSurface {
    struct wl_list link;
    struct Server *server;
    struct wlr_session_lock_surface_v1 *lockSurface;
    struct wlr_scene_tree *tree;
    struct wl_listener map;
    struct wl_listener destroy;
};

struct IdleInhibitor {
    struct wl_list link;
    struct Server *server;
    struct wlr_idle_inhibitor_v1 *inhibitor;
    struct wl_listener destroy;
};

// A connection on the compositor's control socket ($NEWTELLER_IPC).
struct IpcClient {
    struct wl_list link;
    struct Server *server;
    int fd;
    struct wl_event_source *source;
    char buffer[1024];
    size_t length;
    bool watching;
    // "idle-inhibit on": the shell relays org.freedesktop.ScreenSaver
    // inhibitions (video players, XWayland applications). Released with
    // the connection, so a crashed shell cannot keep the screen on.
    bool inhibitsIdle;
};

enum CursorMode {
    CURSOR_PASSTHROUGH,
    CURSOR_MOVE,
    CURSOR_RESIZE,
};

struct Server {
    struct wl_display *display;
    struct wl_event_loop *eventLoop;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_session *session;

    struct wlr_scene *scene;
    struct wlr_scene_output_layout *sceneLayout;
    struct wlr_output_layout *outputLayout;

    struct wlr_scene_tree *backgroundTree;
    struct wlr_scene_tree *bottomTree;
    struct wlr_scene_tree *toplevelTree;
    // Where a dragged window will snap: a translucent rectangle above the
    // windows, shown while the pointer is at a screen edge.
    struct wlr_scene_tree *snapTree;
    struct wlr_scene_rect *snapRect;
    struct wlr_scene_tree *topTree;
    struct wlr_scene_tree *overlayTree;
    // Input method candidate windows, above everything but the lock screen.
    struct wlr_scene_tree *inputPopupTree;

    struct wlr_compositor *compositor;
    struct wlr_xdg_shell *xdgShell;
    struct wlr_layer_shell_v1 *layerShell;
    struct wlr_foreign_toplevel_manager_v1 *foreignToplevels;
    struct wlr_ext_foreign_toplevel_list_v1 *extToplevels;
    // Sandboxed clients (Flatpak) are known by their security context and
    // kept away from the privileged protocols (globalFilter).
    struct wlr_security_context_manager_v1 *securityContexts;
    struct wl_listener securityContextsDestroy;
    struct wlr_xdg_activation_v1 *activation;
    struct wlr_server_decoration_manager *serverDecoration;
    // org_kde_kwin_server_decoration objects (GTK 3 asks through this one).
    struct wl_list kdeDecorations;
    struct wl_listener newKdeDecoration;
    struct wlr_seat *seat;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursorManager;
    struct wlr_xwayland *xwayland;
    bool xwaylandReady;
    bool xwaylandFailed;

    char socketName[108];
    char shellProgram[PATH_MAX];
    char shellPath[PATH_MAX];
    bool noShell;
    bool noXwayland;
    bool nested;
    bool headless;
    int headlessWidth;
    int headlessHeight;
    bool running;

    pid_t shellPid;
    bool shellStarted;
    int shellTries;
    // Crash restarts of the shell within the current one-minute window.
    int shellRestarts;
    time_t shellRestartWindow;
    struct wl_event_source *shellTimer;
    struct wl_event_source *arrangeIdle;
    // A pass over every screen after the layout changed (handleLayoutChange).
    struct wl_event_source *layoutIdle;
    struct wl_event_source *sigchld;

    struct wl_list outputs;
    struct wl_list views;
    struct wl_list layers;
    struct wl_list keyboards;

    struct View *focusedView;
    struct Layer *focusedLayer;
    struct View *previousView;
    struct View *fullscreenView;
    struct View *grabbedView;
    // Alt+Tab: while Alt is held, Tab walks the stacking order without
    // reordering it; the window under the cursor is committed on release.
    struct View *switchTarget;
    bool switching;
    // A Super+drag move/resize: the client never saw the press, so it does
    // not get the release either.
    bool superGrab;
    // A button pressed while a window was being moved or resized: nobody
    // saw the press, so its release is dropped too (0 = none).
    uint32_t swallowedButton;
    // Distance between the pointer and the edges being resized when the
    // resize started (0 when a client starts it from its border).
    double resizeOffsetX;
    double resizeOffsetY;
    // Set when Super goes down alone; any other key or click clears it, so
    // only a plain tap of Super toggles the start menu.
    bool logoTap;
    bool showDesktop;
    // The hot corner the pointer last fired (-1: none); it fires again only
    // after the pointer has left that corner.
    int hotCornerFired;
    // The output under the pointer, as last reported ("event output NAME"):
    // the shell opens keyboard-invoked cards on that screen.
    struct wlr_output *pointerOutput;
    // A fullscreen window is shown (as last reported, "event fullscreen").
    bool fullscreenShown;
    enum CursorMode cursorMode;
    double grabX;
    double grabY;
    int grabWidth;
    int grabHeight;
    uint32_t resizeEdges;
    int cascade;

    struct CompositorConfig config;
    int workspace;
    uint32_t nextViewId;
    struct Binding bindings[NT_MAX_BINDINGS];
    int bindingCount;
    struct wlr_xdg_decoration_manager_v1 *xdgDecorationManager;
    struct wl_listener newXdgDecoration;
    // Pointer state over compositor-drawn frames.
    const char *cursorImage;
    struct View *hoverView;
    struct View *lastTitleView;
    uint32_t lastTitleClick;
    bool suppressRaise;

    // Touchpad gestures: what the current swipe does.
    struct wlr_pointer_gestures_v1 *pointerGestures;
    int gestureKind;            // 0 none (forwarded), 1 move window, 2 desktops
    double gestureDx;
    double gestureDy;
    struct View *gestureView;
    struct wl_listener swipeBegin;
    struct wl_listener swipeUpdate;
    struct wl_listener swipeEnd;
    struct wl_listener pinchBegin;
    struct wl_listener pinchUpdate;
    struct wl_listener pinchEnd;
    struct wl_listener holdBegin;
    struct wl_listener holdEnd;

    // Effects renderer state.
    uint64_t effectFrames;      // frames drawn by the effects renderer
    uint64_t fallbackFrames;    // frames it handed back to the scene
    struct wl_list ghosts;
    struct wl_list blurRegions;
    bool wsAnimating;
    int wsFrom;
    int wsDirection;
    uint64_t wsStart;
    int wsDuration;
    struct wl_list pointers;
    int layoutCount;
    int layoutIndex;
    char layoutNames[8][32];

    struct wlr_session_lock_manager_v1 *lockManager;
    struct wlr_session_lock_v1 *activeLock;
    struct wlr_scene_tree *lockTree;
    struct wl_list lockSurfaces;
    bool locked;
    int lockRespawns;
    struct wl_event_source *lockRespawnTimer;

    struct wlr_idle_notifier_v1 *idleNotifier;
    int idleInhibitors;
    struct wl_list idleInhibitorList; // struct IdleInhibitor
    int ipcIdleInhibitors;
    // Input methods (text-input-v3 <-> input-method-v2 relay).
    struct wl_list textInputs;   // struct TextInput
    struct wl_list inputPopups;  // struct InputPopup
    struct wlr_input_method_v2 *inputMethod;
    struct wl_listener newTextInput;
    struct wl_listener newInputMethod;
    struct wl_listener inputMethodCommit;
    struct wl_listener inputMethodNewPopup;
    struct wl_listener inputMethodGrabKeyboard;
    struct wl_listener inputMethodDestroy;
    struct wl_listener inputMethodGrabDestroy;
    struct wl_listener keyboardFocusChange;

    struct wlr_output_manager_v1 *outputManager;
    struct wlr_gamma_control_manager_v1 *gammaManager;
    // wlr-output-power-management: wlopm and idle tools turn screens off.
    struct wlr_output_power_manager_v1 *outputPowerManager;
    struct wl_listener outputPowerSetMode;
    // xdg-system-bell (wlroots 0.19+): a terminal's or an application's bell.
    struct wl_listener systemBellRing;
    uint64_t lastBell;

    int ipcFd;
    struct wl_event_source *ipcSource;
    char ipcPath[PATH_MAX];
    struct wl_list ipcClients;

    struct wl_listener newLock;
    struct wl_listener lockNewSurface;
    struct wl_listener lockUnlock;
    struct wl_listener lockDestroy;
    struct wl_listener newIdleInhibitor;
    struct wl_listener outputManagerApply;
    struct wl_listener outputManagerTest;
    struct wl_listener layoutChange;
    struct wl_listener gammaSetGamma;
    struct wl_listener newOutput;
    struct wl_listener newInput;
    struct wl_listener cursorMotion;
    struct wl_listener cursorMotionAbsolute;
    struct wl_listener cursorButton;
    struct wl_listener cursorAxis;
    struct wl_listener cursorFrame;
    struct wl_listener requestCursor;
    struct wl_listener requestSetSelection;
    struct wl_listener requestSetPrimarySelection;
    // Drag and drop between (and inside) applications.
    struct wl_listener requestStartDrag;
    struct wl_listener startDrag;
    struct wlr_scene_tree *dragIconTree;
    // Relative motion and pointer locking/confinement (games, Blender,
    // remote desktops; XWayland uses both for X11 games).
    struct wlr_relative_pointer_manager_v1 *relativePointers;
    struct wlr_pointer_constraints_v1 *pointerConstraints;
    struct wlr_pointer_constraint_v1 *activeConstraint;
    struct wl_listener newPointerConstraint;
    // cursor-shape-v1: applications name their cursor (GTK 4, Firefox).
    struct wl_listener cursorShape;
    // keyboard-shortcuts-inhibit: virtual machines and remote desktops get
    // every key while focused. Super+Escape gives the shortcuts back.
    struct wl_list shortcutsInhibitors; // struct ShortcutsInhibitor
    struct wl_listener newShortcutsInhibitor;
    // Touchscreens: touch points go to the surface under them.
    struct wl_list touchDevices; // struct TouchDevice
    // The finger that acts as the pointer: on a title bar or frame drawn by
    // the compositor (touchEmulated: its press was turned into a click), or
    // while it drags a window that asked to be moved. -1 for none.
    int32_t touchPointerId;
    bool touchEmulated;
    bool touchDragging;
    double touchX;
    double touchY;
    struct wl_listener touchDown;
    struct wl_listener touchUp;
    struct wl_listener touchMotion;
    struct wl_listener touchFrame;
    // Graphics tablets: the pen moves the pointer and its tip clicks.
    struct wl_listener tabletAxis;
    struct wl_listener tabletTip;
    struct wl_listener newXdgToplevel;
    struct wl_listener newXdgPopup;
    struct wl_listener newLayerSurface;
    struct wl_listener newXWaylandSurface;
    struct wl_listener xwaylandReadyListener;
    struct wl_listener newVirtualKeyboard;
    struct wl_listener newVirtualPointer;
    struct wl_listener activationRequest;
};

// ---------------------------------------------------------------------------
// Process and session startup
// ---------------------------------------------------------------------------

// The event loop blocks the signals it handles (signalfd); a child must not
// inherit that mask, or SIGTERM/SIGINT would never reach it.
static void resetChildSignals(void)
{
    sigset_t set;
    sigemptyset(&set);
    sigprocmask(SIG_SETMASK, &set, NULL);
}

static void spawnDetached(const char *program, const char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) {
        wlr_log_errno(WLR_ERROR, "fork failed for %s", program);
        return;
    }
    if (pid == 0) {
        // Fork again so the program is re-parented to init: the compositor
        // only reaps the shell, and this way no zombie is left behind.
        resetChildSignals();
        pid_t child = fork();
        if (child != 0)
            _exit(child < 0 ? 127 : 0);
        setsid();
        execvp(program, (char *const *)argv);
        wlr_log_errno(WLR_ERROR, "failed to start %s", program);
        _exit(127);
    }
    waitpid(pid, NULL, 0);
}

static void setChildEnvironment(struct Server *server)
{
    if (server->socketName[0] != '\0')
        setenv("WAYLAND_DISPLAY", server->socketName, 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("XDG_SESSION_DESKTOP", "newteller", 1);
    setenv("XDG_CURRENT_DESKTOP", "NewTeller", 1);
    setenv("DESKTOP_SESSION", "newteller", 1);
    if (server->ipcPath[0] != '\0')
        setenv("NEWTELLER_IPC", server->ipcPath, 1);
    if (server->config.cursorTheme[0] != '\0')
        setenv("XCURSOR_THEME", server->config.cursorTheme, 1);
    char cursorSize[16];
    snprintf(cursorSize, sizeof(cursorSize), "%d", server->config.cursorSize);
    setenv("XCURSOR_SIZE", cursorSize, 1);
    if (server->xwaylandReady && server->xwayland
        && server->xwayland->display_name) {
        setenv("DISPLAY", server->xwayland->display_name, 1);
    }
}

static void updateActivationEnvironment(struct Server *server)
{
    (void)server;
    // Make sure portals, D-Bus activated applications and systemd user units
    // inherit the compositor's socket. --systemd also updates the user manager,
    // which is where graphical-session.target lives on Debian.
    const char *argv[] = {
        "dbus-update-activation-environment",
        "--systemd",
        "WAYLAND_DISPLAY",
        "DISPLAY",
        "XDG_CURRENT_DESKTOP",
        "XDG_SESSION_DESKTOP",
        "XDG_SESSION_TYPE",
        "NEWTELLER_IPC",
        NULL,
    };
    spawnDetached(argv[0], argv);

    const char *argv2[] = {
        "systemctl", "--user", "import-environment",
        "WAYLAND_DISPLAY", "DISPLAY", "XDG_CURRENT_DESKTOP",
        "XDG_SESSION_DESKTOP", "XDG_SESSION_TYPE", NULL,
    };
    spawnDetached(argv2[0], argv2);
}

static int shellTimerCallback(void *data)
{
    struct Server *server = data;
    if (server->shellTimer)
        wl_event_source_remove(server->shellTimer);
    server->shellTimer = NULL;
    maybeStartShell(server);
    return 0;
}

static void maybeStartShell(struct Server *server)
{
    if (server->shellStarted || server->noShell)
        return;

    // Wait for XWayland to publish DISPLAY so the shell and every application
    // it launches inherit it. If XWayland never arrives, start the shell after
    // roughly two seconds anyway.
    if (!server->noXwayland && server->xwayland && !server->xwaylandReady
        && !server->xwaylandFailed && server->shellTries < 10) {
        ++server->shellTries;
        server->shellTimer = wl_event_loop_add_timer(
            server->eventLoop, shellTimerCallback, server);
        wl_event_source_timer_update(server->shellTimer, 200);
        return;
    }

    setChildEnvironment(server);
    updateActivationEnvironment(server);

    const char *program = server->shellPath[0] != '\0'
                              ? server->shellPath : server->shellProgram;
    pid_t pid = fork();
    if (pid < 0) {
        wlr_log_errno(WLR_ERROR, "failed to fork the NewTeller shell");
        return;
    }
    if (pid == 0) {
        resetChildSignals();
        setenv("NEWTELLER_SESSION", "1", 1);
        execlp(program, program, (char *)NULL);
        wlr_log_errno(WLR_ERROR, "failed to start %s", program);
        _exit(127);
    }

    server->shellPid = pid;
    server->shellStarted = true;
    wlr_log(WLR_INFO, "NewTeller shell started (pid %d)", (int)pid);
}

static int terminateHandler(int signal, void *data)
{
    (void)signal;
    struct Server *server = data;
    wlr_log(WLR_INFO, "session termination requested");
    server->running = false;
    wl_display_terminate(server->display);
    return 0;
}

static int sigchldHandler(int signal, void *data)
{
    (void)signal;
    struct Server *server = data;
    if (server->shellPid <= 0)
        return 0;

    int status = 0;
    pid_t pid = waitpid(server->shellPid, &status, WNOHANG);
    if (pid != server->shellPid)
        return 0;

    server->shellPid = 0;
    if (WIFEXITED(status))
        wlr_log(WLR_INFO, "NewTeller shell exited with status %d", WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        wlr_log(WLR_INFO, "NewTeller shell was killed by signal %d", WTERMSIG(status));

    // A crashed shell comes back instead of taking every open application
    // down with the session. A clean exit (status 0) still ends it, and so
    // does a shell that keeps crashing.
    bool crashed = WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0);
    if (crashed && server->running) {
        time_t now = time(NULL);
        if (now - server->shellRestartWindow > 60) {
            server->shellRestartWindow = now;
            server->shellRestarts = 0;
        }
        if (server->shellRestarts < 5) {
            ++server->shellRestarts;
            server->shellStarted = false;
            wlr_log(WLR_ERROR, "the NewTeller shell stopped unexpectedly; restarting it (%d/5)",
                    server->shellRestarts);
            server->shellTimer = wl_event_loop_add_timer(server->eventLoop,
                                                         shellTimerCallback, server);
            if (server->shellTimer)
                wl_event_source_timer_update(server->shellTimer, 1000);
            return 0;
        }
        wlr_log(WLR_ERROR, "the NewTeller shell keeps crashing; ending the session");
    }
    server->running = false;
    wl_display_terminate(server->display);
    return 0;
}

// ---------------------------------------------------------------------------
// Views (Wayland xdg-toplevel and XWayland windows)
// ---------------------------------------------------------------------------

static struct wlr_surface *viewSurface(struct View *view)
{
    if (view->xdg)
        return view->xdg->base->surface;
    if (view->x11)
        return view->x11->surface;
    return NULL;
}

static struct wlr_box viewGeometry(struct View *view)
{
    struct wlr_box box = {0, 0, 0, 0};
    if (view->xdg) {
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR >= 19
        box = view->xdg->base->geometry;
#else
        wlr_xdg_surface_get_geometry(view->xdg->base, &box);
#endif
    } else if (view->x11) {
        box.width = view->x11->width;
        box.height = view->x11->height;
    }
    return box;
}

static const char *viewTitle(struct View *view)
{
    if (view->xdg)
        return view->xdg->title ? view->xdg->title : "Window";
    if (view->x11)
        return view->x11->title ? view->x11->title : "X11 window";
    return "Window";
}

static const char *viewAppId(struct View *view)
{
    if (view->xdg)
        return view->xdg->app_id ? view->xdg->app_id : "";
    if (view->x11)
        return view->x11->class ? view->x11->class : "";
    return "";
}

// Override-redirect X11 windows (menus, tooltips) follow their parent and
// are never hidden by a workspace switch.
static bool viewOnWorkspace(struct View *view)
{
    return view->overrideRedirect || view->sticky || view->workspace == view->server->workspace;
}

static void viewSyncVisible(struct View *view)
{
    if (view->tree) {
        wlr_scene_node_set_enabled(&view->tree->node,
                                   view->mapped && !view->minimized
                                       && viewOnWorkspace(view));
    }
    // xdg-shell v6: a minimized window or one on another desktop is told it
    // is suspended, so browsers and GTK 4 applications stop drawing frames
    // nobody sees (battery, CPU). Its last frame stays for previews.
    // (Only clients that bound xdg_wm_base version 6 know the state.)
    if (view->xdg && view->xdg->base->initialized && view->mapped
        && wl_resource_get_version(view->xdg->resource) >= 6) {
        bool hidden = view->minimized || !viewOnWorkspace(view);
        if (hidden != view->suspended) {
            view->suspended = hidden;
            wlr_xdg_toplevel_set_suspended(view->xdg, hidden);
        }
    }
    // A video in a minimized window or on another desktop no longer keeps
    // the screen awake.
    updateIdleInhibited(view->server);
}

// ---------------------------------------------------------------------------
// Window decorations (server side)
// ---------------------------------------------------------------------------

// A cairo image surface as a wlroots buffer, for the scene graph.
struct CairoBuffer {
    struct wlr_buffer base;
    cairo_surface_t *surface;
};

static void cairoBufferDestroy(struct wlr_buffer *buffer)
{
    struct CairoBuffer *cairoBuffer = wl_container_of(buffer, cairoBuffer, base);
    cairo_surface_destroy(cairoBuffer->surface);
    free(cairoBuffer);
}

static bool cairoBufferBeginAccess(struct wlr_buffer *buffer, uint32_t flags,
                                   void **data, uint32_t *format, size_t *stride)
{
    struct CairoBuffer *cairoBuffer = wl_container_of(buffer, cairoBuffer, base);
    if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
        return false;
    *format = DRM_FORMAT_ARGB8888;
    *data = cairo_image_surface_get_data(cairoBuffer->surface);
    *stride = (size_t)cairo_image_surface_get_stride(cairoBuffer->surface);
    return true;
}

static void cairoBufferEndAccess(struct wlr_buffer *buffer)
{
    (void)buffer;
}

static const struct wlr_buffer_impl cairoBufferImpl = {
    .destroy = cairoBufferDestroy,
    .begin_data_ptr_access = cairoBufferBeginAccess,
    .end_data_ptr_access = cairoBufferEndAccess,
};

static struct CairoBuffer *cairoBufferCreate(cairo_surface_t *surface)
{
    struct CairoBuffer *buffer = xcalloc(1, sizeof(*buffer));
    wlr_buffer_init(&buffer->base, &cairoBufferImpl,
                    cairo_image_surface_get_width(surface),
                    cairo_image_surface_get_height(surface));
    buffer->surface = surface;
    return buffer;
}

// Every window that asked for a server-side frame gets one - the shell's own
// windows (Settings) too: Qt asks for one and then draws nothing itself, so
// leaving them out left them without a title bar, buttons or a way to move.
// The shell's popups are frameless and never ask.
static bool viewDecorated(struct View *view)
{
    return view->ssd && !view->fullscreen && !view->overrideRedirect && view->mapped;
}

// Frame thickness around the window content: title bar on top, border on
// the other sides (none while maximized).
static void viewFrame(struct View *view, int *top, int *side, int *bottom)
{
    *top = *side = *bottom = 0;
    if (!viewDecorated(view))
        return;
    const struct CompositorConfig *config = &view->server->config;
    *top = config->titleHeight;
    if (!view->maximized) {
        *side = config->borderWidth;
        *bottom = config->borderWidth;
    }
}

// The content box of a window whose frame should fill `outer`.
static struct wlr_box frameContentBox(struct View *view, struct wlr_box outer)
{
    int top, side, bottom;
    viewFrame(view, &top, &side, &bottom);
    outer.x += side;
    outer.y += top;
    outer.width -= 2 * side;
    outer.height -= top + bottom;
    return outer;
}

struct ButtonSlot {
    int part;
    int x;
};

// The title bar buttons, left to right, in title-bar coordinates.
static int decorationButtons(struct View *view, int width, struct ButtonSlot slots[3])
{
    const struct CompositorConfig *config = &view->server->config;
    int size = config->titleHeight;
    int parts[3];
    int count = 0;
    if (config->buttonsLeft) {
        parts[count++] = FRAME_CLOSE;
        if (config->showMinimize)
            parts[count++] = FRAME_MINIMIZE;
        if (config->showMaximize)
            parts[count++] = FRAME_MAXIMIZE;
        for (int i = 0; i < count; ++i)
            slots[i] = (struct ButtonSlot){parts[i], i * size};
    } else {
        if (config->showMinimize)
            parts[count++] = FRAME_MINIMIZE;
        if (config->showMaximize)
            parts[count++] = FRAME_MAXIMIZE;
        parts[count++] = FRAME_CLOSE;
        for (int i = 0; i < count; ++i)
            slots[i] = (struct ButtonSlot){parts[i], width - (count - i) * size};
    }
    return count;
}

static void setSourceColor(cairo_t *cr, const float color[4], double alpha)
{
    cairo_set_source_rgba(cr, color[0], color[1], color[2], color[3] * alpha);
}

static void renderDecorationTitle(struct View *view, int width, bool focused,
                                  float scale, const char *title)
{
    const struct CompositorConfig *config = &view->server->config;
    int height = config->titleHeight;
    int pixelWidth = (int)ceilf((float)width * scale);
    int pixelHeight = (int)ceilf((float)height * scale);
    if (pixelWidth <= 0 || pixelHeight <= 0)
        return;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                          pixelWidth, pixelHeight);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return;
    }
    cairo_t *cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);
    const float *background = focused ? config->activeBackground : config->inactiveBackground;
    const float *foreground = focused ? config->activeForeground : config->inactiveForeground;
    setSourceColor(cr, background, 1.0);
    cairo_paint(cr);

    struct ButtonSlot slots[3];
    int count = decorationButtons(view, width, slots);
    double glyph = height * 0.16;
    for (int i = 0; i < count; ++i) {
        double cx = slots[i].x + height / 2.0;
        double cy = height / 2.0;
        if (view->decoHover == slots[i].part) {
            if (slots[i].part == FRAME_CLOSE)
                setSourceColor(cr, config->closeHover, 1.0);
            else
                setSourceColor(cr, foreground, 0.16);
            cairo_rectangle(cr, slots[i].x, 0, height, height);
            cairo_fill(cr);
        }
        setSourceColor(cr, foreground, 1.0);
        if (view->decoHover == slots[i].part && slots[i].part == FRAME_CLOSE)
            cairo_set_source_rgba(cr, 1, 1, 1, 1);
        cairo_set_line_width(cr, 1.4);
        switch (slots[i].part) {
        case FRAME_CLOSE:
            cairo_move_to(cr, cx - glyph, cy - glyph);
            cairo_line_to(cr, cx + glyph, cy + glyph);
            cairo_move_to(cr, cx + glyph, cy - glyph);
            cairo_line_to(cr, cx - glyph, cy + glyph);
            break;
        case FRAME_MAXIMIZE:
            if (view->maximized) {
                cairo_rectangle(cr, cx - glyph, cy - glyph + 2, glyph * 2 - 2, glyph * 2 - 2);
                cairo_move_to(cr, cx - glyph + 2, cy - glyph);
                cairo_line_to(cr, cx + glyph, cy - glyph);
                cairo_line_to(cr, cx + glyph, cy + glyph - 2);
            } else {
                cairo_rectangle(cr, cx - glyph, cy - glyph, glyph * 2, glyph * 2);
            }
            break;
        case FRAME_MINIMIZE:
            cairo_move_to(cr, cx - glyph, cy);
            cairo_line_to(cr, cx + glyph, cy);
            break;
        default:
            break;
        }
        cairo_stroke(cr);
    }

    // The title, ellipsized between the button groups.
    int buttonsWidth = count * height;
    int left = config->buttonsLeft ? buttonsWidth + 8 : 12;
    int right = config->buttonsLeft ? 12 : buttonsWidth + 8;
    if (config->titleCentered) {
        int margin = left > right ? left : right;
        left = right = margin;
    }
    int available = width - left - right;
    if (available > 8 && title && title[0]) {
        PangoLayout *layout = pango_cairo_create_layout(cr);
        PangoFontDescription *font = pango_font_description_from_string(config->titleFont);
        pango_layout_set_font_description(layout, font);
        pango_layout_set_text(layout, title, -1);
        pango_layout_set_single_paragraph_mode(layout, TRUE);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_width(layout, available * PANGO_SCALE);
        pango_layout_set_alignment(layout, config->titleCentered ? PANGO_ALIGN_CENTER
                                                                 : PANGO_ALIGN_LEFT);
        int textWidth = 0;
        int textHeight = 0;
        pango_layout_get_pixel_size(layout, &textWidth, &textHeight);
        setSourceColor(cr, foreground, 1.0);
        cairo_move_to(cr, left, (height - textHeight) / 2.0);
        pango_cairo_show_layout(cr, layout);
        pango_font_description_free(font);
        g_object_unref(layout);
    }
    cairo_destroy(cr);
    cairo_surface_flush(surface);

    struct CairoBuffer *buffer = cairoBufferCreate(surface);
    wlr_scene_buffer_set_buffer(view->decoTitle, &buffer->base);
    wlr_scene_buffer_set_dest_size(view->decoTitle, width, height);
    wlr_buffer_drop(&buffer->base);
}

static float viewOutputScale(struct View *view)
{
    // The nearest screen when the centre is past an edge: at scale 1 the
    // title of a half-hidden window on a HiDPI screen was drawn blurred.
    struct wlr_output *output = viewNearestOutput(view);
    return output ? output->scale : 1.0f;
}

// Creates, updates or hides the frame. Cheap when nothing changed, so it is
// called from every place that might matter (commit, focus, title, state).
static void viewUpdateDecoration(struct View *view)
{
    if (!view->tree)
        return;
    if (!viewDecorated(view)) {
        if (view->decoTree)
            wlr_scene_node_set_enabled(&view->decoTree->node, false);
        return;
    }
    const struct CompositorConfig *config = &view->server->config;
    if (!view->decoTree) {
        view->decoTree = wlr_scene_tree_create(view->tree);
        if (!view->decoTree)
            return;
        wlr_scene_node_lower_to_bottom(&view->decoTree->node);
        view->decoTitle = wlr_scene_buffer_create(view->decoTree, NULL);
        for (int i = 0; i < 3; ++i)
            view->decoBorder[i] = wlr_scene_rect_create(view->decoTree, 0, 0,
                                                        config->activeBackground);
        view->decoWidth = -1;
    }
    wlr_scene_node_set_enabled(&view->decoTree->node, true);

    struct wlr_box geometry = viewGeometry(view);
    int contentWidth = geometry.width > 0 ? geometry.width : view->width;
    int contentHeight = geometry.height > 0 ? geometry.height : view->height;
    int top, side, bottom;
    viewFrame(view, &top, &side, &bottom);
    int outer = contentWidth + 2 * side;
    bool focused = view->server->focusedView == view;
    const float *frameColor = focused ? config->activeBackground : config->inactiveBackground;

    if (view->decoTitle)
        wlr_scene_node_set_position(&view->decoTitle->node, -side, -top);
    if (view->decoBorder[0]) {
        wlr_scene_rect_set_size(view->decoBorder[0], side, contentHeight);
        wlr_scene_node_set_position(&view->decoBorder[0]->node, -side, 0);
        wlr_scene_rect_set_size(view->decoBorder[1], side, contentHeight);
        wlr_scene_node_set_position(&view->decoBorder[1]->node, contentWidth, 0);
        wlr_scene_rect_set_size(view->decoBorder[2], outer, bottom);
        wlr_scene_node_set_position(&view->decoBorder[2]->node, -side, contentHeight);
        for (int i = 0; i < 3; ++i)
            wlr_scene_rect_set_color(view->decoBorder[i], frameColor);
    }

    char title[sizeof(view->decoTitleText)];
    snprintf(title, sizeof(title), "%s", viewTitle(view));
    float scale = viewOutputScale(view);
    if (outer != view->decoWidth || focused != view->decoFocused
        || scale != view->decoScale || view->decoHover != view->decoRenderedHover
        || view->maximized != view->decoMaximized || contentHeight != view->decoHeight
        || strcmp(title, view->decoTitleText) != 0) {
        view->decoWidth = outer;
        view->decoHeight = contentHeight;
        view->decoFocused = focused;
        view->decoScale = scale;
        view->decoRenderedHover = view->decoHover;
        view->decoMaximized = view->maximized;
        snprintf(view->decoTitleText, sizeof(view->decoTitleText), "%s", title);
        if (view->decoTitle)
            renderDecorationTitle(view, outer, focused, scale, title);
    }
}

static void updateExtToplevel(struct View *view)
{
    if (!view->extHandle)
        return;
    const struct wlr_ext_foreign_toplevel_handle_v1_state state = {
        .title = viewTitle(view),
        .app_id = viewAppId(view),
    };
    wlr_ext_foreign_toplevel_handle_v1_update_state(view->extHandle, &state);
}

static void updateForeignToplevel(struct View *view)
{
    viewUpdateDecoration(view);
    updateExtToplevel(view);
    if (!view->handle)
        return;
    wlr_foreign_toplevel_handle_v1_set_title(view->handle, viewTitle(view));
    wlr_foreign_toplevel_handle_v1_set_app_id(view->handle, viewAppId(view));
    wlr_foreign_toplevel_handle_v1_set_maximized(view->handle, view->maximized);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(view->handle, view->fullscreen);
    wlr_foreign_toplevel_handle_v1_set_minimized(view->handle, view->minimized);
    wlr_foreign_toplevel_handle_v1_set_activated(
        view->handle, view->server->focusedView == view);
}

static void viewSetPosition(struct View *view, int x, int y)
{
    view->x = x;
    view->y = y;
    if (view->tree)
        wlr_scene_node_set_position(&view->tree->node, x, y);
    // X11 clients place their menus from the position they believe they
    // have; tell them where the window went.
    if (view->x11 && !view->overrideRedirect && view->width > 0 && view->height > 0)
        wlr_xwayland_surface_configure(view->x11, (int16_t)x, (int16_t)y,
                                       (uint16_t)view->width, (uint16_t)view->height);
}

static void viewSetSize(struct View *view, int width, int height)
{
    if (view->xdg && !view->xdg->base->initialized) {
        wlr_log(WLR_DEBUG, "ignoring size before initial xdg configure: %s",
                viewTitle(view));
        return;
    }
    if (width < 64)
        width = 64;
    if (height < 64)
        height = 64;
    view->width = width;
    view->height = height;

    if (view->xdg) {
        wlr_xdg_toplevel_set_size(view->xdg, width, height);
    } else if (view->x11) {
        wlr_xwayland_surface_configure(view->x11, view->x, view->y,
                                       (uint16_t)width, (uint16_t)height);
        viewUpdateDecoration(view);
    }
}

static void viewSetActivated(struct View *view, bool activated)
{
    if (view->xdg && !view->xdg->base->initialized) {
        wlr_log(WLR_DEBUG, "ignoring activation before initial xdg configure: %s",
                viewTitle(view));
        return;
    }
    if (view->xdg)
        wlr_xdg_toplevel_set_activated(view->xdg, activated);
    else if (view->x11)
        wlr_xwayland_surface_activate(view->x11, activated);
    if (view->handle)
        wlr_foreign_toplevel_handle_v1_set_activated(view->handle, activated);
    viewUpdateDecoration(view);
}

// The window a dialog belongs to (xdg parent, X11 transient-for), if it is
// one of ours and mapped.
static struct View *viewParent(struct View *view)
{
    struct View *other;
    wl_list_for_each(other, &view->server->views, link) {
        if (other == view || !other->mapped)
            continue;
        if (view->xdg && view->xdg->parent && other->xdg == view->xdg->parent)
            return other;
        if (view->x11 && view->x11->parent && other->x11 == view->x11->parent)
            return other;
    }
    return NULL;
}

// Always on top itself, or a dialog of such a window (it must not open
// under its pinned parent).
static bool viewPinned(struct View *view)
{
    for (int depth = 0; view && depth < 8; ++depth) {
        if (view->alwaysOnTop)
            return true;
        view = viewParent(view);
    }
    return false;
}

// Raises a window and then its dialogs, so a dialog never ends up hidden
// behind the window it belongs to.
static void raiseWithChildren(struct View *view, int depth)
{
    if (!view->tree || depth > 8)
        return;
    wlr_scene_node_raise_to_top(&view->tree->node);
    struct View *child;
    wl_list_for_each_reverse(child, &view->server->views, link) {
        if (child != view && child->mapped && !child->overrideRedirect
            && viewParent(child) == view)
            raiseWithChildren(child, depth + 1);
    }
}

static void viewRaise(struct View *view)
{
    if (!view->tree)
        return;
    raiseWithChildren(view, 0);
    wl_list_remove(&view->link);
    wl_list_insert(&view->server->views, &view->link);
    raisePinnedViews(view->server);
}

// Always-on-top windows go back above the ordinary ones after anything was
// raised. X11 menus and tooltips (override-redirect) stay above them, or a
// pinned window would cover the menu of the application under it.
static void raisePinnedViews(struct Server *server)
{
    bool any = false;
    struct View *view;
    wl_list_for_each(view, &server->views, link) {
        if (view->alwaysOnTop && view->tree) {
            any = true;
            break;
        }
    }
    if (!any)
        return;
    // Least recently used first, so the most recent one ends on top.
    wl_list_for_each_reverse(view, &server->views, link) {
        if (view->tree && !view->overrideRedirect && viewPinned(view))
            wlr_scene_node_raise_to_top(&view->tree->node);
    }
    wl_list_for_each_reverse(view, &server->views, link) {
        if (view->overrideRedirect && view->tree)
            wlr_scene_node_raise_to_top(&view->tree->node);
    }
}

static void viewSetAlwaysOnTop(struct View *view, bool onTop)
{
    view->alwaysOnTop = onTop;
    if (onTop && view->tree)
        wlr_scene_node_raise_to_top(&view->tree->node);
    raisePinnedViews(view->server);
    scheduleAllFrames(view->server);
}

// Settings -> Windows -> Window rules: applications that always start on
// top or maximized. Applied once, when the window first appears.
static void applyWindowRules(struct View *view)
{
    if (view->rulesApplied || view->overrideRedirect)
        return;
    view->rulesApplied = true;
    const struct CompositorConfig *config = &view->server->config;
    const char *appId = viewAppId(view);
    if (ntConfigListContains(config->rulesAlwaysOnTop, appId))
        viewSetAlwaysOnTop(view, true);
    if (ntConfigListContains(config->rulesMaximized, appId) && !view->maximized
        && !view->fullscreen)
        viewSetMaximized(view, true);
}

static void focusView(struct View *view, struct wlr_surface *surface)
{
    // A hidden (unmapped) window, e.g. an application closed to the tray,
    // cannot take the focus; the dock starts the application instead.
    if (!view || !view->mapped || !viewSurface(view) || view->minimized)
        return;

    struct Server *server = view->server;
    // Nothing takes the keyboard from the screen locker.
    if (server->locked)
        return;
    // Activating a window on another desktop (from the dock, or a link that
    // raises the browser) goes to that desktop.
    if (!viewOnWorkspace(view))
        switchWorkspace(server, view->workspace);
    if (server->focusedLayer)
        server->focusedLayer = NULL;
    if (server->focusedView && server->focusedView != view) {
        struct View *previous = server->focusedView;
        server->focusedView = NULL;
        viewSetActivated(previous, false);
    }

    struct wlr_surface *target = surface ? surface : viewSurface(view);
    if (server->focusedView == view
        && wlr_seat_get_keyboard(server->seat)
        && server->seat->keyboard_state.focused_surface == target)
        return;

    server->focusedView = view;
    server->previousView = view;
    if (!server->suppressRaise)
        viewRaise(view);
    viewSetActivated(view, true);
    if (view->xdg && view->xdg->base->initialized)
        wlr_xdg_surface_ping(view->xdg->base);
    // keyboard.layoutPerWindow: the window gets its own layout back before
    // the enter event carries the keyboard state to it.
    if (server->config.layoutPerWindow && view->layoutIndex != server->layoutIndex)
        setLayout(server, view->layoutIndex);

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(server->seat, target,
                                       keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    } else {
        // No keyboard yet (it is plugged in later, or only a virtual one
        // will come): the window still has the focus when one arrives.
        wlr_seat_keyboard_notify_enter(server->seat, target, NULL, 0, NULL);
    }
    if (view->handle)
        wlr_foreign_toplevel_handle_v1_set_activated(view->handle, true);
}

static void focusLayer(struct Layer *layer)
{
    if (!layer || !layer->layer || !layer->layer->surface)
        return;
    struct Server *server = layer->server;
    if (server->locked)
        return;
    if (!server->focusedView && server->focusedLayer == layer)
        return;

    if (server->focusedView) {
        server->previousView = server->focusedView;
        struct View *previous = server->focusedView;
        server->focusedView = NULL;
        viewSetActivated(previous, false);
    }

    server->focusedLayer = layer;
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(server->seat, layer->layer->surface,
                                       keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    } else {
        wlr_seat_keyboard_notify_enter(server->seat, layer->layer->surface, NULL, 0, NULL);
    }
}

static void clearKeyboardFocus(struct Server *server)
{
    if (server->focusedView) {
        struct View *view = server->focusedView;
        server->focusedView = NULL;
        viewSetActivated(view, false);
    }
    server->focusedLayer = NULL;
    // The locker keeps the keyboard: a window closing or minimizing behind
    // the lock screen must not take it away from the password field.
    struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
    if (server->locked && focused && wlr_session_lock_surface_v1_try_from_wlr_surface(focused))
        return;
    wlr_seat_keyboard_notify_clear_focus(server->seat);
}

static void restoreFocus(struct Server *server)
{
    // Behind the lock screen nothing gets focus; unlocking restores it.
    if (server->locked)
        return;
    if (server->previousView && server->previousView->mapped
        && !server->previousView->minimized
        && !server->previousView->overrideRedirect
        && viewOnWorkspace(server->previousView)) {
        struct View *view = server->previousView;
        server->previousView = NULL;
        focusView(view, NULL);
        return;
    }
    // The most recently used window of the current desktop.
    struct View *view;
    wl_list_for_each(view, &server->views, link) {
        if (view->mapped && !view->minimized && !view->overrideRedirect
            && viewOnWorkspace(view)) {
            focusView(view, NULL);
            return;
        }
    }
    clearKeyboardFocus(server);
}

static void viewSetMinimized(struct View *view, bool minimized)
{
    if (view->minimized == minimized || view->overrideRedirect)
        return;
    if (minimized)
        createGhost(view, GHOST_MINIMIZE);
    else
        viewAnimate(view, 0.85f, 0.0f, 220);
    view->minimized = minimized;
    viewSyncVisible(view);
    if (minimized) {
        if (view->server->focusedView == view) {
            view->server->focusedView = NULL;
            viewSetActivated(view, false);
            restoreFocus(view->server);
        }
    } else {
        focusView(view, NULL);
    }
    updateForeignToplevel(view);
}

// The screen a window belongs to: the one under its centre, or the nearest
// one when the centre was dragged past an edge. NULL only without screens.
static struct wlr_output *viewNearestOutput(struct View *view)
{
    struct wlr_output_layout *layout = view->server->outputLayout;
    double x = view->x + view->width / 2.0;
    double y = view->y + view->height / 2.0;
    struct wlr_output *output = wlr_output_layout_output_at(layout, x, y);
    if (!output) {
        double nearX = x, nearY = y;
        wlr_output_layout_closest_point(layout, NULL, x, y, &nearX, &nearY);
        output = wlr_output_layout_output_at(layout, nearX, nearY);
    }
    return output;
}

static struct wlr_box usableBoxForView(struct View *view)
{
    struct Server *server = view->server;
    struct wlr_output *output = viewNearestOutput(view);
    if (!output) {
        output = wlr_output_layout_output_at(server->outputLayout,
                                             server->cursor->x,
                                             server->cursor->y);
    }
    struct Output *candidate;
    wl_list_for_each(candidate, &server->outputs, link) {
        if (!output || candidate->output == output)
            return candidate->usable;
    }
    struct wlr_box fallback = {0, 0, 1280, 800};
    return fallback;
}

static void viewSetMaximized(struct View *view, bool maximized);
static void viewSetFullscreen(struct View *view, bool fullscreen);

static void viewClearSnap(struct View *view)
{
    view->snapEdges = 0;
}

static void viewRestoreSnap(struct View *view)
{
    if (view->snapEdges == 0)
        return;
    viewSetPosition(view, view->snapSavedX, view->snapSavedY);
    viewSetSize(view, view->snapSavedWidth, view->snapSavedHeight);
    view->snapEdges = 0;
}

// The half or quarter of `area` a side (and a row) stands for.
static struct wlr_box snapBox(struct wlr_box area, uint32_t edge)
{
    const uint32_t side = edge & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
    const uint32_t vertical = edge & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM);
    const int half = area.width / 2;
    const int height = vertical ? area.height / 2 : area.height;
    return (struct wlr_box){side == WLR_EDGE_LEFT ? area.x : area.x + area.width - half,
                            vertical == WLR_EDGE_BOTTOM ? area.y + area.height - height : area.y,
                            half, height};
}

// The usable area of a screen changed (the panel grew, moved to another
// edge or went away; a new resolution or scale): windows that fill it, or
// a half or quarter of it, are fitted again. They used to keep the old
// rectangle, under the panel or with a gap beside it.
static void refitViews(struct Server *server, struct Output *output, struct wlr_box full,
                       struct wlr_box usable)
{
    const struct wlr_box oldFull = output->fitFull;
    const bool changed = !wlr_box_equal(&oldFull, &full)
                         || !wlr_box_equal(&output->fitUsable, &usable);
    output->fitFull = full;
    output->fitUsable = usable;
    if (!changed || wlr_box_empty(&oldFull) || wlr_box_empty(&usable))
        return;
    struct View *view;
    wl_list_for_each(view, &server->views, link) {
        if (!view->mapped || view->overrideRedirect
            || (!view->fullscreen && !view->maximized && view->snapEdges == 0))
            continue;
        // Windows of this screen: by where they were fitted before.
        if (!wlr_box_contains_point(&oldFull, view->x + view->width / 2.0,
                                    view->y + view->height / 2.0))
            continue;
        struct wlr_box box = full;
        if (!view->fullscreen)
            box = frameContentBox(view, view->maximized ? usable
                                                         : snapBox(usable, view->snapEdges));
        viewSetPosition(view, box.x, box.y);
        viewSetSize(view, box.width, box.height);
    }
}

// Snap the focused window to one half of the output's usable area. The
// direction is a wlr edge: WLR_EDGE_LEFT/RIGHT for the two halves, one of
// them with TOP or BOTTOM for a quarter, WLR_EDGE_TOP alone for maximise.
// WLR_EDGE_BOTTOM alone restores the saved geometry.
static void viewSnap(struct View *view, uint32_t edge)
{
    if (!view || view->overrideRedirect)
        return;
    wlr_log(WLR_DEBUG, "snap %s edge=%u at %d,%d %dx%d max=%d",
            viewTitle(view), edge, view->x, view->y, view->width, view->height,
            view->maximized);

    if (edge == WLR_EDGE_BOTTOM) {
        if (view->maximized) {
            viewSetMaximized(view, false);
            return;
        }
        viewRestoreSnap(view);
        return;
    }
    if (edge == WLR_EDGE_TOP) {
        viewSetMaximized(view, true);
        return;
    }
    // A half (left or right), or a quarter: a side plus top or bottom.
    const uint32_t side = edge & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
    const uint32_t vertical = edge & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM);
    if ((side != WLR_EDGE_LEFT && side != WLR_EDGE_RIGHT)
        || vertical == (WLR_EDGE_TOP | WLR_EDGE_BOTTOM))
        return;
    if (view->maximized)
        viewSetMaximized(view, false);
    if (view->fullscreen)
        viewSetFullscreen(view, false);

    if (view->snapEdges == 0) {
        view->snapSavedX = view->x;
        view->snapSavedY = view->y;
        view->snapSavedWidth = view->width;
        view->snapSavedHeight = view->height;
    }

    const struct wlr_box area = usableBoxForView(view);
    struct wlr_box content = frameContentBox(view, snapBox(area, edge));
    viewSetPosition(view, content.x, content.y);
    viewSetSize(view, content.width, content.height);
    view->snapEdges = edge;
    wlr_log(WLR_DEBUG, "snapped to %d,%d %dx%d (area %d,%d %dx%d)",
            view->x, view->y, view->width, view->height,
            area.x, area.y, area.width, area.height);
}

static struct wlr_output *outputForView(struct View *view)
{
    return wlr_output_layout_output_at(view->server->outputLayout,
                                       view->x + view->width / 2,
                                       view->y + view->height / 2);
}

static void moveViewToOutput(struct View *view, int direction)
{
    if (!view)
        return;
    struct wlr_output *current = outputForView(view);
    struct wlr_output *target = NULL;
    if (direction > 0) {
        struct Output *candidate;
        wl_list_for_each(candidate, &view->server->outputs, link) {
            if (!current || candidate->output == current) {
                struct wl_list *next = candidate->link.prev;
                if (next == &view->server->outputs)
                    next = view->server->outputs.prev;
                struct Output *nextOutput =
                    wl_container_of(next, nextOutput, link);
                target = nextOutput->output;
                break;
            }
        }
    } else {
        struct Output *candidate;
        wl_list_for_each(candidate, &view->server->outputs, link) {
            if (!current || candidate->output == current) {
                struct wl_list *prev = candidate->link.next;
                if (prev == &view->server->outputs)
                    prev = view->server->outputs.next;
                struct Output *prevOutput =
                    wl_container_of(prev, prevOutput, link);
                target = prevOutput->output;
                break;
            }
        }
    }
    if (!target || target == current)
        return;

    struct wlr_box area = {0, 0, 1280, 800};
    wlr_output_layout_get_box(view->server->outputLayout, target, &area);
    viewClearSnap(view);
    if (view->fullscreen) {
        // A fullscreen window fills the screen it moves to.
        viewSetPosition(view, area.x, area.y);
        viewSetSize(view, area.width, area.height);
        updateFullscreenLayers(view->server);
        return;
    }
    area = frameContentBox(view, area);
    int width = view->width > area.width ? area.width : view->width;
    int height = view->height > area.height ? area.height : view->height;
    viewSetSize(view, width, height);
    viewSetPosition(view, area.x + (area.width - width) / 2,
                    area.y + (area.height - height) / 2);
}

static void toggleShowDesktop(struct Server *server)
{
    // Minimizing and restoring change the focus, and the focus moves windows
    // within server->views: walk a copy. Least recently used first, so the
    // windows keep their order and the one that had the focus gets it back
    // (walking the live list restored them in reverse, with the focus on
    // the window used longest ago).
    int count = wl_list_length(&server->views);
    if (count == 0) {
        server->showDesktop = false;
        return;
    }
    struct View **views = xcalloc((size_t)count, sizeof(*views));
    int filled = 0;
    struct View *view;
    wl_list_for_each_reverse(view, &server->views, link)
        views[filled++] = view;

    if (!server->showDesktop) {
        server->showDesktop = true;
        for (int i = 0; i < filled; ++i) {
            view = views[i];
            if (view->mapped && !view->minimized && !view->overrideRedirect
                && viewOnWorkspace(view)) {
                view->hiddenByShowDesktop = true;
                viewSetMinimized(view, true);
            }
        }
    } else {
        server->showDesktop = false;
        for (int i = 0; i < filled; ++i) {
            view = views[i];
            if (view->hiddenByShowDesktop) {
                view->hiddenByShowDesktop = false;
                viewSetMinimized(view, false);
            }
        }
    }
    free(views);
}

// ---------------------------------------------------------------------------
// Workspaces (virtual desktops)
// ---------------------------------------------------------------------------

// The panel and menus (the top layer) hide behind a fullscreen window only
// while that window is on the desktop being shown, and only on the screen
// it fills: a film on the second monitor used to take the panel away from
// the first one as well. The overlay layer stays: it holds what must reach
// the user over a game or a video (the administrator prompt,
// Ctrl+Shift+Esc, Alt+Tab, the volume display).
static void updateFullscreenLayers(struct Server *server)
{
    if (server->locked)
        return;
    bool covered = server->fullscreenView && server->fullscreenView->mapped
                   && viewOnWorkspace(server->fullscreenView);
    struct wlr_output *filled = covered ? viewNearestOutput(server->fullscreenView) : NULL;
    wlr_scene_node_set_enabled(&server->topTree->node, true);
    wlr_scene_node_set_enabled(&server->overlayTree->node, true);
    // wlroots shows a layer surface's node while the surface is mapped;
    // on the filled screen the top layer is hidden on top of that.
    struct Layer *layer;
    wl_list_for_each(layer, &server->layers, link) {
        if (!layer->scene || !layer->layer || !layer->layer->surface)
            continue;
        bool hidden = filled && layer->output && layer->output->output == filled
                      && layer->layer->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP;
        wlr_scene_node_set_enabled(&layer->scene->tree->node,
                                   layer->layer->surface->mapped && !hidden);
    }
    broadcastFullscreen(server, false);
}

// "event fullscreen 1|0": a fullscreen window is (no longer) shown. The
// shell holds back notification popups and sounds meanwhile.
static void broadcastFullscreen(struct Server *server, bool force)
{
    bool shown = server->fullscreenView && server->fullscreenView->mapped
                 && viewOnWorkspace(server->fullscreenView);
    if (shown == server->fullscreenShown && !force)
        return;
    server->fullscreenShown = shown;
    if (ipcHasWatchers(server))
        ipcBroadcast(server, shown ? "event fullscreen 1" : "event fullscreen 0");
}

static void broadcastWorkspaces(struct Server *server)
{
    unsigned occupied = 0;
    struct View *view;
    wl_list_for_each(view, &server->views, link) {
        if (view->mapped && !view->overrideRedirect && view->workspace >= 0
            && view->workspace < 31)
            occupied |= 1u << view->workspace;
    }
    char line[64];
    snprintf(line, sizeof(line), "event workspace %d %d %u", server->workspace,
             server->config.workspaceCount, occupied);
    ipcBroadcast(server, line);
}

static int wrapWorkspace(struct Server *server, int index)
{
    int count = server->config.workspaceCount;
    if (server->config.workspaceWrap && count > 0)
        return ((index % count) + count) % count;
    return index;
}

static void switchWorkspace(struct Server *server, int index)
{
    int count = server->config.workspaceCount;
    index = wrapWorkspace(server, index);
    if (index < 0 || index >= count || index == server->workspace)
        return;
    switcherCancel(server);
    if (server->focusedView)
        clearKeyboardFocus(server);
    server->previousView = NULL;
    startWorkspaceSlide(server, server->workspace, index, index > server->workspace ? 1 : -1);
    server->workspace = index;
    struct View *view;
    wl_list_for_each(view, &server->views, link)
        viewSyncVisible(view);
    updateFullscreenLayers(server);
    if (!server->locked && !server->focusedLayer)
        restoreFocus(server);
    refreshPointerFocus(server);
    broadcastWorkspaces(server);
}

static void moveViewToWorkspace(struct View *view, int index, bool follow)
{
    struct Server *server = view->server;
    index = wrapWorkspace(server, index);
    if (index < 0 || index >= server->config.workspaceCount
        || view->overrideRedirect || index == view->workspace)
        return;
    bool wasFocused = server->focusedView == view;
    view->workspace = index;
    viewSyncVisible(view);
    if (follow) {
        switchWorkspace(server, index);
        focusView(view, NULL);
    } else if (wasFocused) {
        server->focusedView = NULL;
        viewSetActivated(view, false);
        restoreFocus(server);
    }
    updateFullscreenLayers(server);
    broadcastWorkspaces(server);
}

static void viewSetMaximized(struct View *view, bool maximized)
{
    if (view->xdg && !view->xdg->base->initialized)
        return;
    if (view->overrideRedirect || view->maximized == maximized)
        return;
    if (maximized)
        viewClearSnap(view);

    if (maximized) {
        view->savedX = view->x;
        view->savedY = view->y;
        view->savedWidth = view->width;
        view->savedHeight = view->height;
        // Maximized windows keep their title bar but lose the side borders;
        // set the flag first so the frame size matches.
        view->maximized = true;
        struct wlr_box box = frameContentBox(view, usableBoxForView(view));
        viewSetPosition(view, box.x, box.y);
        viewSetSize(view, box.width, box.height);
    } else {
        view->maximized = false;
        viewRestoreGeometry(view, view->savedX, view->savedY, view->savedWidth,
                            view->savedHeight);
    }

    view->maximized = maximized;
    if (view->xdg) {
        wlr_xdg_toplevel_set_maximized(view->xdg, maximized);
    } else if (view->x11) {
        // The X11 application learns its state too (_NET_WM_STATE): it
        // draws its own maximize button and decides by it what to ask next.
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR >= 19
        wlr_xwayland_surface_set_maximized(view->x11, maximized, maximized);
#else
        wlr_xwayland_surface_set_maximized(view->x11, maximized);
#endif
    }
    updateForeignToplevel(view);
}

static void viewSetFullscreen(struct View *view, bool fullscreen)
{
    if (view->xdg && !view->xdg->base->initialized)
        return;
    if (view->overrideRedirect || view->fullscreen == fullscreen)
        return;
    if (fullscreen)
        viewClearSnap(view);

    if (fullscreen) {
        view->fullSavedX = view->x;
        view->fullSavedY = view->y;
        view->fullSavedWidth = view->width;
        view->fullSavedHeight = view->height;
        // The screen most of the window is on, as for maximizing; its
        // corner may well be past the edge of every screen.
        struct wlr_output *output = viewNearestOutput(view);
        struct wlr_box box = {0, 0, 1280, 800};
        if (output)
            wlr_output_layout_get_box(view->server->outputLayout, output, &box);
        viewSetPosition(view, box.x, box.y);
        viewSetSize(view, box.width, box.height);
        viewRaise(view);
    } else {
        // The frame comes back with the window: decide the geometry with
        // fullscreen already off.
        view->fullscreen = false;
        if (view->maximized) {
            struct wlr_box box = frameContentBox(view, usableBoxForView(view));
            viewSetPosition(view, box.x, box.y);
            viewSetSize(view, box.width, box.height);
        } else {
            viewRestoreGeometry(view, view->fullSavedX, view->fullSavedY,
                                view->fullSavedWidth, view->fullSavedHeight);
        }
    }

    view->fullscreen = fullscreen;
    if (fullscreen) {
        if (view->server->fullscreenView
            && view->server->fullscreenView != view) {
            viewSetFullscreen(view->server->fullscreenView, false);
        }
        view->server->fullscreenView = view;
    } else if (view->server->fullscreenView == view) {
        view->server->fullscreenView = NULL;
    }
    // The panel and menus are layer-shell surfaces above normal windows:
    // they hide on the screen a fullscreen window fills, as a desktop
    // session normally does; Super+F (or F11) brings them back. Overlay
    // surfaces (prompts, Alt+Tab, Ctrl+Shift+Esc) still show.
    updateFullscreenLayers(view->server);
    if (view->xdg)
        wlr_xdg_toplevel_set_fullscreen(view->xdg, fullscreen);
    else if (view->x11)
        wlr_xwayland_surface_set_fullscreen(view->x11, fullscreen);
    updateForeignToplevel(view);
    broadcastFullscreen(view->server, false);
}

static void viewClose(struct View *view)
{
    if (view->xdg) {
        wlr_xdg_toplevel_send_close(view->xdg);
        // A client that does not answer the ping is hung; the shell then
        // offers to end it (see handleXdgPingTimeout).
        wlr_xdg_surface_ping(view->xdg->base);
    } else if (view->x11) {
        wlr_xwayland_surface_close(view->x11);
    }
}

static pid_t viewPid(struct View *view)
{
    pid_t pid = 0;
    if (view->xdg && view->xdg->base->resource) {
        wl_client_get_credentials(wl_resource_get_client(view->xdg->base->resource),
                                  &pid, NULL, NULL);
    } else if (view->x11) {
        pid = view->x11->pid;
    }
    return pid;
}

// A usable window geometry to come back to: an application that went
// fullscreen or maximized before it ever had a size has nothing saved, and
// restoring "nothing" left a 64 px square at 0,0 whose title bar was off
// screen. Such windows get two thirds of the screen, centred; everything is
// kept on screen with the title bar reachable.
static void viewRestoreGeometry(struct View *view, int x, int y, int width, int height)
{
    struct wlr_box area = usableBoxForView(view);
    int top, side, bottom;
    viewFrame(view, &top, &side, &bottom);
    if (width < 160 || height < 100) {
        width = area.width * 2 / 3;
        height = area.height * 2 / 3;
        x = area.x + (area.width - width) / 2;
        y = area.y + (area.height - height) / 2;
    }
    if (width > area.width - 2 * side)
        width = area.width - 2 * side;
    if (height > area.height - top - bottom)
        height = area.height - top - bottom;
    if (x < area.x + side - width + 120)
        x = area.x + side;
    if (x > area.x + area.width - 120)
        x = area.x + area.width - width - side;
    if (y < area.y + top)
        y = area.y + top;
    if (y > area.y + area.height - 60)
        y = area.y + area.height - height - bottom;
    viewSetPosition(view, x, y);
    viewSetSize(view, width, height);
}

static void centerView(struct View *view)
{
    if (view->positioned)
        return;
    view->positioned = true;

    struct wlr_box geometry = viewGeometry(view);
    if (geometry.width <= 0) {
        geometry.width = (view->xdg && view->xdg->current.width > 0)
                             ? view->xdg->current.width : 800;
    }
    if (geometry.height <= 0) {
        geometry.height = (view->xdg && view->xdg->current.height > 0)
                              ? view->xdg->current.height : 600;
    }
    view->width = geometry.width;
    view->height = geometry.height;

    if (view->x11 && (view->x11->x != 0 || view->x11->y != 0)
        && !view->overrideRedirect) {
        viewSetPosition(view, view->x11->x, view->x11->y);
        return;
    }

    // A dialog opens centred over its window (file choosers, "save
    // changes?"), kept inside that window's screen.
    struct View *parent = viewParent(view);
    if (parent && !parent->minimized) {
        struct wlr_box parentArea = usableBoxForView(parent);
        parentArea = frameContentBox(view, parentArea);
        int x = parent->x + (parent->width - view->width) / 2;
        int y = parent->y + (parent->height - view->height) / 2;
        if (x + view->width > parentArea.x + parentArea.width)
            x = parentArea.x + parentArea.width - view->width;
        if (y + view->height > parentArea.y + parentArea.height)
            y = parentArea.y + parentArea.height - view->height;
        viewSetPosition(view, x < parentArea.x ? parentArea.x : x,
                        y < parentArea.y ? parentArea.y : y);
        return;
    }

    struct wlr_output *output = wlr_output_layout_output_at(
        view->server->outputLayout, view->server->cursor->x,
        view->server->cursor->y);
    struct wlr_box area = {0, 0, 1280, 800};
    struct Output *candidate;
    wl_list_for_each(candidate, &view->server->outputs, link) {
        if (!output || candidate->output == output) {
            area = candidate->usable;
            break;
        }
    }

    // New windows: centred (GNOME), cascaded (Windows) or under the pointer.
    area = frameContentBox(view, area);
    const char *placement = view->server->config.placement;
    int offset = 0;
    if (strcmp(placement, "cascade") == 0)
        offset = (view->server->cascade++ % 8) * 28;
    int x = area.x + (area.width - view->width) / 2 + offset;
    int y = area.y + (area.height - view->height) / 2 + offset;
    if (strcmp(placement, "mouse") == 0) {
        x = (int)view->server->cursor->x - view->width / 2;
        y = (int)view->server->cursor->y - view->height / 2;
    }
    if (x + view->width > area.x + area.width)
        x = area.x + area.width - view->width;
    if (y + view->height > area.y + area.height)
        y = area.y + area.height - view->height;
    viewSetPosition(view, x < area.x ? area.x : x, y < area.y ? area.y : y);
}

// Forward declarations.
static void handleXdgMap(struct wl_listener *listener, void *data);
static void handleXdgUnmap(struct wl_listener *listener, void *data);
static void handleXdgDestroy(struct wl_listener *listener, void *data);
static void handleXdgRequestMove(struct wl_listener *listener, void *data);
static void handleXdgRequestResize(struct wl_listener *listener, void *data);
static void handleXdgRequestMaximize(struct wl_listener *listener, void *data);
static void handleXdgRequestFullscreen(struct wl_listener *listener, void *data);
static void handleXdgRequestMinimize(struct wl_listener *listener, void *data);
static void handleXdgSetTitle(struct wl_listener *listener, void *data);
static void handleXdgSetAppId(struct wl_listener *listener, void *data);
static void handleNewPopup(struct wl_listener *listener, void *data);

static void handleViewForeignActivate(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, foreignActivate);
    if (!view->mapped)
        return;
    if (view->minimized)
        viewSetMinimized(view, false);
    focusView(view, NULL);
}

static void handleViewForeignClose(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, foreignClose);
    viewClose(view);
}

static void handleViewForeignMinimize(struct wl_listener *listener, void *data)
{
    struct View *view = wl_container_of(listener, view, foreignMinimize);
    struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;
    viewSetMinimized(view, event->minimized);
}

static void handleViewForeignMaximize(struct wl_listener *listener, void *data)
{
    struct View *view = wl_container_of(listener, view, foreignMaximize);
    struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
    viewSetMaximized(view, event->maximized);
}

static void handleViewForeignFullscreen(struct wl_listener *listener, void *data)
{
    struct View *view = wl_container_of(listener, view, foreignFullscreen);
    struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
    viewSetFullscreen(view, event->fullscreen);
}

static void ensureForeignToplevel(struct View *view)
{
    if (!view->extHandle && !view->overrideRedirect && view->server->extToplevels) {
        const struct wlr_ext_foreign_toplevel_handle_v1_state state = {
            .title = viewTitle(view),
            .app_id = viewAppId(view),
        };
        view->extHandle = wlr_ext_foreign_toplevel_handle_v1_create(view->server->extToplevels,
                                                                    &state);
    }
    if (view->handle || view->overrideRedirect || !view->server->foreignToplevels)
        return;

    view->handle = wlr_foreign_toplevel_handle_v1_create(
        view->server->foreignToplevels);
    view->handle->data = view;
    view->foreignActivate.notify = handleViewForeignActivate;
    wl_signal_add(&view->handle->events.request_activate, &view->foreignActivate);
    view->foreignClose.notify = handleViewForeignClose;
    wl_signal_add(&view->handle->events.request_close, &view->foreignClose);
    view->foreignMinimize.notify = handleViewForeignMinimize;
    wl_signal_add(&view->handle->events.request_minimize, &view->foreignMinimize);
    view->foreignMaximize.notify = handleViewForeignMaximize;
    wl_signal_add(&view->handle->events.request_maximize, &view->foreignMaximize);
    view->foreignFullscreen.notify = handleViewForeignFullscreen;
    wl_signal_add(&view->handle->events.request_fullscreen, &view->foreignFullscreen);
    updateForeignToplevel(view);
}

// The window leaves the window lists (the dock, portals) when it is
// unmapped, not only when it is destroyed: applications that "close to
// the tray" hide their window and would otherwise stay on the dock.
static void destroyForeignToplevel(struct View *view)
{
    if (view->handle) {
        wl_list_remove(&view->foreignActivate.link);
        wl_list_remove(&view->foreignClose.link);
        wl_list_remove(&view->foreignMinimize.link);
        wl_list_remove(&view->foreignMaximize.link);
        wl_list_remove(&view->foreignFullscreen.link);
        wlr_foreign_toplevel_handle_v1_destroy(view->handle);
        view->handle = NULL;
    }
    if (view->extHandle) {
        wlr_ext_foreign_toplevel_handle_v1_destroy(view->extHandle);
        view->extHandle = NULL;
    }
}

static void destroyView(struct View *view)
{
    if (!view)
        return;

    struct Server *server = view->server;
    if (server->focusedView == view) {
        server->focusedView = NULL;
        wlr_seat_keyboard_notify_clear_focus(server->seat);
    }
    if (server->previousView == view)
        server->previousView = NULL;
    if (server->fullscreenView == view) {
        server->fullscreenView = NULL;
        updateFullscreenLayers(server);
        broadcastFullscreen(server, false);
    }
    if (server->grabbedView == view) {
        server->grabbedView = NULL;
        updateSnapPreview(server, false);
    }
    if (server->switchTarget == view)
        server->switchTarget = NULL;
    if (server->hoverView == view)
        server->hoverView = NULL;
    if (server->lastTitleView == view)
        server->lastTitleView = NULL;
    if (server->gestureView == view) {
        server->gestureView = NULL;
        server->gestureKind = 0;
    }
    if (view->thumbValid) {
        // Its preview goes with it (they used to pile up until logout).
        char path[PATH_MAX];
        thumbnailPath(view, path, sizeof(path));
        unlink(path);
    }
    if (view->hung) {
        // The force-quit dialog stops listing it.
        char line[48];
        snprintf(line, sizeof(line), "event closed %u", view->id);
        ipcBroadcast(server, line);
    }
    if (view->xdgDecoration) {
        wl_list_remove(&view->xdgDecorationMode.link);
        wl_list_remove(&view->xdgDecorationDestroy.link);
        view->xdgDecoration = NULL;
    }

    destroyForeignToplevel(view);

    if (view->tree)
        wlr_scene_node_destroy(&view->tree->node);

    wl_list_remove(&view->link);
    free(view);

    if (!server->focusedView && !server->focusedLayer)
        restoreFocus(server);
}

// ---------------------------------------------------------------------------
// Layer shell
// ---------------------------------------------------------------------------

static bool layerStateChanged(const struct wlr_layer_surface_v1_state *a,
                              const struct wlr_layer_surface_v1_state *b)
{
    return a->anchor != b->anchor
        || a->exclusive_zone != b->exclusive_zone
        || a->margin.top != b->margin.top || a->margin.right != b->margin.right
        || a->margin.bottom != b->margin.bottom || a->margin.left != b->margin.left
        || a->keyboard_interactive != b->keyboard_interactive
        || a->desired_width != b->desired_width
        || a->desired_height != b->desired_height
        || a->layer != b->layer
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR >= 19
        || a->exclusive_edge != b->exclusive_edge
#endif
        ;
}

static void arrangeOutput(struct Server *server, struct Output *output)
{
    if (!output || !output->output)
        return;

    struct wlr_box full = {0, 0, 1280, 800};
    wlr_output_layout_get_box(server->outputLayout, output->output, &full);
    struct wlr_box usable = full;

    for (int layerType = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
         layerType <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; ++layerType) {
        // Reserve edges first, then place menus and popups constrained by the
        // freshly reserved usable area.
        for (int pass = 0; pass < 2; ++pass) {
            struct Layer *layer;
            wl_list_for_each(layer, &server->layers, link) {
                if (layer->output != output || !layer->scene || !layer->layer)
                    continue;
                if (!layer->layer->initialized)
                    continue;
                if ((int)layer->layer->current.layer != layerType)
                    continue;
                bool reserves = layer->layer->current.exclusive_zone >= 0;
                if ((pass == 0) != reserves)
                    continue;
                wlr_log(WLR_DEBUG, "arrange %s layer %s on %s",
                        layer->layer->namespace ? layer->layer->namespace : "?",
                        layer->layer->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP ? "top" : "other",
                        output->output->name);
                wlr_scene_layer_surface_v1_configure(layer->scene, &full, &usable);
                layer->lastState = layer->layer->current;
                layer->hasLastState = true;
                layer->arrangePending = false;
            }
        }
    }
    output->usable = usable;
    refitViews(server, output, full, usable);
}

static void arrangeIdleCallback(void *data)
{
    struct Server *server = data;
    server->arrangeIdle = NULL;

    struct Output *output;
    wl_list_for_each(output, &server->outputs, link) {
        bool pending = false;
        struct Layer *layer;
        wl_list_for_each(layer, &server->layers, link) {
            if (layer->output == output && layer->arrangePending) {
                pending = true;
                break;
            }
        }
        if (pending)
            arrangeOutput(server, output);
    }
}

static void scheduleArrange(struct Server *server)
{
    if (!server->arrangeIdle) {
        server->arrangeIdle = wl_event_loop_add_idle(
            server->eventLoop, arrangeIdleCallback, server);
    }
}

static struct wlr_scene_tree *treeForLayer(struct Server *server,
                                           enum zwlr_layer_shell_v1_layer which)
{
    switch (which) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
        return server->backgroundTree;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
        return server->bottomTree;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
        return server->topTree;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
        return server->overlayTree;
    }
    return NULL;
}

static void layerHandleCommit(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Layer *layer = wl_container_of(listener, layer, commit);
    // set_layer moves a surface to another layer (an urgent notification
    // goes above a fullscreen window): its scene node follows, or it would
    // be drawn in the old layer while the pointer finds it in the new one.
    struct wlr_scene_tree *wanted = treeForLayer(layer->server, layer->layer->current.layer);
    if (layer->scene && wanted && layer->scene->tree->node.parent != wanted) {
        wlr_scene_node_reparent(&layer->scene->tree->node, wanted);
        layer->arrangePending = true;
        scheduleArrange(layer->server);
        updateFullscreenLayers(layer->server);
    }
    if (!layer->hasLastState
        || layerStateChanged(&layer->lastState, &layer->layer->current)) {
        layer->arrangePending = true;
        scheduleArrange(layer->server);
    }
}

static void layerHandleMap(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Layer *layer = wl_container_of(listener, layer, map);
    layer->arrangePending = true;
    scheduleArrange(layer->server);
    // A panel or menu that appears on a screen filled by a fullscreen
    // window stays behind it.
    updateFullscreenLayers(layer->server);
    wlr_log(WLR_DEBUG, "layer map %s keyboard=%d",
            layer->layer->namespace ? layer->layer->namespace : "(none)",
            (int)layer->layer->current.keyboard_interactive);

    if (layer->layer
        && layer->layer->current.keyboard_interactive
               == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
        focusLayer(layer);
    }
}

static void layerHandleUnmap(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Layer *layer = wl_container_of(listener, layer, unmap);
    layer->arrangePending = true;
    scheduleArrange(layer->server);
    if (layer->server->focusedLayer == layer) {
        layer->server->focusedLayer = NULL;
        restoreFocus(layer->server);
    }
}

static void layerHandleNewPopup(struct wl_listener *listener, void *data);

static void layerHandleDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Layer *layer = wl_container_of(listener, layer, destroy);
    struct Server *server = layer->server;
    if (server->focusedLayer == layer) {
        server->focusedLayer = NULL;
        restoreFocus(server);
    }
    wl_list_remove(&layer->commit.link);
    wl_list_remove(&layer->map.link);
    wl_list_remove(&layer->unmap.link);
    wl_list_remove(&layer->destroy.link);
    wl_list_remove(&layer->newPopup.link);
    wl_list_remove(&layer->link);
    scheduleArrange(server);
    free(layer);
}

static void layerHandleNewPopup(struct wl_listener *listener, void *data)
{
    (void)listener;
    handleNewPopup(NULL, data);
}

static void handleNewLayerSurface(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newLayerSurface);
    struct wlr_layer_surface_v1 *layerSurface = data;

    struct Output *output = NULL;
    if (layerSurface->output) {
        struct Output *candidate;
        wl_list_for_each(candidate, &server->outputs, link) {
            if (candidate->output == layerSurface->output) {
                output = candidate;
                break;
            }
        }
    }
    if (!output && !wl_list_empty(&server->outputs))
        output = wl_container_of(server->outputs.next, output, link);

    if (!output) {
        wlr_log(WLR_ERROR, "layer surface has no output; closing it");
        wlr_layer_surface_v1_destroy(layerSurface);
        return;
    }
    layerSurface->output = output->output;

    struct Layer *layer = xcalloc(1, sizeof(*layer));
    layer->server = server;
    layer->layer = layerSurface;
    layer->output = output;

    struct wlr_scene_tree *parent = treeForLayer(server, layerSurface->current.layer);
    layer->scene = wlr_scene_layer_surface_v1_create(parent, layerSurface);
    if (!layer->scene) {
        free(layer);
        wlr_layer_surface_v1_destroy(layerSurface);
        return;
    }
    layerSurface->surface->data = layer->scene->tree;

    layer->commit.notify = layerHandleCommit;
    wl_signal_add(&layerSurface->surface->events.commit, &layer->commit);
    layer->map.notify = layerHandleMap;
    wl_signal_add(&layerSurface->surface->events.map, &layer->map);
    layer->unmap.notify = layerHandleUnmap;
    wl_signal_add(&layerSurface->surface->events.unmap, &layer->unmap);
    layer->destroy.notify = layerHandleDestroy;
    wl_signal_add(&layerSurface->events.destroy, &layer->destroy);
    layer->newPopup.notify = layerHandleNewPopup;
    wl_signal_add(&layerSurface->events.new_popup, &layer->newPopup);

    wlr_log(WLR_DEBUG, "layer surface %s on %s",
            layerSurface->namespace ? layerSurface->namespace : "(unnamed)",
            output->output->name);
    wl_list_insert(&server->layers, &layer->link);
    layer->arrangePending = true;
    scheduleArrange(server);
    struct Output *first;
    wl_list_for_each(first, &server->outputs, link) {
        if (first->sceneOutput)
            wlr_scene_output_commit(first->sceneOutput, NULL);
        break;
    }
}

// ---------------------------------------------------------------------------
// Hit testing and pointer focus
// ---------------------------------------------------------------------------

struct Hit {
    struct wlr_surface *surface;
    double sx;
    double sy;
    struct View *view;
    struct Layer *layer;
    // Set when the pointer is over a compositor-drawn frame instead of a
    // surface: which part, and for FRAME_EDGE the edges.
    int frame;
    uint32_t frameEdges;
};

// Which part of the frame is at layout point (lx, ly); false when the point
// is outside it. A few pixels around the frame also grab its edges.
static bool viewFrameAt(struct View *view, double lx, double ly, struct Hit *hit)
{
    int top, side, bottom;
    viewFrame(view, &top, &side, &bottom);
    if (top == 0 || !view->tree)
        return false;
    const int grace = 4;
    double rx = lx - view->tree->node.x;
    double ry = ly - view->tree->node.y;
    struct wlr_box geometry = viewGeometry(view);
    int width = geometry.width > 0 ? geometry.width : view->width;
    int height = geometry.height > 0 ? geometry.height : view->height;
    int resizeGrace = view->maximized ? 0 : grace;
    if (rx < -side - resizeGrace || rx >= width + side + resizeGrace
        || ry < -top - resizeGrace || ry >= height + bottom + resizeGrace)
        return false;
    if (rx >= 0 && rx < width && ry >= 0 && ry < height)
        return false;  // the content area, but not the surface's input region

    hit->view = view;
    hit->surface = NULL;
    uint32_t edges = 0;
    if (!view->maximized) {
        if (rx < 0)
            edges |= WLR_EDGE_LEFT;
        else if (rx >= width)
            edges |= WLR_EDGE_RIGHT;
        if (ry < -top + 3)
            edges |= WLR_EDGE_TOP;
        else if (ry >= height)
            edges |= WLR_EDGE_BOTTOM;
        // A corner is the edge band plus a stretch along the neighbour.
        if ((edges == WLR_EDGE_TOP || edges == WLR_EDGE_BOTTOM) && rx < 12)
            edges |= WLR_EDGE_LEFT;
        if ((edges == WLR_EDGE_TOP || edges == WLR_EDGE_BOTTOM) && rx >= width - 12)
            edges |= WLR_EDGE_RIGHT;
    }
    if (edges) {
        hit->frame = FRAME_EDGE;
        hit->frameEdges = edges;
        return true;
    }
    hit->frame = FRAME_TITLE;
    if (ry < 0) {
        struct ButtonSlot slots[3];
        int count = decorationButtons(view, width + 2 * side, slots);
        double bx = rx + side;
        for (int i = 0; i < count; ++i) {
            if (bx >= slots[i].x && bx < slots[i].x + top) {
                hit->frame = slots[i].part;
                break;
            }
        }
    }
    return true;
}

static bool layerAt(struct Layer *layer, double lx, double ly, struct Hit *hit)
{
    if (!layer->scene || !layer->layer || !layer->layer->surface->mapped
        || !layer->scene->tree->node.enabled)
        return false;
    int nodeX = layer->scene->tree->node.x;
    int nodeY = layer->scene->tree->node.y;
    double sx = 0;
    double sy = 0;
    struct wlr_surface *surface = wlr_layer_surface_v1_popup_surface_at(
        layer->layer, lx - nodeX, ly - nodeY, &sx, &sy);
    if (!surface) {
        surface = wlr_layer_surface_v1_surface_at(
            layer->layer, lx - nodeX, ly - nodeY, &sx, &sy);
    }
    if (!surface)
        return false;
    hit->surface = surface;
    hit->sx = sx;
    hit->sy = sy;
    hit->layer = layer;
    return true;
}

static bool viewAt(struct View *view, double lx, double ly, struct Hit *hit)
{
    if (!view->mapped || view->minimized || !view->tree || !viewOnWorkspace(view))
        return false;
    int nodeX = view->tree->node.x;
    int nodeY = view->tree->node.y;
    double sx = 0;
    double sy = 0;
    struct wlr_surface *surface = NULL;
    if (view->xdg) {
        // The tree sits at the window geometry; the surface (with any
        // client-side shadow around it) starts geometry.x/y further up-left.
        // Without this offset every click on a GTK4/libadwaita window landed
        // 20-50 px off: close buttons and header bars did not react.
        struct wlr_box geometry = viewGeometry(view);
        surface = wlr_xdg_surface_surface_at(view->xdg->base, lx - nodeX + geometry.x,
                                             ly - nodeY + geometry.y, &sx, &sy);
    } else if (view->x11 && view->x11->surface) {
        surface = wlr_surface_surface_at(view->x11->surface, lx - nodeX,
                                         ly - nodeY, &sx, &sy);
    }
    if (!surface)
        return viewFrameAt(view, lx, ly, hit);
    hit->surface = surface;
    hit->sx = sx;
    hit->sy = sy;
    hit->view = view;
    return true;
}

static struct Hit hitTest(struct Server *server, double lx, double ly)
{
    struct Hit hit = {0};

    if (server->locked) {
        struct LockSurface *lock;
        wl_list_for_each(lock, &server->lockSurfaces, link) {
            struct wlr_surface *surface = lock->lockSurface->surface;
            if (!surface || !surface->mapped || !lock->tree)
                continue;
            double sx = 0;
            double sy = 0;
            struct wlr_surface *found = wlr_surface_surface_at(
                surface, lx - lock->tree->node.x, ly - lock->tree->node.y,
                &sx, &sy);
            if (found) {
                hit.surface = found;
                hit.sx = sx;
                hit.sy = sy;
                return hit;
            }
        }
        return hit;
    }

    const int topLayers[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
    };
    // Only what is drawn takes the pointer: behind a fullscreen window the
    // panel (top layer) is hidden and must not catch clicks meant for the
    // game or the video.
    const struct wlr_scene_tree *topTrees[] = {server->overlayTree, server->topTree};
    for (size_t i = 0; i < sizeof(topLayers) / sizeof(topLayers[0]); ++i) {
        if (!topTrees[i]->node.enabled)
            continue;
        struct Layer *layer;
        wl_list_for_each(layer, &server->layers, link) {
            if ((int)layer->layer->current.layer != topLayers[i])
                continue;
            if (layerAt(layer, lx, ly, &hit))
                return hit;
        }
    }

    // Top-most first, in drawing order: an always-on-top window or the
    // Alt+Tab preview is above windows used more recently.
    struct wlr_scene_node *child;
    wl_list_for_each_reverse(child, &server->toplevelTree->children, link) {
        struct View *view = child->data;
        if (!view || !child->enabled)
            continue;
        if (viewAt(view, lx, ly, &hit))
            return hit;
    }

    const int bottomLayers[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
    };
    const struct wlr_scene_tree *bottomTrees[] = {server->bottomTree, server->backgroundTree};
    for (size_t i = 0; i < sizeof(bottomLayers) / sizeof(bottomLayers[0]); ++i) {
        if (!bottomTrees[i]->node.enabled)
            continue;
        struct Layer *layer;
        wl_list_for_each(layer, &server->layers, link) {
            if ((int)layer->layer->current.layer != bottomLayers[i])
                continue;
            if (layerAt(layer, lx, ly, &hit))
                return hit;
        }
    }
    return hit;
}

// ---------------------------------------------------------------------------
// Pointer constraints (locked / confined pointer)
// ---------------------------------------------------------------------------

struct PointerConstraint {
    struct Server *server;
    struct wlr_pointer_constraint_v1 *constraint;
    struct wl_listener destroy;
};

// A locked pointer reappears where the application hinted (the crosshair).
static void constraintWarpToHint(struct Server *server, struct wlr_pointer_constraint_v1 *constraint)
{
    if (constraint->type != WLR_POINTER_CONSTRAINT_V1_LOCKED
        || !(constraint->current.committed & WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT))
        return;
    int x = 0;
    int y = 0;
    if (!surfaceLayoutPosition(server, constraint->surface, &x, &y))
        return;
    const double sx = constraint->current.cursor_hint.x;
    const double sy = constraint->current.cursor_hint.y;
    wlr_cursor_warp(server->cursor, NULL, x + sx, y + sy);
    wlr_seat_pointer_warp(server->seat, sx, sy);
}

static void setActiveConstraint(struct Server *server, struct wlr_pointer_constraint_v1 *constraint)
{
    if (server->activeConstraint == constraint)
        return;
    if (server->activeConstraint) {
        constraintWarpToHint(server, server->activeConstraint);
        wlr_pointer_constraint_v1_send_deactivated(server->activeConstraint);
    }
    server->activeConstraint = constraint;
    if (constraint)
        wlr_pointer_constraint_v1_send_activated(constraint);
}

// The constraint of the surface under the pointer is the active one, while
// its window also has the keyboard; none while the session is locked. A
// game that locked the pointer used to keep it after Alt+Tab: the pointer
// could not move at all until the game was closed.
static void updateActiveConstraint(struct Server *server)
{
    struct wlr_surface *surface = server->seat->pointer_state.focused_surface;
    struct wlr_surface *keys = server->seat->keyboard_state.focused_surface;
    struct wlr_pointer_constraint_v1 *constraint = NULL;
    if (surface && keys && !server->locked && server->pointerConstraints
        && wlr_surface_get_root_surface(surface) == wlr_surface_get_root_surface(keys))
        constraint = wlr_pointer_constraints_v1_constraint_for_surface(server->pointerConstraints,
                                                                       surface, server->seat);
    setActiveConstraint(server, constraint);
}

static void pointerConstraintDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct PointerConstraint *entry = wl_container_of(listener, entry, destroy);
    if (entry->server->activeConstraint == entry->constraint)
        entry->server->activeConstraint = NULL;
    wl_list_remove(&entry->destroy.link);
    free(entry);
}

static void handleNewPointerConstraint(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newPointerConstraint);
    struct wlr_pointer_constraint_v1 *constraint = data;
    struct PointerConstraint *entry = xcalloc(1, sizeof(*entry));
    entry->server = server;
    entry->constraint = constraint;
    entry->destroy.notify = pointerConstraintDestroy;
    wl_signal_add(&constraint->events.destroy, &entry->destroy);
    if (constraint->surface == server->seat->pointer_state.focused_surface)
        updateActiveConstraint(server);
}

static void pointerFocus(struct Server *server, const struct Hit *hit)
{
    if (!hit->surface) {
        wlr_seat_pointer_clear_focus(server->seat);
        updateActiveConstraint(server);
        return;
    }
    const char *role = "?";
    if (hit->layer && hit->layer->layer->namespace)
        role = hit->layer->layer->namespace;
    else if (hit->view)
        role = viewTitle(hit->view);
    wlr_log(WLR_DEBUG, "pointer enter %s at %.0f,%.0f", role,
            hit->sx, hit->sy);
    wlr_seat_pointer_notify_enter(server->seat, hit->surface, hit->sx, hit->sy);
    updateActiveConstraint(server);
}

// Re-evaluates which surface is under the pointer after the scene changed
// under it (workspace switch), without waiting for the next motion.
static void refreshPointerFocus(struct Server *server)
{
    struct Hit hit = hitTest(server, server->cursor->x, server->cursor->y);
    pointerFocus(server, &hit);
    wlr_seat_pointer_notify_frame(server->seat);
}

// ---------------------------------------------------------------------------
// Pointer interaction
// ---------------------------------------------------------------------------

// The smallest and largest size the application accepts (0 = no maximum).
static void viewSizeLimits(struct View *view, int *minWidth, int *minHeight,
                           int *maxWidth, int *maxHeight)
{
    *minWidth = 64;
    *minHeight = 64;
    *maxWidth = 0;
    *maxHeight = 0;
    int wantMinWidth = 0, wantMinHeight = 0;
    if (view->xdg) {
        wantMinWidth = view->xdg->current.min_width;
        wantMinHeight = view->xdg->current.min_height;
        *maxWidth = view->xdg->current.max_width;
        *maxHeight = view->xdg->current.max_height;
    } else if (view->x11 && view->x11->size_hints) {
        const xcb_size_hints_t *hints = view->x11->size_hints;
        if (hints->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
            wantMinWidth = hints->min_width;
            wantMinHeight = hints->min_height;
        }
        if (hints->flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) {
            *maxWidth = hints->max_width;
            *maxHeight = hints->max_height;
        }
    }
    if (wantMinWidth > *minWidth)
        *minWidth = wantMinWidth;
    if (wantMinHeight > *minHeight)
        *minHeight = wantMinHeight;
    // A maximum below the minimum is a client mistake: the minimum wins.
    if (*maxWidth < *minWidth)
        *maxWidth = *maxWidth > 0 ? *minWidth : 0;
    if (*maxHeight < *minHeight)
        *maxHeight = *maxHeight > 0 ? *minHeight : 0;
}

static void processCursorMotion(struct Server *server, uint32_t time)
{
    // A dragged icon follows the pointer.
    if (server->seat->drag) {
        wlr_scene_node_set_position(&server->dragIconTree->node, (int)server->cursor->x,
                                    (int)server->cursor->y);
        scheduleAllFrames(server);
    }
    if (server->cursorMode == CURSOR_MOVE && server->grabbedView) {
        struct View *view = server->grabbedView;
        viewSetPosition(view,
                        (int)(server->cursor->x - server->grabX),
                        (int)(server->cursor->y - server->grabY));
        updateSnapPreview(server, true);
        return;
    }
    if (server->cursorMode == CURSOR_RESIZE && server->grabbedView) {
        struct View *view = server->grabbedView;
        int left = (int)server->grabX;
        int top = (int)server->grabY;
        int right = left + server->grabWidth;
        int bottom = top + server->grabHeight;

        int pointerX = (int)(server->cursor->x - server->resizeOffsetX);
        int pointerY = (int)(server->cursor->y - server->resizeOffsetY);
        if (server->resizeEdges & WLR_EDGE_LEFT)
            left = pointerX;
        if (server->resizeEdges & WLR_EDGE_RIGHT)
            right = pointerX;
        if (server->resizeEdges & WLR_EDGE_TOP)
            top = pointerY;
        if (server->resizeEdges & WLR_EDGE_BOTTOM)
            bottom = pointerY;

        // Stop at the sizes the application accepts. Asking for less than
        // its minimum from the left or the top made the whole window slide:
        // the position followed the pointer while the size stayed.
        int minWidth, minHeight, maxWidth, maxHeight;
        viewSizeLimits(view, &minWidth, &minHeight, &maxWidth, &maxHeight);
        int width = right - left;
        int height = bottom - top;
        if (width < minWidth)
            width = minWidth;
        if (maxWidth > 0 && width > maxWidth)
            width = maxWidth;
        if (height < minHeight)
            height = minHeight;
        if (maxHeight > 0 && height > maxHeight)
            height = maxHeight;
        if (server->resizeEdges & WLR_EDGE_LEFT)
            left = right - width;
        if (server->resizeEdges & WLR_EDGE_TOP)
            top = bottom - height;

        viewSetPosition(view, left, top);
        viewSetSize(view, width, height);
        return;
    }

    struct Hit hit = hitTest(server, server->cursor->x, server->cursor->y);
    struct wlr_surface *focused = server->seat->pointer_state.focused_surface;
    if (server->locked && hit.surface) {
        struct wlr_surface *keys = server->seat->keyboard_state.focused_surface;
        if (!keys || !wlr_session_lock_surface_v1_try_from_wlr_surface(keys))
            lockFocusSurface(server, wlr_surface_get_root_surface(hit.surface));
    }
    pointerFocus(server, &hit);
    if (hit.surface && focused == hit.surface)
        wlr_seat_pointer_notify_motion(server->seat, time, hit.sx, hit.sy);
    wlr_seat_pointer_notify_frame(server->seat);

    // Over a compositor-drawn frame (or nothing at all) the compositor owns
    // the cursor image; over a surface the client sets it.
    if (!hit.surface) {
        const char *image = "left_ptr";
        if (hit.view && hit.frame == FRAME_EDGE) {
            uint32_t e = hit.frameEdges;
            image = e == (WLR_EDGE_TOP | WLR_EDGE_LEFT) ? "top_left_corner"
                  : e == (WLR_EDGE_TOP | WLR_EDGE_RIGHT) ? "top_right_corner"
                  : e == (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT) ? "bottom_left_corner"
                  : e == (WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT) ? "bottom_right_corner"
                  : (e & WLR_EDGE_LEFT) ? "left_side"
                  : (e & WLR_EDGE_RIGHT) ? "right_side"
                  : (e & WLR_EDGE_TOP) ? "top_side" : "bottom_side";
        }
        if (server->cursorImage != image) {
            wlr_cursor_set_xcursor(server->cursor, server->cursorManager, image);
            server->cursorImage = image;
        }
    } else {
        server->cursorImage = NULL;
    }

    // Title bar buttons light up under the pointer.
    struct View *hoverView = NULL;
    int hoverPart = FRAME_NONE;
    if (hit.view && (hit.frame == FRAME_CLOSE || hit.frame == FRAME_MAXIMIZE
                     || hit.frame == FRAME_MINIMIZE)) {
        hoverView = hit.view;
        hoverPart = hit.frame;
    }
    if (server->hoverView && (server->hoverView != hoverView
                              || server->hoverView->decoHover != hoverPart)) {
        server->hoverView->decoHover = FRAME_NONE;
        viewUpdateDecoration(server->hoverView);
        server->hoverView = NULL;
    }
    if (hoverView && hoverView->decoHover != hoverPart) {
        hoverView->decoHover = hoverPart;
        viewUpdateDecoration(hoverView);
        server->hoverView = hoverView;
    }

    checkHotCorner(server);

    // Focus follows the mouse (without raising), when chosen.
    if (server->config.focusFollowsMouse && hit.view && !hit.view->overrideRedirect
        && hit.view != server->focusedView && !server->focusedLayer && !server->locked) {
        server->suppressRaise = true;
        focusView(hit.view, NULL);
        server->suppressRaise = false;
    }
}

// Where a window dropped at the pointer would snap: a side (half), a side
// and a row (quarter, near a corner) or the top (maximise); 0 for none.
static uint32_t dragSnapEdges(struct Server *server, struct View *view)
{
    const struct wlr_box area = usableBoxForView(view);
    // Corners (the outer 10% of a side) give quarters.
    const int cornerReach = area.height / 10 > 24 ? area.height / 10 : 24;
    uint32_t row = 0;
    if (server->cursor->y <= area.y + cornerReach)
        row = WLR_EDGE_TOP;
    else if (server->cursor->y >= area.y + area.height - cornerReach)
        row = WLR_EDGE_BOTTOM;
    if (server->cursor->x <= area.x + 12)
        return WLR_EDGE_LEFT | row;
    if (server->cursor->x >= area.x + area.width - 12)
        return WLR_EDGE_RIGHT | row;
    if (server->cursor->y <= area.y + 10)
        return WLR_EDGE_TOP;
    return 0;
}

// Shows where the dragged window will snap (while `dragging`), or hides it.
static void updateSnapPreview(struct Server *server, bool dragging)
{
    struct View *view = server->grabbedView;
    uint32_t edges = dragging && view && server->config.snapping && !server->locked
                         ? dragSnapEdges(server, view) : 0;
    if (!edges) {
        if (server->snapTree->node.enabled) {
            wlr_scene_node_set_enabled(&server->snapTree->node, false);
            scheduleAllFrames(server);
        }
        return;
    }
    const struct wlr_box area = usableBoxForView(view);
    struct wlr_box box = area;
    const uint32_t side = edges & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
    const uint32_t row = edges & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM);
    if (side) {
        box.width = area.width / 2;
        if (side == WLR_EDGE_RIGHT)
            box.x = area.x + area.width - box.width;
        if (row) {
            box.height = area.height / 2;
            if (row == WLR_EDGE_BOTTOM)
                box.y = area.y + area.height - box.height;
        }
    }
    // A small inset, so the preview reads as a target, not a window.
    const int inset = 6;
    wlr_scene_node_set_position(&server->snapTree->node, box.x + inset, box.y + inset);
    wlr_scene_rect_set_size(server->snapRect, box.width - 2 * inset > 1 ? box.width - 2 * inset : 1,
                            box.height - 2 * inset > 1 ? box.height - 2 * inset : 1);
    wlr_scene_node_set_enabled(&server->snapTree->node, true);
    scheduleAllFrames(server);
}

// Hot corners (Settings -> Windows): pushing the pointer into a screen corner
// opens the overview, shows the desktop, ... Never while dragging, locked,
// in a full-screen window or a game that holds the pointer.
static void broadcastPointerOutput(struct Server *server)
{
    if (!server->pointerOutput || !ipcHasWatchers(server))
        return;
    char line[128];
    snprintf(line, sizeof(line), "event output %s", server->pointerOutput->name);
    ipcBroadcast(server, line);
}

static void checkHotCorner(struct Server *server)
{
    struct wlr_output *output = wlr_output_layout_output_at(server->outputLayout,
                                                            server->cursor->x, server->cursor->y);
    if (output && output != server->pointerOutput) {
        server->pointerOutput = output;
        broadcastPointerOutput(server);
    }
    if (!output) {
        server->hotCornerFired = -1;
        return;
    }
    struct wlr_box box;
    wlr_output_layout_get_box(server->outputLayout, output, &box);
    const double x = server->cursor->x - box.x;
    const double y = server->cursor->y - box.y;
    const bool left = x < 1;
    const bool right = x > box.width - 2;
    const bool top = y < 1;
    const bool bottom = y > box.height - 2;
    int corner = -1;
    if (top && left)
        corner = 0;
    else if (top && right)
        corner = 1;
    else if (bottom && left)
        corner = 2;
    else if (bottom && right)
        corner = 3;
    if (corner < 0) {
        // Re-armed once the pointer is clearly away from the corner.
        const double cx = (server->hotCornerFired == 1 || server->hotCornerFired == 3)
                              ? box.width - x : x;
        const double cy = (server->hotCornerFired == 2 || server->hotCornerFired == 3)
                              ? box.height - y : y;
        if (cx > 12 || cy > 12)
            server->hotCornerFired = -1;
        return;
    }
    if (server->hotCornerFired == corner)
        return;
    server->hotCornerFired = corner;
    const char *action = server->config.hotCorners[corner];
    if (strcmp(action, "none") == 0 || action[0] == '\0' || server->locked
        || server->seat->drag || server->cursorMode != CURSOR_PASSTHROUGH
        || server->fullscreenShown || server->activeConstraint || shortcutsInhibited(server))
        return;
    if (strcmp(action, "overview") == 0)
        shellAction("toggleTaskView");
    else if (strcmp(action, "desktop") == 0)
        toggleShowDesktop(server);
    else if (strcmp(action, "startMenu") == 0)
        shellAction("toggleStartMenu");
    else if (strcmp(action, "notifications") == 0)
        shellAction("toggleNotifications");
    else if (strcmp(action, "lock") == 0)
        spawnLocker(server);
}

// Every input event counts as activity for ext-idle-notify clients
// (swayidle): without this the screen would blank while it is being used.
static void notifyActivity(struct Server *server)
{
    if (server->idleNotifier)
        wlr_idle_notifier_v1_notify_activity(server->idleNotifier, server->seat);
}

static void cursorMotion(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, cursorMotion);
    struct wlr_pointer_motion_event *event = data;
    notifyActivity(server);
    // Raw movement for games and 3D views, whether or not the pointer moves.
    wlr_relative_pointer_manager_v1_send_relative_motion(
        server->relativePointers, server->seat, (uint64_t)event->time_msec * 1000,
        event->delta_x, event->delta_y, event->unaccel_dx, event->unaccel_dy);
    double dx = event->delta_x;
    double dy = event->delta_y;
    struct wlr_pointer_constraint_v1 *constraint = server->activeConstraint;
    if (constraint && server->cursorMode == CURSOR_PASSTHROUGH) {
        // Locked: the pointer stays put, the application gets the motion.
        if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
            return;
        // Confined: it stops at the edge of the allowed region.
        int originX = 0;
        int originY = 0;
        if (surfaceLayoutPosition(server, constraint->surface, &originX, &originY)) {
            const double sx = server->cursor->x - originX;
            const double sy = server->cursor->y - originY;
            double confinedX = 0;
            double confinedY = 0;
            if (wlr_region_confine(&constraint->region, sx, sy, sx + dx, sy + dy, &confinedX,
                                   &confinedY)) {
                dx = confinedX - sx;
                dy = confinedY - sy;
            }
        }
    }
    wlr_cursor_move(server->cursor, &event->pointer->base, dx, dy);
    processCursorMotion(server, event->time_msec);
}

static void cursorMotionAbsolute(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, cursorMotionAbsolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    notifyActivity(server);
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base,
                             event->x, event->y);
    processCursorMotion(server, event->time_msec);
}

static void cursorButton(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, cursorButton);
    struct wlr_pointer_button_event *event = data;
    // Super+click is a chord, not a tap of Super.
    server->logoTap = false;
    notifyActivity(server);
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED && !server->locked
        && ipcHasWatchers(server)) {
        // The shell closes an open card when the click is outside it.
        char line[64];
        snprintf(line, sizeof(line), "event click %d %d", (int)server->cursor->x,
                 (int)server->cursor->y);
        ipcBroadcast(server, line);
    }

    if (server->locked) {
        // Only the locker gets pointer input; a click also gives it the
        // keyboard (one lock surface per output).
        struct Hit hit = hitTest(server, server->cursor->x, server->cursor->y);
        if (hit.surface && event->state == WL_POINTER_BUTTON_STATE_PRESSED)
            lockFocusSurface(server, wlr_surface_get_root_surface(hit.surface));
        pointerFocus(server, &hit);
        wlr_seat_pointer_notify_button(server->seat, event->time_msec,
                                       event->button, event->state);
        wlr_seat_pointer_notify_frame(server->seat);
        return;
    }

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (server->swallowedButton && event->button == server->swallowedButton) {
            server->swallowedButton = 0;
            return;
        }
        if (server->cursorMode == CURSOR_MOVE && server->grabbedView
            && server->config.snapping) {
            uint32_t edges = dragSnapEdges(server, server->grabbedView);
            if (edges)
                viewSnap(server->grabbedView, edges);
        }
        updateSnapPreview(server, false);
        if (!server->superGrab) {
            wlr_seat_pointer_notify_button(server->seat, event->time_msec,
                                           event->button, event->state);
        }
        const bool grabbed = server->cursorMode != CURSOR_PASSTHROUGH;
        server->superGrab = false;
        server->resizeOffsetX = 0;
        server->resizeOffsetY = 0;
        server->cursorMode = CURSOR_PASSTHROUGH;
        server->grabbedView = NULL;
        // The window stopped under a pointer that did not move: whatever is
        // there now gets the pointer without waiting for the next motion.
        if (grabbed)
            refreshPointerFocus(server);
        return;
    }

    // A second button during a move or resize belongs to nobody: it used to
    // go through to the window below (or open a title bar menu) mid-drag.
    if (server->cursorMode != CURSOR_PASSTHROUGH && server->grabbedView) {
        server->swallowedButton = event->button;
        return;
    }
    server->swallowedButton = 0;

    // Super + left drag moves a window from anywhere inside it, Super +
    // right drag resizes it from the nearest corner (as on GNOME and KDE).
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    bool superHeld = keyboard
                     && (wlr_keyboard_get_modifiers(keyboard) & WLR_MODIFIER_LOGO);
    if (superHeld && (event->button == BTN_LEFT || event->button == BTN_RIGHT)) {
        struct Hit grab = hitTest(server, server->cursor->x, server->cursor->y);
        struct View *view = grab.view;
        if (view && !view->overrideRedirect && !view->fullscreen) {
            focusView(view, NULL);
            if (view->maximized)
                viewSetMaximized(view, false);
            viewClearSnap(view);
            server->grabbedView = view;
            server->superGrab = true;
            if (event->button == BTN_LEFT) {
                server->cursorMode = CURSOR_MOVE;
                server->grabX = server->cursor->x - view->x;
                server->grabY = server->cursor->y - view->y;
            } else {
                server->cursorMode = CURSOR_RESIZE;
                server->resizeEdges =
                    (server->cursor->x < view->x + view->width / 2.0 ? WLR_EDGE_LEFT
                                                                     : WLR_EDGE_RIGHT)
                    | (server->cursor->y < view->y + view->height / 2.0 ? WLR_EDGE_TOP
                                                                        : WLR_EDGE_BOTTOM);
                server->grabWidth = view->width;
                server->grabHeight = view->height;
                server->grabX = view->x;
                server->grabY = view->y;
                server->resizeOffsetX = server->cursor->x
                    - ((server->resizeEdges & WLR_EDGE_LEFT) ? view->x : view->x + view->width);
                server->resizeOffsetY = server->cursor->y
                    - ((server->resizeEdges & WLR_EDGE_TOP) ? view->y : view->y + view->height);
            }
            return;
        }
    }

    struct Hit hit = hitTest(server, server->cursor->x, server->cursor->y);

    // The compositor-drawn frame: buttons, title drag/double click, edges.
    if (hit.view && hit.frame != FRAME_NONE) {
        struct View *view = hit.view;
        if (server->focusedLayer) {
            server->focusedLayer = NULL;
            wlr_seat_keyboard_notify_clear_focus(server->seat);
        }
        focusView(view, NULL);
        server->superGrab = true;  // the client never sees this click
        switch (hit.frame) {
        case FRAME_CLOSE:
            viewClose(view);
            break;
        case FRAME_MAXIMIZE:
            viewSetMaximized(view, !view->maximized);
            break;
        case FRAME_MINIMIZE:
            viewSetMinimized(view, true);
            break;
        case FRAME_TITLE:
            if (event->button == BTN_MIDDLE) {
                viewSetMinimized(view, true);
                break;
            }
            if (event->button == BTN_RIGHT) {
                // The window menu, as on every other desktop.
                requestWindowMenu(view, (int)server->cursor->x, (int)server->cursor->y);
                break;
            }
            if (event->button != BTN_LEFT)
                break;
            if (server->lastTitleView == view
                && event->time_msec - server->lastTitleClick < 400) {
                server->lastTitleView = NULL;
                viewSetMaximized(view, !view->maximized);
                break;
            }
            server->lastTitleView = view;
            server->lastTitleClick = event->time_msec;
            if (view->maximized) {
                // Dragging a maximized window off its title restores it
                // under the pointer.
                double ratio = view->width > 0
                    ? (server->cursor->x - view->x) / view->width : 0.5;
                viewSetMaximized(view, false);
                viewSetPosition(view, (int)(server->cursor->x - ratio * view->width),
                                (int)server->cursor->y + server->config.titleHeight / 2);
            }
            viewClearSnap(view);
            server->grabbedView = view;
            server->cursorMode = CURSOR_MOVE;
            server->grabX = server->cursor->x - view->x;
            server->grabY = server->cursor->y - view->y;
            break;
        case FRAME_EDGE:
            viewClearSnap(view);
            server->grabbedView = view;
            server->cursorMode = CURSOR_RESIZE;
            server->resizeEdges = hit.frameEdges;
            server->grabWidth = view->width;
            server->grabHeight = view->height;
            server->grabX = view->x;
            server->grabY = view->y;
            server->resizeOffsetX = server->cursor->x
                - ((hit.frameEdges & WLR_EDGE_LEFT) ? view->x
                   : (hit.frameEdges & WLR_EDGE_RIGHT) ? view->x + view->width
                   : server->cursor->x);
            server->resizeOffsetY = server->cursor->y
                - ((hit.frameEdges & WLR_EDGE_TOP) ? view->y
                   : (hit.frameEdges & WLR_EDGE_BOTTOM) ? view->y + view->height
                   : server->cursor->y);
            break;
        default:
            break;
        }
        return;
    }
    wlr_log(WLR_DEBUG, "button %u at %.0f,%.0f view=%p layer=%s focused=%p",
            event->button, server->cursor->x, server->cursor->y,
            (void *)hit.view,
            hit.layer && hit.layer->layer->namespace
                ? hit.layer->layer->namespace : "(none)",
            (void *)server->seat->pointer_state.focused_surface);
    // Clicking outside an exclusive layer (a menu, for example) has to
    // deactivate it even when the surface under the pointer cannot take
    // keyboard focus itself.
    if (server->focusedLayer && hit.layer != server->focusedLayer) {
        server->focusedLayer = NULL;
        wlr_seat_keyboard_notify_clear_focus(server->seat);
    }

    if (hit.view) {
        if (!hit.view->overrideRedirect)
            focusView(hit.view, hit.surface);
        viewRaise(hit.view);
    } else if (hit.layer) {
        enum zwlr_layer_surface_v1_keyboard_interactivity interactivity =
            hit.layer->layer->current.keyboard_interactive;
        if (interactivity == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
            || interactivity == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND) {
            focusLayer(hit.layer);
        }
    } else if (server->focusedView) {
        clearKeyboardFocus(server);
    }

    pointerFocus(server, &hit);
    wlr_seat_pointer_notify_frame(server->seat);
    wlr_seat_pointer_notify_button(server->seat, event->time_msec,
                                   event->button, event->state);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void cursorAxis(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, cursorAxis);
    struct wlr_pointer_axis_event *event = data;
    notifyActivity(server);
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
                                 event->orientation, event->delta,
                                 event->delta_discrete, event->source,
                                 event->relative_direction);
}

static void cursorFrame(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Server *server = wl_container_of(listener, server, cursorFrame);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void seatRequestCursor(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, requestCursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    if (server->seat->pointer_state.focused_client == event->seat_client) {
        wlr_cursor_set_surface(server->cursor, event->surface,
                               event->hotspot_x, event->hotspot_y);
    }
}

// cursor-shape-v1: the application names a cursor ("text", "pointer",
// "ew-resize") and the compositor draws it from the cursor theme. Only the
// client under the pointer may change it.
static void handleCursorShape(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, cursorShape);
    struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
    if (event->device_type != WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_POINTER
        || server->seat->pointer_state.focused_client != event->seat_client)
        return;
    wlr_cursor_set_xcursor(server->cursor, server->cursorManager,
                           wlr_cursor_shape_v1_name(event->shape));
}

static void seatRequestSetSelection(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, requestSetSelection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void seatRequestSetPrimarySelection(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, requestSetPrimarySelection);
    struct wlr_seat_request_set_primary_selection_event *event = data;
    wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

// ---------------------------------------------------------------------------
// Drag and drop
// ---------------------------------------------------------------------------

struct DragIcon {
    struct Server *server;
    struct wl_listener destroy;
};

// A client asks to start dragging (files, text, tabs). Without this
// handler wlroots never starts a drag, so nothing could be dropped
// anywhere. The request must come from a real button press.
static void seatRequestStartDrag(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, requestStartDrag);
    struct wlr_seat_request_start_drag_event *event = data;
    if (!server->locked
        && wlr_seat_validate_pointer_grab_serial(server->seat, event->origin, event->serial)) {
        wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
        return;
    }
    if (event->drag->source)
        wlr_data_source_destroy(event->drag->source);
}

static void dragIconDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct DragIcon *icon = wl_container_of(listener, icon, destroy);
    wl_list_remove(&icon->destroy.link);
    scheduleAllFrames(icon->server);
    free(icon);
}

static void seatStartDrag(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, startDrag);
    struct wlr_drag *drag = data;
    wlr_scene_node_set_position(&server->dragIconTree->node, (int)server->cursor->x,
                                (int)server->cursor->y);
    if (!drag->icon)
        return;
    // The scene helper follows the icon's own offsets and goes away with it.
    wlr_scene_drag_icon_create(server->dragIconTree, drag->icon);
    struct DragIcon *icon = xcalloc(1, sizeof(*icon));
    icon->server = server;
    icon->destroy.notify = dragIconDestroy;
    wl_signal_add(&drag->icon->events.destroy, &icon->destroy);
    scheduleAllFrames(server);
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

// Calls org.newteller.Shell.<method> with one optional argument (gdbus
// parses it as a GVariant: "3" is an int32).
static void shellActionWith(const char *method, const char *argument)
{
    // gdbus is part of glib2, present on every supported Debian/Ubuntu
    // desktop. The service is registered by the running NewTeller shell.
    char call[256];
    snprintf(call, sizeof(call), "org.newteller.Shell.%s", method);
    const char *argv[] = {
        "gdbus", "call", "--session",
        "--dest", "org.newteller.Shell",
        "--object-path", "/org/newteller/Shell",
        "--method", call, argument, NULL,
    };
    spawnDetached(argv[0], argv);
}

static void shellAction(const char *method)
{
    shellActionWith(method, NULL);
}

// ---------------------------------------------------------------------------
// Keyboard layouts and input device settings (compositor.conf)
// ---------------------------------------------------------------------------

static struct xkb_keymap *buildKeymap(struct Server *server)
{
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context)
        return NULL;
    const struct CompositorConfig *config = &server->config;
    struct xkb_rule_names rules = {0};
    rules.layout = config->keyboardLayout[0] ? config->keyboardLayout : NULL;
    rules.variant = config->keyboardVariant[0] ? config->keyboardVariant : NULL;
    rules.options = config->keyboardOptions[0] ? config->keyboardOptions : NULL;
    rules.model = config->keyboardModel[0] ? config->keyboardModel : NULL;
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(
        context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        wlr_log(WLR_ERROR,
                "keyboard layout '%s' (variant '%s', options '%s') does not "
                "compile; using the default layout",
                config->keyboardLayout, config->keyboardVariant,
                config->keyboardOptions);
        struct xkb_rule_names fallback = {0};
        keymap = xkb_keymap_new_from_names(context, &fallback,
                                           XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
    xkb_context_unref(context);
    return keymap;
}

// Short names for the panel indicator: the codes from the configured list
// ("us", "ru"), or the keymap's own names when no list is configured.
static void updateLayoutNames(struct Server *server, struct xkb_keymap *keymap)
{
    char codes[8][32];
    int codeCount = ntConfigSplitList(server->config.keyboardLayout, codes, 8);
    xkb_layout_index_t count = xkb_keymap_num_layouts(keymap);
    if (count > 8)
        count = 8;
    server->layoutCount = (int)count;
    for (xkb_layout_index_t i = 0; i < count; ++i) {
        const char *name = NULL;
        if ((int)i < codeCount && codes[i][0] != '\0')
            name = codes[i];
        else
            name = xkb_keymap_layout_get_name(keymap, i);
        snprintf(server->layoutNames[i], sizeof(server->layoutNames[i]), "%s",
                 name ? name : "?");
        for (char *p = server->layoutNames[i]; *p; ++p) {
            if (*p == '|' || *p == '\n')
                *p = ' ';
        }
    }
    if (server->layoutIndex >= server->layoutCount)
        server->layoutIndex = 0;
}

static void configureKeyboard(struct Server *server, struct Keyboard *keyboard,
                              struct xkb_keymap *keymap)
{
    if (!keyboard->ownKeymap && keymap) {
        wlr_keyboard_set_keymap(keyboard->keyboard, keymap);
        xkb_mod_mask_t locked = 0;
        if (server->config.numlock) {
            xkb_mod_index_t num = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_NUM);
            if (num != XKB_MOD_INVALID)
                locked |= (xkb_mod_mask_t)1 << num;
        }
        if (locked || server->layoutIndex > 0) {
            wlr_keyboard_notify_modifiers(keyboard->keyboard, 0, 0, locked,
                                          (uint32_t)server->layoutIndex);
        }
    }
    wlr_keyboard_set_repeat_info(keyboard->keyboard, server->config.repeatRate,
                                 server->config.repeatDelay);
}

static void applyKeyboardConfig(struct Server *server)
{
    struct xkb_keymap *keymap = buildKeymap(server);
    if (!keymap)
        return;
    updateLayoutNames(server, keymap);
    struct wlr_keyboard *active = wlr_seat_get_keyboard(server->seat);
    struct Keyboard *keyboard;
    wl_list_for_each(keyboard, &server->keyboards, link)
        configureKeyboard(server, keyboard, keymap);
    xkb_keymap_unref(keymap);
    if (active) {
        wlr_seat_set_keyboard(server->seat, active);
        wlr_seat_keyboard_notify_modifiers(server->seat, &active->modifiers);
    }
    broadcastLayout(server);
}

static void setLayout(struct Server *server, int index)
{
    if (server->layoutCount <= 0)
        return;
    index = ((index % server->layoutCount) + server->layoutCount)
            % server->layoutCount;
    struct wlr_keyboard *active = wlr_seat_get_keyboard(server->seat);
    server->layoutIndex = index;
    if (server->config.layoutPerWindow && server->focusedView)
        server->focusedView->layoutIndex = index;
    struct Keyboard *keyboard;
    wl_list_for_each(keyboard, &server->keyboards, link) {
        if (keyboard->ownKeymap || !keyboard->keyboard->keymap)
            continue;
        const struct wlr_keyboard_modifiers *mods = &keyboard->keyboard->modifiers;
        wlr_keyboard_notify_modifiers(keyboard->keyboard, mods->depressed,
                                      mods->latched, mods->locked,
                                      (uint32_t)index);
    }
    if (active) {
        wlr_seat_set_keyboard(server->seat, active);
        wlr_seat_keyboard_notify_modifiers(server->seat, &active->modifiers);
    }
    broadcastLayout(server);
}

static void broadcastLayout(struct Server *server)
{
    char line[400];
    int used = snprintf(line, sizeof(line), "event layout %d ",
                        server->layoutIndex);
    for (int i = 0; i < server->layoutCount && used < (int)sizeof(line) - 40; ++i) {
        used += snprintf(line + used, sizeof(line) - (size_t)used, "%s%s",
                         i > 0 ? "|" : "", server->layoutNames[i]);
    }
    ipcBroadcast(server, line);
}

static void configurePointer(struct Server *server, struct wlr_input_device *device)
{
    if (!wlr_input_device_is_libinput(device))
        return;
    struct libinput_device *handle = wlr_libinput_get_device_handle(device);
    if (!handle)
        return;
    const struct CompositorConfig *config = &server->config;
    bool touchpad = libinput_device_config_tap_get_finger_count(handle) > 0;
    if (touchpad) {
        libinput_device_config_tap_set_enabled(
            handle, config->touchpadTap ? LIBINPUT_CONFIG_TAP_ENABLED
                                        : LIBINPUT_CONFIG_TAP_DISABLED);
        if (libinput_device_config_dwt_is_available(handle)) {
            libinput_device_config_dwt_set_enabled(
                handle, config->touchpadDisableWhileTyping
                            ? LIBINPUT_CONFIG_DWT_ENABLED
                            : LIBINPUT_CONFIG_DWT_DISABLED);
        }
    }
    if (libinput_device_config_scroll_has_natural_scroll(handle)) {
        libinput_device_config_scroll_set_natural_scroll_enabled(
            handle, touchpad ? config->touchpadNaturalScroll
                             : config->mouseNaturalScroll);
    }
    if (libinput_device_config_accel_is_available(handle)) {
        libinput_device_config_accel_set_speed(
            handle, touchpad ? config->touchpadSpeed : config->mouseSpeed);
    }
    if (libinput_device_config_left_handed_is_available(handle))
        libinput_device_config_left_handed_set(handle, config->leftHanded ? 1 : 0);
}

static void loadConfig(struct Server *server)
{
    struct CompositorConfig config;
    ntConfigDefaults(&config);
    char path[PATH_MAX];
    ntConfigPath(path, sizeof(path), "compositor.conf");
    ntConfigLoadFile(&config, path);
    // No layout of our own: use the system keyboard configuration (what the
    // installer or `dpkg-reconfigure keyboard-configuration` wrote).
    // Only Debian and Ubuntu have /etc/default/keyboard; Arch and Fedora keep
    // the layout where localectl puts it. A file is only read while there is
    // still no layout, so the variant never comes from a different file.
    if (config.keyboardLayout[0] == '\0')
        ntConfigLoadSystemKeyboard(&config, "/etc/default/keyboard");
    if (config.keyboardLayout[0] == '\0')
        ntConfigLoadSystemKeyboard(&config, "/etc/vconsole.conf");
    if (config.keyboardLayout[0] == '\0')
        ntConfigLoadXorgKeyboard(&config, "/etc/X11/xorg.conf.d/00-keyboard.conf");
    server->config = config;
    compileBindings(server);
}

// "Super+Shift+T" -> modifier mask and keysym.
static bool parseBinding(const char *keys, uint32_t *modifiers, xkb_keysym_t *sym)
{
    char copy[64];
    snprintf(copy, sizeof(copy), "%s", keys);
    *modifiers = 0;
    *sym = XKB_KEY_NoSymbol;
    char *save = NULL;
    for (char *token = strtok_r(copy, "+", &save); token; token = strtok_r(NULL, "+", &save)) {
        while (*token == ' ')
            ++token;
        char *end = token + strlen(token);
        while (end > token && end[-1] == ' ')
            *--end = '\0';
        if (strcasecmp(token, "super") == 0 || strcasecmp(token, "meta") == 0
            || strcasecmp(token, "win") == 0 || strcasecmp(token, "logo") == 0)
            *modifiers |= WLR_MODIFIER_LOGO;
        else if (strcasecmp(token, "ctrl") == 0 || strcasecmp(token, "control") == 0)
            *modifiers |= WLR_MODIFIER_CTRL;
        else if (strcasecmp(token, "alt") == 0)
            *modifiers |= WLR_MODIFIER_ALT;
        else if (strcasecmp(token, "shift") == 0)
            *modifiers |= WLR_MODIFIER_SHIFT;
        else
            *sym = xkb_keysym_to_lower(xkb_keysym_from_name(token, XKB_KEYSYM_CASE_INSENSITIVE));
    }
    return *sym != XKB_KEY_NoSymbol;
}

static void compileBindings(struct Server *server)
{
    server->bindingCount = 0;
    for (int i = 0; i < server->config.bindingCount; ++i) {
        struct Binding *binding = &server->bindings[server->bindingCount];
        if (!parseBinding(server->config.bindings[i].keys, &binding->modifiers, &binding->sym)) {
            wlr_log(WLR_ERROR, "ignoring shortcut '%s': unknown key",
                    server->config.bindings[i].keys);
            continue;
        }
        snprintf(binding->command, sizeof(binding->command), "%s",
                 server->config.bindings[i].command);
        ++server->bindingCount;
    }
}

static void applyCursorTheme(struct Server *server)
{
    const char *theme = server->config.cursorTheme[0]
                            ? server->config.cursorTheme : NULL;
    struct wlr_xcursor_manager *manager = wlr_xcursor_manager_create(
        theme, (uint32_t)server->config.cursorSize);
    if (!manager)
        return;
    struct Output *output;
    wl_list_for_each(output, &server->outputs, link)
        wlr_xcursor_manager_load(manager, output->output->scale);
    struct wlr_xcursor_manager *old = server->cursorManager;
    server->cursorManager = manager;
    if (server->cursor)
        wlr_cursor_set_xcursor(server->cursor, manager, "left_ptr");
    if (old)
        wlr_xcursor_manager_destroy(old);
}

static void reloadConfig(struct Server *server)
{
    char oldTheme[NT_CONFIG_TEXT];
    snprintf(oldTheme, sizeof(oldTheme), "%s", server->config.cursorTheme);
    int oldSize = server->config.cursorSize;

    loadConfig(server);
    applyKeyboardConfig(server);
    // Fewer desktops than before: their windows move to the last one.
    int last = server->config.workspaceCount - 1;
    struct View *view;
    wl_list_for_each(view, &server->views, link) {
        if (view->workspace > last)
            view->workspace = last;
    }
    if (server->workspace > last)
        server->workspace = last;
    // Also when the desktop shown stays: windows of a removed desktop may
    // have just arrived on it.
    wl_list_for_each(view, &server->views, link)
        viewSyncVisible(view);
    updateFullscreenLayers(server);
    broadcastWorkspaces(server);
    scheduleAllFrames(server);
    if (server->serverDecoration) {
        wlr_server_decoration_manager_set_default_mode(
            server->serverDecoration, server->config.decorationsServer
                                          ? WLR_SERVER_DECORATION_MANAGER_MODE_SERVER
                                          : WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT);
    }
    // New frame colours, sizes or button layout.
    wl_list_for_each(view, &server->views, link) {
        view->decoWidth = -1;
        if (view->xdg && view->xdgDecoration && view->mapped)
            applyDecorationMode(view);
        else
            viewUpdateDecoration(view);
    }
    struct Pointer *pointer;
    wl_list_for_each(pointer, &server->pointers, link)
        configurePointer(server, pointer->device);
    if (strcmp(oldTheme, server->config.cursorTheme) != 0
        || oldSize != server->config.cursorSize) {
        applyCursorTheme(server);
    }
    wlr_log(WLR_INFO, "configuration reloaded (layout '%s')",
            server->config.keyboardLayout);
}

static int reloadSignalHandler(int signal, void *data)
{
    (void)signal;
    reloadConfig(data);
    return 0;
}

// ---------------------------------------------------------------------------
// Window switcher (Alt+Tab)
// ---------------------------------------------------------------------------

// Minimized windows take part, as on Windows and GNOME: Alt+Tab is how one
// gets back to them without reaching for the dock.
static bool switcherCandidate(struct View *view)
{
    return view && view->mapped && !view->overrideRedirect
        && (view->server->config.switcherAllWorkspaces || viewOnWorkspace(view))
        && viewSurface(view);
}

// The view after (or before) `from` in server->views, wrapping around and
// skipping anything that cannot take focus. server->views is most recently
// focused first, because focusView() moves the view to the head.
static struct View *switcherNeighbour(struct Server *server, struct View *from,
                                      bool backwards)
{
    struct wl_list *link = from ? &from->link : &server->views;
    int count = wl_list_length(&server->views);
    for (int i = 0; i < count; ++i) {
        link = backwards ? link->prev : link->next;
        if (link == &server->views)
            link = backwards ? link->prev : link->next;
        struct View *view = wl_container_of(link, view, link);
        if (view != from && switcherCandidate(view))
            return view;
    }
    return NULL;
}

// Restores the scene stacking to the order of server->views, undoing the
// preview raises done while cycling.
static void switcherRestoreStacking(struct Server *server)
{
    struct View *view;
    wl_list_for_each_reverse(view, &server->views, link) {
        if (view->tree)
            wlr_scene_node_raise_to_top(&view->tree->node);
    }
    raisePinnedViews(server);
}

static void switcherStep(struct Server *server, bool backwards)
{
    struct View *from = server->switching ? server->switchTarget
                                          : server->focusedView;
    if (!switcherCandidate(from))
        from = NULL;
    struct View *next = switcherNeighbour(server, from, backwards);
    if (!next)
        return;
    if (!server->switching) {
        // Fresh thumbnails for the overlay, once per Alt+Tab.
        int captured = 0;
        struct View *view;
        wl_list_for_each(view, &server->views, link) {
            if (captured >= 16)
                break;
            if (switcherCandidate(view) && captureThumbnail(view, 240))
                ++captured;
        }
    }
    server->switching = true;
    server->switchTarget = next;
    // Preview: lift the candidate to the top of the stack without moving
    // keyboard focus or the MRU order, so the next Tab keeps walking.
    switcherRestoreStacking(server);
    if (next->tree)
        wlr_scene_node_raise_to_top(&next->tree->node);
    broadcastSwitcher(server);
}

static void switcherCommit(struct Server *server)
{
    if (!server->switching)
        return;
    struct View *target = server->switchTarget;
    server->switching = false;
    server->switchTarget = NULL;
    switcherRestoreStacking(server);
    broadcastSwitcher(server);
    if (switcherCandidate(target)) {
        if (target->minimized)
            viewSetMinimized(target, false); // comes back and takes the focus
        else
            focusView(target, NULL);
    }
}

static void switcherCancel(struct Server *server)
{
    bool wasSwitching = server->switching;
    server->switching = false;
    server->switchTarget = NULL;
    switcherRestoreStacking(server);
    if (wasSwitching)
        broadcastSwitcher(server);
}

// Cuts a string that snprintf truncated back to a whole UTF-8 character, so
// a long Cyrillic or CJK title never ends in half a character.
static void trimUtf8(char *text)
{
    size_t length = strlen(text);
    size_t start = length;
    while (start > 0 && ((unsigned char)text[start - 1] & 0xC0) == 0x80)
        --start;
    if (start == 0 || !((unsigned char)text[start - 1] & 0x80))
        return;
    unsigned char lead = (unsigned char)text[start - 1];
    size_t needed = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (length - (start - 1) < needed)
        text[start - 1] = '\0';
}

// ---------------------------------------------------------------------------
// Window thumbnails (overview, Alt+Tab)
// ---------------------------------------------------------------------------

static void thumbnailPath(struct View *view, char *path, size_t size)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    snprintf(path, size, "%s/newteller-thumbs/%u.png", runtime ? runtime : "/tmp", view->id);
}

// Reads the window's current buffer back from the renderer and writes a
// scaled PNG next to the control socket. Subsurfaces (video overlays,
// some toolkits' popovers) are not included; the main surface is.
static bool captureThumbnail(struct View *view, int maxSize)
{
    struct wlr_surface *surface = viewSurface(view);
    if (!surface)
        return false;
    struct wlr_texture *texture = wlr_surface_get_texture(surface);
    if (!texture)
        return false;
    // Alt+Tab asks for every window at once, on the compositor's only
    // thread; reading back and encoding a dozen 4K windows stalled the
    // first Tab. Unchanged windows keep the file they have.
    if (view->xdg && view->thumbValid && view->thumbSeq == view->commits
        && view->thumbSize == maxSize)
        return true;
    int width = (int)texture->width;
    int height = (int)texture->height;
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
        return false;
    uint8_t *pixels = malloc((size_t)width * (size_t)height * 4);
    if (!pixels)
        return false;
    bool swap = false;
    struct wlr_texture_read_pixels_options argb = {
        .data = pixels, .format = DRM_FORMAT_ARGB8888, .stride = (uint32_t)width * 4,
    };
    if (!wlr_texture_read_pixels(texture, &argb)) {
        // GLES without BGRA reads: RGBA always works, swap afterwards.
        struct wlr_texture_read_pixels_options abgr = {
            .data = pixels, .format = DRM_FORMAT_ABGR8888, .stride = (uint32_t)width * 4,
        };
        if (!wlr_texture_read_pixels(texture, &abgr)) {
            free(pixels);
            return false;
        }
        swap = true;
    }
    if (swap) {
        for (size_t i = 0; i < (size_t)width * (size_t)height; ++i) {
            uint8_t r = pixels[i * 4];
            pixels[i * 4] = pixels[i * 4 + 2];
            pixels[i * 4 + 2] = r;
        }
    }

    // Crop client-side shadows: the window geometry, in buffer pixels.
    struct wlr_box geometry = viewGeometry(view);
    double bufferScale = surface->current.width > 0
                             ? (double)width / surface->current.width : 1.0;
    double cropX = 0, cropY = 0, cropWidth = width, cropHeight = height;
    if (geometry.width > 0 && geometry.height > 0) {
        cropX = geometry.x * bufferScale;
        cropY = geometry.y * bufferScale;
        cropWidth = geometry.width * bufferScale;
        cropHeight = geometry.height * bufferScale;
    }
    double factor = maxSize / (cropWidth > cropHeight ? cropWidth : cropHeight);
    if (factor > 1.0)
        factor = 1.0;
    int outWidth = (int)ceil(cropWidth * factor);
    int outHeight = (int)ceil(cropHeight * factor);
    bool ok = false;
    cairo_surface_t *source = cairo_image_surface_create_for_data(
        pixels, CAIRO_FORMAT_ARGB32, width, height, width * 4);
    cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                      outWidth > 0 ? outWidth : 1,
                                                      outHeight > 0 ? outHeight : 1);
    if (cairo_surface_status(source) == CAIRO_STATUS_SUCCESS
        && cairo_surface_status(out) == CAIRO_STATUS_SUCCESS) {
        cairo_t *cr = cairo_create(out);
        cairo_scale(cr, factor, factor);
        cairo_set_source_surface(cr, source, -cropX, -cropY);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_destroy(cr);
        char path[PATH_MAX];
        thumbnailPath(view, path, sizeof(path));
        char directory[PATH_MAX];
        snprintf(directory, sizeof(directory), "%s", path);
        char *slash = strrchr(directory, '/');
        if (slash) {
            *slash = '\0';
            mkdir(directory, 0700);
        }
        ok = cairo_surface_write_to_png(out, path) == CAIRO_STATUS_SUCCESS;
        view->thumbValid = ok;
        view->thumbSeq = view->commits;
        view->thumbSize = maxSize;
    }
    cairo_surface_destroy(out);
    cairo_surface_destroy(source);
    free(pixels);
    return ok;
}

// Copies a title or app id into an IPC field: no separators, no newlines,
// cut on a UTF-8 boundary.
static void ipcField(char *out, size_t size, const char *text, bool noSpaces)
{
    snprintf(out, size, "%s", text ? text : "");
    trimUtf8(out);
    for (char *p = out; *p; ++p) {
        if (*p == '\n' || *p == '\r' || *p == '\x1e' || *p == '\x1f'
            || (noSpaces && *p == ' '))
            *p = noSpaces ? '_' : ' ';
    }
}

// The state letters of a window, shared by `windows` and the window menu:
// f focused, m minimized, x maximized, F fullscreen, t always on top,
// s on every desktop, h not responding. "-" when none applies, so the field is never empty.
static void viewFlags(struct View *view, char *flags, size_t size)
{
    size_t used = 0;
    const struct {
        bool on;
        char letter;
    } states[] = {
        {view == view->server->focusedView, 'f'},
        {view->minimized, 'm'},
        {view->maximized, 'x'},
        {view->hung, 'h'},
        {view->fullscreen, 'F'},
        {view->alwaysOnTop, 't'},
        {view->sticky, 's'},
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]) && used + 1 < size; ++i) {
        if (states[i].on)
            flags[used++] = states[i].letter;
    }
    if (used == 0 && size > 1)
        flags[used++] = '-';
    flags[used] = '\0';
}

// "event window-menu <id> <x> <y> <desktop> <flags> <title>": the shell
// opens the window menu at x/y (layout coordinates). Asked for by a right
// click on a title bar (ours or a client's), Alt+Space or `window-menu`.
static void requestWindowMenu(struct View *view, int x, int y)
{
    struct Server *server = view->server;
    if (server->locked || view->overrideRedirect || !view->mapped
        || !ipcHasWatchers(server))
        return;
    char title[160];
    ipcField(title, sizeof(title), viewTitle(view), false);
    char flags[10];
    viewFlags(view, flags, sizeof(flags));
    char line[256];
    snprintf(line, sizeof(line), "event window-menu %u %d %d %d %s %s", view->id, x, y,
             view->workspace, flags, title);
    ipcBroadcast(server, line);
}

// "on", "off" or "toggle" (the default) applied to the current state.
static bool ipcToggle(const char *argument, bool current)
{
    if (!argument || strcmp(argument, "toggle") == 0)
        return !current;
    return strcmp(argument, "on") == 0 || strcmp(argument, "1") == 0;
}

// "event switcher <selected> <entries>": the shell draws the Alt+Tab
// overlay from it. Entries are "<id>\x1e<app id>\x1e<title>" joined by
// \x1f, most recently used first; <selected> is -1 when the switcher
// closes. Thumbnails are in $XDG_RUNTIME_DIR/newteller-thumbs/<id>.png.
static void broadcastSwitcher(struct Server *server)
{
    if (!server->switching) {
        ipcBroadcast(server, "event switcher -1");
        return;
    }
    int selected = -1;
    int index = 0;
    struct Text entries = {0};
    struct View *view;
    wl_list_for_each(view, &server->views, link) {
        if (!switcherCandidate(view))
            continue;
        if (view == server->switchTarget)
            selected = index;
        char title[96];
        ipcField(title, sizeof(title), viewTitle(view), false);
        char appId[96];
        ipcField(appId, sizeof(appId), viewAppId(view), true);
        textAppend(&entries, "%s%u\x1e%s\x1e%s", index > 0 ? "\x1f" : "", view->id,
                   appId, title);
        ++index;
    }
    struct Text line = {0};
    textAppend(&line, "event switcher %d %s", selected, entries.data ? entries.data : "");
    ipcBroadcast(server, line.data);
    textFree(&line);
    textFree(&entries);
}

struct ShortcutsInhibitor {
    struct wl_list link;
    struct Server *server;
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor;
    struct wl_listener destroy;
};

static bool shortcutsInhibited(struct Server *server)
{
    if (server->locked)
        return false;
    struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
    struct ShortcutsInhibitor *entry;
    wl_list_for_each(entry, &server->shortcutsInhibitors, link) {
        if (entry->inhibitor->active && entry->inhibitor->surface == focused)
            return true;
    }
    return false;
}

// Inhibitors are active only while their surface has the keyboard.
static void updateShortcutsInhibitors(struct Server *server, struct wlr_surface *focused)
{
    struct ShortcutsInhibitor *entry;
    wl_list_for_each(entry, &server->shortcutsInhibitors, link) {
        const bool want = focused && entry->inhibitor->surface == focused && !server->locked;
        if (want && !entry->inhibitor->active)
            wlr_keyboard_shortcuts_inhibitor_v1_activate(entry->inhibitor);
        else if (!want && entry->inhibitor->active)
            wlr_keyboard_shortcuts_inhibitor_v1_deactivate(entry->inhibitor);
    }
}

static void shortcutsInhibitorDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct ShortcutsInhibitor *entry = wl_container_of(listener, entry, destroy);
    wl_list_remove(&entry->destroy.link);
    wl_list_remove(&entry->link);
    free(entry);
}

static void handleNewShortcutsInhibitor(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newShortcutsInhibitor);
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor = data;
    struct ShortcutsInhibitor *entry = xcalloc(1, sizeof(*entry));
    entry->server = server;
    entry->inhibitor = inhibitor;
    entry->destroy.notify = shortcutsInhibitorDestroy;
    wl_signal_add(&inhibitor->events.destroy, &entry->destroy);
    wl_list_insert(&server->shortcutsInhibitors, &entry->link);
    if (!server->locked && inhibitor->surface == server->seat->keyboard_state.focused_surface)
        wlr_keyboard_shortcuts_inhibitor_v1_activate(inhibitor);
}

// `baseSym` is the key's symbol without Shift and other levels (1 for the
// key that gives "!" with Shift), for shortcuts that use Shift on digits.
static bool keyHandled(struct Server *server, xkb_keysym_t sym,
                       uint32_t modifiers, xkb_keysym_t baseSym)
{
    bool logo = (modifiers & WLR_MODIFIER_LOGO) != 0;
    bool alt = (modifiers & WLR_MODIFIER_ALT) != 0;

    // Ctrl+Alt+F1..F12: switch virtual terminals. This must keep working
    // while the screen is locked (it is how one reaches a text console).
    if (sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12) {
        if (server->session) {
            wlr_session_change_vt(server->session,
                                  (unsigned)(sym - XKB_KEY_XF86Switch_VT_1 + 1));
        }
        return true;
    }
    // Volume, brightness and media keys go to the shell, which changes the
    // level and shows it on screen; they also work on the lock screen. With
    // no shell listening they stay with the focused application.
    const char *mediaKey = NULL;
    switch (sym) {
    case XKB_KEY_XF86AudioRaiseVolume: mediaKey = "volume-up"; break;
    case XKB_KEY_XF86AudioLowerVolume: mediaKey = "volume-down"; break;
    case XKB_KEY_XF86AudioMute: mediaKey = "mute"; break;
    case XKB_KEY_XF86AudioMicMute: mediaKey = "mic-mute"; break;
    case XKB_KEY_XF86MonBrightnessUp: mediaKey = "brightness-up"; break;
    case XKB_KEY_XF86MonBrightnessDown: mediaKey = "brightness-down"; break;
    case XKB_KEY_XF86AudioPlay:
    case XKB_KEY_XF86AudioPause: mediaKey = "play-pause"; break;
    case XKB_KEY_XF86AudioNext: mediaKey = "next"; break;
    case XKB_KEY_XF86AudioPrev: mediaKey = "previous"; break;
    default: break;
    }
    if (mediaKey && ipcHasWatchers(server)) {
        char line[64];
        snprintf(line, sizeof(line), "event key %s", mediaKey);
        ipcBroadcast(server, line);
        return true;
    }

    // Everything else belongs to the screen locker while it runs.
    if (server->locked)
        return false;

    // A virtual machine or remote desktop asked for every key. Super+Escape
    // (as in GNOME) hands the shortcuts back; VT switching above always works.
    if (shortcutsInhibited(server)) {
        if (logo && sym == XKB_KEY_Escape) {
            struct ShortcutsInhibitor *entry;
            wl_list_for_each(entry, &server->shortcutsInhibitors, link) {
                if (entry->inhibitor->active)
                    wlr_keyboard_shortcuts_inhibitor_v1_deactivate(entry->inhibitor);
            }
            return true;
        }
        return false;
    }

    // The user's own shortcuts (Settings -> Shortcuts) come first, so they
    // can also replace a built-in one.
    uint32_t chord = modifiers & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL
                                  | WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT);
    xkb_keysym_t lower = xkb_keysym_to_lower(sym);
    for (int i = 0; i < server->bindingCount; ++i) {
        if (server->bindings[i].sym == lower && server->bindings[i].modifiers == chord) {
            const char *argv[] = {"sh", "-c", server->bindings[i].command, NULL};
            setChildEnvironment(server);
            spawnDetached(argv[0], argv);
            return true;
        }
    }
    // Ctrl+Shift+Esc (Windows) or Ctrl+Alt+Esc: end a frozen application.
    if ((modifiers & WLR_MODIFIER_CTRL) && sym == XKB_KEY_Escape
        && ((modifiers & WLR_MODIFIER_SHIFT) || alt)) {
        shellAction("showForceQuit");
        return true;
    }
    if (logo && (sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab) && !alt) {
        // Super+Tab: the overview of every window (as Windows' Task View).
        shellAction("toggleTaskView");
        return true;
    }
    if (logo && (sym == XKB_KEY_l || sym == XKB_KEY_L)) {
        spawnLocker(server);
        return true;
    }
    // Workspaces: Ctrl+Super+Left/Right (as on Windows), Super+Page Up/Down
    // (as on GNOME); with Shift the focused window moves along.
    bool ctrl = (modifiers & WLR_MODIFIER_CTRL) != 0;
    bool shift = (modifiers & WLR_MODIFIER_SHIFT) != 0;
    int workspaceStep = 0;
    if (logo && ctrl && sym == XKB_KEY_Left)
        workspaceStep = -1;
    else if (logo && ctrl && sym == XKB_KEY_Right)
        workspaceStep = 1;
    else if (logo && (sym == XKB_KEY_Page_Up || sym == XKB_KEY_KP_Page_Up))
        workspaceStep = -1;
    else if (logo && (sym == XKB_KEY_Page_Down || sym == XKB_KEY_KP_Page_Down))
        workspaceStep = 1;
    if (workspaceStep != 0) {
        int target = server->workspace + workspaceStep;
        if (shift && server->focusedView)
            moveViewToWorkspace(server->focusedView, target, true);
        else
            switchWorkspace(server, target);
        return true;
    }
    if (logo && sym == XKB_KEY_space && server->layoutCount > 1) {
        setLayout(server, server->layoutIndex + 1);
        return true;
    }
    // Alt+Space: the focused window's menu (as on Windows and GNOME), at its
    // top-left corner since the pointer may be anywhere.
    if (alt && !logo && !ctrl && sym == XKB_KEY_space && server->focusedView
        && ipcHasWatchers(server)) {
        struct View *view = server->focusedView;
        int top = view->ssd ? view->y - server->config.titleHeight / 2 : view->y + 8;
        requestWindowMenu(view, view->x + 16, top);
        return true;
    }
    // Super+Alt+R: start or stop a screen recording (as Windows' Game Bar).
    if (logo && alt && !ctrl && (sym == XKB_KEY_r || sym == XKB_KEY_R)) {
        shellAction("toggleScreenRecording");
        return true;
    }
    // Super+Shift+1..9: the focused window to that desktop, followed.
    if (logo && shift && !ctrl && !alt && baseSym >= XKB_KEY_1 && baseSym <= XKB_KEY_9) {
        int index = (int)(baseSym - XKB_KEY_1);
        if (server->focusedView && index < server->config.workspaceCount)
            moveViewToWorkspace(server->focusedView, index, true);
        return true;
    }
    // Ctrl+Super+1..9: straight to that desktop.
    if (logo && ctrl && !alt && !shift && sym >= XKB_KEY_1 && sym <= XKB_KEY_9) {
        int index = (int)(sym - XKB_KEY_1);
        if (index < server->config.workspaceCount)
            switchWorkspace(server, index);
        return true;
    }
    // Super+1..9: start or bring up the Nth application on the dock (as on
    // Windows). The shell owns the dock, so it gets the index.
    if (logo && !alt && !ctrl && !shift && sym >= XKB_KEY_1 && sym <= XKB_KEY_9) {
        char argument[8];
        snprintf(argument, sizeof(argument), "%d", (int)(sym - XKB_KEY_1));
        shellActionWith("activateDockItem", argument);
        return true;
    }
    if (logo)
        wlr_log(WLR_DEBUG, "logo key sym=%u shift=%d focus=%p", sym,
                (modifiers & WLR_MODIFIER_SHIFT) != 0,
                (void *)server->focusedView);

    if (alt && sym == XKB_KEY_F2) {
        shellAction("showRunDialog");
        return true;
    }
    if (logo && sym == XKB_KEY_Return) {
        // newteller-terminal finds the installed terminal (and follows the
        // shell's apps.terminalCommand); x-terminal-emulator is Debian's.
        const char *argv[] = {"/bin/sh", "-c",
                              "command -v newteller-terminal >/dev/null 2>&1"
                              " && exec newteller-terminal; exec x-terminal-emulator",
                              NULL};
        setChildEnvironment(server);
        spawnDetached(argv[0], argv);
        return true;
    }
    if (logo && !(modifiers & WLR_MODIFIER_SHIFT)
        && (sym == XKB_KEY_s || sym == XKB_KEY_S || sym == XKB_KEY_i || sym == XKB_KEY_I)) {
        shellAction("openSettings");
        return true;
    }
    // The Windows shortcuts: Super+N notifications, Super+A quick
    // settings, Super+R run, Super+E the home folder in the file manager.
    if (logo && (sym == XKB_KEY_n || sym == XKB_KEY_N)) {
        shellAction("toggleNotifications");
        return true;
    }
    if (logo && (sym == XKB_KEY_a || sym == XKB_KEY_A)) {
        shellAction("toggleQuickSettings");
        return true;
    }
    if (logo && (sym == XKB_KEY_v || sym == XKB_KEY_V)) {
        shellAction("toggleClipboard");
        return true;
    }
    if (logo && (sym == XKB_KEY_r || sym == XKB_KEY_R)) {
        shellAction("showRunDialog");
        return true;
    }
    if (logo && (sym == XKB_KEY_e || sym == XKB_KEY_E)) {
        const char *home = getenv("HOME");
        const char *argv[] = {"xdg-open", home ? home : "/", NULL};
        setChildEnvironment(server);
        spawnDetached(argv[0], argv);
        return true;
    }
    if (logo && (sym == XKB_KEY_q || sym == XKB_KEY_Q)) {
        if (server->focusedView)
            viewClose(server->focusedView);
        return true;
    }
    if (logo && (sym == XKB_KEY_m || sym == XKB_KEY_M)) {
        if (server->focusedView) {
            viewSetMaximized(server->focusedView,
                             !server->focusedView->maximized);
        }
        return true;
    }
    if (logo && (sym == XKB_KEY_f || sym == XKB_KEY_F)) {
        if (server->focusedView) {
            viewSetFullscreen(server->focusedView,
                              !server->focusedView->fullscreen);
        }
        return true;
    }
    if (logo && (modifiers & WLR_MODIFIER_SHIFT)
        && (sym == XKB_KEY_Left || sym == XKB_KEY_Right)) {
        if (server->focusedView)
            moveViewToOutput(server->focusedView, sym == XKB_KEY_Right ? 1 : -1);
        return true;
    }
    if (logo && (sym == XKB_KEY_Left || sym == XKB_KEY_Right
                 || sym == XKB_KEY_Up || sym == XKB_KEY_Down)) {
        if (server->focusedView) {
            // As on Windows: a half goes to a quarter with Up/Down, a
            // quarter back to its half; Left/Right keep a quarter's row.
            struct View *view = server->focusedView;
            const uint32_t side = (uint32_t)view->snapEdges & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
            const uint32_t row = (uint32_t)view->snapEdges & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM);
            if (sym == XKB_KEY_Left)
                viewSnap(view, WLR_EDGE_LEFT | row);
            else if (sym == XKB_KEY_Right)
                viewSnap(view, WLR_EDGE_RIGHT | row);
            else if (sym == XKB_KEY_Up)
                viewSnap(view, side == 0 ? WLR_EDGE_TOP
                               : row == WLR_EDGE_BOTTOM ? side : side | WLR_EDGE_TOP);
            else
                viewSnap(view, side == 0 ? WLR_EDGE_BOTTOM
                               : row == WLR_EDGE_TOP ? side : side | WLR_EDGE_BOTTOM);
        }
        return true;
    }
    if (logo && (sym == XKB_KEY_d || sym == XKB_KEY_D)) {
        toggleShowDesktop(server);
        return true;
    }
    if (alt && sym == XKB_KEY_F4) {
        if (server->focusedView)
            viewClose(server->focusedView);
        return true;
    }
    // Print: the whole screen. Shift+Print or Super+Shift+S (the Windows
    // snipping shortcut): pick an area first.
    bool shiftHeld = (modifiers & WLR_MODIFIER_SHIFT) != 0;
    // Alt+Print (often reported as SysRq): the focused window with its frame.
    if (alt && !logo && (sym == XKB_KEY_Print || sym == XKB_KEY_Sys_Req)
        && server->focusedView) {
        struct wlr_box box = viewOuterBox(server->focusedView);
        char geometry[64];
        snprintf(geometry, sizeof(geometry), "%d,%d %dx%d", box.x, box.y, box.width,
                 box.height);
        const char *argv[] = {"newteller-screenshot", "--geometry", geometry, NULL};
        setChildEnvironment(server);
        spawnDetached(argv[0], argv);
        return true;
    }
    if ((sym == XKB_KEY_Print && shiftHeld)
        || (logo && shiftHeld && (sym == XKB_KEY_s || sym == XKB_KEY_S))) {
        const char *argv[] = {"newteller-screenshot", "--area", NULL};
        setChildEnvironment(server);
        spawnDetached(argv[0], argv);
        return true;
    }
    if (sym == XKB_KEY_Print) {
        const char *argv[] = {"newteller-screenshot", NULL};
        setChildEnvironment(server);
        spawnDetached(argv[0], argv);
        return true;
    }
    // Alt+` (the key above Tab; Ё on a Russian layout): the next window of
    // the same application, as on GNOME and macOS. Without another window
    // of that application the key goes to the application.
    if (alt && !logo && !ctrl && !server->switching && server->focusedView
        && (sym == XKB_KEY_grave || sym == XKB_KEY_dead_grave
            || sym == XKB_KEY_Cyrillic_io || sym == XKB_KEY_Cyrillic_IO)) {
        struct View *current = server->focusedView;
        const char *app = viewAppId(current);
        struct View *target = NULL;
        struct View *view;
        // Least recently used first, so repeated presses visit them all.
        wl_list_for_each_reverse(view, &server->views, link) {
            if (view != current && view->mapped && !view->overrideRedirect
                && viewOnWorkspace(view) && viewSurface(view) && app[0] != '\0'
                && strcmp(viewAppId(view), app) == 0) {
                target = view;
                break;
            }
        }
        if (target) {
            if (target->minimized)
                viewSetMinimized(target, false);
            focusView(target, NULL);
            return true;
        }
    }
    if (alt && (sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab)) {
        bool backwards = sym == XKB_KEY_ISO_Left_Tab
            || (modifiers & WLR_MODIFIER_SHIFT) != 0;
        switcherStep(server, backwards);
        return true;
    }
    if (server->switching && sym == XKB_KEY_Escape) {
        switcherCancel(server);
        return true;
    }
    return false;
}

static void keyboardModifiers(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
    struct Server *server = keyboard->server;
    if (server->switching
        && !(wlr_keyboard_get_modifiers(keyboard->keyboard) & WLR_MODIFIER_ALT))
        switcherCommit(server);
    // A layout switch through an xkb option (grp:alt_shift_toggle, ...)
    // only shows up as a new effective group.
    if (!keyboard->ownKeymap && keyboard->keyboard->xkb_state) {
        int group = (int)xkb_state_serialize_layout(keyboard->keyboard->xkb_state,
                                                    XKB_STATE_LAYOUT_EFFECTIVE);
        if (group != server->layoutIndex && group < server->layoutCount) {
            server->layoutIndex = group;
            if (server->config.layoutPerWindow && server->focusedView)
                server->focusedView->layoutIndex = group;
            broadcastLayout(server);
        }
    }
    // Caps Lock / Num Lock went on or off: "event key caps-on" and so on,
    // for the on-screen indicator (many laptops have no LED for them). The
    // first state seen (Num Lock switched on at start) is not announced.
    if (!keyboard->ownKeymap && keyboard->keyboard->xkb_state) {
        struct xkb_state *state = keyboard->keyboard->xkb_state;
        int locks = (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CAPS,
                                                  XKB_STATE_MODS_LOCKED) > 0 ? 1 : 0)
                    | (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_NUM,
                                                    XKB_STATE_MODS_LOCKED) > 0 ? 2 : 0);
        if (keyboard->lockKeysKnown && locks != keyboard->lockKeys && !server->locked
            && ipcHasWatchers(server)) {
            int changed = locks ^ keyboard->lockKeys;
            if (changed & 1)
                ipcBroadcast(server, (locks & 1) ? "event key caps-on" : "event key caps-off");
            if (changed & 2)
                ipcBroadcast(server, (locks & 2) ? "event key num-on" : "event key num-off");
        }
        keyboard->lockKeys = locks;
        keyboard->lockKeysKnown = true;
    }
    struct wlr_input_method_keyboard_grab_v2 *grab = keyboardImGrab(keyboard);
    if (grab) {
        wlr_input_method_keyboard_grab_v2_set_keyboard(grab, keyboard->keyboard);
        wlr_input_method_keyboard_grab_v2_send_modifiers(grab, &keyboard->keyboard->modifiers);
        return;
    }
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
                                       &keyboard->keyboard->modifiers);
}

// Shortcuts are named by Latin keys (Super+L, Super+E). On a Russian, Greek
// or Hebrew layout the same key gives another symbol and none of them
// matched: Super+L did nothing until the layout was switched back. For a
// symbol from another alphabet this returns what the key gives in the first
// layout that has a Latin symbol on it, or in a plain "us" layout when the
// user has no such layout at all. Other symbols are returned unchanged.
static xkb_keysym_t latinShortcutSym(struct Keyboard *keyboard, uint32_t keycode,
                                     xkb_keysym_t sym)
{
    // Legacy keysyms of other alphabets, and the Unicode keysyms. Function,
    // keypad and XF86 keys are outside both ranges.
    bool foreign = (sym >= 0x100 && sym < 0xfd00) || (sym >= 0x01000100 && sym <= 0x0110ffff);
    if (!foreign)
        return sym;
    struct xkb_keymap *keymap = keyboard->keyboard->keymap;
    const xkb_keysym_t *syms = NULL;
    if (keymap) {
        xkb_layout_index_t count = xkb_keymap_num_layouts_for_key(keymap, keycode);
        for (xkb_layout_index_t layout = 0; layout < count; ++layout) {
            if (xkb_keymap_key_get_syms_by_level(keymap, keycode, layout, 0, &syms) > 0
                && syms[0] >= 0x20 && syms[0] <= 0x7e)
                return syms[0];
        }
    }
    static struct xkb_keymap *us;
    static bool usTried;
    if (!usTried) {
        usTried = true;
        struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (context) {
            const struct xkb_rule_names names = {.layout = "us"};
            us = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
            xkb_context_unref(context);
        }
    }
    if (us && xkb_keymap_key_get_syms_by_level(us, keycode, 0, 0, &syms) > 0
        && syms[0] >= 0x20 && syms[0] <= 0x7e)
        return syms[0];
    return sym;
}

static void keyboardKey(struct wl_listener *listener, void *data)
{
    struct Keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct wlr_keyboard_key_event *event = data;
    struct Server *server = keyboard->server;

    wlr_seat_set_keyboard(server->seat, keyboard->keyboard);
    wlr_log(WLR_DEBUG, "key %u state %d focused=%p", event->keycode,
            (int)event->state,
            (void *)server->seat->keyboard_state.focused_surface);

    notifyActivity(server);
    // While locked, keys always go to a lock surface: if the locker's
    // window has not got the keyboard yet (it mapped late, focus was lost),
    // the first key press hands it over instead of typing into nothing.
    if (server->locked) {
        struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
        if (!focused || !wlr_session_lock_surface_v1_try_from_wlr_surface(focused)) {
            struct LockSurface *lock;
            wl_list_for_each(lock, &server->lockSurfaces, link) {
                if (lock->lockSurface->surface && lock->lockSurface->surface->mapped) {
                    lockFocusSurface(server, lock->lockSurface->surface);
                    break;
                }
            }
        }
    }
    xkb_keysym_t sym = xkb_state_key_get_one_sym(
        keyboard->keyboard->xkb_state, event->keycode + 8);
    bool isLogo = sym == XKB_KEY_Super_L || sym == XKB_KEY_Super_R;
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->keyboard);
        // Super pressed on its own starts a tap; anything else cancels it.
        // Lock modifiers (Caps, Num) do not count as a chord.
        server->logoTap = isLogo && !shortcutsInhibited(server)
            && !(modifiers & (WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL
                              | WLR_MODIFIER_ALT));
        // The key's first-level symbol in the active layout.
        xkb_keysym_t baseSym = XKB_KEY_NoSymbol;
        struct xkb_state *xkb = keyboard->keyboard->xkb_state;
        if (xkb) {
            const xkb_keysym_t *syms = NULL;
            xkb_layout_index_t layout = xkb_state_key_get_layout(xkb, event->keycode + 8);
            if (layout != XKB_LAYOUT_INVALID
                && xkb_keymap_key_get_syms_by_level(keyboard->keyboard->keymap, event->keycode + 8,
                                                    layout, 0, &syms) > 0)
                baseSym = syms[0];
        }
        if (keyHandled(server, sym, modifiers, baseSym))
            return;
        // The same key by its Latin name, for the other layouts. Only with
        // Super, Ctrl or Alt held: plain typing is never a shortcut.
        if (modifiers & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT)) {
            xkb_keysym_t latin = latinShortcutSym(keyboard, event->keycode + 8, sym);
            if (latin != sym && keyHandled(server, latin, modifiers, baseSym))
                return;
        }
    } else if (isLogo && server->logoTap) {
        server->logoTap = false;
        if (!server->switching && !server->locked)
            shellAction("toggleStartMenu");
    }

    // An input method that grabbed the keyboard composes the text itself
    // (shortcuts above still win); it types through its virtual keyboard.
    struct wlr_input_method_keyboard_grab_v2 *grab = keyboardImGrab(keyboard);
    if (grab) {
        wlr_input_method_keyboard_grab_v2_set_keyboard(grab, keyboard->keyboard);
        wlr_input_method_keyboard_grab_v2_send_key(grab, event->time_msec, event->keycode,
                                                   event->state);
        return;
    }
    wlr_seat_keyboard_notify_key(server->seat, event->time_msec,
                                 event->keycode, event->state);
}

// The seat offers what is plugged in. Setting POINTER alone when a mouse
// arrived (USB, Bluetooth, a virtual pointer from wayvnc or ydotool) used
// to drop KEYBOARD, and wlroots then disables every client's keyboard.
static void updateSeatCapabilities(struct Server *server)
{
    uint32_t capabilities = WL_SEAT_CAPABILITY_POINTER; // the cursor is always there
    if (!wl_list_empty(&server->keyboards))
        capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
    if (!wl_list_empty(&server->touchDevices))
        capabilities |= WL_SEAT_CAPABILITY_TOUCH;
    wlr_seat_set_capabilities(server->seat, capabilities);
}

static void keyboardDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
    struct Server *server = keyboard->server;
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    free(keyboard);
    if (server->running)
        updateSeatCapabilities(server);
}


static void addKeyboard(struct Server *server, struct wlr_keyboard *wlrKeyboard,
                        bool hasKeymap)
{
    struct Keyboard *keyboard = xcalloc(1, sizeof(*keyboard));
    keyboard->server = server;
    keyboard->keyboard = wlrKeyboard;

    keyboard->ownKeymap = hasKeymap;
    struct xkb_keymap *keymap = hasKeymap ? NULL : buildKeymap(server);
    if (keymap && server->layoutCount == 0)
        updateLayoutNames(server, keymap);
    configureKeyboard(server, keyboard, keymap);
    if (keymap)
        xkb_keymap_unref(keymap);

    keyboard->modifiers.notify = keyboardModifiers;
    wl_signal_add(&wlrKeyboard->events.modifiers, &keyboard->modifiers);
    keyboard->key.notify = keyboardKey;
    wl_signal_add(&wlrKeyboard->events.key, &keyboard->key);
    keyboard->destroy.notify = keyboardDestroy;
    wl_signal_add(&wlrKeyboard->base.events.destroy, &keyboard->destroy);

    wl_list_insert(&server->keyboards, &keyboard->link);
    wlr_seat_set_keyboard(server->seat, wlrKeyboard);
    updateSeatCapabilities(server);
    // A keyboard plugin (or a virtual keyboard) can appear after a layer
    // surface already owns the keyboard. Send it the keymap and enter event,
    // otherwise it sees no keys until focus moves.
    struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
    if (!focused && server->focusedLayer && server->focusedLayer->layer)
        focused = server->focusedLayer->layer->surface;
    if (!focused && server->focusedView)
        focused = viewSurface(server->focusedView);
    if (focused) {
        wlr_seat_keyboard_notify_enter(server->seat, focused,
                                       wlrKeyboard->keycodes,
                                       wlrKeyboard->num_keycodes,
                                       &wlrKeyboard->modifiers);
    }
}

static void pointerDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Pointer *pointer = wl_container_of(listener, pointer, destroy);
    wl_list_remove(&pointer->destroy.link);
    wl_list_remove(&pointer->link);
    free(pointer);
}

static void touchDeviceDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct TouchDevice *touch = wl_container_of(listener, touch, destroy);
    struct Server *server = touch->server;
    wl_list_remove(&touch->destroy.link);
    wl_list_remove(&touch->link);
    free(touch);
    if (server->running)
        updateSeatCapabilities(server);
}

// A finger touches down: the surface under it gets the touch point, and its
// window the focus, as a click would. Shell cards close on a touch outside.
// A press or release of the left button on behalf of a finger.
static void touchPointerButton(struct Server *server, uint32_t time, bool pressed)
{
    struct wlr_pointer_button_event click = {0};
    click.time_msec = time;
    click.button = BTN_LEFT;
    click.state = pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED;
    cursorButton(&server->cursorButton, &click);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void handleTouchDown(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, touchDown);
    struct wlr_touch_down_event *event = data;
    notifyActivity(server);
    double lx = 0;
    double ly = 0;
    wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base, event->x, event->y,
                                         &lx, &ly);
    if (!server->locked) {
        char line[64];
        snprintf(line, sizeof(line), "event click %d %d", (int)lx, (int)ly);
        ipcBroadcast(server, line);
    }
    // A finger whose release never arrived (the screen was unplugged, the
    // touch was cancelled) must not hold the role for ever.
    if (server->touchPointerId >= 0
        && (event->touch_id == server->touchPointerId
            || (!server->touchEmulated && wlr_seat_touch_num_points(server->seat) == 0))) {
        const bool emulated = server->touchEmulated;
        server->touchPointerId = -1;
        server->touchEmulated = false;
        server->touchDragging = false;
        if (emulated)
            touchPointerButton(server, event->time_msec, false);
    }
    struct Hit hit = hitTest(server, lx, ly);
    const bool free = server->touchPointerId < 0 && server->cursorMode == CURSOR_PASSTHROUGH;
    if (!server->locked && !hit.surface && hit.view && hit.frame != FRAME_NONE && free) {
        // A title bar or frame drawn by the compositor has no surface to
        // send the touch to: the finger clicks and drags it as the mouse
        // would (close, maximize, move, resize). It used to do nothing.
        server->touchPointerId = event->touch_id;
        server->touchEmulated = true;
        server->touchDragging = false;
        wlr_cursor_warp_closest(server->cursor, NULL, lx, ly);
        processCursorMotion(server, event->time_msec);
        touchPointerButton(server, event->time_msec, true);
        return;
    }
    if (!server->locked && hit.view && !hit.view->overrideRedirect)
        focusView(hit.view, NULL);
    if (hit.surface) {
        if (free && !server->locked) {
            // Remembered in case the window starts a move from this touch
            // (a finger on a header bar the application draws itself).
            server->touchPointerId = event->touch_id;
            server->touchEmulated = false;
            server->touchDragging = false;
            server->touchX = lx;
            server->touchY = ly;
        }
        wlr_seat_touch_notify_down(server->seat, hit.surface, event->time_msec, event->touch_id,
                                   hit.sx, hit.sy);
    }
}

static void handleTouchMotion(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, touchMotion);
    struct wlr_touch_motion_event *event = data;
    notifyActivity(server);
    double lx = 0;
    double ly = 0;
    wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base, event->x, event->y,
                                         &lx, &ly);
    if (event->touch_id == server->touchPointerId) {
        const bool grabbing = server->cursorMode != CURSOR_PASSTHROUGH && server->grabbedView;
        if (!server->touchEmulated && grabbing && !server->touchDragging) {
            // The window asked to be moved or resized from this touch. The
            // grab was measured from the mouse pointer, which is somewhere
            // else: measure it from the finger instead.
            server->touchDragging = true;
            if (server->cursorMode == CURSOR_MOVE) {
                server->grabX = server->touchX - server->grabbedView->x;
                server->grabY = server->touchY - server->grabbedView->y;
            } else {
                resizeOffsetsFrom(server, server->touchX, server->touchY);
            }
        }
        if (server->touchEmulated || server->touchDragging) {
            wlr_cursor_warp_closest(server->cursor, NULL, lx, ly);
            processCursorMotion(server, event->time_msec);
            return;
        }
        server->touchX = lx;
        server->touchY = ly;
    }
    struct wlr_touch_point *point = wlr_seat_touch_get_point(server->seat, event->touch_id);
    if (!point || !point->surface)
        return;
    // Coordinates stay relative to the surface the finger went down on.
    int originX = 0;
    int originY = 0;
    if (!surfaceLayoutPosition(server, point->surface, &originX, &originY))
        return;
    wlr_seat_touch_notify_motion(server->seat, event->time_msec, event->touch_id, lx - originX,
                                 ly - originY);
}

static void handleTouchUp(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, touchUp);
    struct wlr_touch_up_event *event = data;
    notifyActivity(server);
    if (event->touch_id == server->touchPointerId) {
        const bool emulated = server->touchEmulated;
        const bool dragging = server->touchDragging;
        server->touchPointerId = -1;
        server->touchEmulated = false;
        server->touchDragging = false;
        if (emulated) {
            touchPointerButton(server, event->time_msec, false);
            return;
        }
        if (dragging) {
            // Ends the move (with edge snapping); the application never saw
            // a button press, so it gets no release either.
            server->superGrab = true;
            touchPointerButton(server, event->time_msec, false);
        }
    }
    if (wlr_seat_touch_get_point(server->seat, event->touch_id))
        wlr_seat_touch_notify_up(server->seat, event->time_msec, event->touch_id);
}

static void handleTouchFrame(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Server *server = wl_container_of(listener, server, touchFrame);
    wlr_seat_touch_notify_frame(server->seat);
}

// A pen on a graphics tablet acts as a mouse: it moves the pointer to the
// point it hovers over, and touching the surface is a left click through
// the same path as a real click (focus, title bars, shell cards).
// Pressure and tilt need tablet-v2, which is not offered yet.
static void handleTabletAxis(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, tabletAxis);
    struct wlr_tablet_tool_axis_event *event = data;
    const double x = (event->updated_axes & WLR_TABLET_TOOL_AXIS_X) ? event->x : NAN;
    const double y = (event->updated_axes & WLR_TABLET_TOOL_AXIS_Y) ? event->y : NAN;
    if (isnan(x) && isnan(y))
        return;
    notifyActivity(server);
    wlr_cursor_warp_absolute(server->cursor, &event->tablet->base, x, y);
    processCursorMotion(server, event->time_msec);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void handleTabletTip(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, tabletTip);
    struct wlr_tablet_tool_tip_event *event = data;
    wlr_cursor_warp_absolute(server->cursor, &event->tablet->base, event->x, event->y);
    processCursorMotion(server, event->time_msec);
    struct wlr_pointer_button_event click = {0};
    click.time_msec = event->time_msec;
    click.button = BTN_LEFT;
    click.state = event->state == WLR_TABLET_TOOL_TIP_DOWN ? WL_POINTER_BUTTON_STATE_PRESSED
                                                           : WL_POINTER_BUTTON_STATE_RELEASED;
    cursorButton(&server->cursorButton, &click);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void newInput(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newInput);
    struct wlr_input_device *device = data;

    if (device->type == WLR_INPUT_DEVICE_KEYBOARD) {
        addKeyboard(server, wlr_keyboard_from_input_device(device), false);
    } else if (device->type == WLR_INPUT_DEVICE_POINTER) {
        wlr_cursor_attach_input_device(server->cursor, device);
        updateSeatCapabilities(server);
        struct Pointer *pointer = xcalloc(1, sizeof(*pointer));
        pointer->server = server;
        pointer->device = device;
        pointer->destroy.notify = pointerDestroy;
        wl_signal_add(&device->events.destroy, &pointer->destroy);
        wl_list_insert(&server->pointers, &pointer->link);
        configurePointer(server, device);
    } else if (device->type == WLR_INPUT_DEVICE_TOUCH) {
        // Touch points are mapped over the whole layout like the pointer.
        wlr_cursor_attach_input_device(server->cursor, device);
        struct TouchDevice *touch = xcalloc(1, sizeof(*touch));
        touch->server = server;
        touch->destroy.notify = touchDeviceDestroy;
        wl_signal_add(&device->events.destroy, &touch->destroy);
        wl_list_insert(&server->touchDevices, &touch->link);
        updateSeatCapabilities(server);
        wlr_log(WLR_INFO, "touchscreen connected: %s", device->name ? device->name : "?");
    } else if (device->type == WLR_INPUT_DEVICE_TABLET) {
        wlr_cursor_attach_input_device(server->cursor, device);
        wlr_log(WLR_INFO, "graphics tablet connected: %s", device->name ? device->name : "?");
    }
}

static void newVirtualKeyboard(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server,
                                            newVirtualKeyboard);
    struct wlr_virtual_keyboard_v1 *virtual = data;
    addKeyboard(server, &virtual->keyboard, virtual->has_keymap);
    wlr_log(WLR_DEBUG, "virtual keyboard connected");
}

static void newVirtualPointer(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server,
                                            newVirtualPointer);
    struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
    wlr_cursor_attach_input_device(server->cursor, &event->new_pointer->pointer.base);
    updateSeatCapabilities(server);
    wlr_log(WLR_DEBUG, "virtual pointer connected");
}

// ---------------------------------------------------------------------------
// XDG shell
// ---------------------------------------------------------------------------

static void popupCommit(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Popup *popup = wl_container_of(listener, popup, commit);
    if (!popup->popup->base->initialized) {
        wlr_log(WLR_DEBUG, "popup commit before initialization ignored");
        return;
    }
    if (popup->popup->base->initial_commit)
        wlr_xdg_surface_schedule_configure(popup->popup->base);
}

static void popupDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Popup *popup = wl_container_of(listener, popup, destroy);
    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);
    free(popup);
}

static void handleNewPopup(struct wl_listener *listener, void *data)
{
    (void)listener;
    struct wlr_xdg_popup *popup = data;
    if (!popup->parent)
        return;

    struct wlr_scene_tree *parentTree = NULL;
    struct wlr_xdg_surface *xdgParent =
        wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    if (xdgParent) {
        parentTree = xdgParent->data;
    } else {
        struct wlr_layer_surface_v1 *layerParent =
            wlr_layer_surface_v1_try_from_wlr_surface(popup->parent);
        if (layerParent)
            parentTree = layerParent->surface->data;
    }
    if (!parentTree)
        return;

    popup->base->data = wlr_scene_xdg_surface_create(parentTree, popup->base);

    struct Popup *holder = xcalloc(1, sizeof(*holder));
    holder->popup = popup;
    holder->commit.notify = popupCommit;
    wl_signal_add(&popup->base->surface->events.commit, &holder->commit);
    holder->destroy.notify = popupDestroy;
    wl_signal_add(&popup->events.destroy, &holder->destroy);
}

static void handleXdgCommit(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, commit);
    ++view->commits;
    wlr_log(WLR_DEBUG, "xdg commit %s initial=%d initialized=%d",
            viewTitle(view), view->xdg->base->initial_commit,
            view->xdg->base->initialized);
    if (view->xdg && view->xdg->base->initial_commit
        && view->xdg->base->initialized) {
        // xdg-shell requires the compositor to answer the first commit with a
        // configure event; otherwise the client never maps its window.
        applyDecorationMode(view);
        wlr_xdg_toplevel_set_size(view->xdg, 0, 0);
        wlr_log(WLR_DEBUG, "sent initial xdg configure for %s", viewTitle(view));
    }
    // The client may have resized itself: keep the frame around it.
    if (view->mapped)
        viewUpdateDecoration(view);
    if (view->hung) {
        view->hung = false;
        char line[64];
        snprintf(line, sizeof(line), "event responsive %u", view->id);
        ipcBroadcast(view->server, line);
    }
}

static void handleXdgMap(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, map);
    view->mapped = true;
    // A window maps on the desktop that is shown when it appears.
    view->workspace = view->server->workspace;
    viewSyncVisible(view);
    viewAnimate(view, 0.94f, 0.0f, 200);
    broadcastWorkspaces(view->server);
    wlr_log(WLR_DEBUG, "xdg window mapped: %s", viewTitle(view));
    centerView(view);
    wlr_log(WLR_DEBUG, "xdg window at %d,%d %dx%d", view->x, view->y,
            view->width, view->height);
    ensureForeignToplevel(view);
    if (view->handle && !wl_list_empty(&view->server->outputs)) {
        struct Output *output = wl_container_of(view->server->outputs.next,
                                                output, link);
        wlr_foreign_toplevel_handle_v1_output_enter(view->handle, output->output);
    }
    viewRaise(view);
    // A newly opened application or dialog takes focus. This also deactivates
    // an open menu layer, which closes itself when it loses focus.
    focusView(view, NULL);
    applyWindowRules(view);
    // Asked for before the window existed (mpv --fs, a browser that was
    // closed maximized): those requests arrive before the first configure,
    // when nothing can be resized yet, and used to be lost.
    if (view->xdg->requested.fullscreen && !view->fullscreen)
        viewSetFullscreen(view, true);
    else if (view->xdg->requested.maximized && !view->maximized && !view->fullscreen)
        viewSetMaximized(view, true);
    updateForeignToplevel(view);
}

static void handleXdgUnmap(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, unmap);
    wlr_log(WLR_DEBUG, "xdg window unmapped: %s", viewTitle(view));
    if (view->mapped && !view->minimized && viewOnWorkspace(view))
        createGhost(view, GHOST_CLOSE);
    view->animating = false;
    view->mapped = false;
    viewSyncVisible(view);
    if (view->server->switchTarget == view)
        switcherCancel(view->server);
    if (view->server->focusedView == view) {
        view->server->focusedView = NULL;
        viewSetActivated(view, false);
        restoreFocus(view->server);
    }
    broadcastWorkspaces(view->server);
    destroyForeignToplevel(view);
    // A hidden fullscreen window gives the panel back.
    updateFullscreenLayers(view->server);
    broadcastFullscreen(view->server, false);
}

static void handleXdgDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, destroy);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->requestMove.link);
    wl_list_remove(&view->requestResize.link);
    wl_list_remove(&view->requestMaximize.link);
    wl_list_remove(&view->requestFullscreen.link);
    wl_list_remove(&view->requestMinimize.link);
    wl_list_remove(&view->requestShowMenu.link);
    wl_list_remove(&view->setTitle.link);
    wl_list_remove(&view->setAppId.link);
    wl_list_remove(&view->pingTimeout.link);
    // The scene helper owns this tree and destroys it with the xdg surface.
    view->tree = NULL;
    destroyView(view);
}

// The distance between a point (the pointer, a finger) and the edges being
// resized. A window resized by its own border is grabbed a few pixels
// outside its geometry (the invisible margin around it); without this the
// edge jumped to the pointer on the first motion.
static void resizeOffsetsFrom(struct Server *server, double x, double y)
{
    const uint32_t edges = server->resizeEdges;
    server->resizeOffsetX = (edges & WLR_EDGE_LEFT) ? x - server->grabX
                          : (edges & WLR_EDGE_RIGHT) ? x - (server->grabX + server->grabWidth)
                          : 0;
    server->resizeOffsetY = (edges & WLR_EDGE_TOP) ? y - server->grabY
                          : (edges & WLR_EDGE_BOTTOM) ? y - (server->grabY + server->grabHeight)
                          : 0;
}

// A window may start moving or resizing itself only in answer to a press:
// with a button (or a finger) down, on a session that is not locked, and
// not while it fills the screen. Without this any client could take the
// pointer whenever it liked and drag itself around until the next click.
static bool interactiveGrabAllowed(struct View *view)
{
    struct Server *server = view->server;
    if (server->locked || !view->mapped || view->fullscreen
        || server->cursorMode != CURSOR_PASSTHROUGH)
        return false;
    return server->seat->pointer_state.button_count > 0
        || wlr_seat_touch_num_points(server->seat) > 0;
}

// Dragging a maximized window by its own header bar restores it under the
// pointer, as dragging a title bar drawn by the compositor does.
static void restoreForDrag(struct View *view)
{
    struct Server *server = view->server;
    if (!view->maximized)
        return;
    double ratio = view->width > 0 ? (server->cursor->x - view->x) / view->width : 0.5;
    int offsetY = (int)server->cursor->y - view->y;
    viewSetMaximized(view, false);
    viewSetPosition(view, (int)(server->cursor->x - ratio * view->width),
                    (int)server->cursor->y - offsetY);
}

static void handleXdgRequestMove(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, requestMove);
    if (!interactiveGrabAllowed(view))
        return;
    restoreForDrag(view);
    viewClearSnap(view);
    view->server->grabbedView = view;
    view->server->cursorMode = CURSOR_MOVE;
    view->server->grabX = view->server->cursor->x - view->x;
    view->server->grabY = view->server->cursor->y - view->y;
}

static void handleXdgRequestResize(struct wl_listener *listener, void *data)
{
    struct View *view = wl_container_of(listener, view, requestResize);
    if (!interactiveGrabAllowed(view) || view->maximized)
        return;
    viewClearSnap(view);
    struct wlr_xdg_toplevel_resize_event *event = data;
    view->server->grabbedView = view;
    view->server->cursorMode = CURSOR_RESIZE;
    view->server->resizeEdges = event->edges;
    // The size the window really has: it may have resized itself since the
    // compositor last asked for one.
    struct wlr_box geometry = viewGeometry(view);
    if (geometry.width > 0 && geometry.height > 0) {
        view->width = geometry.width;
        view->height = geometry.height;
    }
    view->server->grabWidth = view->width;
    view->server->grabHeight = view->height;
    view->server->grabX = view->x;
    view->server->grabY = view->y;
    resizeOffsetsFrom(view->server, view->server->cursor->x, view->server->cursor->y);
}

static void handleXdgRequestMaximize(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, requestMaximize);
    // What the client asked for, not a toggle: unset_maximized on a window
    // that is not maximized used to maximize it. Before the first configure
    // the request is kept by wlroots and applied when the window maps.
    if (!view->xdg->base->initialized)
        return;
    if (view->xdg->requested.maximized != view->maximized)
        viewSetMaximized(view, view->xdg->requested.maximized);
    // xdg-shell wants an answer even when nothing changes.
    wlr_xdg_surface_schedule_configure(view->xdg->base);
}

static void handleXdgRequestFullscreen(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, requestFullscreen);
    if (!view->xdg->base->initialized)
        return;
    if (view->xdg->requested.fullscreen != view->fullscreen)
        viewSetFullscreen(view, view->xdg->requested.fullscreen);
    wlr_xdg_surface_schedule_configure(view->xdg->base);
}

static void handleXdgRequestMinimize(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, requestMinimize);
    if (view->mapped && view->xdg->requested.minimized)
        viewSetMinimized(view, true);
}

// GTK header bars and other client-side title bars ask for the window menu
// on a right click; x/y are relative to the surface.
static void handleXdgRequestWindowMenu(struct wl_listener *listener, void *data)
{
    struct View *view = wl_container_of(listener, view, requestShowMenu);
    struct wlr_xdg_toplevel_show_window_menu_event *event = data;
    struct wlr_box geometry = viewGeometry(view);
    requestWindowMenu(view, view->x - geometry.x + event->x,
                      view->y - geometry.y + event->y);
}

static void handleXdgSetTitle(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, setTitle);
    updateForeignToplevel(view);
}

static void handleXdgSetAppId(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, setAppId);
    updateForeignToplevel(view);
}

// xdg-decoration: what the window asked for, else the configured default.
static enum wlr_xdg_toplevel_decoration_v1_mode decorationModeFor(struct View *view)
{
    struct wlr_xdg_toplevel_decoration_v1 *decoration = view->xdgDecoration;
    if (decoration && decoration->requested_mode != WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE)
        return decoration->requested_mode;
    return view->server->config.decorationsServer
               ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
               : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
}

// The mode a window chose through the KDE protocol, or -1 without one.
static int kdeDecorationMode(struct View *view)
{
    struct wlr_surface *surface = viewSurface(view);
    struct KdeDecoration *kde;
    if (!surface)
        return -1;
    wl_list_for_each(kde, &view->server->kdeDecorations, link) {
        if (kde->decoration->surface == surface)
            return (int)kde->decoration->mode;
    }
    return -1;
}

static void applyDecorationMode(struct View *view)
{
    if (!view->xdg || !view->xdg->base->initialized)
        return;
    if (!view->xdgDecoration) {
        int kde = kdeDecorationMode(view);
        if (kde < 0)
            return;
        view->ssd = kde == WLR_SERVER_DECORATION_MANAGER_MODE_SERVER;
        viewUpdateDecoration(view);
        return;
    }
    enum wlr_xdg_toplevel_decoration_v1_mode mode = decorationModeFor(view);
    wlr_xdg_toplevel_decoration_v1_set_mode(view->xdgDecoration, mode);
    view->ssd = mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    viewUpdateDecoration(view);
}

static void handleXdgDecorationMode(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, xdgDecorationMode);
    applyDecorationMode(view);
}

static void handleXdgDecorationDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, xdgDecorationDestroy);
    wl_list_remove(&view->xdgDecorationMode.link);
    wl_list_remove(&view->xdgDecorationDestroy.link);
    view->xdgDecoration = NULL;
    view->ssd = false;
    // Only touch the scene while the window is mapped: during teardown the
    // tree may already be gone.
    if (view->mapped)
        viewUpdateDecoration(view);
}

static void kdeDecorationApply(struct KdeDecoration *kde)
{
    struct View *view;
    wl_list_for_each(view, &kde->server->views, link) {
        if (view->xdg && viewSurface(view) == kde->decoration->surface) {
            applyDecorationMode(view);
            return;
        }
    }
}

static void handleKdeDecorationMode(struct wl_listener *listener, void *data)
{
    (void)data;
    struct KdeDecoration *kde = wl_container_of(listener, kde, mode);
    kdeDecorationApply(kde);
}

static void handleKdeDecorationDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct KdeDecoration *kde = wl_container_of(listener, kde, destroy);
    struct Server *server = kde->server;
    struct wlr_surface *surface = kde->decoration->surface;
    wl_list_remove(&kde->mode.link);
    wl_list_remove(&kde->destroy.link);
    wl_list_remove(&kde->link);
    free(kde);
    struct View *view;
    wl_list_for_each(view, &server->views, link) {
        if (view->xdg && !view->xdgDecoration && view->mapped && viewSurface(view) == surface) {
            view->ssd = false;
            viewUpdateDecoration(view);
            break;
        }
    }
}

static void handleNewKdeDecoration(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newKdeDecoration);
    struct KdeDecoration *kde = xcalloc(1, sizeof(*kde));
    kde->server = server;
    kde->decoration = data;
    kde->mode.notify = handleKdeDecorationMode;
    wl_signal_add(&kde->decoration->events.mode, &kde->mode);
    kde->destroy.notify = handleKdeDecorationDestroy;
    wl_signal_add(&kde->decoration->events.destroy, &kde->destroy);
    wl_list_insert(&server->kdeDecorations, &kde->link);
    kdeDecorationApply(kde);
}

static void handleNewXdgDecoration(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newXdgDecoration);
    struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
    struct View *view;
    wl_list_for_each(view, &server->views, link) {
        if (view->xdg != decoration->toplevel || view->xdgDecoration)
            continue;
        view->xdgDecoration = decoration;
        view->xdgDecorationMode.notify = handleXdgDecorationMode;
        wl_signal_add(&decoration->events.request_mode, &view->xdgDecorationMode);
        view->xdgDecorationDestroy.notify = handleXdgDecorationDestroy;
        wl_signal_add(&decoration->events.destroy, &view->xdgDecorationDestroy);
        // Before the first commit the mode goes out with the initial
        // configure (handleXdgCommit).
        applyDecorationMode(view);
        return;
    }
}

// The client did not answer a ping (sent when it is asked to close or
// gets focus): tell the shell, which offers to end it.
static void handleXdgPingTimeout(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, pingTimeout);
    if (view->hung)
        return;
    view->hung = true;
    char line[64];
    snprintf(line, sizeof(line), "event hung %u", view->id);
    ipcBroadcast(view->server, line);
}

static void handleNewXdgToplevel(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newXdgToplevel);
    struct wlr_xdg_toplevel *toplevel = data;
    wlr_log(WLR_DEBUG, "new xdg toplevel: %s (%s)",
            toplevel->title ? toplevel->title : "(untitled)",
            toplevel->app_id ? toplevel->app_id : "(no app id)");

    struct View *view = xcalloc(1, sizeof(*view));
    view->server = server;
    view->workspace = server->workspace;
    view->xdg = toplevel;
    view->tree = wlr_scene_xdg_surface_create(server->toplevelTree, toplevel->base);
    if (!view->tree) {
        free(view);
        return;
    }
    toplevel->base->data = view->tree;
    view->tree->node.data = view;
    view->id = ++server->nextViewId;

    wl_list_insert(&server->views, &view->link);

    view->commit.notify = handleXdgCommit;
    wl_signal_add(&toplevel->base->surface->events.commit, &view->commit);
    view->map.notify = handleXdgMap;
    wl_signal_add(&toplevel->base->surface->events.map, &view->map);
    view->unmap.notify = handleXdgUnmap;
    wl_signal_add(&toplevel->base->surface->events.unmap, &view->unmap);
    view->destroy.notify = handleXdgDestroy;
    wl_signal_add(&toplevel->events.destroy, &view->destroy);
    view->requestMove.notify = handleXdgRequestMove;
    wl_signal_add(&toplevel->events.request_move, &view->requestMove);
    view->requestResize.notify = handleXdgRequestResize;
    wl_signal_add(&toplevel->events.request_resize, &view->requestResize);
    view->requestMaximize.notify = handleXdgRequestMaximize;
    wl_signal_add(&toplevel->events.request_maximize, &view->requestMaximize);
    view->requestFullscreen.notify = handleXdgRequestFullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &view->requestFullscreen);
    view->requestMinimize.notify = handleXdgRequestMinimize;
    wl_signal_add(&toplevel->events.request_minimize, &view->requestMinimize);
    view->requestShowMenu.notify = handleXdgRequestWindowMenu;
    wl_signal_add(&toplevel->events.request_show_window_menu, &view->requestShowMenu);
    view->setTitle.notify = handleXdgSetTitle;
    wl_signal_add(&toplevel->events.set_title, &view->setTitle);
    view->setAppId.notify = handleXdgSetAppId;
    wl_signal_add(&toplevel->events.set_app_id, &view->setAppId);
    view->pingTimeout.notify = handleXdgPingTimeout;
    wl_signal_add(&toplevel->base->events.ping_timeout, &view->pingTimeout);
}

// ---------------------------------------------------------------------------
// Output configuration: saved layouts and wlr-output-management
// ---------------------------------------------------------------------------

#define NT_MAX_SAVED_OUTPUTS 32

// Monitors are remembered by make/model/serial, so a laptop panel and an
// external screen keep their settings whatever connector they appear on.
static void outputKey(struct wlr_output *output, char *key, size_t size)
{
    const char *make = output->make ? output->make : "";
    const char *model = output->model ? output->model : "";
    const char *serial = output->serial ? output->serial : "";
    if (make[0] == '\0' && model[0] == '\0' && serial[0] == '\0')
        snprintf(key, size, "%s", output->name);
    else
        snprintf(key, size, "%s %s %s", make, model, serial);
}

static void outputsConfigPath(char *path, size_t size)
{
    ntConfigPath(path, size, "outputs.conf");
}

static bool findSavedOutput(struct wlr_output *output, struct SavedOutput *found)
{
    struct SavedOutput saved[NT_MAX_SAVED_OUTPUTS];
    char path[PATH_MAX];
    outputsConfigPath(path, sizeof(path));
    int count = ntOutputsLoad(path, saved, NT_MAX_SAVED_OUTPUTS);
    char key[NT_CONFIG_TEXT];
    outputKey(output, key, sizeof(key));
    for (int i = 0; i < count; ++i) {
        if (strcmp(saved[i].key, key) == 0) {
            *found = saved[i];
            return true;
        }
    }
    return false;
}

static struct Output *outputFromWlr(struct Server *server, struct wlr_output *wlrOutput)
{
    struct Output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output->output == wlrOutput)
            return output;
    }
    return NULL;
}

// Writes the current state of every connected output into outputs.conf,
// keeping the entries of monitors that are not connected right now.
static void saveOutputs(struct Server *server)
{
    struct SavedOutput saved[NT_MAX_SAVED_OUTPUTS];
    char path[PATH_MAX];
    outputsConfigPath(path, sizeof(path));
    int count = ntOutputsLoad(path, saved, NT_MAX_SAVED_OUTPUTS);

    struct Output *output;
    wl_list_for_each(output, &server->outputs, link) {
        struct wlr_output *o = output->output;
        struct SavedOutput entry = {0};
        outputKey(o, entry.key, sizeof(entry.key));
        entry.enabled = !output->disabled;
        entry.width = o->width;
        entry.height = o->height;
        entry.refresh = o->refresh;
        if (o->current_mode) {
            entry.width = o->current_mode->width;
            entry.height = o->current_mode->height;
            entry.refresh = o->current_mode->refresh;
        }
        struct wlr_box box = {0};
        wlr_output_layout_get_box(server->outputLayout, o, &box);
        entry.x = box.x;
        entry.y = box.y;
        entry.scale = o->scale;
        entry.transform = (int)o->transform;

        int slot = -1;
        for (int i = 0; i < count; ++i) {
            if (strcmp(saved[i].key, entry.key) == 0) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            if (count < NT_MAX_SAVED_OUTPUTS) {
                slot = count++;
            } else {
                memmove(&saved[0], &saved[1], sizeof(saved[0]) * (NT_MAX_SAVED_OUTPUTS - 1));
                slot = NT_MAX_SAVED_OUTPUTS - 1;
            }
        }
        if (!entry.enabled && slot < count && saved[slot].width > 0) {
            // A disabled output keeps the mode it had while enabled.
            entry.width = saved[slot].width;
            entry.height = saved[slot].height;
            entry.refresh = saved[slot].refresh;
        }
        saved[slot] = entry;
    }
    if (!ntOutputsSave(path, saved, count))
        wlr_log(WLR_ERROR, "could not write %s", path);
}

static struct wlr_output_mode *findMode(struct wlr_output *output, int width,
                                        int height, int refresh)
{
    struct wlr_output_mode *best = NULL;
    struct wlr_output_mode *mode;
    wl_list_for_each(mode, &output->modes, link) {
        if (mode->width != width || mode->height != height)
            continue;
        if (refresh <= 0)
            return mode;
        if (abs(mode->refresh - refresh) <= 500)
            return mode;
        if (!best || abs(mode->refresh - refresh) < abs(best->refresh - refresh))
            best = mode;
    }
    return best;
}

// The scale a new monitor starts with: 2 on high-density panels, as GNOME
// and KDE do, 1 everywhere else (and in nested/headless sessions).
static float automaticScale(struct wlr_output *output, int width, int height)
{
    if (output->phys_width <= 0 || output->phys_height <= 0)
        return 1.0f;
    double dpi = width / (output->phys_width / 25.4);
    if (dpi >= 192.0 && height >= 1200)
        return 2.0f;
    return 1.0f;
}

static void outputSceneDestroy(struct wl_listener *listener, void *data);

// Puts an output into the layout (and the scene) or takes it out.
static void placeOutput(struct Server *server, struct Output *output,
                        bool enabled, bool automatic, int x, int y)
{
    if (!enabled) {
        wlr_output_layout_remove(server->outputLayout, output->output);
        return;
    }
    bool wasPlaced = wlr_output_layout_get(server->outputLayout, output->output) != NULL;
    struct wlr_output_layout_output *layoutOutput = automatic
        ? wlr_output_layout_add_auto(server->outputLayout, output->output)
        : wlr_output_layout_add(server->outputLayout, output->output, x, y);
    if (!layoutOutput)
        return;
    // Moving an output that is already placed keeps its scene association.
    if (wasPlaced && output->sceneOutput)
        return;
    if (!output->sceneOutput) {
        output->sceneOutput = wlr_scene_output_create(server->scene, output->output);
        if (!output->sceneOutput)
            return;
        output->sceneDestroy.notify = outputSceneDestroy;
        wl_signal_add(&output->sceneOutput->events.destroy, &output->sceneDestroy);
    }
    wlr_scene_output_layout_add_output(server->sceneLayout, layoutOutput,
                                       output->sceneOutput);
}

static void updateOutputManager(struct Server *server)
{
    if (!server->outputManager)
        return;
    struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
    if (!config)
        return;
    struct Output *output;
    wl_list_for_each(output, &server->outputs, link) {
        struct wlr_output_configuration_head_v1 *head =
            wlr_output_configuration_head_v1_create(config, output->output);
        if (!head)
            continue;
        head->state.enabled = !output->disabled;
        struct wlr_box box = {0};
        wlr_output_layout_get_box(server->outputLayout, output->output, &box);
        head->state.x = box.x;
        head->state.y = box.y;
    }
    wlr_output_manager_v1_set_configuration(server->outputManager, config);
}

static void arrangeAllOutputs(struct Server *server)
{
    struct Output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output->disabled)
            continue;
        struct wlr_box box = {0};
        wlr_output_layout_get_box(server->outputLayout, output->output, &box);
        // Not in the layout (on its way out): nothing to arrange on it.
        if (wlr_box_empty(&box))
            continue;
        output->usable = box;
        arrangeOutput(server, output);
    }
}

// Windows left on a monitor that went away come back to a visible one.
static void rescueViews(struct Server *server)
{
    struct View *view;
    wl_list_for_each(view, &server->views, link) {
        if (!view->mapped || view->overrideRedirect)
            continue;
        double cx = view->x + view->width / 2.0;
        double cy = view->y + view->height / 2.0;
        if (wlr_output_layout_output_at(server->outputLayout, cx, cy))
            continue;
        struct Output *output;
        wl_list_for_each(output, &server->outputs, link) {
            if (output->disabled || wlr_box_empty(&output->usable))
                continue;
            // A window that filled its old screen fills the new one.
            if (view->fullscreen) {
                struct wlr_box full = output->usable;
                wlr_output_layout_get_box(server->outputLayout, output->output, &full);
                viewSetPosition(view, full.x, full.y);
                viewSetSize(view, full.width, full.height);
            } else if (view->maximized || view->snapEdges) {
                struct wlr_box box = frameContentBox(
                    view, view->maximized ? output->usable
                                          : snapBox(output->usable, view->snapEdges));
                viewSetPosition(view, box.x, box.y);
                viewSetSize(view, box.width, box.height);
            } else {
                viewSetPosition(view, output->usable.x + 40, output->usable.y + 40);
            }
            break;
        }
    }
}

static bool applyOutputConfiguration(struct Server *server,
                                     struct wlr_output_configuration_v1 *config,
                                     bool testOnly)
{
    // Never let a configuration switch every monitor off.
    bool anyEnabled = false;
    struct wlr_output_configuration_head_v1 *head;
    wl_list_for_each(head, &config->heads, link) {
        if (head->state.enabled)
            anyEnabled = true;
    }
    if (!anyEnabled)
        return false;

    bool ok = true;
    wl_list_for_each(head, &config->heads, link) {
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_head_v1_state_apply(&head->state, &state);
        if (testOnly) {
            if (!wlr_output_test_state(head->state.output, &state))
                ok = false;
        } else if (wlr_output_commit_state(head->state.output, &state)) {
            struct Output *output = outputFromWlr(server, head->state.output);
            if (output) {
                output->disabled = !head->state.enabled;
                output->poweredOff = false;
                placeOutput(server, output, head->state.enabled, false,
                            head->state.x, head->state.y);
                if (head->state.enabled) {
                    wlr_xcursor_manager_load(server->cursorManager,
                                             head->state.output->scale);
                }
            }
        } else {
            ok = false;
        }
        wlr_output_state_finish(&state);
    }
    if (!testOnly) {
        arrangeAllOutputs(server);
        rescueViews(server);
        updateOutputManager(server);
        if (ok)
            saveOutputs(server);
    }
    return ok;
}

static void handleOutputManagerApply(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, outputManagerApply);
    struct wlr_output_configuration_v1 *config = data;
    if (applyOutputConfiguration(server, config, false))
        wlr_output_configuration_v1_send_succeeded(config);
    else
        wlr_output_configuration_v1_send_failed(config);
    wlr_output_configuration_v1_destroy(config);
}

static void handleOutputManagerTest(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, outputManagerTest);
    struct wlr_output_configuration_v1 *config = data;
    if (applyOutputConfiguration(server, config, true))
        wlr_output_configuration_v1_send_succeeded(config);
    else
        wlr_output_configuration_v1_send_failed(config);
    wlr_output_configuration_v1_destroy(config);
}

// After the screens changed: panels go to where their screen is now,
// windows that fill a screen are fitted again and windows left outside
// every screen come back. Done from the event loop, not from inside the
// change itself (an output may be half-way through being destroyed).
static void layoutIdleCallback(void *data)
{
    struct Server *server = data;
    server->layoutIdle = NULL;
    arrangeAllOutputs(server);
    rescueViews(server);
    arrangeLockSurfaces(server);
    updateFullscreenLayers(server);
    scheduleAllFrames(server);
}

// A monitor that is unplugged can move the others (they are placed side by
// side): their panels used to stay at the old coordinates, off the screen.
static void handleLayoutChange(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Server *server = wl_container_of(listener, server, layoutChange);
    updateOutputManager(server);
    arrangeLockSurfaces(server);
    updateFullscreenLayers(server);
    if (!server->layoutIdle && server->running) {
        server->layoutIdle = wl_event_loop_add_idle(server->eventLoop, layoutIdleCallback,
                                                    server);
    }
}

// `dpms off|on`: blank the monitors without forgetting their configuration.
static void setOutputPower(struct Output *output, bool on)
{
    if (output->disabled || output->poweredOff == !on)
        return;
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, on);
    if (wlr_output_commit_state(output->output, &state))
        output->poweredOff = !on;
    wlr_output_state_finish(&state);
}

static void setOutputsPower(struct Server *server, bool on)
{
    struct Output *output;
    wl_list_for_each(output, &server->outputs, link)
        setOutputPower(output, on);
}

// wlopm --off / --on (and idle tools that use the protocol). Screens that
// Settings -> Displays switched off stay off.
static void handleOutputPowerSetMode(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, outputPowerSetMode);
    const struct wlr_output_power_v1_set_mode_event *event = data;
    struct Output *output = outputFromWlr(server, event->output);
    if (!output)
        return;
    setOutputPower(output, event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
    scheduleAllFrames(server);
}

static void handleGammaSet(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, gammaSetGamma);
    const struct wlr_gamma_control_manager_v1_set_gamma_event *event = data;
    struct Output *output = outputFromWlr(server, event->output);
    if (!output)
        return;
    output->gammaPending = true;
    wlr_output_schedule_frame(event->output);
}

// ---------------------------------------------------------------------------
// Effects renderer: rounded corners, blur, animations
// ---------------------------------------------------------------------------
//
// The scene graph keeps the structure (stacking, positions, visibility) but,
// with effects on, each frame is drawn here instead of by
// wlr_scene_output_commit(): every texture can then be clipped to a rounded
// window shape, faded and scaled while it animates, and the shell's panels
// and menus get a blurred copy of what is behind them. Without a render pass
// (or with effects off) the scene draws the frame as before.

static uint64_t nowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static double easeOutCubic(double t)
{
    if (t <= 0)
        return 0;
    if (t >= 1)
        return 1;
    double u = 1 - t;
    return 1 - u * u * u;
}

static bool animationsOn(struct Server *server)
{
    return server->config.effects && server->config.animations;
}

static int animationDuration(struct Server *server, int baseMs)
{
    if (!animationsOn(server))
        return 0;
    return (int)(baseMs / server->config.animationSpeed);
}

static void scheduleAllFrames(struct Server *server)
{
    struct Output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (!output->disabled && !output->poweredOff)
            wlr_output_schedule_frame(output->output);
    }
}

// A window grows in from `fromScale` and fades in from `fromAlpha`.
static void viewAnimate(struct View *view, float fromScale, float fromAlpha, int baseMs)
{
    int duration = animationDuration(view->server, baseMs);
    if (duration <= 0) {
        view->animating = false;
        return;
    }
    view->animating = true;
    view->animStart = nowMs();
    view->animDuration = duration;
    view->animFromScale = fromScale;
    view->animFromAlpha = fromAlpha;
    scheduleAllFrames(view->server);
}

static void viewAnimationState(struct View *view, uint64_t now, double *scale, double *alpha)
{
    *scale = 1.0;
    *alpha = 1.0;
    if (!view->animating)
        return;
    double t = (double)(now - view->animStart) / view->animDuration;
    if (t >= 1.0) {
        view->animating = false;
        return;
    }
    double e = easeOutCubic(t);
    *scale = view->animFromScale + (1.0 - view->animFromScale) * e;
    *alpha = view->animFromAlpha + (1.0 - view->animFromAlpha) * e;
}

// The window's frame (title bar and border included) in layout coordinates.
static struct wlr_box viewOuterBox(struct View *view)
{
    struct wlr_box geometry = viewGeometry(view);
    int width = geometry.width > 0 ? geometry.width : view->width;
    int height = geometry.height > 0 ? geometry.height : view->height;
    int top, side, bottom;
    viewFrame(view, &top, &side, &bottom);
    return (struct wlr_box){view->x - side, view->y - top, width + 2 * side,
                            height + top + bottom};
}

// Rounded only where the compositor owns the edge: windows with a server
// frame, or client windows without client-side shadows around them (those
// round themselves). Never maximized or fullscreen windows.
static int viewCornerRadius(struct View *view)
{
    int radius = view->server->config.cornerRadius;
    if (!view->server->config.effects || radius <= 0 || view->maximized
        || view->fullscreen || view->overrideRedirect)
        return 0;
    if (view->ssd && viewDecorated(view))
        return radius;
    struct wlr_box geometry = viewGeometry(view);
    struct wlr_surface *surface = viewSurface(view);
    if (view->xdg && surface
        && (geometry.x > 0 || geometry.y > 0
            || (geometry.width > 0 && geometry.width < surface->current.width)))
        return 0;
    return radius;
}

// A closed or minimized window keeps being drawn for a moment from the
// buffer it last showed.
static void createGhost(struct View *view, int kind)
{
    struct Server *server = view->server;
    int duration = animationDuration(server, kind == GHOST_MINIMIZE ? 240 : 180);
    struct wlr_surface *surface = viewSurface(view);
    if (duration <= 0 || !surface || !surface->buffer || view->overrideRedirect
        || !view->tree)
        return;
    struct Ghost *ghost = xcalloc(1, sizeof(*ghost));
    ghost->buffer = wlr_buffer_lock(&surface->buffer->base);
    ghost->client = surface->buffer;
    struct wlr_box geometry = viewGeometry(view);
    double originX = view->x - (view->xdg ? geometry.x : 0);
    double originY = view->y - (view->xdg ? geometry.y : 0);
    ghost->box = (struct wlr_box){(int)originX, (int)originY, surface->current.width,
                                  surface->current.height};
    if (view->decoTitle && view->decoTitle->buffer && viewDecorated(view)) {
        int top, side, bottom;
        viewFrame(view, &top, &side, &bottom);
        ghost->titleBuffer = wlr_buffer_lock(view->decoTitle->buffer);
        ghost->titleBox = (struct wlr_box){view->x - side, view->y - top,
                                           view->decoWidth, top};
    }
    ghost->frame = viewOuterBox(view);
    ghost->radius = viewCornerRadius(view);
    ghost->workspace = view->workspace;
    ghost->kind = kind;
    ghost->start = nowMs();
    ghost->duration = duration;
    wl_list_insert(server->ghosts.prev, &ghost->link);
    scheduleAllFrames(server);
}

static void destroyGhost(struct Ghost *ghost)
{
    wl_list_remove(&ghost->link);
    wlr_buffer_unlock(ghost->buffer);
    if (ghost->titleBuffer)
        wlr_buffer_unlock(ghost->titleBuffer);
    free(ghost);
}

// Workspace switches slide the old desktop out and the new one in.
static void startWorkspaceSlide(struct Server *server, int from, int to, int direction)
{
    int duration = animationDuration(server, 260);
    if (duration <= 0 || from == to)
        return;
    server->wsAnimating = true;
    server->wsFrom = from;
    server->wsDirection = direction;
    server->wsStart = nowMs();
    server->wsDuration = duration;
    scheduleAllFrames(server);
}

struct RenderTarget {
    struct wlr_render_pass *pass;
    double scale;                   // layout px -> target px
    enum wl_output_transform transform;
    int width, height;              // transformed size, for wlr_box_transform
    double ox, oy;                  // layout position of the target's corner
    double logicalWidth;            // slide distance
    int bufferWidth, bufferHeight;  // the buffer drawn into (untransformed)
};

struct RenderState {
    double alpha;
    double scale;                   // about (cx, cy)
    double cx, cy;
    double shiftX, shiftY;
    struct View *view;
    const pixman_region32_t *clip;
};

#define NT_MAX_FRAME_TEXTURES 256

// Textures created for one frame (title bars, blur steps).
struct FrameTextures {
    struct wlr_texture *items[NT_MAX_FRAME_TEXTURES];
    int count;
};

static struct wlr_texture *frameTexture(struct Server *server, struct FrameTextures *temp,
                                        struct wlr_buffer *buffer)
{
    if (temp->count >= NT_MAX_FRAME_TEXTURES)
        return NULL;
    struct wlr_texture *texture = wlr_texture_from_buffer(server->renderer, buffer);
    if (texture)
        temp->items[temp->count++] = texture;
    return texture;
}

static void releaseFrameTextures(struct FrameTextures *temp)
{
    for (int i = 0; i < temp->count; ++i)
        wlr_texture_destroy(temp->items[i]);
    temp->count = 0;
}

static struct wlr_box targetBox(const struct RenderTarget *t, const struct RenderState *s,
                                double x, double y, double w, double h)
{
    double x1 = s->cx + (x - s->cx) * s->scale + s->shiftX;
    double y1 = s->cy + (y - s->cy) * s->scale + s->shiftY;
    double x2 = s->cx + (x + w - s->cx) * s->scale + s->shiftX;
    double y2 = s->cy + (y + h - s->cy) * s->scale + s->shiftY;
    struct wlr_box box;
    box.x = (int)lround((x1 - t->ox) * t->scale);
    box.y = (int)lround((y1 - t->oy) * t->scale);
    box.width = (int)lround((x2 - t->ox) * t->scale) - box.x;
    box.height = (int)lround((y2 - t->oy) * t->scale) - box.y;
    wlr_box_transform(&box, &box, wlr_output_transform_invert(t->transform),
                      t->width, t->height);
    return box;
}

// A rounded rectangle as a region of one-pixel rows in the corners.
static void roundedRegion(pixman_region32_t *region, struct wlr_box box, int radius)
{
    pixman_region32_init(region);
    if (box.width <= 0 || box.height <= 0)
        return;
    if (radius * 2 > box.width)
        radius = box.width / 2;
    if (radius * 2 > box.height)
        radius = box.height / 2;
    if (radius <= 0) {
        pixman_region32_union_rect(region, region, box.x, box.y,
                                   (unsigned)box.width, (unsigned)box.height);
        return;
    }
    pixman_region32_union_rect(region, region, box.x, box.y + radius,
                               (unsigned)box.width, (unsigned)(box.height - 2 * radius));
    for (int i = 0; i < radius; ++i) {
        double dy = radius - i - 0.5;
        int inset = (int)ceil(radius - sqrt((double)radius * radius - dy * dy));
        if (box.width - 2 * inset <= 0)
            continue;
        pixman_region32_union_rect(region, region, box.x + inset, box.y + i,
                                   (unsigned)(box.width - 2 * inset), 1);
        pixman_region32_union_rect(region, region, box.x + inset,
                                   box.y + box.height - 1 - i,
                                   (unsigned)(box.width - 2 * inset), 1);
    }
}

static void renderNode(struct Server *server, const struct RenderTarget *t,
                       const struct RenderState *state, struct wlr_scene_node *node,
                       double x, double y, bool force, struct FrameTextures *temp)
{
    if (!node->enabled && !force)
        return;
    x += node->x;
    y += node->y;
    switch (node->type) {
    case WLR_SCENE_NODE_TREE: {
        struct wlr_scene_tree *tree = wlr_scene_tree_from_node(node);
        struct wlr_scene_node *child;
        wl_list_for_each(child, &tree->children, link)
            renderNode(server, t, state, child, x, y, false, temp);
        break;
    }
    case WLR_SCENE_NODE_RECT: {
        struct wlr_scene_rect *rect = wlr_scene_rect_from_node(node);
        if (rect->width <= 0 || rect->height <= 0)
            break;
        struct wlr_box box = targetBox(t, state, x, y, rect->width, rect->height);
        float a = (float)state->alpha;
        wlr_render_pass_add_rect(t->pass, &(struct wlr_render_rect_options){
            .box = box,
            .color = {rect->color[0] * a, rect->color[1] * a, rect->color[2] * a,
                      rect->color[3] * a},
            .clip = state->clip,
            .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
        });
        break;
    }
    case WLR_SCENE_NODE_BUFFER: {
        struct wlr_scene_buffer *sceneBuffer = wlr_scene_buffer_from_node(node);
        if (!sceneBuffer->buffer)
            break;
        struct wlr_texture *texture = NULL;
        const pixman_region32_t *clip = state->clip;
        struct wlr_scene_surface *sceneSurface = wlr_scene_surface_try_from_buffer(sceneBuffer);
        if (sceneSurface) {
            texture = wlr_surface_get_texture(sceneSurface->surface);
            // Popups stick out of the window: never clip them.
            if (clip && state->view
                && wlr_surface_get_root_surface(sceneSurface->surface)
                       != viewSurface(state->view))
                clip = NULL;
        } else {
            texture = frameTexture(server, temp, sceneBuffer->buffer);
        }
        if (!texture)
            break;
        int width = sceneBuffer->dst_width > 0 ? sceneBuffer->dst_width : (int)texture->width;
        int height = sceneBuffer->dst_height > 0 ? sceneBuffer->dst_height : (int)texture->height;
        struct wlr_box box = targetBox(t, state, x, y, width, height);
        if (box.width <= 0 || box.height <= 0)
            break;
        float alpha = (float)(sceneBuffer->opacity * state->alpha);
        enum wl_output_transform transform = wlr_output_transform_compose(
            wlr_output_transform_invert(sceneBuffer->transform), t->transform);
        wlr_render_pass_add_texture(t->pass, &(struct wlr_render_texture_options){
            .texture = texture,
            .src_box = sceneBuffer->src_box,
            .dst_box = box,
            .alpha = &alpha,
            .clip = clip,
            .transform = transform,
            .filter_mode = sceneBuffer->filter_mode,
            .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
        });
        break;
    }
    }
}

static void renderViews(struct Server *server, const struct RenderTarget *t, uint64_t now,
                        struct FrameTextures *temp)
{
    double slide = 0;
    if (server->wsAnimating) {
        double progress = (double)(now - server->wsStart) / server->wsDuration;
        if (progress >= 1.0)
            server->wsAnimating = false;
        else
            slide = easeOutCubic(progress);
    }
    struct wlr_scene_node *child;
    wl_list_for_each(child, &server->toplevelTree->children, link) {
        struct View *view = child->data;
        struct RenderState state = {.alpha = 1, .scale = 1};
        if (!view) {
            renderNode(server, t, &state, child, 0, 0, false, temp);
            continue;
        }
        bool visible = child->enabled;
        if (server->wsAnimating && !view->overrideRedirect && !view->sticky && view->mapped
            && !view->minimized) {
            double distance = t->logicalWidth * server->wsDirection;
            if (view->workspace == server->wsFrom) {
                visible = true;
                state.shiftX = -distance * slide;
            } else if (view->workspace == server->workspace) {
                state.shiftX = distance * (1.0 - slide);
            }
        }
        if (!visible)
            continue;
        double scale, alpha;
        viewAnimationState(view, now, &scale, &alpha);
        struct wlr_box outer = viewOuterBox(view);
        state.alpha = alpha;
        state.scale = scale;
        state.cx = outer.x + outer.width / 2.0;
        state.cy = outer.y + outer.height / 2.0;
        state.view = view;
        pixman_region32_t region;
        bool clipped = false;
        int radius = viewCornerRadius(view);
        if (radius > 0) {
            struct wlr_box box = targetBox(t, &state, outer.x, outer.y, outer.width,
                                           outer.height);
            roundedRegion(&region, box, (int)lround(radius * t->scale * scale));
            state.clip = &region;
            clipped = true;
        }
        renderNode(server, t, &state, child, 0, 0, true, temp);
        if (clipped)
            pixman_region32_fini(&region);
    }

    // Windows that just closed or minimized fade away.
    struct Ghost *ghost;
    struct Ghost *next;
    wl_list_for_each_safe(ghost, next, &server->ghosts, link) {
        double progress = (double)(now - ghost->start) / ghost->duration;
        if (progress >= 1.0) {
            destroyGhost(ghost);
            continue;
        }
        if (ghost->workspace != server->workspace)
            continue;
        double e = easeOutCubic(progress);
        struct RenderState state = {.alpha = 1.0 - e};
        state.cx = ghost->frame.x + ghost->frame.width / 2.0;
        state.cy = ghost->frame.y + ghost->frame.height / 2.0;
        if (ghost->kind == GHOST_MINIMIZE) {
            state.scale = 1.0 - 0.5 * e;
            state.shiftY = (t->oy + t->height / t->scale - state.cy) * 0.7 * e;
        } else {
            state.scale = 1.0 - 0.08 * e;
        }
        pixman_region32_t region;
        bool clipped = false;
        if (ghost->radius > 0) {
            struct wlr_box box = targetBox(t, &state, ghost->frame.x, ghost->frame.y,
                                           ghost->frame.width, ghost->frame.height);
            roundedRegion(&region, box, (int)lround(ghost->radius * t->scale * state.scale));
            state.clip = &region;
            clipped = true;
        }
        float alpha = (float)state.alpha;
        if (ghost->titleBuffer) {
            struct wlr_texture *title = frameTexture(server, temp, ghost->titleBuffer);
            if (title) {
                wlr_render_pass_add_texture(t->pass, &(struct wlr_render_texture_options){
                    .texture = title,
                    .dst_box = targetBox(t, &state, ghost->titleBox.x, ghost->titleBox.y,
                                         ghost->titleBox.width, ghost->titleBox.height),
                    .alpha = &alpha,
                    .clip = state.clip,
                    .transform = t->transform,
                    .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
                });
            }
        }
        if (ghost->client && ghost->client->texture) {
            wlr_render_pass_add_texture(t->pass, &(struct wlr_render_texture_options){
                .texture = ghost->client->texture,
                .dst_box = targetBox(t, &state, ghost->box.x, ghost->box.y,
                                     ghost->box.width, ghost->box.height),
                .alpha = &alpha,
                .clip = state.clip,
                .transform = t->transform,
                .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
            });
        }
        if (clipped)
            pixman_region32_fini(&region);
    }
}

static struct Layer *layerForNode(struct Server *server, struct wlr_scene_node *node)
{
    struct Layer *layer;
    wl_list_for_each(layer, &server->layers, link) {
        if (layer->scene && &layer->scene->tree->node == node)
            return layer;
    }
    return NULL;
}

static struct BlurRegion *blurRegionFor(struct Server *server, struct Layer *layer)
{
    if (!layer || !layer->layer || !layer->layer->namespace || !layer->layer->output)
        return NULL;
    struct BlurRegion *region;
    wl_list_for_each(region, &server->blurRegions, link) {
        if (strcmp(region->ns, layer->layer->namespace) == 0
            && strcmp(region->output, layer->layer->output->name) == 0)
            return region;
    }
    return NULL;
}

// Top and overlay layers, each blur-enabled surface over the blurred copy
// of what is below it.
static void renderLayerTree(struct Server *server, const struct RenderTarget *t,
                            struct wlr_scene_tree *tree, struct wlr_texture *blurred,
                            struct FrameTextures *temp)
{
    struct wlr_scene_node *child;
    wl_list_for_each(child, &tree->children, link) {
        if (!child->enabled)
            continue;
        struct RenderState state = {.alpha = 1, .scale = 1};
        if (blurred) {
            struct BlurRegion *blur = blurRegionFor(server, layerForNode(server, child));
            if (blur) {
                struct wlr_box box = targetBox(t, &state, child->x + blur->box.x,
                                               child->y + blur->box.y, blur->box.width,
                                               blur->box.height);
                pixman_region32_t region;
                roundedRegion(&region, box, (int)lround(blur->radius * t->scale));
                wlr_render_pass_add_texture(t->pass, &(struct wlr_render_texture_options){
                    .texture = blurred,
                    .dst_box = {0, 0, t->bufferWidth, t->bufferHeight},
                    .clip = &region,
                    .filter_mode = WLR_SCALE_FILTER_BILINEAR,
                    .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
                });
                pixman_region32_fini(&region);
            }
        }
        renderNode(server, t, &state, child, 0, 0, false, temp);
    }
}

static void renderBelowTop(struct Server *server, const struct RenderTarget *t, uint64_t now,
                           struct FrameTextures *temp)
{
    struct RenderState state = {.alpha = 1, .scale = 1};
    wlr_render_pass_add_rect(t->pass, &(struct wlr_render_rect_options){
        .box = {0, 0, t->bufferWidth, t->bufferHeight},
        .color = {0, 0, 0, 1},
        .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
    });
    renderNode(server, t, &state, &server->backgroundTree->node, 0, 0, false, temp);
    renderNode(server, t, &state, &server->bottomTree->node, 0, 0, false, temp);
    if (server->toplevelTree->node.enabled)
        renderViews(server, t, now, temp);
    if (server->snapTree->node.enabled)
        renderNode(server, t, &state, &server->snapTree->node, 0, 0, false, temp);
}

static bool anyBlurOn(struct Server *server, struct Output *output)
{
    if (!server->config.blur || wl_list_empty(&server->blurRegions) || server->locked)
        return false;
    struct Layer *layer;
    wl_list_for_each(layer, &server->layers, link) {
        if (!layer->scene || !layer->scene->tree->node.enabled || layer->output != output)
            continue;
        if (blurRegionFor(server, layer))
            return true;
    }
    return false;
}

static struct wlr_buffer *offscreenBuffer(struct Server *server, struct wlr_buffer **slot,
                                          int width, int height)
{
    if (*slot && ((*slot)->width != width || (*slot)->height != height)) {
        wlr_buffer_drop(*slot);
        *slot = NULL;
    }
    if (!*slot) {
        uint64_t modifiers[] = {DRM_FORMAT_MOD_INVALID, DRM_FORMAT_MOD_LINEAR};
        struct wlr_drm_format format = {
            .format = DRM_FORMAT_ARGB8888,
            .len = 2,
            .capacity = 2,
            .modifiers = modifiers,
        };
        *slot = wlr_allocator_create_buffer(server->allocator, width, height, &format);
    }
    return *slot;
}

// What lies under the shell's surfaces, at a quarter of the resolution,
// smoothed by repeatedly halving and doubling it (a cheap Kawase-like blur
// that every renderer, pixman included, can do).
static struct wlr_texture *renderBlurredBackdrop(struct Server *server, struct Output *output,
                                                 const struct RenderTarget *full, uint64_t now,
                                                 struct FrameTextures *temp)
{
    struct wlr_output *o = output->output;
    int width = o->width / 4 > 0 ? o->width / 4 : 1;
    int height = o->height / 4 > 0 ? o->height / 4 : 1;
    struct wlr_buffer *large = offscreenBuffer(server, &output->blurBuffers[0], width, height);
    struct wlr_buffer *small = offscreenBuffer(server, &output->blurBuffers[1],
                                               width / 2 > 0 ? width / 2 : 1,
                                               height / 2 > 0 ? height / 2 : 1);
    if (!large || !small)
        return NULL;

    struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(server->renderer, large, NULL);
    if (!pass)
        return NULL;
    struct RenderTarget quarter = *full;
    quarter.pass = pass;
    quarter.scale = full->scale / 4.0;
    quarter.width = full->width / 4 > 0 ? full->width / 4 : 1;
    quarter.height = full->height / 4 > 0 ? full->height / 4 : 1;
    quarter.bufferWidth = large->width;
    quarter.bufferHeight = large->height;
    struct FrameTextures sceneTextures = {.count = 0};
    renderBelowTop(server, &quarter, now, &sceneTextures);
    bool ok = wlr_render_pass_submit(pass);
    releaseFrameTextures(&sceneTextures);
    if (!ok)
        return NULL;

    for (int i = 0; i < server->config.blurStrength; ++i) {
        // Each intermediate texture is released before its buffer is drawn
        // into again (pixman holds the buffer while a texture reads it).
        struct wlr_texture *source = wlr_texture_from_buffer(server->renderer, large);
        if (!source)
            return NULL;
        pass = wlr_renderer_begin_buffer_pass(server->renderer, small, NULL);
        if (pass) {
            wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){
                .texture = source,
                .dst_box = {0, 0, small->width, small->height},
                .filter_mode = WLR_SCALE_FILTER_BILINEAR,
                .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
            });
            ok = wlr_render_pass_submit(pass);
        }
        wlr_texture_destroy(source);
        if (!pass || !ok)
            return NULL;
        source = wlr_texture_from_buffer(server->renderer, small);
        if (!source)
            return NULL;
        pass = wlr_renderer_begin_buffer_pass(server->renderer, large, NULL);
        if (pass) {
            wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){
                .texture = source,
                .dst_box = {0, 0, large->width, large->height},
                .filter_mode = WLR_SCALE_FILTER_BILINEAR,
                .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
            });
            ok = wlr_render_pass_submit(pass);
        }
        wlr_texture_destroy(source);
        if (!pass || !ok)
            return NULL;
    }
    (void)temp;
    return wlr_texture_from_buffer(server->renderer, large);
}

static bool animationsRunning(struct Server *server)
{
    if (server->wsAnimating || !wl_list_empty(&server->ghosts))
        return true;
    struct View *view;
    wl_list_for_each(view, &server->views, link) {
        if (view->animating)
            return true;
    }
    return false;
}

// Draws and commits one frame with effects. False: let the scene do it.
static bool renderEffectsFrame(struct Output *output)
{
    struct Server *server = output->server;
    struct wlr_output *o = output->output;
    uint64_t now = nowMs();
    struct wlr_box layoutBox = {0};
    wlr_output_layout_get_box(server->outputLayout, o, &layoutBox);
    int transformedWidth = 0, transformedHeight = 0;
    wlr_output_transformed_resolution(o, &transformedWidth, &transformedHeight);

    struct RenderTarget target = {
        .scale = o->scale,
        .transform = o->transform,
        .width = transformedWidth,
        .height = transformedHeight,
        .ox = layoutBox.x,
        .oy = layoutBox.y,
        .logicalWidth = layoutBox.width > 0 ? layoutBox.width : transformedWidth,
        .bufferWidth = o->width,
        .bufferHeight = o->height,
    };
    struct FrameTextures temp = {.count = 0};

    // The blurred backdrop is drawn into its own buffers first.
    struct wlr_texture *blurred = NULL;
    if (anyBlurOn(server, output))
        blurred = renderBlurredBackdrop(server, output, &target, now, &temp);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR >= 19
    struct wlr_render_pass *pass = wlr_output_begin_render_pass(o, &state, NULL);
#else
    struct wlr_render_pass *pass = wlr_output_begin_render_pass(o, &state, NULL, NULL);
#endif
    if (!pass) {
        wlr_output_state_finish(&state);
        if (blurred)
            wlr_texture_destroy(blurred);
        return false;
    }
    target.pass = pass;

    renderBelowTop(server, &target, now, &temp);
    // A fullscreen window hides these trees.
    if (server->topTree->node.enabled)
        renderLayerTree(server, &target, server->topTree, blurred, &temp);
    if (server->overlayTree->node.enabled)
        renderLayerTree(server, &target, server->overlayTree, blurred, &temp);
    struct RenderState plain = {.alpha = 1, .scale = 1};
    // The input method's candidate list and the icon of a drag in progress
    // sit above everything but the lock screen, as in the scene graph.
    renderNode(server, &target, &plain, &server->inputPopupTree->node, 0, 0, false, &temp);
    renderNode(server, &target, &plain, &server->dragIconTree->node, 0, 0, false, &temp);
    renderNode(server, &target, &plain, &server->lockTree->node, 0, 0, false, &temp);
    wlr_output_add_software_cursors_to_render_pass(o, pass, NULL);
    bool submitted = wlr_render_pass_submit(pass);
    releaseFrameTextures(&temp);
    if (blurred)
        wlr_texture_destroy(blurred);
    if (!submitted) {
        wlr_output_state_finish(&state);
        return false;
    }

    if (output->gammaPending) {
        output->gammaPending = false;
        struct wlr_gamma_control_v1 *gamma =
            wlr_gamma_control_manager_v1_get_control(server->gammaManager, o);
        if (!wlr_gamma_control_v1_apply(gamma, &state) && gamma)
            wlr_gamma_control_v1_send_failed_and_destroy(gamma);
    }
    bool committed = wlr_output_commit_state(o, &state);
    wlr_output_state_finish(&state);
    if (!committed)
        return false;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    wlr_scene_output_send_frame_done(output->sceneOutput, &ts);
    if (animationsRunning(server))
        wlr_output_schedule_frame(o);
    return true;
}

static void setBlurRegion(struct Server *server, const char *ns, const char *output,
                          struct wlr_box box, int radius)
{
    struct BlurRegion *region;
    wl_list_for_each(region, &server->blurRegions, link) {
        if (strcmp(region->ns, ns) == 0 && strcmp(region->output, output) == 0)
            break;
    }
    if (&region->link == &server->blurRegions) {
        region = xcalloc(1, sizeof(*region));
        snprintf(region->ns, sizeof(region->ns), "%s", ns);
        snprintf(region->output, sizeof(region->output), "%s", output);
        wl_list_insert(&server->blurRegions, &region->link);
    }
    region->box = box;
    region->radius = radius;
    scheduleAllFrames(server);
}

static void clearBlurRegion(struct Server *server, const char *ns, const char *output)
{
    struct BlurRegion *region;
    struct BlurRegion *next;
    wl_list_for_each_safe(region, next, &server->blurRegions, link) {
        if (strcmp(region->ns, ns) == 0 && strcmp(region->output, output) == 0) {
            wl_list_remove(&region->link);
            free(region);
        }
    }
    scheduleAllFrames(server);
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

static void outputFrame(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Output *output = wl_container_of(listener, output, frame);
    if (!output->sceneOutput)
        return;
    if (output->server->config.effects) {
        if (renderEffectsFrame(output)) {
            ++output->server->effectFrames;
            return;
        }
        ++output->server->fallbackFrames;
    }
    if (output->gammaPending) {
        // A night light (wlsunset) changed the gamma ramp: commit it with
        // the next frame.
        struct Server *server = output->server;
        output->gammaPending = false;
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        if (wlr_scene_output_build_state(output->sceneOutput, &state, NULL)) {
            struct wlr_gamma_control_v1 *gamma =
                wlr_gamma_control_manager_v1_get_control(server->gammaManager,
                                                         output->output);
            if (wlr_gamma_control_v1_apply(gamma, &state)
                && !wlr_output_commit_state(output->output, &state) && gamma) {
                wlr_gamma_control_v1_send_failed_and_destroy(gamma);
            }
        }
        wlr_output_state_finish(&state);
        struct timespec now = {0};
        clock_gettime(CLOCK_MONOTONIC, &now);
        wlr_scene_output_send_frame_done(output->sceneOutput, &now);
        return;
    }
    bool committed = wlr_scene_output_commit(output->sceneOutput, NULL);
    wlr_log(WLR_DEBUG, "frame %s committed=%d", output->output->name, committed);
    struct timespec now = {0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(output->sceneOutput, &now);
}

static void outputRequestState(struct wl_listener *listener, void *data)
{
    struct Output *output = wl_container_of(listener, output, requestState);
    struct wlr_output_event_request_state *event = data;
    wlr_output_commit_state(output->output, event->state);
}

static void outputDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Output *output = wl_container_of(listener, output, destroy);
    struct Server *server = output->server;
    if (server->pointerOutput == output->output)
        server->pointerOutput = NULL;
    // Panels and wallpapers of this screen are told to close (as every
    // wlroots desktop does); the shell opens new ones where it wants them.
    // Left alone they stayed in the scene at the old coordinates.
    struct Layer *layer;
    struct Layer *nextLayer;
    wl_list_for_each_safe(layer, nextLayer, &server->layers, link) {
        if (layer->output != output)
            continue;
        layer->output = NULL;
        if (layer->layer)
            wlr_layer_surface_v1_destroy(layer->layer);
    }
    if (output->sceneOutput) {
        wl_list_remove(&output->sceneDestroy.link);
        wlr_scene_output_destroy(output->sceneOutput);
        output->sceneOutput = NULL;
    }
    for (int i = 0; i < 2; ++i) {
        if (output->blurBuffers[i])
            wlr_buffer_drop(output->blurBuffers[i]);
    }
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->requestState.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);
    rescueViews(server);
    updateOutputManager(server);
}

static void outputSceneDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Output *output = wl_container_of(listener, output, sceneDestroy);
    wl_list_remove(&output->sceneDestroy.link);
    output->sceneOutput = NULL;
}

static void handleNewOutput(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newOutput);
    struct wlr_output *wlrOutput = data;

    if (!wlr_output_init_render(wlrOutput, server->allocator, server->renderer)) {
        wlr_log(WLR_ERROR, "failed to initialise output rendering");
        return;
    }

    struct SavedOutput saved = {0};
    bool haveSaved = findSavedOutput(wlrOutput, &saved);
    // A monitor saved as "off" stays off, unless it is the only one.
    bool enabled = !haveSaved || saved.enabled || wl_list_empty(&server->outputs);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    struct wlr_output_mode *mode = NULL;
    if (haveSaved)
        mode = findMode(wlrOutput, saved.width, saved.height, saved.refresh);
    if (!mode)
        mode = wlr_output_preferred_mode(wlrOutput);
    if (mode)
        wlr_output_state_set_mode(&state, mode);
    if (haveSaved) {
        wlr_output_state_set_scale(&state, (float)saved.scale);
        wlr_output_state_set_transform(&state, (enum wl_output_transform)saved.transform);
    } else if (mode) {
        wlr_output_state_set_scale(&state, automaticScale(wlrOutput, mode->width,
                                                          mode->height));
    }
    if (!wlr_output_commit_state(wlrOutput, &state)) {
        // A saved mode the monitor no longer accepts: fall back to defaults.
        wlr_output_state_finish(&state);
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, true);
        struct wlr_output_mode *preferred = wlr_output_preferred_mode(wlrOutput);
        if (preferred)
            wlr_output_state_set_mode(&state, preferred);
        wlr_output_commit_state(wlrOutput, &state);
        haveSaved = false;
        enabled = true;
    }
    wlr_output_state_finish(&state);

    struct Output *output = xcalloc(1, sizeof(*output));
    output->server = server;
    output->output = wlrOutput;
    output->disabled = !enabled;
    placeOutput(server, output, enabled, !haveSaved, saved.x, saved.y);
    if (!enabled) {
        struct wlr_output_state off;
        wlr_output_state_init(&off);
        wlr_output_state_set_enabled(&off, false);
        wlr_output_commit_state(wlrOutput, &off);
        wlr_output_state_finish(&off);
    } else if (!output->sceneOutput) {
        wlr_log(WLR_ERROR, "failed to create scene output");
    }
    output->frame.notify = outputFrame;
    wl_signal_add(&wlrOutput->events.frame, &output->frame);
    output->requestState.notify = outputRequestState;
    wl_signal_add(&wlrOutput->events.request_state, &output->requestState);
    output->destroy.notify = outputDestroy;
    wl_signal_add(&wlrOutput->events.destroy, &output->destroy);

    wl_list_insert(&server->outputs, &output->link);
    wlr_output_layout_get_box(server->outputLayout, wlrOutput, &output->usable);
    if (output->sceneOutput)
        wlr_scene_output_commit(output->sceneOutput, NULL);
    updateOutputManager(server);

    wlr_xcursor_manager_load(server->cursorManager, wlrOutput->scale);
    wlr_cursor_set_xcursor(server->cursor, server->cursorManager, "left_ptr");

    wlr_log(WLR_INFO, "output %s: %dx%d", wlrOutput->name,
            wlrOutput->width, wlrOutput->height);
}

// ---------------------------------------------------------------------------
// XWayland
// ---------------------------------------------------------------------------

static void handleX11Map(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, map);
    view->mapped = true;
    if (!view->overrideRedirect) {
        view->workspace = view->server->workspace;
        viewAnimate(view, 0.94f, 0.0f, 200);
    }
    viewSyncVisible(view);
    broadcastWorkspaces(view->server);
    view->ssd = x11WantsFrame(view);
    if (view->x11) {
        view->width = view->x11->width;
        view->height = view->x11->height;
        if (view->x11->x == 0 && view->x11->y == 0
            && !view->overrideRedirect) {
            centerView(view);
        } else {
            view->positioned = true;
            viewSetPosition(view, view->x11->x, view->x11->y);
        }
    }
    if (!view->overrideRedirect) {
        ensureForeignToplevel(view);
        viewRaise(view);
        focusView(view, NULL);
        applyWindowRules(view);
        // _NET_WM_STATE set before the window was shown (games, Wine).
        if (view->x11->fullscreen && !view->fullscreen)
            viewSetFullscreen(view, true);
        else if (view->x11->maximized_vert && view->x11->maximized_horz
                 && !view->maximized && !view->fullscreen)
            viewSetMaximized(view, true);
    }
    updateForeignToplevel(view);
}

static void handleX11Unmap(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, unmap);
    wlr_log(WLR_DEBUG, "xdg window unmapped: %s", viewTitle(view));
    if (view->mapped && !view->minimized && viewOnWorkspace(view))
        createGhost(view, GHOST_CLOSE);
    view->animating = false;
    view->mapped = false;
    viewSyncVisible(view);
    if (view->server->switchTarget == view)
        switcherCancel(view->server);
    if (view->server->focusedView == view) {
        view->server->focusedView = NULL;
        viewSetActivated(view, false);
        restoreFocus(view->server);
    }
    broadcastWorkspaces(view->server);
    destroyForeignToplevel(view);
    // A hidden fullscreen window gives the panel back.
    updateFullscreenLayers(view->server);
    broadcastFullscreen(view->server, false);
}

static void handleX11Destroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, destroy);
    wl_list_remove(&view->destroy.link);
    if (view->x11) {
        wl_list_remove(&view->associate.link);
        wl_list_remove(&view->dissociate.link);
        if (view->x11->surface) {
            wl_list_remove(&view->map.link);
            wl_list_remove(&view->unmap.link);
        }
    }
    wl_list_remove(&view->requestMove.link);
    wl_list_remove(&view->requestConfigure.link);
    wl_list_remove(&view->requestResize.link);
    wl_list_remove(&view->requestMaximize.link);
    wl_list_remove(&view->requestFullscreen.link);
    wl_list_remove(&view->requestMinimize.link);
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR >= 19
    wl_list_remove(&view->x11RequestClose.link);
#endif
    wl_list_remove(&view->x11RequestActivate.link);
    wl_list_remove(&view->setTitle.link);
    wl_list_remove(&view->setAppId.link);
    wl_list_remove(&view->x11SetDecorations.link);
    view->tree = NULL;
    view->decoTree = NULL;
    view->decoTitle = NULL;
    destroyView(view);
}

static void handleX11RequestConfigure(struct wl_listener *listener, void *data)
{
    struct View *view = wl_container_of(listener, view, requestConfigure);
    struct wlr_xwayland_surface_configure_event *event = data;
    if (view->overrideRedirect || !view->x11)
        return;
    // A window that fills the screen stays as it is: X11 applications (old
    // games, Wine) ask for their startup size again while maximized or
    // fullscreen and used to shrink back inside the frame of that state.
    if (view->mapped && (view->maximized || view->fullscreen)) {
        wlr_xwayland_surface_configure(view->x11, (int16_t)view->x, (int16_t)view->y,
                                       (uint16_t)view->width, (uint16_t)view->height);
        return;
    }
    view->x = event->x;
    view->y = event->y;
    if (event->width > 0)
        view->width = event->width;
    if (event->height > 0)
        view->height = event->height;
    viewSetPosition(view, event->x, event->y);
    wlr_xwayland_surface_configure(view->x11, event->x, event->y,
                                   event->width, event->height);
}

static void handleX11RequestMove(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, requestMove);
    if (!interactiveGrabAllowed(view))
        return;
    restoreForDrag(view);
    viewClearSnap(view);
    view->server->grabbedView = view;
    view->server->cursorMode = CURSOR_MOVE;
    view->server->grabX = view->server->cursor->x - view->x;
    view->server->grabY = view->server->cursor->y - view->y;
}

static void handleX11RequestResize(struct wl_listener *listener, void *data)
{
    struct View *view = wl_container_of(listener, view, requestResize);
    if (!interactiveGrabAllowed(view) || view->maximized)
        return;
    viewClearSnap(view);
    struct wlr_xwayland_resize_event *event = data;
    view->server->grabbedView = view;
    view->server->cursorMode = CURSOR_RESIZE;
    view->server->resizeEdges = event->edges;
    view->server->grabWidth = view->width;
    view->server->grabHeight = view->height;
    view->server->grabX = view->x;
    view->server->grabY = view->y;
    resizeOffsetsFrom(view->server, view->server->cursor->x, view->server->cursor->y);
}

static void handleX11RequestMinimize(struct wl_listener *listener, void *data)
{
    struct View *view = wl_container_of(listener, view, requestMinimize);
    struct wlr_xwayland_minimize_event *event = data;
    viewSetMinimized(view, event->minimize);
}

static void handleX11RequestMaximize(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, requestMaximize);
    // The state the window asked for (_NET_WM_STATE), not a toggle.
    viewSetMaximized(view, view->x11->maximized_vert && view->x11->maximized_horz);
}

static void handleX11RequestFullscreen(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, requestFullscreen);
    viewSetFullscreen(view, view->x11->fullscreen);
}

static void handleX11RequestClose(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, foreignClose);
    viewClose(view);
}

static void handleX11RequestActivate(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, x11RequestActivate);
    if (view->mapped && view->minimized && !view->server->locked)
        viewSetMinimized(view, false);
    focusView(view, NULL);
}

static void handleX11SetTitle(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, setTitle);
    updateForeignToplevel(view);
}

static void handleX11SetClass(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, setAppId);
    updateForeignToplevel(view);
}

// X11 windows get a frame unless they asked for none (Motif hints), as a
// window manager would draw one.
static bool x11WantsFrame(struct View *view)
{
    return view->x11 && !view->overrideRedirect
           && view->x11->decorations == WLR_XWAYLAND_SURFACE_DECORATIONS_ALL;
}

static void handleX11SetDecorations(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, x11SetDecorations);
    view->ssd = x11WantsFrame(view);
    viewUpdateDecoration(view);
}

static void handleX11Associate(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, associate);
    if (!view->x11 || !view->x11->surface || view->tree)
        return;

    view->tree = wlr_scene_subsurface_tree_create(view->server->toplevelTree,
                                                  view->x11->surface);
    if (!view->tree)
        return;
    view->tree->node.data = view;
    view->map.notify = handleX11Map;
    wl_signal_add(&view->x11->surface->events.map, &view->map);
    view->unmap.notify = handleX11Unmap;
    wl_signal_add(&view->x11->surface->events.unmap, &view->unmap);
}

static void handleX11Dissociate(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, dissociate);
    if (view->x11 && view->x11->surface) {
        wl_list_remove(&view->map.link);
        wl_list_remove(&view->unmap.link);
    }
    if (view->tree) {
        // The frame is a child of the tree and goes with it.
        wlr_scene_node_destroy(&view->tree->node);
        view->tree = NULL;
        view->decoTree = NULL;
        view->decoTitle = NULL;
        for (int i = 0; i < 3; ++i)
            view->decoBorder[i] = NULL;
    }
}

static void handleNewXWaylandSurface(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newXWaylandSurface);
    struct wlr_xwayland_surface *xsurface = data;

    struct View *view = xcalloc(1, sizeof(*view));
    view->server = server;
    view->workspace = server->workspace;
    view->x11 = xsurface;
    view->overrideRedirect = xsurface->override_redirect;
    view->x = xsurface->x;
    view->y = xsurface->y;

    wl_list_insert(&server->views, &view->link);

    view->associate.notify = handleX11Associate;
    wl_signal_add(&xsurface->events.associate, &view->associate);
    view->dissociate.notify = handleX11Dissociate;
    wl_signal_add(&xsurface->events.dissociate, &view->dissociate);
    view->destroy.notify = handleX11Destroy;
    wl_signal_add(&xsurface->events.destroy, &view->destroy);
    view->requestMove.notify = handleX11RequestMove;
    wl_signal_add(&xsurface->events.request_move, &view->requestMove);
    view->requestConfigure.notify = handleX11RequestConfigure;
    wl_signal_add(&xsurface->events.request_configure, &view->requestConfigure);
    view->requestResize.notify = handleX11RequestResize;
    wl_signal_add(&xsurface->events.request_resize, &view->requestResize);
    view->requestMaximize.notify = handleX11RequestMaximize;
    wl_signal_add(&xsurface->events.request_maximize, &view->requestMaximize);
    view->requestFullscreen.notify = handleX11RequestFullscreen;
    wl_signal_add(&xsurface->events.request_fullscreen, &view->requestFullscreen);
    view->requestMinimize.notify = handleX11RequestMinimize;
    wl_signal_add(&xsurface->events.request_minimize, &view->requestMinimize);
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR >= 19
    view->x11RequestClose.notify = handleX11RequestClose;
    wl_signal_add(&xsurface->events.request_close, &view->x11RequestClose);
#endif
    view->x11RequestActivate.notify = handleX11RequestActivate;
    wl_signal_add(&xsurface->events.request_activate, &view->x11RequestActivate);
    view->setTitle.notify = handleX11SetTitle;
    wl_signal_add(&xsurface->events.set_title, &view->setTitle);
    view->setAppId.notify = handleX11SetClass;
    wl_signal_add(&xsurface->events.set_class, &view->setAppId);
    view->x11SetDecorations.notify = handleX11SetDecorations;
    wl_signal_add(&xsurface->events.set_decorations, &view->x11SetDecorations);
    view->id = ++server->nextViewId;

    if (xsurface->surface)
        handleX11Associate(&view->associate, NULL);
}

static void handleXWaylandReady(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Server *server = wl_container_of(listener, server, xwaylandReadyListener);
    server->xwaylandReady = true;
    if (server->xwayland && server->xwayland->display_name)
        setenv("DISPLAY", server->xwayland->display_name, 1);
    wlr_xwayland_set_seat(server->xwayland, server->seat);
    wlr_log(WLR_INFO, "XWayland ready on DISPLAY=%s",
            server->xwayland && server->xwayland->display_name
                ? server->xwayland->display_name : "(unknown)");
    maybeStartShell(server);
}

// ---------------------------------------------------------------------------
// Screen lock (ext-session-lock-v1)
// ---------------------------------------------------------------------------
//
// The locker (swaylock, started through newteller-lock) asks for the lock.
// From then on only its surfaces are drawn and receive input; everything
// else sits under an opaque backdrop. If the locker dies without unlocking,
// the session stays locked and a new locker is started.

static void spawnLocker(struct Server *server)
{
    const char *argv[] = {"newteller-lock", NULL};
    setChildEnvironment(server);
    spawnDetached(argv[0], argv);
}

static void lockFocusSurface(struct Server *server, struct wlr_surface *surface)
{
    if (!surface)
        return;
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(server->seat, surface, keyboard->keycodes,
                                       keyboard->num_keycodes, &keyboard->modifiers);
    } else {
        wlr_seat_keyboard_notify_enter(server->seat, surface, NULL, 0, NULL);
    }
}

static void lockSurfaceMap(struct wl_listener *listener, void *data)
{
    (void)data;
    struct LockSurface *lock = wl_container_of(listener, lock, map);
    struct Server *server = lock->server;
    struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
    // The first locker surface (or the one under the cursor) takes the keys.
    if (!focused || !wlr_session_lock_surface_v1_try_from_wlr_surface(focused))
        lockFocusSurface(server, lock->lockSurface->surface);
    struct Hit hit = hitTest(server, server->cursor->x, server->cursor->y);
    pointerFocus(server, &hit);
}

static void lockSurfaceDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct LockSurface *lock = wl_container_of(listener, lock, destroy);
    struct Server *server = lock->server;
    struct wlr_surface *surface = lock->lockSurface->surface;
    wl_list_remove(&lock->map.link);
    wl_list_remove(&lock->destroy.link);
    wl_list_remove(&lock->link);
    free(lock);
    if (server->seat->keyboard_state.focused_surface == surface) {
        wlr_seat_keyboard_notify_clear_focus(server->seat);
        struct LockSurface *other;
        wl_list_for_each(other, &server->lockSurfaces, link) {
            if (other->lockSurface->surface && other->lockSurface->surface->mapped) {
                lockFocusSurface(server, other->lockSurface->surface);
                break;
            }
        }
    }
}

// The screens changed under a locked session (a monitor unplugged from a
// docked laptop, a new resolution): every lock surface follows its screen.
// Left where it was, the password prompt stayed at the old position, off
// the screen that remained, with only the backdrop to look at.
static void arrangeLockSurfaces(struct Server *server)
{
    struct LockSurface *lock;
    wl_list_for_each(lock, &server->lockSurfaces, link) {
        struct wlr_session_lock_surface_v1 *surface = lock->lockSurface;
        struct wlr_box box = {0};
        wlr_output_layout_get_box(server->outputLayout, surface->output, &box);
        if (wlr_box_empty(&box))
            continue;
        if (lock->tree)
            wlr_scene_node_set_position(&lock->tree->node, box.x, box.y);
        if (surface->current.width != (uint32_t)box.width
            || surface->current.height != (uint32_t)box.height) {
            wlr_session_lock_surface_v1_configure(surface, (uint32_t)box.width,
                                                  (uint32_t)box.height);
        }
    }
}

static void handleLockNewSurface(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, lockNewSurface);
    struct wlr_session_lock_surface_v1 *lockSurface = data;
    struct LockSurface *lock = xcalloc(1, sizeof(*lock));
    lock->server = server;
    lock->lockSurface = lockSurface;
    lock->tree = wlr_scene_subsurface_tree_create(server->lockTree,
                                                  lockSurface->surface);
    struct wlr_box box = {0, 0, 1280, 800};
    wlr_output_layout_get_box(server->outputLayout, lockSurface->output, &box);
    if (lock->tree)
        wlr_scene_node_set_position(&lock->tree->node, box.x, box.y);
    wlr_session_lock_surface_v1_configure(lockSurface, (uint32_t)box.width,
                                          (uint32_t)box.height);
    lock->map.notify = lockSurfaceMap;
    wl_signal_add(&lockSurface->surface->events.map, &lock->map);
    lock->destroy.notify = lockSurfaceDestroy;
    wl_signal_add(&lockSurface->events.destroy, &lock->destroy);
    wl_list_insert(&server->lockSurfaces, &lock->link);
}

static void setDesktopVisible(struct Server *server, bool visible)
{
    wlr_scene_node_set_enabled(&server->backgroundTree->node, visible);
    wlr_scene_node_set_enabled(&server->bottomTree->node, visible);
    wlr_scene_node_set_enabled(&server->toplevelTree->node, visible);
    wlr_scene_node_set_enabled(&server->topTree->node, visible);
    wlr_scene_node_set_enabled(&server->overlayTree->node, visible);
}

static void detachLock(struct Server *server)
{
    if (!server->activeLock)
        return;
    wl_list_remove(&server->lockNewSurface.link);
    wl_list_remove(&server->lockUnlock.link);
    wl_list_remove(&server->lockDestroy.link);
    server->activeLock = NULL;
}

static int lockRespawnCallback(void *data)
{
    struct Server *server = data;
    if (server->lockRespawnTimer) {
        wl_event_source_remove(server->lockRespawnTimer);
        server->lockRespawnTimer = NULL;
    }
    if (server->locked && !server->activeLock && server->lockRespawns < 5) {
        ++server->lockRespawns;
        wlr_log(WLR_INFO, "starting the screen locker again (attempt %d)",
                server->lockRespawns);
        spawnLocker(server);
    }
    return 0;
}

static void handleLockUnlock(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Server *server = wl_container_of(listener, server, lockUnlock);
    detachLock(server);
    server->locked = false;
    server->lockRespawns = 0;
    wlr_scene_node_set_enabled(&server->lockTree->node, false);
    setDesktopVisible(server, true);
    updateFullscreenLayers(server);
    wlr_seat_keyboard_notify_clear_focus(server->seat);
    restoreFocus(server);
    struct Hit hit = hitTest(server, server->cursor->x, server->cursor->y);
    pointerFocus(server, &hit);
    ipcBroadcast(server, "event locked 0");
    wlr_log(WLR_INFO, "session unlocked");
}

static void handleLockDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Server *server = wl_container_of(listener, server, lockDestroy);
    detachLock(server);
    if (!server->locked || !server->running)
        return;
    wlr_log(WLR_ERROR, "the screen locker went away without unlocking; "
                       "the session stays locked");
    if (!server->lockRespawnTimer) {
        server->lockRespawnTimer = wl_event_loop_add_timer(
            server->eventLoop, lockRespawnCallback, server);
        if (server->lockRespawnTimer)
            wl_event_source_timer_update(server->lockRespawnTimer, 1000);
    }
}

static void handleNewLock(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newLock);
    struct wlr_session_lock_v1 *lock = data;
    if (server->activeLock) {
        wlr_log(WLR_INFO, "refusing a second screen locker");
        wlr_session_lock_v1_destroy(lock);
        return;
    }

    server->activeLock = lock;
    server->lockNewSurface.notify = handleLockNewSurface;
    wl_signal_add(&lock->events.new_surface, &server->lockNewSurface);
    server->lockUnlock.notify = handleLockUnlock;
    wl_signal_add(&lock->events.unlock, &server->lockUnlock);
    server->lockDestroy.notify = handleLockDestroy;
    wl_signal_add(&lock->events.destroy, &server->lockDestroy);

    if (!server->locked) {
        server->locked = true;
        switcherCancel(server);
        server->cursorMode = CURSOR_PASSTHROUGH;
        server->grabbedView = NULL;
        server->logoTap = false;
        if (server->focusedView)
            server->previousView = server->focusedView;
        clearKeyboardFocus(server);
        wlr_seat_pointer_clear_focus(server->seat);
        wlr_scene_node_raise_to_top(&server->lockTree->node);
        wlr_scene_node_set_enabled(&server->lockTree->node, true);
        setDesktopVisible(server, false);
        ipcBroadcast(server, "event locked 1");
        wlr_log(WLR_INFO, "session locked");
    }
    if (server->lockRespawnTimer) {
        wl_event_source_remove(server->lockRespawnTimer);
        server->lockRespawnTimer = NULL;
    }
    wlr_session_lock_v1_send_locked(lock);
}

// ---------------------------------------------------------------------------
// Idle inhibition (video players, presentations)
// ---------------------------------------------------------------------------

// Idle is inhibited by Wayland clients (idle-inhibit-unstable-v1) and by
// control-socket clients ("idle-inhibit on").
// idle-inhibit-v1: an inhibitor counts only while its surface is visible.
// Windows are visible when shown on the current desktop; other surfaces
// (layer surfaces, popups) while they exist.
static bool idleInhibitorVisible(struct Server *server, struct IdleInhibitor *inhibitor)
{
    struct wlr_surface *surface = inhibitor->inhibitor->surface;
    if (!surface)
        return false;
    struct wlr_surface *root = wlr_surface_get_root_surface(surface);
    struct View *view;
    wl_list_for_each(view, &server->views, link) {
        if (viewSurface(view) == root)
            return view->tree && view->tree->node.enabled;
    }
    return true;
}

static bool idleInhibited(struct Server *server)
{
    if (server->ipcIdleInhibitors > 0)
        return true;
    struct IdleInhibitor *inhibitor;
    wl_list_for_each(inhibitor, &server->idleInhibitorList, link) {
        if (idleInhibitorVisible(server, inhibitor))
            return true;
    }
    return false;
}

static void updateIdleInhibited(struct Server *server)
{
    if (!server->idleNotifier)
        return;
    wlr_idle_notifier_v1_set_inhibited(server->idleNotifier, idleInhibited(server));
}

static void ipcSetIdleInhibit(struct IpcClient *client, bool inhibit)
{
    struct Server *server = client->server;
    if (client->inhibitsIdle == inhibit)
        return;
    client->inhibitsIdle = inhibit;
    if (inhibit)
        ++server->ipcIdleInhibitors;
    else if (server->ipcIdleInhibitors > 0)
        --server->ipcIdleInhibitors;
    updateIdleInhibited(server);
}

static void idleInhibitorDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct IdleInhibitor *inhibitor = wl_container_of(listener, inhibitor, destroy);
    struct Server *server = inhibitor->server;
    wl_list_remove(&inhibitor->destroy.link);
    wl_list_remove(&inhibitor->link);
    free(inhibitor);
    if (server->idleInhibitors > 0)
        --server->idleInhibitors;
    updateIdleInhibited(server);
}

static void handleNewIdleInhibitor(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newIdleInhibitor);
    struct wlr_idle_inhibitor_v1 *wlrInhibitor = data;
    struct IdleInhibitor *inhibitor = xcalloc(1, sizeof(*inhibitor));
    inhibitor->server = server;
    inhibitor->inhibitor = wlrInhibitor;
    inhibitor->destroy.notify = idleInhibitorDestroy;
    wl_signal_add(&wlrInhibitor->events.destroy, &inhibitor->destroy);
    wl_list_insert(&server->idleInhibitorList, &inhibitor->link);
    ++server->idleInhibitors;
    updateIdleInhibited(server);
}

// ---------------------------------------------------------------------------
// Input methods (IBus, fcitx5): a relay between the text fields of
// applications (text-input-v3) and the input method (input-method-v2).
// ---------------------------------------------------------------------------

#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR >= 20
#define NT_NEW_TEXT_INPUT_EVENT new_text_input
#define NT_NEW_INPUT_METHOD_EVENT new_input_method
#else
#define NT_NEW_TEXT_INPUT_EVENT text_input
#define NT_NEW_INPUT_METHOD_EVENT input_method
#endif

static struct wlr_input_method_keyboard_grab_v2 *keyboardImGrab(struct Keyboard *keyboard)
{
    struct Server *server = keyboard->server;
    struct wlr_input_method_v2 *method = server->inputMethod;
    // The lock screen's password never goes through an input method.
    if (!method || !method->keyboard_grab || server->locked)
        return NULL;
    // The input method's own virtual keyboard types into the application.
    struct wlr_virtual_keyboard_v1 *virtual =
        wlr_input_device_get_virtual_keyboard(&keyboard->keyboard->base);
    if (virtual && wl_resource_get_client(virtual->resource)
                       == wl_resource_get_client(method->keyboard_grab->resource))
        return NULL;
    return method->keyboard_grab;
}

// The text field that has the focus (preferably an enabled one).
static struct TextInput *focusedTextInput(struct Server *server)
{
    struct TextInput *textInput;
    struct TextInput *fallback = NULL;
    wl_list_for_each(textInput, &server->textInputs, link) {
        if (!textInput->input->focused_surface)
            continue;
        if (textInput->input->current_enabled)
            return textInput;
        if (!fallback)
            fallback = textInput;
    }
    return fallback;
}

struct SurfaceSearch {
    struct wlr_surface *surface;
    int x;
    int y;
    bool found;
};

static void findSurfaceBuffer(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
    struct SurfaceSearch *search = data;
    if (search->found)
        return;
    struct wlr_scene_surface *sceneSurface = wlr_scene_surface_try_from_buffer(buffer);
    if (sceneSurface && sceneSurface->surface == search->surface) {
        search->x = sx;
        search->y = sy;
        search->found = true;
    }
}

// Where a surface is drawn, in layout coordinates.
static bool surfaceLayoutPosition(struct Server *server, struct wlr_surface *surface, int *x, int *y)
{
    struct SurfaceSearch search = {.surface = surface};
    if (!surface)
        return false;
    wlr_scene_node_for_each_buffer(&server->scene->tree.node, findSurfaceBuffer, &search);
    *x = search.x;
    *y = search.y;
    return search.found;
}

static void updateInputPopup(struct InputPopup *popup)
{
    struct Server *server = popup->server;
    struct TextInput *textInput = focusedTextInput(server);
    int surfaceX = 0;
    int surfaceY = 0;
    if (!textInput || !textInput->input->current_enabled || server->locked
        || !popup->popup->surface->mapped
        || !surfaceLayoutPosition(server, textInput->input->focused_surface, &surfaceX, &surfaceY)) {
        wlr_scene_node_set_enabled(&popup->tree->node, false);
        return;
    }
    // Under the text cursor; above it when there is no room below, and
    // always on the screen that shows the cursor.
    struct wlr_box cursor = textInput->input->current.cursor_rectangle;
    int width = popup->popup->surface->current.width;
    int height = popup->popup->surface->current.height;
    int cursorX = surfaceX + cursor.x;
    int cursorY = surfaceY + cursor.y;
    int x = cursorX;
    int y = cursorY + cursor.height;
    struct wlr_output *output = wlr_output_layout_output_at(server->outputLayout, cursorX, cursorY);
    if (output) {
        struct wlr_box box;
        wlr_output_layout_get_box(server->outputLayout, output, &box);
        if (x + width > box.x + box.width)
            x = box.x + box.width - width;
        if (x < box.x)
            x = box.x;
        if (y + height > box.y + box.height)
            y = cursorY - height;
        if (y < box.y)
            y = box.y;
    }
    wlr_scene_node_set_position(&popup->tree->node, x, y);
    wlr_scene_node_set_enabled(&popup->tree->node, true);
    // The cursor relative to the popup, sent only when it changed: the
    // input method may commit again on every rectangle.
    struct wlr_box rectangle = {
        .x = cursorX - x, .y = cursorY - y, .width = cursor.width, .height = cursor.height};
    if (memcmp(&rectangle, &popup->sentRectangle, sizeof(rectangle)) != 0) {
        popup->sentRectangle = rectangle;
        wlr_input_popup_surface_v2_send_text_input_rectangle(popup->popup, &rectangle);
    }
    scheduleAllFrames(server);
}

static void updateInputPopups(struct Server *server)
{
    struct InputPopup *popup;
    wl_list_for_each(popup, &server->inputPopups, link)
        updateInputPopup(popup);
}

static void inputPopupMap(struct wl_listener *listener, void *data)
{
    (void)data;
    struct InputPopup *popup = wl_container_of(listener, popup, map);
    updateInputPopup(popup);
}

static void inputPopupUnmap(struct wl_listener *listener, void *data)
{
    (void)data;
    struct InputPopup *popup = wl_container_of(listener, popup, unmap);
    wlr_scene_node_set_enabled(&popup->tree->node, false);
    scheduleAllFrames(popup->server);
}

static void inputPopupCommit(struct wl_listener *listener, void *data)
{
    (void)data;
    struct InputPopup *popup = wl_container_of(listener, popup, commit);
    if (popup->popup->surface->mapped)
        updateInputPopup(popup);
}

static void inputPopupDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct InputPopup *popup = wl_container_of(listener, popup, destroy);
    wl_list_remove(&popup->map.link);
    wl_list_remove(&popup->unmap.link);
    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);
    wl_list_remove(&popup->link);
    wlr_scene_node_destroy(&popup->tree->node);
    scheduleAllFrames(popup->server);
    free(popup);
}

static void handleInputMethodNewPopup(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, inputMethodNewPopup);
    struct wlr_input_popup_surface_v2 *surface = data;
    struct InputPopup *popup = xcalloc(1, sizeof(*popup));
    popup->server = server;
    popup->popup = surface;
    // An own tree around the surface tree: the latter goes away with the
    // surface by itself, ours with the popup.
    popup->tree = wlr_scene_tree_create(server->inputPopupTree);
    wlr_scene_subsurface_tree_create(popup->tree, surface->surface);
    wlr_scene_node_set_enabled(&popup->tree->node, false);
    popup->sentRectangle.x = -1;
    popup->map.notify = inputPopupMap;
    wl_signal_add(&surface->surface->events.map, &popup->map);
    popup->unmap.notify = inputPopupUnmap;
    wl_signal_add(&surface->surface->events.unmap, &popup->unmap);
    popup->commit.notify = inputPopupCommit;
    wl_signal_add(&surface->surface->events.commit, &popup->commit);
    popup->destroy.notify = inputPopupDestroy;
    wl_signal_add(&surface->events.destroy, &popup->destroy);
    wl_list_insert(&server->inputPopups, &popup->link);
}

static void sendTextInputState(struct Server *server, struct TextInput *textInput)
{
    struct wlr_input_method_v2 *method = server->inputMethod;
    struct wlr_text_input_v3 *input = textInput->input;
    if (input->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT) {
        wlr_input_method_v2_send_surrounding_text(method,
                                                  input->current.surrounding.text
                                                      ? input->current.surrounding.text
                                                      : "",
                                                  input->current.surrounding.cursor,
                                                  input->current.surrounding.anchor);
    }
    wlr_input_method_v2_send_text_change_cause(method, input->current.text_change_cause);
    if (input->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE) {
        wlr_input_method_v2_send_content_type(method, input->current.content_type.hint,
                                              input->current.content_type.purpose);
    }
    wlr_input_method_v2_send_done(method);
}

static void disableTextInput(struct Server *server, struct TextInput *textInput)
{
    if (!server->inputMethod)
        return;
    wlr_input_method_v2_send_deactivate(server->inputMethod);
    sendTextInputState(server, textInput);
}

static void textInputPendingFocusDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct TextInput *textInput = wl_container_of(listener, textInput, pendingFocusDestroy);
    wl_list_remove(&textInput->pendingFocusDestroy.link);
    wl_list_init(&textInput->pendingFocusDestroy.link);
    textInput->pendingFocus = NULL;
}

static void textInputSetPendingFocus(struct TextInput *textInput, struct wlr_surface *surface)
{
    wl_list_remove(&textInput->pendingFocusDestroy.link);
    wl_list_init(&textInput->pendingFocusDestroy.link);
    textInput->pendingFocus = surface;
    if (surface)
        wl_signal_add(&surface->events.destroy, &textInput->pendingFocusDestroy);
}

static void textInputEnable(struct wl_listener *listener, void *data)
{
    (void)data;
    struct TextInput *textInput = wl_container_of(listener, textInput, enable);
    struct Server *server = textInput->server;
    if (!server->inputMethod)
        return;
    wlr_input_method_v2_send_activate(server->inputMethod);
    sendTextInputState(server, textInput);
    updateInputPopups(server);
}

static void textInputCommit(struct wl_listener *listener, void *data)
{
    (void)data;
    struct TextInput *textInput = wl_container_of(listener, textInput, commit);
    struct Server *server = textInput->server;
    if (!server->inputMethod || !textInput->input->current_enabled)
        return;
    sendTextInputState(server, textInput);
    updateInputPopups(server);
}

static void textInputDisable(struct wl_listener *listener, void *data)
{
    (void)data;
    struct TextInput *textInput = wl_container_of(listener, textInput, disable);
    disableTextInput(textInput->server, textInput);
    updateInputPopups(textInput->server);
}

static void textInputDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct TextInput *textInput = wl_container_of(listener, textInput, destroy);
    struct Server *server = textInput->server;
    if (textInput->input->current_enabled)
        disableTextInput(server, textInput);
    textInputSetPendingFocus(textInput, NULL);
    wl_list_remove(&textInput->pendingFocusDestroy.link);
    wl_list_remove(&textInput->enable.link);
    wl_list_remove(&textInput->commit.link);
    wl_list_remove(&textInput->disable.link);
    wl_list_remove(&textInput->destroy.link);
    wl_list_remove(&textInput->link);
    free(textInput);
    updateInputPopups(server);
}

static bool sameClient(struct wlr_text_input_v3 *input, struct wlr_surface *surface)
{
    return surface && wl_resource_get_client(input->resource) == wl_resource_get_client(surface->resource);
}

// The keyboard focus moved: text fields follow it. Without an input method
// the focus is only remembered (see TextInput.pendingFocus).
static void textInputsSetFocus(struct Server *server, struct wlr_surface *surface)
{
    if (server->locked)
        surface = NULL;
    struct TextInput *textInput;
    wl_list_for_each(textInput, &server->textInputs, link) {
        struct wlr_text_input_v3 *input = textInput->input;
        if (textInput->pendingFocus) {
            if (surface != textInput->pendingFocus)
                textInputSetPendingFocus(textInput, NULL);
        } else if (input->focused_surface) {
            if (surface == input->focused_surface)
                continue;
            if (input->current_enabled)
                disableTextInput(server, textInput);
            wlr_text_input_v3_send_leave(input);
        }
        if (sameClient(input, surface)) {
            if (server->inputMethod)
                wlr_text_input_v3_send_enter(input, surface);
            else
                textInputSetPendingFocus(textInput, surface);
        }
    }
    updateInputPopups(server);
}

static void handleKeyboardFocusChange(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, keyboardFocusChange);
    struct wlr_seat_keyboard_focus_change_event *event = data;
    textInputsSetFocus(server, event->new_surface);
    updateShortcutsInhibitors(server, event->new_surface);
    // Another window came to the front (Alt+Tab, a new window, one that
    // closed): what is under a pointer that did not move may have changed,
    // and a pointer lock belongs to the window with the keyboard.
    if (!server->locked && server->cursorMode == CURSOR_PASSTHROUGH && !server->seat->drag
        && server->seat->pointer_state.button_count == 0)
        refreshPointerFocus(server);
    else
        updateActiveConstraint(server);
}

static void handleNewTextInput(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newTextInput);
    struct wlr_text_input_v3 *input = data;
    struct TextInput *textInput = xcalloc(1, sizeof(*textInput));
    textInput->server = server;
    textInput->input = input;
    wl_list_init(&textInput->pendingFocusDestroy.link);
    textInput->pendingFocusDestroy.notify = textInputPendingFocusDestroy;
    textInput->enable.notify = textInputEnable;
    wl_signal_add(&input->events.enable, &textInput->enable);
    textInput->commit.notify = textInputCommit;
    wl_signal_add(&input->events.commit, &textInput->commit);
    textInput->disable.notify = textInputDisable;
    wl_signal_add(&input->events.disable, &textInput->disable);
    textInput->destroy.notify = textInputDestroy;
    wl_signal_add(&input->events.destroy, &textInput->destroy);
    wl_list_insert(&server->textInputs, &textInput->link);
    // Its window may already have the keyboard.
    struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
    if (!server->locked && sameClient(input, focused)) {
        if (server->inputMethod)
            wlr_text_input_v3_send_enter(input, focused);
        else
            textInputSetPendingFocus(textInput, focused);
    }
}

static void handleInputMethodCommit(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, inputMethodCommit);
    struct wlr_input_method_v2 *method = data;
    struct TextInput *textInput = focusedTextInput(server);
    if (!textInput)
        return;
    struct wlr_text_input_v3 *input = textInput->input;
    if (method->current.preedit.text) {
        wlr_text_input_v3_send_preedit_string(input, method->current.preedit.text,
                                              method->current.preedit.cursor_begin,
                                              method->current.preedit.cursor_end);
    }
    if (method->current.commit_text)
        wlr_text_input_v3_send_commit_string(input, method->current.commit_text);
    if (method->current.delete.before_length || method->current.delete.after_length) {
        wlr_text_input_v3_send_delete_surrounding_text(input, method->current.delete.before_length,
                                                       method->current.delete.after_length);
    }
    wlr_text_input_v3_send_done(input);
}

static void handleInputMethodGrabDestroy(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, inputMethodGrabDestroy);
    struct wlr_input_method_keyboard_grab_v2 *grab = data;
    wl_list_remove(&server->inputMethodGrabDestroy.link);
    wl_list_init(&server->inputMethodGrabDestroy.link);
    // The application gets the modifiers the input method had.
    if (grab->keyboard)
        wlr_seat_keyboard_notify_modifiers(server->seat, &grab->keyboard->modifiers);
}

static void handleInputMethodGrabKeyboard(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, inputMethodGrabKeyboard);
    struct wlr_input_method_keyboard_grab_v2 *grab = data;
    wlr_input_method_keyboard_grab_v2_set_keyboard(grab, wlr_seat_get_keyboard(server->seat));
    wl_list_remove(&server->inputMethodGrabDestroy.link);
    server->inputMethodGrabDestroy.notify = handleInputMethodGrabDestroy;
    wl_signal_add(&grab->events.destroy, &server->inputMethodGrabDestroy);
}

static void handleInputMethodDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Server *server = wl_container_of(listener, server, inputMethodDestroy);
    wl_list_remove(&server->inputMethodCommit.link);
    wl_list_remove(&server->inputMethodNewPopup.link);
    wl_list_remove(&server->inputMethodGrabKeyboard.link);
    wl_list_remove(&server->inputMethodDestroy.link);
    wl_list_remove(&server->inputMethodGrabDestroy.link);
    wl_list_init(&server->inputMethodGrabDestroy.link);
    server->inputMethod = NULL;
    // Text fields wait for the next input method.
    struct TextInput *textInput;
    wl_list_for_each(textInput, &server->textInputs, link) {
        struct wlr_surface *focused = textInput->input->focused_surface;
        if (focused) {
            textInputSetPendingFocus(textInput, focused);
            wlr_text_input_v3_send_leave(textInput->input);
        }
    }
    wlr_log(WLR_INFO, "input method went away");
}

static void handleNewInputMethod(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newInputMethod);
    struct wlr_input_method_v2 *method = data;
    if (server->inputMethod) {
        wlr_input_method_v2_send_unavailable(method);
        return;
    }
    server->inputMethod = method;
    server->inputMethodCommit.notify = handleInputMethodCommit;
    wl_signal_add(&method->events.commit, &server->inputMethodCommit);
    server->inputMethodNewPopup.notify = handleInputMethodNewPopup;
    wl_signal_add(&method->events.new_popup_surface, &server->inputMethodNewPopup);
    server->inputMethodGrabKeyboard.notify = handleInputMethodGrabKeyboard;
    wl_signal_add(&method->events.grab_keyboard, &server->inputMethodGrabKeyboard);
    server->inputMethodDestroy.notify = handleInputMethodDestroy;
    wl_signal_add(&method->events.destroy, &server->inputMethodDestroy);
    // Text fields that got the focus before the input method started.
    struct TextInput *textInput;
    wl_list_for_each(textInput, &server->textInputs, link) {
        struct wlr_surface *surface = textInput->pendingFocus;
        if (surface) {
            textInputSetPendingFocus(textInput, NULL);
            wlr_text_input_v3_send_enter(textInput->input, surface);
        }
    }
    wlr_log(WLR_INFO, "input method connected");
}

static void setupInputMethods(struct Server *server)
{
    wl_list_init(&server->textInputs);
    wl_list_init(&server->inputPopups);
    wl_list_init(&server->inputMethodGrabDestroy.link);
    struct wlr_text_input_manager_v3 *textInputs = wlr_text_input_manager_v3_create(server->display);
    server->newTextInput.notify = handleNewTextInput;
    wl_signal_add(&textInputs->events.NT_NEW_TEXT_INPUT_EVENT, &server->newTextInput);
    struct wlr_input_method_manager_v2 *methods = wlr_input_method_manager_v2_create(server->display);
    server->newInputMethod.notify = handleNewInputMethod;
    wl_signal_add(&methods->events.NT_NEW_INPUT_METHOD_EVENT, &server->newInputMethod);
    server->keyboardFocusChange.notify = handleKeyboardFocusChange;
    wl_signal_add(&server->seat->keyboard_state.events.focus_change, &server->keyboardFocusChange);
}

// ---------------------------------------------------------------------------
// Control socket
// ---------------------------------------------------------------------------
//
// $XDG_RUNTIME_DIR/newteller-<WAYLAND_DISPLAY>.sock, exported to every child
// as NEWTELLER_IPC. One command per line, one reply line per command
// ("ok ..." or "error ..."). After `watch`, the connection also receives
// "event ..." lines (keyboard layout, lock state).
// `newteller-compositor --msg "<command>"` is the command-line client.

static void ipcClientDestroy(struct IpcClient *client)
{
    ipcSetIdleInhibit(client, false);
    if (client->source)
        wl_event_source_remove(client->source);
    close(client->fd);
    wl_list_remove(&client->link);
    free(client);
}

static void ipcSend(struct IpcClient *client, const char *line)
{
    // The line and its newline go out in one call, whatever the length.
    char newline = '\n';
    struct iovec parts[2] = {
        {.iov_base = (void *)line, .iov_len = strlen(line)},
        {.iov_base = &newline, .iov_len = 1},
    };
    struct msghdr message = {.msg_iov = parts, .msg_iovlen = 2};
    // Non-blocking: a client that stops reading loses events, it never
    // blocks the compositor.
    ssize_t written = sendmsg(client->fd, &message, MSG_NOSIGNAL);
    (void)written;
}

static bool ipcHasWatchers(struct Server *server)
{
    if (!server->ipcClients.next)
        return false;
    struct IpcClient *client;
    wl_list_for_each(client, &server->ipcClients, link) {
        if (client->watching)
            return true;
    }
    return false;
}

static void ipcBroadcast(struct Server *server, const char *line)
{
    if (!server->ipcClients.next)
        return;
    struct IpcClient *client;
    wl_list_for_each(client, &server->ipcClients, link) {
        if (client->watching)
            ipcSend(client, line);
    }
}

static void ipcDescribeOutputs(struct Server *server, struct Text *out)
{
    textAppend(out, "ok");
    struct Output *output;
    wl_list_for_each(output, &server->outputs, link) {
        struct wlr_output *o = output->output;
        struct wlr_box box = {0};
        wlr_output_layout_get_box(server->outputLayout, o, &box);
        textAppend(out, " %s:%dx%d@%d:%d,%d:%.2f:%s", o->name, o->width, o->height,
                   o->refresh, box.x, box.y, o->scale, output->disabled ? "off" : "on");
    }
}

// ok <name>\x1e<description>\x1e<on|off>\x1e<WxH@mHz>\x1e<x>,<y>\x1e<scale>
//    \x1e<transform>\x1e<mode>,<mode>,...  joined by \x1f
static void ipcDescribeOutputsDetail(struct Server *server, struct IpcClient *client)
{
    struct Text reply = {0};
    textAppend(&reply, "ok ");
    bool first = true;
    struct Output *output;
    wl_list_for_each(output, &server->outputs, link) {
        struct wlr_output *o = output->output;
        struct wlr_box box = {0};
        wlr_output_layout_get_box(server->outputLayout, o, &box);
        char description[96];
        char rawDescription[160];
        snprintf(rawDescription, sizeof(rawDescription), "%s %s",
                 o->make ? o->make : "", o->model ? o->model : "");
        ipcField(description, sizeof(description), rawDescription, false);
        int width = o->current_mode ? o->current_mode->width : o->width;
        int height = o->current_mode ? o->current_mode->height : o->height;
        int refresh = o->current_mode ? o->current_mode->refresh : o->refresh;
        textAppend(&reply, "%s%s\x1e%s\x1e%s\x1e%dx%d@%d\x1e%d,%d\x1e%.2f\x1e%d\x1e",
                   first ? "" : "\x1f", o->name, description,
                   output->disabled ? "off" : "on", width, height, refresh, box.x, box.y,
                   o->scale, (int)o->transform);
        // The modes, without repeating one (some monitors list duplicates).
        struct wlr_output_mode *mode;
        int listed = 0;
        int lastWidth = -1, lastHeight = -1, lastRefresh = -1;
        wl_list_for_each(mode, &o->modes, link) {
            if (mode->width == lastWidth && mode->height == lastHeight
                && mode->refresh / 1000 == lastRefresh / 1000)
                continue;
            lastWidth = mode->width;
            lastHeight = mode->height;
            lastRefresh = mode->refresh;
            textAppend(&reply, "%s%dx%d@%d", listed ? "," : "", mode->width, mode->height,
                       mode->refresh);
            if (++listed >= 40)
                break;
        }
        first = false;
    }
    ipcSend(client, reply.data);
    textFree(&reply);
}

// output-set <name> <on|off> <WxH@mHz|-> <x> <y> <scale> <transform>
static void ipcSetOutput(struct Server *server, struct IpcClient *client, char *name)
{
    char *enabledText = strtok(NULL, " \t\r");
    char *modeText = strtok(NULL, " \t\r");
    char *xText = strtok(NULL, " \t\r");
    char *yText = strtok(NULL, " \t\r");
    char *scaleText = strtok(NULL, " \t\r");
    char *transformText = strtok(NULL, " \t\r");
    if (!enabledText || !modeText || !xText || !yText || !scaleText || !transformText) {
        ipcSend(client, "error usage: output-set NAME on|off WxH@mHz X Y SCALE TRANSFORM");
        return;
    }
    struct Output *target = NULL;
    struct Output *output;
    int enabledOthers = 0;
    wl_list_for_each(output, &server->outputs, link) {
        if (strcmp(output->output->name, name) == 0)
            target = output;
        else if (!output->disabled)
            ++enabledOthers;
    }
    if (!target) {
        ipcSend(client, "error no such output");
        return;
    }
    bool enabled = strcmp(enabledText, "off") != 0;
    if (!enabled && enabledOthers == 0) {
        ipcSend(client, "error the last screen cannot be turned off");
        return;
    }
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, enabled);
    if (enabled) {
        int width = 0, height = 0, refresh = 0;
        if (sscanf(modeText, "%dx%d@%d", &width, &height, &refresh) >= 2) {
            struct wlr_output_mode *mode = findMode(target->output, width, height, refresh);
            if (mode)
                wlr_output_state_set_mode(&state, mode);
        }
        double scale = atof(scaleText);
        if (scale >= 0.5 && scale <= 4.0)
            wlr_output_state_set_scale(&state, (float)scale);
        int transform = atoi(transformText);
        if (transform >= 0 && transform <= 7)
            wlr_output_state_set_transform(&state, (enum wl_output_transform)transform);
    }
    bool ok = wlr_output_commit_state(target->output, &state);
    wlr_output_state_finish(&state);
    if (!ok) {
        ipcSend(client, "error the screen rejected this mode");
        return;
    }
    target->disabled = !enabled;
    target->poweredOff = false;
    placeOutput(server, target, enabled, false, atoi(xText), atoi(yText));
    if (enabled)
        wlr_xcursor_manager_load(server->cursorManager, target->output->scale);
    arrangeAllOutputs(server);
    rescueViews(server);
    updateOutputManager(server);
    saveOutputs(server);
    struct View *view;
    wl_list_for_each(view, &server->views, link)
        viewUpdateDecoration(view);
    ipcSend(client, "ok");
}

static void ipcHandleCommand(struct IpcClient *client, char *line)
{
    struct Server *server = client->server;
    char *command = strtok(line, " \t\r");
    char *argument = strtok(NULL, " \t\r");
    if (!command)
        return;

    if (strcmp(command, "ping") == 0) {
        ipcSend(client, "ok pong");
    } else if (strcmp(command, "reload") == 0) {
        reloadConfig(server);
        ipcSend(client, "ok");
    } else if (strcmp(command, "watch") == 0) {
        client->watching = true;
        ipcSend(client, "ok");
        broadcastLayout(server);
        broadcastWorkspaces(server);
        broadcastPointerOutput(server);
        broadcastFullscreen(server, true);
        ipcSend(client, server->locked ? "event locked 1" : "event locked 0");
    } else if (strcmp(command, "layout") == 0) {
        if (!argument) {
            char reply[64];
            snprintf(reply, sizeof(reply), "ok %d %d", server->layoutIndex,
                     server->layoutCount);
            ipcSend(client, reply);
            return;
        }
        if (strcmp(argument, "next") == 0)
            setLayout(server, server->layoutIndex + 1);
        else if (strcmp(argument, "prev") == 0)
            setLayout(server, server->layoutIndex - 1);
        else
            setLayout(server, atoi(argument));
        ipcSend(client, "ok");
    } else if (strcmp(command, "windows") == 0) {
        // ok <id>\x1e<desktop>\x1e<app id>\x1e<title>\x1e<flags>\x1e<pid>
        //    \x1e<x>,<y>,<width>,<height>\x1f...
        // flags: see viewFlags() ("-" for none). Most recent first. The
        // geometry is the window's content, in layout coordinates.
        struct Text reply = {0};
        textAppend(&reply, "ok ");
        bool first = true;
        struct View *view;
        wl_list_for_each(view, &server->views, link) {
            if (!view->mapped || view->overrideRedirect || !viewSurface(view))
                continue;
            char title[120];
            ipcField(title, sizeof(title), viewTitle(view), false);
            char appId[96];
            ipcField(appId, sizeof(appId), viewAppId(view), true);
            char flags[10];
            viewFlags(view, flags, sizeof(flags));
            textAppend(&reply, "%s%u\x1e%d\x1e%s\x1e%s\x1e%s\x1e%d\x1e%d,%d,%d,%d",
                       first ? "" : "\x1f", view->id, view->workspace, appId, title, flags,
                       (int)viewPid(view), view->x, view->y, view->width, view->height);
            first = false;
        }
        ipcSend(client, reply.data);
        textFree(&reply);
    } else if (strcmp(command, "window-menu") == 0 && !argument) {
        // The focused window's menu, at its corner (as Alt+Space).
        struct View *view = server->focusedView;
        if (!view) {
            ipcSend(client, "error no focused window");
            return;
        }
        requestWindowMenu(view, view->x + 16, view->y + 8);
        ipcSend(client, "ok");
    } else if ((strcmp(command, "thumbnail") == 0 || strcmp(command, "activate") == 0
                || strcmp(command, "close") == 0 || strcmp(command, "move-window") == 0
                || strcmp(command, "kill") == 0 || strcmp(command, "minimize") == 0
                || strcmp(command, "maximize") == 0 || strcmp(command, "fullscreen") == 0
                || strcmp(command, "always-on-top") == 0 || strcmp(command, "sticky") == 0
                || strcmp(command, "move-to-output") == 0
                || strcmp(command, "window-menu") == 0)
               && argument) {
        uint32_t id = (uint32_t)strtoul(argument, NULL, 10);
        struct View *target = NULL;
        struct View *view;
        wl_list_for_each(view, &server->views, link) {
            if (view->id == id && view->mapped && !view->overrideRedirect) {
                target = view;
                break;
            }
        }
        if (!target) {
            ipcSend(client, "error no such window");
            return;
        }
        if (strcmp(command, "thumbnail") == 0) {
            char *sizeText = strtok(NULL, " \t\r");
            int size = sizeText ? atoi(sizeText) : 320;
            if (size < 32 || size > 1024)
                size = 320;
            char path[PATH_MAX];
            thumbnailPath(target, path, sizeof(path));
            if (captureThumbnail(target, size)) {
                char reply[PATH_MAX + 8];
                snprintf(reply, sizeof(reply), "ok %s", path);
                ipcSend(client, reply);
            } else {
                ipcSend(client, "error no image");
            }
        } else if (strcmp(command, "activate") == 0) {
            if (target->minimized)
                viewSetMinimized(target, false);
            focusView(target, NULL);
            ipcSend(client, "ok");
        } else if (strcmp(command, "close") == 0) {
            viewClose(target);
            ipcSend(client, "ok");
        } else if (strcmp(command, "minimize") == 0) {
            // minimize ID [on|off|toggle]: on by default.
            char *state = strtok(NULL, " \t\r");
            bool minimize = state ? ipcToggle(state, target->minimized) : true;
            if (minimize != target->minimized) {
                viewSetMinimized(target, minimize);
                if (!minimize)
                    focusView(target, NULL);
            }
            ipcSend(client, "ok");
        } else if (strcmp(command, "maximize") == 0) {
            bool maximize = ipcToggle(strtok(NULL, " \t\r"), target->maximized);
            if (target->minimized)
                viewSetMinimized(target, false);
            if (maximize != target->maximized)
                viewSetMaximized(target, maximize);
            focusView(target, NULL);
            ipcSend(client, "ok");
        } else if (strcmp(command, "fullscreen") == 0) {
            bool fullscreen = ipcToggle(strtok(NULL, " \t\r"), target->fullscreen);
            if (target->minimized)
                viewSetMinimized(target, false);
            if (fullscreen != target->fullscreen)
                viewSetFullscreen(target, fullscreen);
            focusView(target, NULL);
            ipcSend(client, "ok");
        } else if (strcmp(command, "move-to-output") == 0) {
            // move-to-output ID [next|prev]: as Super+Shift+Right/Left.
            char *which = strtok(NULL, " \t\r");
            if (target->minimized)
                viewSetMinimized(target, false);
            if (target->maximized)
                viewSetMaximized(target, false);
            moveViewToOutput(target, which && strcmp(which, "prev") == 0 ? -1 : 1);
            focusView(target, NULL);
            ipcSend(client, "ok");
        } else if (strcmp(command, "sticky") == 0) {
            // sticky ID [on|off|toggle]: shown on every desktop. Taken off,
            // the window stays on the desktop being shown.
            target->sticky = ipcToggle(strtok(NULL, " \t\r"), target->sticky);
            if (!target->sticky)
                target->workspace = server->workspace;
            viewSyncVisible(target);
            broadcastWorkspaces(server);
            ipcSend(client, target->sticky ? "ok 1" : "ok 0");
        } else if (strcmp(command, "always-on-top") == 0) {
            viewSetAlwaysOnTop(target, ipcToggle(strtok(NULL, " \t\r"), target->alwaysOnTop));
            ipcSend(client, target->alwaysOnTop ? "ok 1" : "ok 0");
        } else if (strcmp(command, "window-menu") == 0) {
            char *x = strtok(NULL, " \t\r");
            char *y = strtok(NULL, " \t\r");
            requestWindowMenu(target, x ? atoi(x) : target->x + 16,
                              y ? atoi(y) : target->y + 8);
            ipcSend(client, "ok");
        } else if (strcmp(command, "kill") == 0) {
            // Force quit: the process behind the window, never the session.
            pid_t pid = viewPid(target);
            if (pid <= 1 || pid == getpid() || pid == server->shellPid) {
                ipcSend(client, "error this window cannot be force-quit");
                return;
            }
            if (kill(pid, SIGKILL) != 0) {
                ipcSend(client, "error the process could not be ended");
                return;
            }
            wlr_log(WLR_INFO, "force-quit pid %d (%s)", (int)pid, viewTitle(target));
            ipcSend(client, "ok");
        } else {
            char *desktop = strtok(NULL, " \t\r");
            if (desktop)
                moveViewToWorkspace(target, atoi(desktop), false);
            ipcSend(client, desktop ? "ok" : "error which desktop");
        }
    } else if (strcmp(command, "blur") == 0 && argument) {
        // blur <namespace> <output> <x> <y> <w> <h> <radius>
        char *output = strtok(NULL, " \t\r");
        char *x = strtok(NULL, " \t\r");
        char *y = strtok(NULL, " \t\r");
        char *w = strtok(NULL, " \t\r");
        char *h = strtok(NULL, " \t\r");
        char *radius = strtok(NULL, " \t\r");
        if (!output || !x || !y || !w || !h || !radius) {
            ipcSend(client, "error usage: blur NAMESPACE OUTPUT X Y W H RADIUS");
            return;
        }
        setBlurRegion(server, argument, output,
                      (struct wlr_box){atoi(x), atoi(y), atoi(w), atoi(h)}, atoi(radius));
        ipcSend(client, "ok");
    } else if (strcmp(command, "effects") == 0) {
        char reply[96];
        snprintf(reply, sizeof(reply), "ok %s %llu %llu", server->config.effects ? "on" : "off",
                 (unsigned long long)server->effectFrames,
                 (unsigned long long)server->fallbackFrames);
        ipcSend(client, reply);
    } else if (strcmp(command, "blur-clear") == 0 && argument) {
        char *output = strtok(NULL, " \t\r");
        clearBlurRegion(server, argument, output ? output : "");
        ipcSend(client, "ok");
    } else if (strcmp(command, "outputs-detail") == 0) {
        ipcDescribeOutputsDetail(server, client);
    } else if (strcmp(command, "output-set") == 0 && argument) {
        ipcSetOutput(server, client, argument);
    } else if (strcmp(command, "workspace") == 0 && argument) {
        if (strcmp(argument, "next") == 0)
            switchWorkspace(server, server->workspace + 1);
        else if (strcmp(argument, "prev") == 0)
            switchWorkspace(server, server->workspace - 1);
        else
            switchWorkspace(server, atoi(argument));
        ipcSend(client, "ok");
    } else if (strcmp(command, "move-to-workspace") == 0 && argument) {
        if (server->focusedView)
            moveViewToWorkspace(server->focusedView, atoi(argument), false);
        ipcSend(client, "ok");
    } else if (strcmp(command, "idle-inhibit") == 0) {
        if (argument)
            ipcSetIdleInhibit(client, strcmp(argument, "on") == 0 || strcmp(argument, "1") == 0);
        char reply[32];
        snprintf(reply, sizeof(reply), "ok %d", idleInhibited(server));
        ipcSend(client, reply);
    } else if (strcmp(command, "dpms") == 0 && argument) {
        setOutputsPower(server, strcmp(argument, "off") != 0);
        ipcSend(client, "ok");
    } else if (strcmp(command, "lock") == 0) {
        if (!server->locked)
            spawnLocker(server);
        ipcSend(client, "ok");
    } else if (strcmp(command, "locked") == 0) {
        ipcSend(client, server->locked ? "ok 1" : "ok 0");
    } else if (strcmp(command, "outputs") == 0) {
        struct Text reply = {0};
        ipcDescribeOutputs(server, &reply);
        ipcSend(client, reply.data);
        textFree(&reply);
    } else if (strcmp(command, "exit") == 0) {
        ipcSend(client, "ok");
        wlr_log(WLR_INFO, "session exit requested over the control socket");
        server->running = false;
        wl_display_terminate(server->display);
    } else {
        ipcSend(client, "error unknown command");
    }
}

static int ipcClientReadable(int fd, uint32_t mask, void *data)
{
    struct IpcClient *client = data;
    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
        ipcClientDestroy(client);
        return 0;
    }
    ssize_t received = recv(fd, client->buffer + client->length,
                            sizeof(client->buffer) - 1 - client->length, 0);
    if (received <= 0) {
        if (received < 0 && (errno == EAGAIN || errno == EINTR))
            return 0;
        ipcClientDestroy(client);
        return 0;
    }
    client->length += (size_t)received;
    client->buffer[client->length] = '\0';
    char *newline;
    while ((newline = memchr(client->buffer, '\n', client->length)) != NULL) {
        *newline = '\0';
        char line[sizeof(client->buffer)];
        snprintf(line, sizeof(line), "%s", client->buffer);
        size_t consumed = (size_t)(newline - client->buffer) + 1;
        memmove(client->buffer, newline + 1, client->length - consumed);
        client->length -= consumed;
        client->buffer[client->length] = '\0';
        ipcHandleCommand(client, line);
        // `exit` stops the display; the client is cleaned up with the rest.
        if (!client->server->running)
            return 0;
    }
    if (client->length >= sizeof(client->buffer) - 1)
        ipcClientDestroy(client);
    return 0;
}

static int ipcAccept(int fd, uint32_t mask, void *data)
{
    (void)mask;
    struct Server *server = data;
    int clientFd = accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (clientFd < 0)
        return 0;
    struct ucred credentials = {0};
    socklen_t length = sizeof(credentials);
    if (getsockopt(clientFd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0
        || credentials.uid != getuid()) {
        close(clientFd);
        return 0;
    }
    struct IpcClient *client = xcalloc(1, sizeof(*client));
    client->server = server;
    client->fd = clientFd;
    client->source = wl_event_loop_add_fd(server->eventLoop, clientFd,
                                          WL_EVENT_READABLE, ipcClientReadable,
                                          client);
    wl_list_insert(&server->ipcClients, &client->link);
    return 0;
}

static void ipcSocketPath(char *path, size_t size, const char *display)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    snprintf(path, size, "%s/newteller-%s.sock", runtime ? runtime : "/tmp",
             display && display[0] ? display : "wayland-0");
}

static void setupIpc(struct Server *server)
{
    server->ipcFd = -1;
    char path[PATH_MAX];
    ipcSocketPath(path, sizeof(path), server->socketName);
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(address.sun_path)) {
        wlr_log(WLR_ERROR, "control socket path is too long: %s", path);
        return;
    }
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return;
    unlink(path);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0
        || listen(fd, 16) != 0) {
        wlr_log_errno(WLR_ERROR, "cannot listen on %s", path);
        close(fd);
        return;
    }
    chmod(path, 0600);
    server->ipcFd = fd;
    snprintf(server->ipcPath, sizeof(server->ipcPath), "%s", path);
    server->ipcSource = wl_event_loop_add_fd(server->eventLoop, fd,
                                             WL_EVENT_READABLE, ipcAccept, server);
    setenv("NEWTELLER_IPC", path, 1);
}

static void teardownIpc(struct Server *server)
{
    struct IpcClient *client;
    struct IpcClient *next;
    wl_list_for_each_safe(client, next, &server->ipcClients, link)
        ipcClientDestroy(client);
    if (server->ipcSource) {
        wl_event_source_remove(server->ipcSource);
        server->ipcSource = NULL;
    }
    if (server->ipcFd >= 0) {
        close(server->ipcFd);
        server->ipcFd = -1;
        unlink(server->ipcPath);
    }
}

// `newteller-compositor --msg "<command>"`: send one command, print the reply.
static int sendCommand(const char *command)
{
    char path[PATH_MAX];
    const char *explicitPath = getenv("NEWTELLER_IPC");
    if (explicitPath && explicitPath[0])
        snprintf(path, sizeof(path), "%s", explicitPath);
    else
        ipcSocketPath(path, sizeof(path), getenv("WAYLAND_DISPLAY"));
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        fprintf(stderr, "newteller-compositor: no compositor at %s\n", path);
        if (fd >= 0)
            close(fd);
        return 1;
    }
    char line[1024];
    int length = snprintf(line, sizeof(line), "%s\n", command);
    if (length < 0 || write(fd, line, (size_t)length) != length) {
        close(fd);
        return 1;
    }
    // One reply line of any length (`windows` with many windows is long).
    char reply[4096];
    char start[3] = "";
    size_t seen = 0;
    for (;;) {
        ssize_t n = read(fd, reply, sizeof(reply));
        if (n <= 0)
            break;
        for (ssize_t i = 0; i < n && seen < 2; ++i)
            start[seen++] = reply[i];
        char *end = memchr(reply, '\n', (size_t)n);
        fwrite(reply, 1, end ? (size_t)(end - reply) + 1 : (size_t)n, stdout);
        if (end)
            break;
    }
    close(fd);
    return strcmp(start, "ok") == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Touchpad gestures
// ---------------------------------------------------------------------------
//
// Four fingers left/right switch desktops; three fingers drag the focused
// window. Everything else (two-finger pinch in a browser, other swipes) goes
// to the application through pointer-gestures-unstable-v1.

static void handleSwipeBegin(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, swipeBegin);
    struct wlr_pointer_swipe_begin_event *event = data;
    notifyActivity(server);
    server->gestureKind = 0;
    server->gestureDx = 0;
    server->gestureDy = 0;
    server->gestureView = NULL;
    if (server->config.gestures && !server->locked) {
        if (event->fingers == 4) {
            server->gestureKind = 2;
            return;
        }
        if (event->fingers == 3 && server->focusedView && !server->focusedView->fullscreen) {
            server->gestureKind = 1;
            server->gestureView = server->focusedView;
            if (server->gestureView->maximized)
                viewSetMaximized(server->gestureView, false);
            viewClearSnap(server->gestureView);
            return;
        }
    }
    wlr_pointer_gestures_v1_send_swipe_begin(server->pointerGestures, server->seat,
                                             event->time_msec, event->fingers);
}

static void handleSwipeUpdate(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, swipeUpdate);
    struct wlr_pointer_swipe_update_event *event = data;
    if (server->gestureKind == 0) {
        wlr_pointer_gestures_v1_send_swipe_update(server->pointerGestures, server->seat,
                                                  event->time_msec, event->dx, event->dy);
        return;
    }
    server->gestureDx += event->dx;
    server->gestureDy += event->dy;
    if (server->gestureKind == 1 && server->gestureView) {
        // Follow the fingers; whole pixels, the rest carries over.
        int stepX = (int)server->gestureDx;
        int stepY = (int)server->gestureDy;
        if (stepX || stepY) {
            server->gestureDx -= stepX;
            server->gestureDy -= stepY;
            viewSetPosition(server->gestureView, server->gestureView->x + stepX,
                            server->gestureView->y + stepY);
        }
    }
}

static void handleSwipeEnd(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, swipeEnd);
    struct wlr_pointer_swipe_end_event *event = data;
    int kind = server->gestureKind;
    server->gestureKind = 0;
    server->gestureView = NULL;
    if (kind == 0) {
        wlr_pointer_gestures_v1_send_swipe_end(server->pointerGestures, server->seat,
                                               event->time_msec, event->cancelled);
        return;
    }
    if (kind == 2 && !event->cancelled && fabs(server->gestureDx) > 80
        && fabs(server->gestureDx) > fabs(server->gestureDy)) {
        // Fingers to the left bring the next desktop in from the right.
        switchWorkspace(server, server->workspace + (server->gestureDx < 0 ? 1 : -1));
    }
}

static void handlePinchBegin(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, pinchBegin);
    struct wlr_pointer_pinch_begin_event *event = data;
    wlr_pointer_gestures_v1_send_pinch_begin(server->pointerGestures, server->seat,
                                             event->time_msec, event->fingers);
}

static void handlePinchUpdate(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, pinchUpdate);
    struct wlr_pointer_pinch_update_event *event = data;
    wlr_pointer_gestures_v1_send_pinch_update(server->pointerGestures, server->seat,
                                              event->time_msec, event->dx, event->dy,
                                              event->scale, event->rotation);
}

static void handlePinchEnd(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, pinchEnd);
    struct wlr_pointer_pinch_end_event *event = data;
    wlr_pointer_gestures_v1_send_pinch_end(server->pointerGestures, server->seat,
                                           event->time_msec, event->cancelled);
}

static void handleHoldBegin(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, holdBegin);
    struct wlr_pointer_hold_begin_event *event = data;
    wlr_pointer_gestures_v1_send_hold_begin(server->pointerGestures, server->seat,
                                            event->time_msec, event->fingers);
}

static void handleHoldEnd(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, holdEnd);
    struct wlr_pointer_hold_end_event *event = data;
    wlr_pointer_gestures_v1_send_hold_end(server->pointerGestures, server->seat,
                                          event->time_msec, event->cancelled);
}

// ---------------------------------------------------------------------------
// Protocol setup
// ---------------------------------------------------------------------------


static void handleActivationRequest(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, activationRequest);
    struct wlr_xdg_activation_v1_request_activate_event *event = data;
    if (!event->surface)
        return;
    struct View *view;
    wl_list_for_each(view, &server->views, link) {
        if (!view->xdg)
            continue;
        if (view->xdg->base->surface == event->surface
            || wlr_surface_get_root_surface(view->xdg->base->surface)
                   == event->surface) {
            // "Open" in a notification, a link handed to a running browser:
            // a minimized window comes back first (it cannot take the focus
            // while hidden, so the request used to do nothing).
            if (view->minimized && !server->locked)
                viewSetMinimized(view, false);
            focusView(view, event->surface);
            return;
        }
    }
    // The shell's menus and cards are layer surfaces; Qt asks for their
    // activation through xdg-activation. Without the keyboard they never
    // lost it either, so a click elsewhere did not close them.
    struct Layer *layer;
    wl_list_for_each(layer, &server->layers, link) {
        if (layer->layer && layer->layer->surface == event->surface
            && layer->layer->current.keyboard_interactive
                   != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
            focusLayer(layer);
            return;
        }
    }
}

// Protocols that read the screen, the clipboard or the input of every
// application, draw over the desktop or control outputs. A sandboxed client
// (one with a security context, e.g. a Flatpak app) never gets them.
static bool globalFilter(const struct wl_client *client, const struct wl_global *global,
                         void *data)
{
    struct Server *server = data;
    if (!server->securityContexts
        || !wlr_security_context_manager_v1_lookup_client(server->securityContexts, client))
        return true;
    static const char *const privileged[] = {
        "zwlr_layer_shell_v1",
        "zwlr_screencopy_manager_v1",
        "zwlr_export_dmabuf_manager_v1",
        "zwlr_data_control_manager_v1",
        "ext_data_control_manager_v1",
        "zwlr_foreign_toplevel_manager_v1",
        "ext_foreign_toplevel_list_v1",
        "zwlr_output_manager_v1",
        "zwlr_output_power_manager_v1",
        "zwlr_gamma_control_manager_v1",
        "zwp_virtual_keyboard_manager_v1",
        "zwlr_virtual_pointer_manager_v1",
        "zwp_input_method_manager_v2",
        "ext_session_lock_manager_v1",
        "zwp_keyboard_shortcuts_inhibit_manager_v1",
        "wp_security_context_manager_v1",
    };
    const struct wl_interface *interface = wl_global_get_interface(global);
    for (size_t i = 0; interface && i < sizeof(privileged) / sizeof(privileged[0]); ++i) {
        if (strcmp(interface->name, privileged[i]) == 0)
            return false;
    }
    return true;
}

// The display tears the manager down before the last globals; the filter
// may still run for them and must not read the freed manager.
static void handleSecurityContextsDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Server *server = wl_container_of(listener, server, securityContextsDestroy);
    wl_list_remove(&server->securityContextsDestroy.link);
    server->securityContexts = NULL;
}

#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR >= 19
// The shell plays the bell sound ("event bell"); a burst of rings (a
// terminal printing many BEL characters) makes one sound.
static void handleSystemBell(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Server *server = wl_container_of(listener, server, systemBellRing);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t ms = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
    if (server->locked || ms - server->lastBell < 250)
        return;
    server->lastBell = ms;
    ipcBroadcast(server, "event bell");
}
#endif

static void setupProtocols(struct Server *server)
{
    server->compositor = wlr_compositor_create(server->display, 6,
                                               server->renderer);
    wlr_subcompositor_create(server->display);
    wlr_data_device_manager_create(server->display);
    wlr_primary_selection_v1_device_manager_create(server->display);
    // wlr-data-control: clipboard managers and wl-copy/wl-paste work without
    // a focused window (clipboard history, screenshots to the clipboard).
    wlr_data_control_manager_v1_create(server->display);

    server->outputLayout = wlr_output_layout_create(server->display);
    server->scene = wlr_scene_create();
    server->sceneLayout = wlr_scene_attach_output_layout(server->scene,
                                                         server->outputLayout);

    server->backgroundTree = wlr_scene_tree_create(&server->scene->tree);
    server->bottomTree = wlr_scene_tree_create(&server->scene->tree);
    server->toplevelTree = wlr_scene_tree_create(&server->scene->tree);
    server->snapTree = wlr_scene_tree_create(&server->scene->tree);
    // Premultiplied: a soft blue at 25%.
    static const float snapColor[4] = {0.24f * 0.25f, 0.52f * 0.25f, 0.98f * 0.25f, 0.25f};
    server->snapRect = wlr_scene_rect_create(server->snapTree, 0, 0, snapColor);
    wlr_scene_node_set_enabled(&server->snapTree->node, false);
    server->topTree = wlr_scene_tree_create(&server->scene->tree);
    server->overlayTree = wlr_scene_tree_create(&server->scene->tree);
    server->inputPopupTree = wlr_scene_tree_create(&server->scene->tree);
    // The icon under the pointer while something is dragged.
    server->dragIconTree = wlr_scene_tree_create(&server->scene->tree);
    // The screen locker draws above everything, over an opaque backdrop that
    // hides the desktop even where a lock surface is missing or late.
    server->lockTree = wlr_scene_tree_create(&server->scene->tree);
    static const float lockBackdrop[4] = {0.05f, 0.05f, 0.07f, 1.0f};
    struct wlr_scene_rect *backdrop = wlr_scene_rect_create(
        server->lockTree, 65536, 65536, lockBackdrop);
    if (backdrop)
        wlr_scene_node_set_position(&backdrop->node, -32768, -32768);
    wlr_scene_node_set_enabled(&server->lockTree->node, false);

    // Version 6 adds the suspended state (see viewSyncVisible).
    server->xdgShell = wlr_xdg_shell_create(server->display, 6);
    server->layerShell = wlr_layer_shell_v1_create(server->display, 4);
    server->foreignToplevels = wlr_foreign_toplevel_manager_v1_create(
        server->display);
    // The read-only window list (ext-foreign-toplevel-list): screen sharing
    // pickers and other tools list windows with it.
    server->extToplevels = wlr_ext_foreign_toplevel_list_v1_create(server->display, 1);
    // xdg-foreign: a portal's file chooser or print dialog (another
    // process) becomes a child of the application window that asked.
    struct wlr_xdg_foreign_registry *foreignRegistry =
        wlr_xdg_foreign_registry_create(server->display);
    if (foreignRegistry) {
        wlr_xdg_foreign_v1_create(server->display, foreignRegistry);
        wlr_xdg_foreign_v2_create(server->display, foreignRegistry);
    }
    // security-context: Flatpak tags its sandboxed connections; they do
    // not see the privileged globals (see globalFilter).
    server->securityContexts = wlr_security_context_manager_v1_create(server->display);
    if (server->securityContexts) {
        server->securityContextsDestroy.notify = handleSecurityContextsDestroy;
        wl_signal_add(&server->securityContexts->events.destroy,
                      &server->securityContextsDestroy);
    }
    wl_display_set_global_filter(server->display, globalFilter, server);
    server->activation = wlr_xdg_activation_v1_create(server->display);
    server->activationRequest.notify = handleActivationRequest;
    wl_signal_add(&server->activation->events.request_activate,
                  &server->activationRequest);
    // xdg-decoration: windows that want a compositor-drawn title bar (Qt,
    // Chromium with "use system title bar", foot, mpv, ...) get one.
    server->xdgDecorationManager = wlr_xdg_decoration_manager_v1_create(server->display);
    server->newXdgDecoration.notify = handleNewXdgDecoration;
    wl_signal_add(&server->xdgDecorationManager->events.new_toplevel_decoration,
                  &server->newXdgDecoration);
    // KDE's server-decoration protocol: GTK 3 (and Firefox) drop their own
    // header bar and get NewTeller's title bar with all three buttons.
    wl_list_init(&server->kdeDecorations);
    server->serverDecoration = wlr_server_decoration_manager_create(
        server->display);
    wlr_server_decoration_manager_set_default_mode(
        server->serverDecoration, server->config.decorationsServer
                                      ? WLR_SERVER_DECORATION_MANAGER_MODE_SERVER
                                      : WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT);
    server->newKdeDecoration.notify = handleNewKdeDecoration;
    wl_signal_add(&server->serverDecoration->events.new_decoration, &server->newKdeDecoration);

#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR >= 19
    wlr_presentation_create(server->display, server->backend, 1);
#else
    wlr_presentation_create(server->display, server->backend);
#endif
    wlr_screencopy_manager_v1_create(server->display);
    wlr_export_dmabuf_manager_v1_create(server->display);
    wlr_xdg_output_manager_v1_create(server->display, server->outputLayout);
    server->outputManager = wlr_output_manager_v1_create(server->display);
    server->outputManagerApply.notify = handleOutputManagerApply;
    wl_signal_add(&server->outputManager->events.apply, &server->outputManagerApply);
    server->outputManagerTest.notify = handleOutputManagerTest;
    wl_signal_add(&server->outputManager->events.test, &server->outputManagerTest);
    server->layoutChange.notify = handleLayoutChange;
    wl_signal_add(&server->outputLayout->events.change, &server->layoutChange);
    server->gammaManager = wlr_gamma_control_manager_v1_create(server->display);
    server->gammaSetGamma.notify = handleGammaSet;
    wl_signal_add(&server->gammaManager->events.set_gamma, &server->gammaSetGamma);
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR >= 19
    struct wlr_xdg_system_bell_v1 *bell = wlr_xdg_system_bell_v1_create(server->display, 1);
    wl_list_init(&server->systemBellRing.link);
    if (bell) {
        server->systemBellRing.notify = handleSystemBell;
        wl_signal_add(&bell->events.ring, &server->systemBellRing);
    }
#else
    wl_list_init(&server->systemBellRing.link);
#endif
    server->outputPowerManager = wlr_output_power_manager_v1_create(server->display);
    server->outputPowerSetMode.notify = handleOutputPowerSetMode;
    wl_signal_add(&server->outputPowerManager->events.set_mode, &server->outputPowerSetMode);
    server->idleNotifier = wlr_idle_notifier_v1_create(server->display);
    struct wlr_idle_inhibit_manager_v1 *idleInhibit =
        wlr_idle_inhibit_v1_create(server->display);
    wl_list_init(&server->idleInhibitorList);
    server->newIdleInhibitor.notify = handleNewIdleInhibitor;
    wl_signal_add(&idleInhibit->events.new_inhibitor, &server->newIdleInhibitor);
    server->lockManager = wlr_session_lock_manager_v1_create(server->display);
    server->newLock.notify = handleNewLock;
    wl_signal_add(&server->lockManager->events.new_lock, &server->newLock);
    server->relativePointers = wlr_relative_pointer_manager_v1_create(server->display);
    server->pointerConstraints = wlr_pointer_constraints_v1_create(server->display);
    struct wlr_cursor_shape_manager_v1 *cursorShapes =
        wlr_cursor_shape_manager_v1_create(server->display, 1);
    server->cursorShape.notify = handleCursorShape;
    wl_signal_add(&cursorShapes->events.request_set_shape, &server->cursorShape);
    server->newPointerConstraint.notify = handleNewPointerConstraint;
    wl_signal_add(&server->pointerConstraints->events.new_constraint, &server->newPointerConstraint);
    wlr_viewporter_create(server->display);
    wlr_fractional_scale_manager_v1_create(server->display, 1);
    wlr_single_pixel_buffer_manager_v1_create(server->display);
    wl_list_init(&server->shortcutsInhibitors);
    struct wlr_keyboard_shortcuts_inhibit_manager_v1 *shortcutsInhibit =
        wlr_keyboard_shortcuts_inhibit_v1_create(server->display);
    server->newShortcutsInhibitor.notify = handleNewShortcutsInhibitor;
    wl_signal_add(&shortcutsInhibit->events.new_inhibitor, &server->newShortcutsInhibitor);
    if (server->renderer) {
        wlr_linux_dmabuf_v1_create_with_renderer(server->display, 5,
                                                 server->renderer);
    }

    server->newXdgToplevel.notify = handleNewXdgToplevel;
    wl_signal_add(&server->xdgShell->events.new_toplevel,
                  &server->newXdgToplevel);
    server->newXdgPopup.notify = handleNewPopup;
    wl_signal_add(&server->xdgShell->events.new_popup, &server->newXdgPopup);
    server->newLayerSurface.notify = handleNewLayerSurface;
    wl_signal_add(&server->layerShell->events.new_surface,
                  &server->newLayerSurface);
}

static void setupInput(struct Server *server)
{
    server->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server->cursor, server->outputLayout);

    server->cursorManager = wlr_xcursor_manager_create(
        server->config.cursorTheme[0] ? server->config.cursorTheme : NULL,
        (uint32_t)server->config.cursorSize);
    server->newInput.notify = newInput;
    wl_signal_add(&server->backend->events.new_input, &server->newInput);

    struct wlr_virtual_keyboard_manager_v1 *virtualKeyboards =
        wlr_virtual_keyboard_manager_v1_create(server->display);
    server->newVirtualKeyboard.notify = newVirtualKeyboard;
    wl_signal_add(&virtualKeyboards->events.new_virtual_keyboard,
                  &server->newVirtualKeyboard);
    struct wlr_virtual_pointer_manager_v1 *virtualPointers =
        wlr_virtual_pointer_manager_v1_create(server->display);
    server->newVirtualPointer.notify = newVirtualPointer;
    wl_signal_add(&virtualPointers->events.new_virtual_pointer,
                  &server->newVirtualPointer);

    server->seat = wlr_seat_create(server->display, "seat0");
    setupInputMethods(server);
    server->requestCursor.notify = seatRequestCursor;
    wl_signal_add(&server->seat->events.request_set_cursor, &server->requestCursor);
    server->requestSetSelection.notify = seatRequestSetSelection;
    wl_signal_add(&server->seat->events.request_set_selection,
                  &server->requestSetSelection);
    server->requestSetPrimarySelection.notify = seatRequestSetPrimarySelection;
    wl_signal_add(&server->seat->events.request_set_primary_selection,
                  &server->requestSetPrimarySelection);
    server->requestStartDrag.notify = seatRequestStartDrag;
    wl_signal_add(&server->seat->events.request_start_drag, &server->requestStartDrag);
    server->startDrag.notify = seatStartDrag;
    wl_signal_add(&server->seat->events.start_drag, &server->startDrag);

    server->cursorMotion.notify = cursorMotion;
    wl_signal_add(&server->cursor->events.motion, &server->cursorMotion);
    server->cursorMotionAbsolute.notify = cursorMotionAbsolute;
    wl_signal_add(&server->cursor->events.motion_absolute,
                  &server->cursorMotionAbsolute);
    server->cursorButton.notify = cursorButton;
    wl_signal_add(&server->cursor->events.button, &server->cursorButton);
    server->cursorAxis.notify = cursorAxis;
    wl_signal_add(&server->cursor->events.axis, &server->cursorAxis);
    server->cursorFrame.notify = cursorFrame;
    wl_signal_add(&server->cursor->events.frame, &server->cursorFrame);
    server->touchDown.notify = handleTouchDown;
    wl_signal_add(&server->cursor->events.touch_down, &server->touchDown);
    server->touchUp.notify = handleTouchUp;
    wl_signal_add(&server->cursor->events.touch_up, &server->touchUp);
    server->touchMotion.notify = handleTouchMotion;
    wl_signal_add(&server->cursor->events.touch_motion, &server->touchMotion);
    server->touchFrame.notify = handleTouchFrame;
    wl_signal_add(&server->cursor->events.touch_frame, &server->touchFrame);
    server->tabletAxis.notify = handleTabletAxis;
    wl_signal_add(&server->cursor->events.tablet_tool_axis, &server->tabletAxis);
    server->tabletTip.notify = handleTabletTip;
    wl_signal_add(&server->cursor->events.tablet_tool_tip, &server->tabletTip);

    server->pointerGestures = wlr_pointer_gestures_v1_create(server->display);
    server->swipeBegin.notify = handleSwipeBegin;
    wl_signal_add(&server->cursor->events.swipe_begin, &server->swipeBegin);
    server->swipeUpdate.notify = handleSwipeUpdate;
    wl_signal_add(&server->cursor->events.swipe_update, &server->swipeUpdate);
    server->swipeEnd.notify = handleSwipeEnd;
    wl_signal_add(&server->cursor->events.swipe_end, &server->swipeEnd);
    server->pinchBegin.notify = handlePinchBegin;
    wl_signal_add(&server->cursor->events.pinch_begin, &server->pinchBegin);
    server->pinchUpdate.notify = handlePinchUpdate;
    wl_signal_add(&server->cursor->events.pinch_update, &server->pinchUpdate);
    server->pinchEnd.notify = handlePinchEnd;
    wl_signal_add(&server->cursor->events.pinch_end, &server->pinchEnd);
    server->holdBegin.notify = handleHoldBegin;
    wl_signal_add(&server->cursor->events.hold_begin, &server->holdBegin);
    server->holdEnd.notify = handleHoldEnd;
    wl_signal_add(&server->cursor->events.hold_end, &server->holdEnd);

    wlr_cursor_set_xcursor(server->cursor, server->cursorManager, "left_ptr");
}

static void setupXWayland(struct Server *server)
{
    if (server->noXwayland)
        return;

    server->xwayland = wlr_xwayland_create(server->display, server->compositor,
                                           false);
    if (!server->xwayland) {
        server->xwaylandFailed = true;
        wlr_log(WLR_INFO,
                "XWayland is not available; continuing without X11 support");
        return;
    }

    server->xwaylandReadyListener.notify = handleXWaylandReady;
    wl_signal_add(&server->xwayland->events.ready, &server->xwaylandReadyListener);
    server->newXWaylandSurface.notify = handleNewXWaylandSurface;
    wl_signal_add(&server->xwayland->events.new_surface,
                  &server->newXWaylandSurface);
}

// ---------------------------------------------------------------------------
// CLI and main
// ---------------------------------------------------------------------------

static void printUsage(void)
{
    printf(
        "Usage: newteller-compositor [options]\n"
        "\n"
        "Starts the NewTeller Wayland session: a wlroots compositor, the\n"
        "NewTeller shell, and XWayland for X11 applications.\n"
        "\n"
        "Options:\n"
        "  -h, --help                 Show this help\n"
        "  -v, --version              Show the version\n"
        "  --headless[=WxH]           Render without a display (tests/screenshots)\n"
        "  --nested[=WxH]             Run as a window inside the current session\n"
        "  --shell <program>          Shell executable (default: newteller)\n"
        "  --no-shell                 Do not start the shell\n"
        "  --no-xwayland              Do not start XWayland\n"
        "  --socket <name>            Use an explicit Wayland socket name\n"
        "  --msg <command>            Send a command to the running compositor\n"
        "                             (reload, lock, dpms on|off, layout next,\n"
        "                             idle-inhibit on|off, outputs, windows,\n"
        "                             minimize|maximize|fullscreen|always-on-top ID,\n"
        "                             window-menu, exit) and print the reply\n"
        "  --debug                    Verbose wlroots logging\n");
}

static bool parseSize(const char *value, int *width, int *height)
{
    int w = 0;
    int h = 0;
    if (sscanf(value, "%dx%d", &w, &h) != 2 || w < 320 || h < 240)
        return false;
    *width = w;
    *height = h;
    return true;
}

static void prepareRuntimeDir(void)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime && runtime[0] != '\0') {
        struct stat st = {0};
        if (stat(runtime, &st) == 0 && S_ISDIR(st.st_mode)
            && st.st_uid == getuid()) {
            return;
        }
    }

    char fallback[PATH_MAX];
    snprintf(fallback, sizeof(fallback), "/tmp/newteller-%u",
             (unsigned)getuid());
    if (mkdir(fallback, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "newteller-compositor: cannot create %s: %s\n",
                fallback, strerror(errno));
        exit(1);
    }
    struct stat st = {0};
    if (stat(fallback, &st) != 0 || !S_ISDIR(st.st_mode)
        || st.st_uid != getuid()) {
        fprintf(stderr,
                "newteller-compositor: %s is not a safe runtime directory\n",
                fallback);
        exit(1);
    }
    setenv("XDG_RUNTIME_DIR", fallback, 1);
}

static int run(struct Server *server)
{
    server->running = true;
    server->display = wl_display_create();
    if (!server->display)
        return 1;
    server->eventLoop = wl_display_get_event_loop(server->display);

    wl_list_init(&server->outputs);
    wl_list_init(&server->views);
    server->hotCornerFired = -1;
    wl_list_init(&server->layers);
    wl_list_init(&server->keyboards);
    wl_list_init(&server->pointers);
    wl_list_init(&server->touchDevices);
    server->touchPointerId = -1;
    wl_list_init(&server->lockSurfaces);
    wl_list_init(&server->ipcClients);
    wl_list_init(&server->ghosts);
    wl_list_init(&server->blurRegions);
    server->ipcFd = -1;
    loadConfig(server);

    if (server->headless) {
        server->backend = wlr_headless_backend_create(server->eventLoop);
    } else if (server->nested) {
        server->backend = wlr_wl_backend_create(server->eventLoop, NULL);
        if (server->backend)
            wlr_wl_output_create(server->backend);
    } else {
        server->backend = wlr_backend_autocreate(server->eventLoop,
                                                 &server->session);
    }
    if (!server->backend) {
        wlr_log(WLR_ERROR, "failed to create the wlroots backend");
        wl_display_destroy(server->display);
        return 1;
    }

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer) {
        wlr_log(WLR_ERROR, "failed to create the renderer");
        wlr_backend_destroy(server->backend);
        wl_display_destroy(server->display);
        return 1;
    }
    wlr_renderer_init_wl_display(server->renderer, server->display);
    server->allocator = wlr_allocator_autocreate(server->backend,
                                                 server->renderer);
    if (!server->allocator) {
        wlr_log(WLR_ERROR, "failed to create the allocator");
        wlr_renderer_destroy(server->renderer);
        wlr_backend_destroy(server->backend);
        wl_display_destroy(server->display);
        return 1;
    }

    setupProtocols(server);
    setupInput(server);
    setupXWayland(server);

    server->newOutput.notify = handleNewOutput;
    wl_signal_add(&server->backend->events.new_output, &server->newOutput);

    if (server->socketName[0] != '\0') {
        if (wl_display_add_socket(server->display, server->socketName) != 0) {
            wlr_log(WLR_ERROR, "failed to use Wayland socket %s",
                    server->socketName);
            return 1;
        }
    } else {
        const char *socket = wl_display_add_socket_auto(server->display);
        if (!socket) {
            wlr_log(WLR_ERROR, "failed to create the Wayland socket");
            return 1;
        }
        snprintf(server->socketName, sizeof(server->socketName), "%s", socket);
    }
    setenv("WAYLAND_DISPLAY", server->socketName, 1);
    setupIpc(server);

    if (!wlr_backend_start(server->backend)) {
        wlr_log(WLR_ERROR, "failed to start the wlroots backend");
        return 1;
    }

    if (server->headless) {
        if (!wlr_headless_add_output(server->backend, server->headlessWidth,
                                     server->headlessHeight)) {
            wlr_log(WLR_ERROR, "failed to add the headless output");
            return 1;
        }
    }

    server->sigchld = wl_event_loop_add_signal(server->eventLoop, SIGCHLD,
                                               sigchldHandler, server);
    wl_event_loop_add_signal(server->eventLoop, SIGINT,
                             terminateHandler, server);
    wl_event_loop_add_signal(server->eventLoop, SIGTERM,
                             terminateHandler, server);
    // `kill -HUP` (or `newteller-compositor --msg reload`) re-reads
    // compositor.conf.
    wl_event_loop_add_signal(server->eventLoop, SIGHUP,
                             reloadSignalHandler, server);

    wlr_log(WLR_INFO, "NewTeller session ready on WAYLAND_DISPLAY=%s",
            server->socketName);
    if (!server->noShell) {
        server->shellTimer = wl_event_loop_add_timer(
            server->eventLoop, shellTimerCallback, server);
        wl_event_source_timer_update(server->shellTimer, 250);
    }

    wl_display_run(server->display);
    wlr_log(WLR_DEBUG, "event loop stopped; cleaning up");

    if (server->shellPid > 0) {
        wlr_log(WLR_INFO, "stopping NewTeller shell");
        kill(server->shellPid, SIGTERM);
        for (int i = 0; i < 20; ++i) {
            if (waitpid(server->shellPid, NULL, WNOHANG) == server->shellPid) {
                server->shellPid = 0;
                break;
            }
            usleep(50 * 1000);
        }
        if (server->shellPid > 0) {
            kill(server->shellPid, SIGKILL);
            waitpid(server->shellPid, NULL, 0);
            server->shellPid = 0;
        }
    }

    wl_log_set_handler_server(NULL);
    teardownIpc(server);
    if (server->lockRespawnTimer) {
        wl_event_source_remove(server->lockRespawnTimer);
        server->lockRespawnTimer = NULL;
    }
    wlr_log(WLR_DEBUG, "destroying XWayland");
    if (server->xwayland) {
        wl_list_remove(&server->xwaylandReadyListener.link);
        wl_list_remove(&server->newXWaylandSurface.link);
        wlr_xwayland_destroy(server->xwayland);
        server->xwayland = NULL;
    }
    struct Ghost *ghost;
    struct Ghost *nextGhost;
    wl_list_for_each_safe(ghost, nextGhost, &server->ghosts, link)
        destroyGhost(ghost);
    struct BlurRegion *blurRegion;
    struct BlurRegion *nextBlur;
    wl_list_for_each_safe(blurRegion, nextBlur, &server->blurRegions, link) {
        wl_list_remove(&blurRegion->link);
        free(blurRegion);
    }
    wlr_log(WLR_DEBUG, "destroying clients");
    wl_display_destroy_clients(server->display);
    wlr_log(WLR_DEBUG, "destroying listeners");
    wl_list_remove(&server->newOutput.link);
    wl_list_remove(&server->newInput.link);
    wl_list_remove(&server->newVirtualKeyboard.link);
    wl_list_remove(&server->newTextInput.link);
    wl_list_remove(&server->newInputMethod.link);
    wl_list_remove(&server->keyboardFocusChange.link);
    wl_list_remove(&server->newVirtualPointer.link);
    wl_list_remove(&server->newXdgToplevel.link);
    wl_list_remove(&server->newXdgPopup.link);
    wl_list_remove(&server->newLayerSurface.link);
    wl_list_remove(&server->cursorMotion.link);
    wl_list_remove(&server->cursorMotionAbsolute.link);
    wl_list_remove(&server->cursorButton.link);
    wl_list_remove(&server->cursorAxis.link);
    wl_list_remove(&server->cursorFrame.link);
    wl_list_remove(&server->touchDown.link);
    wl_list_remove(&server->touchUp.link);
    wl_list_remove(&server->touchMotion.link);
    wl_list_remove(&server->touchFrame.link);
    wl_list_remove(&server->tabletAxis.link);
    wl_list_remove(&server->tabletTip.link);
    wl_list_remove(&server->swipeBegin.link);
    wl_list_remove(&server->swipeUpdate.link);
    wl_list_remove(&server->swipeEnd.link);
    wl_list_remove(&server->pinchBegin.link);
    wl_list_remove(&server->pinchUpdate.link);
    wl_list_remove(&server->pinchEnd.link);
    wl_list_remove(&server->holdBegin.link);
    wl_list_remove(&server->holdEnd.link);
    wl_list_remove(&server->requestCursor.link);
    wl_list_remove(&server->requestSetSelection.link);
    wl_list_remove(&server->requestSetPrimarySelection.link);
    wl_list_remove(&server->requestStartDrag.link);
    wl_list_remove(&server->newPointerConstraint.link);
    wl_list_remove(&server->cursorShape.link);
    wl_list_remove(&server->newShortcutsInhibitor.link);
    wl_list_remove(&server->startDrag.link);
    detachLock(server);
    wl_list_remove(&server->newLock.link);
    wl_list_remove(&server->newIdleInhibitor.link);
    wl_list_remove(&server->outputManagerApply.link);
    wl_list_remove(&server->outputManagerTest.link);
    wl_list_remove(&server->layoutChange.link);
    wl_list_remove(&server->gammaSetGamma.link);
    wl_list_remove(&server->outputPowerSetMode.link);
    wl_list_remove(&server->newXdgDecoration.link);
    wl_list_remove(&server->newKdeDecoration.link);
    wl_list_remove(&server->systemBellRing.link);
    wlr_log(WLR_DEBUG, "destroying cursor");
    if (server->cursorManager)
        wlr_xcursor_manager_destroy(server->cursorManager);
    if (server->cursor)
        wlr_cursor_destroy(server->cursor);
    // The backend owns the outputs; destroying it first runs outputDestroy()
    // while the scene and renderer are still alive.
    wlr_log(WLR_DEBUG, "destroying backend");
    wlr_backend_destroy(server->backend);
    wlr_log(WLR_DEBUG, "destroying scene");
    wlr_scene_node_destroy(&server->scene->tree.node);
    wlr_log(WLR_DEBUG, "destroying allocator");
    wlr_allocator_destroy(server->allocator);
    wlr_log(WLR_DEBUG, "destroying renderer");
    wlr_renderer_destroy(server->renderer);
    wlr_log(WLR_DEBUG, "destroying display");
    wl_display_destroy(server->display);
    return 0;
}

static int parseArguments(struct Server *server, int argc, char **argv)
{
    snprintf(server->shellProgram, sizeof(server->shellProgram), "%s",
             "newteller");
    server->headlessWidth = 1280;
    server->headlessHeight = 800;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            printUsage();
            return 1;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            printf("NewTeller compositor %s\n", NEWTELLER_VERSION);
            return 1;
        }
        if (strcmp(arg, "--debug") == 0) {
            wlr_log_init(WLR_DEBUG, NULL);
            continue;
        }
        if (strcmp(arg, "--no-shell") == 0) {
            server->noShell = true;
            continue;
        }
        if (strcmp(arg, "--no-xwayland") == 0) {
            server->noXwayland = true;
            continue;
        }
        if (strcmp(arg, "--headless") == 0
            || strncmp(arg, "--headless=", 11) == 0) {
            server->headless = true;
            server->nested = false;
            if (arg[10] == '=')
                parseSize(arg + 11, &server->headlessWidth,
                          &server->headlessHeight);
            continue;
        }
        if (strcmp(arg, "--nested") == 0
            || strncmp(arg, "--nested=", 9) == 0) {
            server->nested = true;
            server->headless = false;
            if (arg[8] == '=')
                parseSize(arg + 9, &server->headlessWidth,
                          &server->headlessHeight);
            continue;
        }
        if (strcmp(arg, "--shell") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "newteller-compositor: --shell needs a value\n");
                return 2;
            }
            snprintf(server->shellPath, sizeof(server->shellPath), "%s",
                     argv[++i]);
            continue;
        }
        if (strcmp(arg, "--socket") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "newteller-compositor: --socket needs a value\n");
                return 2;
            }
            snprintf(server->socketName, sizeof(server->socketName), "%s",
                     argv[++i]);
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            if (i + 1 < argc) {
                snprintf(server->shellPath, sizeof(server->shellPath), "%s",
                         argv[++i]);
            }
            break;
        }
        fprintf(stderr, "newteller-compositor: unknown option %s\n", arg);
        printUsage();
        return 2;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--msg") == 0) {
        if (argc < 3) {
            fprintf(stderr, "newteller-compositor: --msg needs a command\n");
            return 2;
        }
        char command[1024] = "";
        size_t used = 0;
        for (int i = 2; i < argc && used < sizeof(command) - 1; ++i) {
            int n = snprintf(command + used, sizeof(command) - used, "%s%s",
                             i > 2 ? " " : "", argv[i]);
            if (n < 0)
                break;
            used += (size_t)n;
        }
        return sendCommand(command);
    }

    wlr_log_init(WLR_INFO, NULL);
    prepareRuntimeDir();

    struct Server server = {0};
    int parsed = parseArguments(&server, argc, argv);
    if (parsed != 0)
        return parsed == 1 ? 0 : 2;

    return run(&server);
}
