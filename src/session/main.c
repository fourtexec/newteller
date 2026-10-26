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
#include <wlr/backend/session.h>
#include <wlr/backend/wayland.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
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
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/version.h>
#include <wlr/xwayland/xwayland.h>

#include <xkbcommon/xkbcommon.h>

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct Server;
struct Output;
struct View;
struct Layer;

static void spawnDetached(const char *program, const char *const argv[]);
static void maybeStartShell(struct Server *server);

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

    // Exactly one of xdg/x11 is set.
    struct wlr_xdg_toplevel *xdg;
    struct wlr_xwayland_surface *x11;

    struct wlr_scene_tree *tree;
    struct wlr_foreign_toplevel_handle_v1 *handle;

    bool mapped;
    bool minimized;
    bool maximized;
    bool fullscreen;
    bool overrideRedirect;
    bool positioned;
    bool hiddenByShowDesktop;
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
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

struct Output {
    struct wl_list link;
    struct Server *server;
    struct wlr_output *output;
    struct wlr_scene_output *sceneOutput;
    struct wlr_box usable;

    struct wl_listener frame;
    struct wl_listener requestState;
    struct wl_listener destroy;
};

struct Popup {
    struct wlr_xdg_popup *popup;
    struct wl_listener commit;
    struct wl_listener destroy;
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
    struct wlr_scene_tree *topTree;
    struct wlr_scene_tree *overlayTree;

    struct wlr_compositor *compositor;
    struct wlr_xdg_shell *xdgShell;
    struct wlr_layer_shell_v1 *layerShell;
    struct wlr_foreign_toplevel_manager_v1 *foreignToplevels;
    struct wlr_xdg_activation_v1 *activation;
    struct wlr_server_decoration_manager *serverDecoration;
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
    struct wl_event_source *shellTimer;
    struct wl_event_source *arrangeIdle;
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
    bool showDesktop;
    enum CursorMode cursorMode;
    double grabX;
    double grabY;
    int grabWidth;
    int grabHeight;
    uint32_t resizeEdges;
    int cascade;

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

static void spawnDetached(const char *program, const char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) {
        wlr_log_errno(WLR_ERROR, "fork failed for %s", program);
        return;
    }
    if (pid == 0) {
        execvp(program, (char *const *)argv);
        wlr_log_errno(WLR_ERROR, "failed to start %s", program);
        _exit(127);
    }
}

