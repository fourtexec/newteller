#pragma once

#include <QHash>
#include <QObject>
#include <QSocketNotifier>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

struct wl_display;
struct wl_registry;
struct wl_seat;
struct wl_proxy;
struct wl_array;
struct xcb_connection_t;

// Running top-level windows, exposed to QML as the context property `Windows`.
//
// The dock (panel) uses this to show every started application: one entry per
// top-level window, with the title, a best-effort application id (matched
// against the catalogue with Apps.matchWindowApp()), and its state.
//
// Back ends, chosen at startup from the Qt platform:
//   * X11 (native or XWayland): EWMH on a private xcb connection.
//     _NET_CLIENT_LIST provides the windows, _NET_ACTIVE_WINDOW the focus,
//     WM_CLASS the application id; activation, iconification and closing go
//     through the _NET_ACTIVE_WINDOW, WM_CHANGE_STATE and _NET_CLOSE_WINDOW
//     client messages. Property notify on the root window and the client
//     windows keeps the list current without polling.
//   * Wayland: on a private wl_display connection two listing protocols are
//     used side by side, so GNOME is covered as well:
//       - zwlr_foreign_toplevel_management_unstable_v1 (KDE Plasma, sway,
//         Hyprland, labwc, Wayfire, niri, ...) reports every toplevel with
//         title, app_id and state; activate/close/minimize are requests.
//       - ext_foreign_toplevel_list_v1 (GNOME 48+, also adopted by other
//         compositors) only lists toplevels with title and app_id and is
//         read-only; its entries carry readOnly: true and the dock falls
//         back to launching the matched application when one is clicked.
//     When the session runs XWayland (GNOME and KDE do by default) the X11
//     back end above is started in addition, and the two lists are merged:
//     a wlr toplevel wins over the matching X11 window, an X11 window (with
//     its working activate/minimize/close) wins over the matching read-only
//     ext entry. Duplicates are recognised by equal title and application id.
//   * Anything else (offscreen tests, compositors without the protocol): an
//     empty list; the dock then falls back to the /proc based running
//     application list of `Apps`.
//
// Window map fields: { id, title, appId, active, minimized, maximized,
// readOnly }. `id` is a stable opaque string for this session (x... = X11,
// w... = wlr toplevel, e... = ext toplevel).
class WindowList : public QObject {
    Q_OBJECT
    // One map per window:
    //   { id, title, appId, active, minimized, maximized, readOnly }
    // `id` is a stable opaque string for this session.
    Q_PROPERTY(QVariantList windows READ windows NOTIFY windowsChanged)
    Q_PROPERTY(int count READ count NOTIFY windowsChanged)
    // Backend name for diagnostics: "x11", "wayland" or "none".
    Q_PROPERTY(QString backend READ backend CONSTANT)

public:
    explicit WindowList(QObject *parent = nullptr);
    ~WindowList() override;

    QVariantList windows() const { return m_windows; }
    int count() const { return m_windows.size(); }
    QString backend() const { return m_backend; }

    Q_INVOKABLE void refresh();
    // Raises and focuses the window (also un-iconifies it on X11).
    Q_INVOKABLE void activate(const QString &id);
    Q_INVOKABLE void closeWindow(const QString &id);
    Q_INVOKABLE void minimize(const QString &id);
    Q_INVOKABLE void toggleMaximized(const QString &id);
    // The focused window's id, or "" (used by the dock and Alt+Tab-like UIs).
    Q_INVOKABLE QString activeWindow() const;
    // All window ids that belong to one application id (QML grouping).
    Q_INVOKABLE QStringList windowsOfApp(const QString &appId) const;

