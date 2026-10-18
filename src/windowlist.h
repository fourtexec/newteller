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
//   * Wayland: zwlr_foreign_toplevel_management_unstable_v1 on a private
//     wl_display connection, so the compositor reports every toplevel with its
//     app_id, title and state (supported by KDE Plasma, sway, Hyprland, labwc,
//     Wayfire, niri, ...). set_active/close/minimize are protocol requests.
//   * Anything else (offscreen tests, compositors without the protocol): an
//     empty list; the dock then falls back to the /proc based running
//     application list of `Apps`.
class WindowList : public QObject {
    Q_OBJECT
    // One map per window:
    //   { id, title, appId, active, minimized, maximized }
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

signals:
    void windowsChanged();

private:
    void setWindows(QVariantList windows);

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
    QSocketNotifier *m_wlNotifier = nullptr;
    uint32_t m_wlManagerName = 0;
    uint32_t m_wlManagerVersion = 0;
    QHash<void *, QVariantMap> m_waylandPending; // handle proxy -> entry
    QHash<QString, void *> m_waylandByWindowId;  // our id -> handle proxy
};