static void setChildEnvironment(struct Server *server)
{
    if (server->socketName[0] != '\0')
        setenv("WAYLAND_DISPLAY", server->socketName, 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("XDG_SESSION_DESKTOP", "newteller", 1);
    setenv("XDG_CURRENT_DESKTOP", "NewTeller", 1);
    setenv("DESKTOP_SESSION", "newteller", 1);
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

static void updateForeignToplevel(struct View *view)
{
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
}

static void viewRaise(struct View *view)
{
    if (!view->tree)
        return;
    wlr_scene_node_raise_to_top(&view->tree->node);
    wl_list_remove(&view->link);
    wl_list_insert(&view->server->views, &view->link);
}

static void focusView(struct View *view, struct wlr_surface *surface)
{
    if (!view || !viewSurface(view) || view->minimized)
        return;

    struct Server *server = view->server;
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
    viewRaise(view);
    viewSetActivated(view, true);

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(server->seat, target,
                                       keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    }
    if (view->handle)
        wlr_foreign_toplevel_handle_v1_set_activated(view->handle, true);
}

static void focusLayer(struct Layer *layer)
{
    if (!layer || !layer->layer || !layer->layer->surface)
        return;
    struct Server *server = layer->server;
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
    wlr_seat_keyboard_notify_clear_focus(server->seat);
}

static void restoreFocus(struct Server *server)
{
    if (server->previousView && server->previousView->mapped
        && !server->previousView->minimized) {
        struct View *view = server->previousView;
        server->previousView = NULL;
        focusView(view, NULL);
        return;
    }
    if (!wl_list_empty(&server->views)) {
        struct View *view = wl_container_of(server->views.next, view, link);
        if (view->mapped && !view->minimized) {
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
    view->minimized = minimized;
    if (view->tree)
        wlr_scene_node_set_enabled(&view->tree->node, !minimized);
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

static struct wlr_box usableBoxForView(struct View *view)
{
    struct Server *server = view->server;
    struct wlr_output *output = wlr_output_layout_output_at(
        server->outputLayout, view->x + view->width / 2,
        view->y + view->height / 2);
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

// Snap the focused window to one half of the output's usable area. The
// direction is a wlr edge: WLR_EDGE_LEFT/RIGHT for the two halves, and
// WLR_EDGE_TOP for maximise. WLR_EDGE_BOTTOM restores the saved geometry.
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
    if (edge != WLR_EDGE_LEFT && edge != WLR_EDGE_RIGHT)
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
    const int half = area.width / 2;
    viewSetPosition(view, edge == WLR_EDGE_LEFT ? area.x : area.x + area.width - half,
                    area.y);
    viewSetSize(view, half, area.height);
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
    int width = view->width > area.width ? area.width : view->width;
    int height = view->height > area.height ? area.height : view->height;
    viewSetSize(view, width, height);
    viewSetPosition(view, area.x + (area.width - width) / 2,
                    area.y + (area.height - height) / 2);
}

static void toggleShowDesktop(struct Server *server)
{
    if (!server->showDesktop) {
        server->showDesktop = true;
        struct View *view;
        wl_list_for_each(view, &server->views, link) {
            if (view->mapped && !view->minimized && !view->overrideRedirect) {
                view->hiddenByShowDesktop = true;
                viewSetMinimized(view, true);
            }
        }
    } else {
        server->showDesktop = false;
        struct View *view;
        wl_list_for_each(view, &server->views, link) {
            if (view->hiddenByShowDesktop) {
                view->hiddenByShowDesktop = false;
                viewSetMinimized(view, false);
            }
        }
    }
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
        struct wlr_box box = usableBoxForView(view);
        viewSetPosition(view, box.x, box.y);
        viewSetSize(view, box.width, box.height);
    } else {
        viewSetPosition(view, view->savedX, view->savedY);
        viewSetSize(view, view->savedWidth, view->savedHeight);
    }

    view->maximized = maximized;
    if (view->xdg)
        wlr_xdg_toplevel_set_maximized(view->xdg, maximized);
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
        view->savedX = view->x;
        view->savedY = view->y;
        view->savedWidth = view->width;
        view->savedHeight = view->height;
        struct wlr_output *output = wlr_output_layout_output_at(
            view->server->outputLayout, view->x, view->y);
        struct wlr_box box = {0, 0, 1280, 800};
        if (output)
            wlr_output_layout_get_box(view->server->outputLayout, output, &box);
        viewSetPosition(view, box.x, box.y);
        viewSetSize(view, box.width, box.height);
        viewRaise(view);
    } else {
        viewSetPosition(view, view->savedX, view->savedY);
        viewSetSize(view, view->savedWidth, view->savedHeight);
    }

    view->fullscreen = fullscreen;
    if (fullscreen) {
        if (view->server->fullscreenView
            && view->server->fullscreenView != view) {
            viewSetFullscreen(view->server->fullscreenView, false);
        }
        view->server->fullscreenView = view;
        // The panel and menus are layer-shell surfaces above normal windows.
        // Hide those layers while a client is fullscreen, as a desktop session
        // normally does; Super+F (or F11) brings them back.
        wlr_scene_node_set_enabled(&view->server->topTree->node, false);
        wlr_scene_node_set_enabled(&view->server->overlayTree->node, false);
    } else if (view->server->fullscreenView == view) {
        view->server->fullscreenView = NULL;
        wlr_scene_node_set_enabled(&view->server->topTree->node, true);
        wlr_scene_node_set_enabled(&view->server->overlayTree->node, true);
    }
    if (view->xdg)
        wlr_xdg_toplevel_set_fullscreen(view->xdg, fullscreen);
    updateForeignToplevel(view);
}

static void viewClose(struct View *view)
{
    if (view->xdg)
        wlr_xdg_toplevel_send_close(view->xdg);
    else if (view->x11)
        wlr_xwayland_surface_close(view->x11);
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

    int offset = (view->server->cascade++ % 8) * 28;
    int x = area.x + (area.width - view->width) / 2 + offset;
    int y = area.y + (area.height - view->height) / 2 + offset;
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
        wlr_scene_node_set_enabled(&server->topTree->node, true);
        wlr_scene_node_set_enabled(&server->overlayTree->node, true);
    }
    if (server->grabbedView == view)
        server->grabbedView = NULL;

    if (view->handle) {
        wl_list_remove(&view->foreignActivate.link);
        wl_list_remove(&view->foreignClose.link);
        wl_list_remove(&view->foreignMinimize.link);
        wl_list_remove(&view->foreignMaximize.link);
        wl_list_remove(&view->foreignFullscreen.link);
        wlr_foreign_toplevel_handle_v1_destroy(view->handle);
        view->handle = NULL;
    }

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

static void layerHandleCommit(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Layer *layer = wl_container_of(listener, layer, commit);
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

    struct wlr_scene_tree *parent = NULL;
    switch (layerSurface->current.layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
        parent = server->backgroundTree;
        break;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
        parent = server->bottomTree;
        break;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
        parent = server->topTree;
        break;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
        parent = server->overlayTree;
        break;
    }
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
};

static bool layerAt(struct Layer *layer, double lx, double ly, struct Hit *hit)
{
    if (!layer->scene || !layer->layer || !layer->layer->surface->mapped)
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
    if (!view->mapped || view->minimized || !view->tree)
        return false;
    int nodeX = view->tree->node.x;
    int nodeY = view->tree->node.y;
    double sx = 0;
    double sy = 0;
    struct wlr_surface *surface = NULL;
    if (view->xdg) {
        surface = wlr_xdg_surface_surface_at(view->xdg->base, lx - nodeX,
                                             ly - nodeY, &sx, &sy);
    } else if (view->x11 && view->x11->surface) {
        surface = wlr_surface_surface_at(view->x11->surface, lx - nodeX,
                                         ly - nodeY, &sx, &sy);
    }
    if (!surface)
        return false;
    hit->surface = surface;
    hit->sx = sx;
    hit->sy = sy;
    hit->view = view;
    return true;
}

static struct Hit hitTest(struct Server *server, double lx, double ly)
{
    struct Hit hit = {0};

    const int topLayers[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
    };
    for (size_t i = 0; i < sizeof(topLayers) / sizeof(topLayers[0]); ++i) {
        struct Layer *layer;
        wl_list_for_each(layer, &server->layers, link) {
            if ((int)layer->layer->current.layer != topLayers[i])
                continue;
            if (layerAt(layer, lx, ly, &hit))
                return hit;
        }
    }

    struct View *view;
    wl_list_for_each(view, &server->views, link) {
        if (viewAt(view, lx, ly, &hit))
            return hit;
    }

    const int bottomLayers[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
    };
    for (size_t i = 0; i < sizeof(bottomLayers) / sizeof(bottomLayers[0]); ++i) {
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

static void pointerFocus(struct Server *server, const struct Hit *hit)
{
    if (!hit->surface) {
        wlr_seat_pointer_clear_focus(server->seat);
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
}

// ---------------------------------------------------------------------------
// Pointer interaction
// ---------------------------------------------------------------------------

static void processCursorMotion(struct Server *server, uint32_t time)
{
    if (server->cursorMode == CURSOR_MOVE && server->grabbedView) {
        struct View *view = server->grabbedView;
        viewSetPosition(view,
                        (int)(server->cursor->x - server->grabX),
                        (int)(server->cursor->y - server->grabY));
        return;
    }
    if (server->cursorMode == CURSOR_RESIZE && server->grabbedView) {
        struct View *view = server->grabbedView;
        int left = (int)server->grabX;
        int top = (int)server->grabY;
        int right = left + server->grabWidth;
        int bottom = top + server->grabHeight;

        if (server->resizeEdges & WLR_EDGE_LEFT)
            left = (int)server->cursor->x;
        if (server->resizeEdges & WLR_EDGE_RIGHT)
            right = (int)server->cursor->x;
        if (server->resizeEdges & WLR_EDGE_TOP)
            top = (int)server->cursor->y;
        if (server->resizeEdges & WLR_EDGE_BOTTOM)
            bottom = (int)server->cursor->y;

        if (right - left < 64 || bottom - top < 64)
            return;

        viewSetPosition(view, left, top);
        viewSetSize(view, right - left, bottom - top);
        return;
    }

    struct Hit hit = hitTest(server, server->cursor->x, server->cursor->y);
    struct wlr_surface *focused = server->seat->pointer_state.focused_surface;
    pointerFocus(server, &hit);
    if (hit.surface && focused == hit.surface)
        wlr_seat_pointer_notify_motion(server->seat, time, hit.sx, hit.sy);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void cursorMotion(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, cursorMotion);
    struct wlr_pointer_motion_event *event = data;
    wlr_cursor_move(server->cursor, &event->pointer->base,
                    event->delta_x, event->delta_y);
    processCursorMotion(server, event->time_msec);
}

static void cursorMotionAbsolute(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, cursorMotionAbsolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base,
                             event->x, event->y);
    processCursorMotion(server, event->time_msec);
}

static void cursorButton(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, cursorButton);
    struct wlr_pointer_button_event *event = data;

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (server->cursorMode == CURSOR_MOVE && server->grabbedView) {
            struct View *view = server->grabbedView;
            const struct wlr_box area = usableBoxForView(view);
            if (server->cursor->x <= area.x + 12)
                viewSnap(view, WLR_EDGE_LEFT);
            else if (server->cursor->x >= area.x + area.width - 12)
                viewSnap(view, WLR_EDGE_RIGHT);
            else if (server->cursor->y <= area.y + 10)
                viewSnap(view, WLR_EDGE_TOP);
        }
        wlr_seat_pointer_notify_button(server->seat, event->time_msec,
                                       event->button, event->state);
        server->cursorMode = CURSOR_PASSTHROUGH;
        server->grabbedView = NULL;
        return;
    }

    struct Hit hit = hitTest(server, server->cursor->x, server->cursor->y);
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
// Keyboard
// ---------------------------------------------------------------------------

static void shellAction(const char *method)
{
    // gdbus is part of glib2, present on every supported Debian/Ubuntu
    // desktop. The service is registered by the running NewTeller shell.
    char call[256];
    snprintf(call, sizeof(call), "org.newteller.Shell.%s", method);
    const char *argv[] = {
        "gdbus", "call", "--session",
        "--dest", "org.newteller.Shell",
        "--object-path", "/org/newteller/Shell",
        "--method", call, NULL,
    };
    spawnDetached(argv[0], argv);
}

static bool keyHandled(struct Server *server, xkb_keysym_t sym,
                       uint32_t modifiers)
{
    bool logo = (modifiers & WLR_MODIFIER_LOGO) != 0;
    bool alt = (modifiers & WLR_MODIFIER_ALT) != 0;
    if (logo)
        wlr_log(WLR_DEBUG, "logo key sym=%u shift=%d focus=%p", sym,
                (modifiers & WLR_MODIFIER_SHIFT) != 0,
                (void *)server->focusedView);

    if (alt && sym == XKB_KEY_F2) {
        shellAction("showRunDialog");
        return true;
    }
    if (logo && sym == XKB_KEY_Return) {
        const char *argv[] = {"x-terminal-emulator", NULL};
        setChildEnvironment(server);
        spawnDetached(argv[0], argv);
        return true;
    }
    if (logo && (sym == XKB_KEY_s || sym == XKB_KEY_S)) {
        shellAction("openSettings");
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
            if (sym == XKB_KEY_Left)
                viewSnap(server->focusedView, WLR_EDGE_LEFT);
            else if (sym == XKB_KEY_Right)
                viewSnap(server->focusedView, WLR_EDGE_RIGHT);
            else if (sym == XKB_KEY_Up)
                viewSnap(server->focusedView, WLR_EDGE_TOP);
            else
                viewSnap(server->focusedView, WLR_EDGE_BOTTOM);
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
    if (sym == XKB_KEY_Print) {
        const char *argv[] = {"newteller-screenshot", NULL};
        setChildEnvironment(server);
        spawnDetached(argv[0], argv);
        return true;
    }
    if (alt && sym == XKB_KEY_Tab) {
        if (wl_list_empty(&server->views))
            return true;
        struct View *next = NULL;
        if (server->focusedView) {
            struct wl_list *link = server->focusedView->link.next;
            if (link == &server->views)
                link = server->views.next;
            next = wl_container_of(link, next, link);
        } else {
            next = wl_container_of(server->views.next, next, link);
        }
        int guard = 0;
        while (next && (next->minimized || !next->mapped) && guard++ < 64) {
            struct wl_list *link = next->link.next;
            if (link == &server->views)
                link = server->views.next;
            next = wl_container_of(link, next, link);
            if (next == server->focusedView)
                break;
        }
        if (next && next->mapped && !next->minimized)
            focusView(next, NULL);
        return true;
    }
    return false;
}

static void keyboardModifiers(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
                                       &keyboard->keyboard->modifiers);
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

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        xkb_keysym_t sym = xkb_state_key_get_one_sym(
            keyboard->keyboard->xkb_state, event->keycode + 8);
        if (keyHandled(server, sym,
                       wlr_keyboard_get_modifiers(keyboard->keyboard))) {
            return;
        }
    }

    wlr_seat_keyboard_notify_key(server->seat, event->time_msec,
                                 event->keycode, event->state);
}

static void keyboardDestroy(struct wl_listener *listener, void *data)
{
    (void)data;
    struct Keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}


static void addKeyboard(struct Server *server, struct wlr_keyboard *wlrKeyboard,
                        bool hasKeymap)
{
    struct Keyboard *keyboard = xcalloc(1, sizeof(*keyboard));
    keyboard->server = server;
    keyboard->keyboard = wlrKeyboard;

    if (!hasKeymap) {
        struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        struct xkb_rule_names rules = {0};
        struct xkb_keymap *keymap = xkb_keymap_new_from_names(
            context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
        wlr_keyboard_set_keymap(wlrKeyboard, keymap);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
    }
    wlr_keyboard_set_repeat_info(wlrKeyboard, 25, 600);

    keyboard->modifiers.notify = keyboardModifiers;
    wl_signal_add(&wlrKeyboard->events.modifiers, &keyboard->modifiers);
    keyboard->key.notify = keyboardKey;
    wl_signal_add(&wlrKeyboard->events.key, &keyboard->key);
    keyboard->destroy.notify = keyboardDestroy;
    wl_signal_add(&wlrKeyboard->base.events.destroy, &keyboard->destroy);

    wl_list_insert(&server->keyboards, &keyboard->link);
    wlr_seat_set_keyboard(server->seat, wlrKeyboard);
    wlr_seat_set_capabilities(server->seat,
                              WL_SEAT_CAPABILITY_POINTER
                                  | WL_SEAT_CAPABILITY_KEYBOARD);
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

static void newInput(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newInput);
    struct wlr_input_device *device = data;

    if (device->type == WLR_INPUT_DEVICE_KEYBOARD) {
        addKeyboard(server, wlr_keyboard_from_input_device(device), false);
    } else if (device->type == WLR_INPUT_DEVICE_POINTER) {
        wlr_cursor_attach_input_device(server->cursor, device);
        wlr_seat_set_capabilities(server->seat, WL_SEAT_CAPABILITY_POINTER);
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
    wlr_seat_set_capabilities(server->seat, WL_SEAT_CAPABILITY_POINTER);
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
    wlr_log(WLR_DEBUG, "xdg commit %s initial=%d initialized=%d",
            viewTitle(view), view->xdg->base->initial_commit,
            view->xdg->base->initialized);
    if (view->xdg && view->xdg->base->initial_commit
        && view->xdg->base->initialized) {
        // xdg-shell requires the compositor to answer the first commit with a
        // configure event; otherwise the client never maps its window.
        wlr_xdg_toplevel_set_size(view->xdg, 0, 0);
        wlr_log(WLR_DEBUG, "sent initial xdg configure for %s", viewTitle(view));
    }
}

static void handleXdgMap(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, map);
    view->mapped = true;
    if (view->tree)
        wlr_scene_node_set_enabled(&view->tree->node, true);
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
    updateForeignToplevel(view);
}

static void handleXdgUnmap(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, unmap);
    wlr_log(WLR_DEBUG, "xdg window unmapped: %s", viewTitle(view));
    view->mapped = false;
    if (view->tree)
        wlr_scene_node_set_enabled(&view->tree->node, false);
    if (view->server->focusedView == view) {
        view->server->focusedView = NULL;
        viewSetActivated(view, false);
        restoreFocus(view->server);
    }
    if (view->handle)
        wlr_foreign_toplevel_handle_v1_set_activated(view->handle, false);
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
    wl_list_remove(&view->setTitle.link);
    wl_list_remove(&view->setAppId.link);
    // The scene helper owns this tree and destroys it with the xdg surface.
    view->tree = NULL;
    destroyView(view);
}

static void handleXdgRequestMove(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, requestMove);
    viewClearSnap(view);
    view->server->grabbedView = view;
    view->server->cursorMode = CURSOR_MOVE;
    view->server->grabX = view->server->cursor->x - view->x;
    view->server->grabY = view->server->cursor->y - view->y;
}

static void handleXdgRequestResize(struct wl_listener *listener, void *data)
{
    struct View *view = wl_container_of(listener, view, requestResize);
    viewClearSnap(view);
    struct wlr_xdg_toplevel_resize_event *event = data;
    view->server->grabbedView = view;
    view->server->cursorMode = CURSOR_RESIZE;
    view->server->resizeEdges = event->edges;
    view->server->grabWidth = view->width;
    view->server->grabHeight = view->height;
    view->server->grabX = view->x;
    view->server->grabY = view->y;
}

static void handleXdgRequestMaximize(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, requestMaximize);
    viewSetMaximized(view, !view->maximized);
}

static void handleXdgRequestFullscreen(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, requestFullscreen);
    viewSetFullscreen(view, !view->fullscreen);
}