    // Implementation detail of windowlist.cpp: the libwayland event callbacks.
    // They are public only so the file-scope listener tables can reference them.
    static void wlRegistryGlobal(void *data, wl_registry *registry, uint32_t name,
                                 const char *interface, uint32_t version);
    static void wlRegistryGlobalRemove(void *data, wl_registry *registry, uint32_t name);
    static void wlToplevelTitle(void *data, void *toplevel, const char *title);
    static void wlToplevelAppId(void *data, void *toplevel, const char *appId);
    static void wlToplevelOutputEnter(void *data, void *toplevel, void *output);
    static void wlToplevelState(void *data, void *toplevel, wl_array *state);
    static void wlToplevelDone(void *data, void *toplevel);
    static void wlToplevelClosed(void *data, void *toplevel);
    // ext_foreign_toplevel_list_v1 (GNOME 48+, read-only listing). The
    // listener lambdas dispatch into these; everything that touches the
    // pending maps is a regular member.
    static void extListFinished(void *data);

signals:
    void windowsChanged();

private:
    void setWindows(QVariantList windows);
    // wlr toplevels, then the X11 windows no wlr toplevel already covers,
    // then the read-only ext toplevels nothing else covers.
    QVariantList mergedWindowList();
    // True when `list` already contains a window with the same title and
    // application id (duplicate reports of one window through two protocols).
    static bool windowListContains(const QVariantList &list, const QVariantMap &window);

    // --- X11 (EWMH) -------------------------------------------------------
    void startX11();
    void stopX11();
    QVariantList readX11Windows();
    void handleX11Events();
    void x11SendClientMessage(quint64 target, quint32 type, const quint32 values[5]);
    void x11Activate(quint64 window);
    void x11Close(quint64 window);
    void x11Minimize(quint64 window);
    void x11ToggleMaximized(quint64 window, bool currentlyMaximized);
    quint32 x11Atom(const char *name);

    // --- Wayland (wlr-foreign-toplevel) -----------------------------------
    // The wire tables and the event callbacks of the hand-declared protocol
    // live in windowlist.cpp; libwayland dispatches straight into them.
    void startWayland();
    void stopWayland();
    void waylandSocketReady();
    void waylandRoundtrip();
    void waylandEmit();
    void waylandToplevelCreated(void *handle);
    void extToplevelCreated(void *handle);
    void extToplevelClosed(void *handle);
    void extToplevelTitle(void *handle, const char *title);
    void extToplevelAppId(void *handle, const char *appId);
    void waylandUpdate(void *handle, const QString &key, const QString &value);
    void waylandState(void *handle, bool active, bool minimized, bool maximized);
    void waylandToplevelClosed(void *handle);
    void waylandFinished();
    QVariantList waylandSnapshot() const;

    QVariantList m_windows;
    QString m_backend = QStringLiteral("none");
    QTimer m_pollTimer;
    int m_nextWindowId = 1;

    // X11 state
    xcb_connection_t *m_xcb = nullptr;
    QSocketNotifier *m_xcbNotifier = nullptr;
    QHash<QString, quint32> m_atoms;
    QHash<QString, quint64> m_x11ByWindowId; // our id -> X window

    // Wayland state
    wl_display *m_wlDisplay = nullptr;
    wl_registry *m_wlRegistry = nullptr;
    wl_seat *m_wlSeat = nullptr;
    void *m_wlManager = nullptr; // zwlr_foreign_toplevel_manager_v1 *
    void *m_extManager = nullptr; // ext_foreign_toplevel_list_v1 *
    QSocketNotifier *m_wlNotifier = nullptr;
    uint32_t m_wlManagerName = 0;
    uint32_t m_wlManagerVersion = 0;
    uint32_t m_extManagerName = 0;
    QHash<void *, QVariantMap> m_waylandPending; // handle proxy -> entry (wlr)
    QHash<QString, void *> m_waylandByWindowId;  // our id -> handle proxy (wlr)
    QHash<void *, QVariantMap> m_extPending;     // handle proxy -> entry (ext, read-only)
    QHash<QString, void *> m_extByWindowId;      // our id -> handle proxy (ext)
};