static void handleXdgRequestMinimize(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, requestMinimize);
    viewSetMinimized(view, true);
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

static void handleNewXdgToplevel(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newXdgToplevel);
    struct wlr_xdg_toplevel *toplevel = data;
    wlr_log(WLR_DEBUG, "new xdg toplevel: %s (%s)",
            toplevel->title ? toplevel->title : "(untitled)",
            toplevel->app_id ? toplevel->app_id : "(no app id)");

    struct View *view = xcalloc(1, sizeof(*view));
    view->server = server;
    view->xdg = toplevel;
    view->tree = wlr_scene_xdg_surface_create(server->toplevelTree, toplevel->base);
    if (!view->tree) {
        free(view);
        return;
    }
    toplevel->base->data = view->tree;
    view->tree->node.data = view;

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
    view->setTitle.notify = handleXdgSetTitle;
    wl_signal_add(&toplevel->events.set_title, &view->setTitle);
    view->setAppId.notify = handleXdgSetAppId;
    wl_signal_add(&toplevel->events.set_app_id, &view->setAppId);
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
    struct Layer *layer;
    wl_list_for_each(layer, &server->layers, link) {
        if (layer->output == output)
            layer->output = NULL;
    }
    if (output->sceneOutput)
        wlr_scene_output_destroy(output->sceneOutput);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->requestState.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);
}

static void handleNewOutput(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newOutput);
    struct wlr_output *wlrOutput = data;

    if (!wlr_output_init_render(wlrOutput, server->allocator, server->renderer)) {
        wlr_log(WLR_ERROR, "failed to initialise output rendering");
        return;
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlrOutput);
    if (mode)
        wlr_output_state_set_mode(&state, mode);
    wlr_output_commit_state(wlrOutput, &state);
    wlr_output_state_finish(&state);

    struct wlr_output_layout_output *layoutOutput =
        wlr_output_layout_add_auto(server->outputLayout, wlrOutput);
    struct wlr_scene_output *sceneOutput = wlr_scene_output_create(
        server->scene, wlrOutput);
    if (!sceneOutput) {
        wlr_log(WLR_ERROR, "failed to create scene output");
        return;
    }
    wlr_scene_output_layout_add_output(server->sceneLayout, layoutOutput,
                                       sceneOutput);

    struct Output *output = xcalloc(1, sizeof(*output));
    output->server = server;
    output->output = wlrOutput;
    output->sceneOutput = sceneOutput;
    output->frame.notify = outputFrame;
    wl_signal_add(&wlrOutput->events.frame, &output->frame);
    output->requestState.notify = outputRequestState;
    wl_signal_add(&wlrOutput->events.request_state, &output->requestState);
    output->destroy.notify = outputDestroy;
    wl_signal_add(&wlrOutput->events.destroy, &output->destroy);

    wl_list_insert(&server->outputs, &output->link);
    wlr_output_layout_get_box(server->outputLayout, wlrOutput, &output->usable);
    wlr_scene_output_commit(sceneOutput, NULL);

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
    if (view->tree)
        wlr_scene_node_set_enabled(&view->tree->node, true);
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
    }
    updateForeignToplevel(view);
}

static void handleX11Unmap(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, unmap);
    wlr_log(WLR_DEBUG, "xdg window unmapped: %s", viewTitle(view));
    view->mapped = false;
    if (view->tree)
        wlr_scene_node_set_enabled(&view->tree->node, false);
    if (view->server->focusedView == view) {
        view->server->focusedView = NULL;
        viewSetActivated(view, false);
        restoreFocus(view->server);
    }
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
    view->tree = NULL;
    destroyView(view);
}

static void handleX11RequestConfigure(struct wl_listener *listener, void *data)
{
    struct View *view = wl_container_of(listener, view, requestConfigure);
    struct wlr_xwayland_surface_configure_event *event = data;
    if (view->overrideRedirect || !view->x11)
        return;
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
    viewClearSnap(view);
    view->server->grabbedView = view;
    view->server->cursorMode = CURSOR_MOVE;
    view->server->grabX = view->server->cursor->x - view->x;
    view->server->grabY = view->server->cursor->y - view->y;
}

static void handleX11RequestResize(struct wl_listener *listener, void *data)
{
    struct View *view = wl_container_of(listener, view, requestResize);
    viewClearSnap(view);
    struct wlr_xwayland_resize_event *event = data;
    view->server->grabbedView = view;
    view->server->cursorMode = CURSOR_RESIZE;
    view->server->resizeEdges = event->edges;
    view->server->grabWidth = view->width;
    view->server->grabHeight = view->height;
    view->server->grabX = view->x;
    view->server->grabY = view->y;
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
    viewSetMaximized(view, !view->maximized);
}

static void handleX11RequestFullscreen(struct wl_listener *listener, void *data)
{
    (void)data;
    struct View *view = wl_container_of(listener, view, requestFullscreen);
    viewSetFullscreen(view, !view->fullscreen);
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
        wlr_scene_node_destroy(&view->tree->node);
        view->tree = NULL;
    }
}

static void handleNewXWaylandSurface(struct wl_listener *listener, void *data)
{
    struct Server *server = wl_container_of(listener, server, newXWaylandSurface);
    struct wlr_xwayland_surface *xsurface = data;

    struct View *view = xcalloc(1, sizeof(*view));
    view->server = server;
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
            focusView(view, event->surface);
            return;
        }
    }
}

static void setupProtocols(struct Server *server)
{
    server->compositor = wlr_compositor_create(server->display, 6,
                                               server->renderer);
    wlr_subcompositor_create(server->display);
    wlr_data_device_manager_create(server->display);
    wlr_primary_selection_v1_device_manager_create(server->display);

    server->outputLayout = wlr_output_layout_create(server->display);
    server->scene = wlr_scene_create();
    server->sceneLayout = wlr_scene_attach_output_layout(server->scene,
                                                         server->outputLayout);

    server->backgroundTree = wlr_scene_tree_create(&server->scene->tree);
    server->bottomTree = wlr_scene_tree_create(&server->scene->tree);
    server->toplevelTree = wlr_scene_tree_create(&server->scene->tree);
    server->topTree = wlr_scene_tree_create(&server->scene->tree);
    server->overlayTree = wlr_scene_tree_create(&server->scene->tree);

    server->xdgShell = wlr_xdg_shell_create(server->display, 5);
    server->layerShell = wlr_layer_shell_v1_create(server->display, 4);
    server->foreignToplevels = wlr_foreign_toplevel_manager_v1_create(
        server->display);
    server->activation = wlr_xdg_activation_v1_create(server->display);
    server->activationRequest.notify = handleActivationRequest;
    wl_signal_add(&server->activation->events.request_activate,
                  &server->activationRequest);
    server->serverDecoration = wlr_server_decoration_manager_create(
        server->display);
    wlr_server_decoration_manager_set_default_mode(
        server->serverDecoration, WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT);

#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR >= 19
    wlr_presentation_create(server->display, server->backend, 1);
#else
    wlr_presentation_create(server->display, server->backend);
#endif
    wlr_screencopy_manager_v1_create(server->display);
    wlr_export_dmabuf_manager_v1_create(server->display);
    wlr_xdg_output_manager_v1_create(server->display, server->outputLayout);
    wlr_output_manager_v1_create(server->display);
    wlr_idle_notifier_v1_create(server->display);
    wlr_idle_inhibit_v1_create(server->display);
    wlr_relative_pointer_manager_v1_create(server->display);
    wlr_viewporter_create(server->display);
    wlr_fractional_scale_manager_v1_create(server->display, 1);
    wlr_single_pixel_buffer_manager_v1_create(server->display);
    wlr_keyboard_shortcuts_inhibit_v1_create(server->display);
    wlr_text_input_manager_v3_create(server->display);
    wlr_input_method_manager_v2_create(server->display);
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

    server->cursorManager = wlr_xcursor_manager_create(NULL, 24);
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
    server->requestCursor.notify = seatRequestCursor;
    wl_signal_add(&server->seat->events.request_set_cursor, &server->requestCursor);
    server->requestSetSelection.notify = seatRequestSetSelection;
    wl_signal_add(&server->seat->events.request_set_selection,
                  &server->requestSetSelection);
    server->requestSetPrimarySelection.notify = seatRequestSetPrimarySelection;
    wl_signal_add(&server->seat->events.request_set_primary_selection,
                  &server->requestSetPrimarySelection);

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
    wl_list_init(&server->layers);
    wl_list_init(&server->keyboards);

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
    wlr_log(WLR_DEBUG, "destroying XWayland");
    if (server->xwayland) {
        wl_list_remove(&server->xwaylandReadyListener.link);
        wl_list_remove(&server->newXWaylandSurface.link);
        wlr_xwayland_destroy(server->xwayland);
        server->xwayland = NULL;
    }
    wlr_log(WLR_DEBUG, "destroying clients");
    wl_display_destroy_clients(server->display);
    wlr_log(WLR_DEBUG, "destroying listeners");
    wl_list_remove(&server->newOutput.link);
    wl_list_remove(&server->newInput.link);
    wl_list_remove(&server->newVirtualKeyboard.link);
    wl_list_remove(&server->newVirtualPointer.link);
    wl_list_remove(&server->newXdgToplevel.link);
    wl_list_remove(&server->newXdgPopup.link);
    wl_list_remove(&server->newLayerSurface.link);
    wl_list_remove(&server->cursorMotion.link);
    wl_list_remove(&server->cursorMotionAbsolute.link);
    wl_list_remove(&server->cursorButton.link);
    wl_list_remove(&server->cursorAxis.link);
    wl_list_remove(&server->cursorFrame.link);
    wl_list_remove(&server->requestCursor.link);
    wl_list_remove(&server->requestSetSelection.link);
    wl_list_remove(&server->requestSetPrimarySelection.link);
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
    wlr_log_init(WLR_INFO, NULL);
    prepareRuntimeDir();

    struct Server server = {0};
    int parsed = parseArguments(&server, argc, argv);
    if (parsed != 0)
        return parsed == 1 ? 0 : 2;

    return run(&server);
}
