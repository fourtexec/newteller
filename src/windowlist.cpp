#include "windowlist.h"

#include <QGuiApplication>
#include <QVariantMap>
#include <QtGlobal>

#include <cstring>

#if QT_CONFIG(xcb)
#include <xcb/xcb.h>
#endif

#include <wayland-client.h>

// ---------------------------------------------------------------------------
// wlr-foreign-toplevel-management-unstable-v1 wire definitions.
//
// The protocol headers are not installed by most distributions, so the two
// interfaces are declared here from the protocol XML. Only the parts the dock
// uses are defined: reading title/app_id/state and sending activate, close,
// minimize/restore and destroy. Signatures must match the protocol exactly
// (libwayland dispatches straight into the listener tables).
// ---------------------------------------------------------------------------

namespace {

// zwlr_foreign_toplevel_handle_v1 requests (opcodes).
enum {
    WLR_HANDLE_SET_MAXIMIZED = 0,
    WLR_HANDLE_UNSET_MAXIMIZED = 1,
    WLR_HANDLE_SET_MINIMIZED = 2,
    WLR_HANDLE_UNSET_MINIMIZED = 3,
    WLR_HANDLE_ACTIVATE = 4,
    WLR_HANDLE_CLOSE = 5,
    WLR_HANDLE_SET_RECTANGLE = 6,
    WLR_HANDLE_DESTROY = 7,
};

// zwlr_foreign_toplevel_handle_v1 state values (the `state` event array).
enum {
    WLR_STATE_MAXIMIZED = 0,
    WLR_STATE_MINIMIZED = 1,
    WLR_STATE_ACTIVATED = 2,
};

// The wl_message.types field is `const wl_interface **`, so these arrays are
// intentionally non-const pointers (a `const wl_interface *const []` would not
// convert to it).
const wl_interface *kActivateTypes[] = { &wl_seat_interface };
const wl_interface *kSetRectangleTypes[] = { &wl_surface_interface, nullptr, nullptr };
const wl_interface *kOutputEventTypes[] = { &wl_output_interface };

const wl_message kHandleRequests[] = {
    { "set_maximized", "", nullptr },
    { "unset_maximized", "", nullptr },
    { "set_minimized", "", nullptr },
    { "unset_minimized", "", nullptr },
    { "activate", "o", kActivateTypes },
    { "close", "", nullptr },
    { "set_rectangle", "oii", kSetRectangleTypes },
    { "destroy", "", nullptr },
};

const wl_message kHandleEvents[] = {
    { "title", "s", nullptr },
    { "app_id", "s", nullptr },
    { "output_enter", "o", kOutputEventTypes },
    { "output_leave", "o", kOutputEventTypes },
    { "state", "a", nullptr },
    { "done", "", nullptr },
    { "closed", "", nullptr },
};

} // namespace

// Named with the project prefix and external linkage so the manager event
// table below can point at it (the handle interface is created by the
// compositor through the `toplevel` event).
extern const wl_interface ntToplevelHandleInterface = {
    "zwlr_foreign_toplevel_handle_v1", 3, 8, kHandleRequests, 7, kHandleEvents
};

// The ext_foreign_toplevel_list_v1 interfaces have external linkage so the
// registry binding and the anonymous-namespace wire tables below can use
// them; the definitions follow their wire tables.
extern const wl_interface ntExtToplevelHandleInterface;
extern const wl_interface ntExtToplevelListInterface;

namespace {

const wl_interface *kManagerEventTypes[] = { &ntToplevelHandleInterface };
const wl_message kManagerRequests[] = { { "stop", "", nullptr } };
const wl_message kManagerEvents[] = {
    { "toplevel", "n", kManagerEventTypes },
    { "finished", "", nullptr },
};

const wl_interface kManagerInterface = {
    "zwlr_foreign_toplevel_management_unstable_v1", 3, 1, kManagerRequests, 2, kManagerEvents
};

// --- ext_foreign_toplevel_list_v1 (GNOME 48+, read-only) -------------------
const wl_interface *kExtStopTypes[] = { &ntExtToplevelListInterface };
const wl_message kExtListRequests[] = { { "stop", "o", kExtStopTypes } };
const wl_interface *kExtToplevelEventTypes[] = { &ntExtToplevelHandleInterface };
const wl_message kExtManagerEvents[] = {
    { "toplevel", "n", kExtToplevelEventTypes },
    { "finished", "", nullptr },
};
const wl_message kExtHandleRequests[] = { { "destroy", "", nullptr } };
const wl_message kExtHandleEvents[] = {
    { "closed", "", nullptr },
    { "done", "", nullptr },
    { "title", "s", nullptr },
    { "app_id", "s", nullptr },
    { "identifier", "s", nullptr },
};

} // namespace

extern const wl_interface ntExtToplevelListInterface = {
    "ext_foreign_toplevel_list_v1", 1, 1, kExtListRequests, 2, kExtManagerEvents
};

extern const wl_interface ntExtToplevelHandleInterface = {
    "ext_foreign_toplevel_handle_v1", 1, 1, kExtHandleRequests, 5, kExtHandleEvents
};

namespace {

// X11 client message source indication: 1 = application, 2 = pager/launcher.
constexpr uint32_t kSourcePager = 2;
constexpr uint32_t kCurrentTime = 0; // XCB_CURRENT_TIME

} // namespace
// ---------------------------------------------------------------------------
// Wayland callbacks. libwayland dispatches through void(**)(void) listener
// tables with one entry per event, so every slot is filled (no-ops included).
// They are static members of WindowList so they can reach the private state.
// ---------------------------------------------------------------------------

void WindowList::wlRegistryGlobal(void *data, wl_registry *registry, uint32_t name,
                                  const char *interface, uint32_t version)
{
    auto *list = static_cast<WindowList *>(data);
    if (std::strcmp(interface, "zwlr_foreign_toplevel_management_unstable_v1") == 0) {
        list->m_wlManagerName = name;
        list->m_wlManagerVersion = qMin(version, uint32_t(3));
        list->m_wlManager = wl_registry_bind(registry, name, &kManagerInterface,
                                             list->m_wlManagerVersion);
        static void (*managerListener[])(void) = {
            reinterpret_cast<void (*)(void)>(+[](void *self, void *manager, void *toplevel) {
                Q_UNUSED(manager);
                static_cast<WindowList *>(self)->waylandToplevelCreated(toplevel);
            }),
            reinterpret_cast<void (*)(void)>(+[](void *self, void *manager) {
                Q_UNUSED(manager);
                static_cast<WindowList *>(self)->waylandFinished();
            }),
        };
        wl_proxy_add_listener(static_cast<wl_proxy *>(list->m_wlManager), managerListener, list);
    } else if (std::strcmp(interface, "ext_foreign_toplevel_list_v1") == 0) {
        // GNOME's read-only window list. Bound next to the wlr manager on the
        // same private connection; whichever protocols the compositor speaks
        // feed the merged window list.
        list->m_extManagerName = name;
        list->m_extManager = wl_registry_bind(registry, name, &ntExtToplevelListInterface, 1);
        static void (*extManagerListener[])(void) = {
            reinterpret_cast<void (*)(void)>(+[](void *self, void *manager, void *toplevel) {
                Q_UNUSED(manager);
                static_cast<WindowList *>(self)->extToplevelCreated(toplevel);
            }),
            reinterpret_cast<void (*)(void)>(+[](void *self, void *manager) {
                Q_UNUSED(manager);
                static_cast<WindowList *>(self)->extListFinished(self);
            }),
        };
        wl_proxy_add_listener(static_cast<wl_proxy *>(list->m_extManager), extManagerListener, list);
    } else if (std::strcmp(interface, "wl_seat") == 0 && list->m_wlSeat == nullptr) {
        list->m_wlSeat = static_cast<wl_seat *>(
            wl_registry_bind(registry, name, &wl_seat_interface, qMin(version, uint32_t(5))));
        // The seat is bound only so the `activate` request can name it. The
        // generated wl_seat_add_listener helper is used because wl_seat and
        // wl_proxy are unrelated opaque types (a C++ static_cast between them
        // is ill-formed; only libwayland's own C cast may go through them).
        static const wl_seat_listener seatListener = {
            [](void *self, wl_seat *seat, uint32_t capabilities) {
                Q_UNUSED(self); Q_UNUSED(seat); Q_UNUSED(capabilities);
            },
            [](void *self, wl_seat *seat, const char *seatName) {
                Q_UNUSED(self); Q_UNUSED(seat); Q_UNUSED(seatName);
            },
        };
        wl_seat_add_listener(list->m_wlSeat, &seatListener, list);
    }
}

void WindowList::wlRegistryGlobalRemove(void *data, wl_registry *registry, uint32_t name)
{
    Q_UNUSED(data);
    Q_UNUSED(registry);
    Q_UNUSED(name);
}

void WindowList::wlToplevelTitle(void *data, void *toplevel, const char *title)
{
    static_cast<WindowList *>(data)->waylandUpdate(toplevel, QStringLiteral("title"),
                                                   QString::fromUtf8(title));
}

void WindowList::wlToplevelAppId(void *data, void *toplevel, const char *appId)
{
    static_cast<WindowList *>(data)->waylandUpdate(toplevel, QStringLiteral("appId"),
                                                   QString::fromUtf8(appId));
}

void WindowList::wlToplevelOutputEnter(void *data, void *toplevel, void *output)
{
    Q_UNUSED(data);
    Q_UNUSED(toplevel);
    Q_UNUSED(output);
}

void WindowList::wlToplevelState(void *data, void *toplevel, wl_array *state)
{
    bool minimized = false;
    bool maximized = false;
    bool active = false;
    if (state != nullptr) {
        const auto *values = static_cast<const uint32_t *>(state->data);
        for (size_t i = 0; i < state->size / sizeof(uint32_t); ++i) {
            switch (values[i]) {
            case WLR_STATE_MAXIMIZED: maximized = true; break;
            case WLR_STATE_MINIMIZED: minimized = true; break;
            case WLR_STATE_ACTIVATED: active = true; break;
            default: break;
            }
        }
    }
    static_cast<WindowList *>(data)->waylandState(toplevel, active, minimized, maximized);
}

void WindowList::wlToplevelDone(void *data, void *toplevel)
{
    Q_UNUSED(toplevel);
    static_cast<WindowList *>(data)->waylandEmit();
}

void WindowList::wlToplevelClosed(void *data, void *toplevel)
{
    static_cast<WindowList *>(data)->waylandToplevelClosed(toplevel);
}

static void (*kToplevelListener[])(void) = {
    reinterpret_cast<void (*)(void)>(&WindowList::wlToplevelTitle),
    reinterpret_cast<void (*)(void)>(&WindowList::wlToplevelAppId),
    reinterpret_cast<void (*)(void)>(&WindowList::wlToplevelOutputEnter),
    reinterpret_cast<void (*)(void)>(&WindowList::wlToplevelOutputEnter), // output_leave
    reinterpret_cast<void (*)(void)>(&WindowList::wlToplevelState),
    reinterpret_cast<void (*)(void)>(&WindowList::wlToplevelDone),
    reinterpret_cast<void (*)(void)>(&WindowList::wlToplevelClosed),
};

static void (*kRegistryListener[])(void) = {
    reinterpret_cast<void (*)(void)>(&WindowList::wlRegistryGlobal),
    reinterpret_cast<void (*)(void)>(&WindowList::wlRegistryGlobalRemove),
};

// --- ext_foreign_toplevel_list_v1 callbacks --------------------------------
// The manager and handle listener tables dispatch straight into these
// members, exactly like the wlr callbacks above.

void WindowList::extListFinished(void *data)
{
    Q_UNUSED(data);
    // The compositor retired the ext list; wlr (if any) keeps running.
}

void WindowList::extToplevelCreated(void *handle)
{
    static void (*extHandleListener[])(void) = {
        reinterpret_cast<void (*)(void)>(+[](void *self, void *toplevel) {
            static_cast<WindowList *>(self)->extToplevelClosed(toplevel);
        }),
        reinterpret_cast<void (*)(void)>(+[](void *self, void *toplevel) {
            Q_UNUSED(toplevel);
            static_cast<WindowList *>(self)->waylandEmit();
        }),
        reinterpret_cast<void (*)(void)>(+[](void *self, void *toplevel, const char *value) {
            static_cast<WindowList *>(self)->extToplevelTitle(toplevel, value);
        }),
        reinterpret_cast<void (*)(void)>(+[](void *self, void *toplevel, const char *value) {
            static_cast<WindowList *>(self)->extToplevelAppId(toplevel, value);
        }),
        reinterpret_cast<void (*)(void)>(+[](void *self, void *toplevel, const char *value) {
            Q_UNUSED(self);
            Q_UNUSED(toplevel);
            Q_UNUSED(value); // stable cross-session identifier, not needed yet
        }),
    };
    wl_proxy_add_listener(static_cast<wl_proxy *>(handle), extHandleListener, this);

    const QString id = QStringLiteral("e%1").arg(m_nextWindowId++);
    m_extByWindowId.insert(id, handle);
    QVariantMap w;
    w.insert(QStringLiteral("id"), id);
    w.insert(QStringLiteral("title"), QString());
    w.insert(QStringLiteral("appId"), QString());
    w.insert(QStringLiteral("active"), false);
    w.insert(QStringLiteral("minimized"), false);
    w.insert(QStringLiteral("maximized"), false);
    // The ext protocol carries no state and no requests: the dock can list
    // the window but cannot focus, minimise or close it.
    w.insert(QStringLiteral("readOnly"), true);
    m_extPending.insert(handle, w);
}

void WindowList::extToplevelClosed(void *handle)
{
    wl_proxy_marshal(static_cast<wl_proxy *>(handle), 0); // destroy
    m_extPending.remove(handle);
    for (auto it = m_extByWindowId.begin(); it != m_extByWindowId.end(); ++it) {
        if (it.value() == handle) {
            m_extByWindowId.erase(it);
            break;
        }
    }
    waylandEmit();
}

void WindowList::extToplevelTitle(void *handle, const char *title)
{
    const auto it = m_extPending.find(handle);
    if (it != m_extPending.end())
        it->insert(QStringLiteral("title"), QString::fromUtf8(title));
}

void WindowList::extToplevelAppId(void *handle, const char *appId)
{
    const auto it = m_extPending.find(handle);
    if (it != m_extPending.end())
        it->insert(QStringLiteral("appId"), QString::fromUtf8(appId));
}

// ---------------------------------------------------------------------------
// WindowList
// ---------------------------------------------------------------------------

WindowList::WindowList(QObject *parent)
    : QObject(parent)
{
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        startWayland();
        // GNOME and KDE run XWayland, and many applications still speak X11:
        // the EWMH back end covers those windows (with working activation,
        // iconification and closing) on a Wayland session as well.
        startX11();
        if (m_wlDisplay && m_xcb)
            m_backend = QStringLiteral("wayland+x11");
        else if (m_wlDisplay)
            m_backend = QStringLiteral("wayland");
        else if (m_xcb)
            m_backend = QStringLiteral("x11");
    } else if (QGuiApplication::platformName().startsWith(QLatin1String("xcb"))) {
        startX11();
        m_backend = m_xcb ? QStringLiteral("x11") : QStringLiteral("none");
    }

    if (m_backend != QLatin1String("none")) {
        // Safety net: catches whatever the event streams may miss and republishes
        // the list after resumes from suspend.
        m_pollTimer.setInterval(2000);
        connect(&m_pollTimer, &QTimer::timeout, this, &WindowList::refresh);
        m_pollTimer.start();
        refresh();
    }
}

WindowList::~WindowList()
{
    stopX11();
    stopWayland();
}

void WindowList::setWindows(QVariantList windows)
{
    if (windows == m_windows)
        return;
    m_windows = std::move(windows);
    emit windowsChanged();
}

void WindowList::refresh()
{
    if (m_backend == QLatin1String("none"))
        return;
    if (m_backend == QLatin1String("wayland") || m_backend == QLatin1String("wayland+x11")) {
        // Dispatches the queued toplevel events into the pending maps.
        waylandRoundtrip();
    }
    if (m_xcb || m_wlDisplay)
        setWindows(mergedWindowList());
}

QString WindowList::activeWindow() const
{
    for (const QVariant &entry : m_windows) {
        const QVariantMap w = entry.toMap();
        if (w.value(QStringLiteral("active")).toBool())
            return w.value(QStringLiteral("id")).toString();
    }
    return {};
}

QStringList WindowList::windowsOfApp(const QString &appId) const
{
    QStringList ids;
    for (const QVariant &entry : m_windows) {
        const QVariantMap w = entry.toMap();
        if (w.value(QStringLiteral("appId")).toString() == appId)
            ids << w.value(QStringLiteral("id")).toString();
    }
    return ids;
}

// ---------------------------------------------------------------------------
// X11 (EWMH)
// ---------------------------------------------------------------------------

#if QT_CONFIG(xcb)

void WindowList::startX11()
{
    m_xcb = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(m_xcb)) {
        xcb_disconnect(m_xcb);
        m_xcb = nullptr;
        return;
    }

    // Property changes on the root window announce new/lost clients and the
    // focus; PropertyChange on every client announces its state changes.
    const xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(m_xcb)).data;
    const uint32_t rootMask = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
    xcb_change_window_attributes(m_xcb, screen->root, XCB_CW_EVENT_MASK, &rootMask);
    xcb_flush(m_xcb);

    m_xcbNotifier = new QSocketNotifier(xcb_get_file_descriptor(m_xcb), QSocketNotifier::Read, this);
    connect(m_xcbNotifier, &QSocketNotifier::activated, this, [this] {
        m_xcbNotifier->setEnabled(false);
        handleX11Events();
        if (m_xcbNotifier)
            m_xcbNotifier->setEnabled(true);
    });
}

void WindowList::stopX11()
{
    if (m_xcbNotifier) {
        m_xcbNotifier->setEnabled(false);
        m_xcbNotifier->deleteLater();
        m_xcbNotifier = nullptr;
    }
    if (m_xcb) {
        xcb_disconnect(m_xcb);
        m_xcb = nullptr;
    }
    m_x11ByWindowId.clear();
}

quint32 WindowList::x11Atom(const char *name)
{
    const auto cached = m_atoms.constFind(QLatin1String(name));
    if (cached != m_atoms.constEnd())
        return cached.value();
    const xcb_intern_atom_cookie_t cookie =
        xcb_intern_atom(m_xcb, 0, uint16_t(std::strlen(name)), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(m_xcb, cookie, nullptr);
    if (!reply)
        return XCB_ATOM_NONE;
    const xcb_atom_t atom = reply->atom;
    std::free(reply);
    m_atoms.insert(QLatin1String(name), atom);
    return atom;
}

namespace {

// Reads a 32-bit property (window/cardinal/atom arrays). Returns false when
// the property is absent or has an unexpected shape.
bool readWindowArray(xcb_connection_t *connection, xcb_window_t window, xcb_atom_t property,
                     xcb_atom_t type, QVector<quint32> &out)
{
    out.clear();
    if (property == XCB_ATOM_NONE)
        return false;
    const xcb_get_property_cookie_t cookie =
        xcb_get_property_unchecked(connection, 0, window, property, type, 0, 4096);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, nullptr);
    if (!reply || reply->type != type || reply->format != 32 || reply->length == 0) {
        std::free(reply);
        return false;
    }
    const auto *data = static_cast<const quint32 *>(xcb_get_property_value(reply));
    out.reserve(int(reply->length));
    for (uint32_t i = 0; i < reply->length; ++i)
        out.append(data[i]);
    std::free(reply);
    return true;
}

QString readTextProperty(xcb_connection_t *connection, xcb_window_t window, xcb_atom_t property,
                         xcb_atom_t type)
{
    if (property == XCB_ATOM_NONE)
        return {};
    const xcb_get_property_cookie_t cookie =
        xcb_get_property_unchecked(connection, 0, window, property, type, 0, 8192);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, nullptr);
    if (!reply || reply->type != type || reply->format != 8) {
        std::free(reply);
        return {};
    }
    const char *data = static_cast<const char *>(xcb_get_property_value(reply));
    const int length = int(xcb_get_property_value_length(reply));
    const QString text = QString::fromUtf8(data, length);
    std::free(reply);
    return text;
}

// WM_CLASS holds two NUL-terminated strings: instance and class. The class is
// the better application identity, the instance the fallback.
QString readWindowClass(xcb_connection_t *connection, xcb_window_t window)
{
    const xcb_get_property_cookie_t cookie = xcb_get_property_unchecked(
        connection, 0, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 0, 1024);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, nullptr);
    if (!reply || reply->type != XCB_ATOM_STRING || reply->format != 8) {
        std::free(reply);
        return {};
    }
    const char *data = static_cast<const char *>(xcb_get_property_value(reply));
    const int length = int(xcb_get_property_value_length(reply));
    const QByteArray raw(data, length);
    std::free(reply);

    const int first = raw.indexOf('\0');
    if (first < 0)
        return QString::fromLatin1(raw);
    const QByteArray instance = raw.left(first);
    const QByteArray rest = raw.mid(first + 1);
    const int second = rest.indexOf('\0');
    const QByteArray klass = second >= 0 ? rest.left(second) : rest;
    return klass.isEmpty() ? QString::fromLatin1(instance) : QString::fromLatin1(klass);
}

} // namespace

QVariantList WindowList::readX11Windows()
{
    const xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(m_xcb)).data;
    const xcb_atom_t clientList = x11Atom("_NET_CLIENT_LIST");
    const xcb_atom_t activeAtom = x11Atom("_NET_ACTIVE_WINDOW");
    const xcb_atom_t nameAtom = x11Atom("_NET_WM_NAME");
    const xcb_atom_t utf8 = x11Atom("UTF8_STRING");
    const xcb_atom_t stateAtom = x11Atom("_NET_WM_STATE");
    const xcb_atom_t hidden = x11Atom("_NET_WM_STATE_HIDDEN");
    const xcb_atom_t skipTaskbar = x11Atom("_NET_WM_STATE_SKIP_TASKBAR");
    const xcb_atom_t maxVert = x11Atom("_NET_WM_STATE_MAXIMIZED_VERT");
    const xcb_atom_t maxHorz = x11Atom("_NET_WM_STATE_MAXIMIZED_HORZ");
    const xcb_atom_t typeAtom = x11Atom("_NET_WM_WINDOW_TYPE");
    const xcb_atom_t dockType = x11Atom("_NET_WM_WINDOW_TYPE_DOCK");
    const xcb_atom_t desktopType = x11Atom("_NET_WM_WINDOW_TYPE_DESKTOP");

    QVector<quint32> clients;
    if (!readWindowArray(m_xcb, screen->root, clientList, XCB_ATOM_WINDOW, clients))
        return m_windows;

    QVector<quint32> activeWin;
    quint64 active = 0;
    if (readWindowArray(m_xcb, screen->root, activeAtom, XCB_ATOM_WINDOW, activeWin)
        && !activeWin.isEmpty())
        active = activeWin.first();

    QVariantList out;
    for (const quint32 raw : clients) {
        const xcb_window_t window = xcb_window_t(raw);

        // New clients: listen for their state changes for the rest of the session.
        const uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
        xcb_change_window_attributes(m_xcb, window, XCB_CW_EVENT_MASK, &mask);

        // Shell windows (the panel and the desktop) are never dock entries.
        QVector<quint32> types;
        if (readWindowArray(m_xcb, window, typeAtom, XCB_ATOM_ATOM, types)) {
            if (types.contains(dockType) || types.contains(desktopType))
                continue;
        }

        QString title = readTextProperty(m_xcb, window, nameAtom, utf8);
        if (title.isEmpty())
            title = readTextProperty(m_xcb, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING);

        QVector<quint32> states;
        bool minimized = false;
        bool maximized = false;
        if (readWindowArray(m_xcb, window, stateAtom, XCB_ATOM_ATOM, states)) {
            minimized = states.contains(hidden);
            maximized = states.contains(maxVert) && states.contains(maxHorz);
            // The start menu, the run dialog and friends are not dock entries.
            if (states.contains(skipTaskbar))
                continue;
        }

        const QString id = QStringLiteral("x%1").arg(window, 0, 16);
        m_x11ByWindowId.insert(id, quint64(window));

        QVariantMap w;
        w.insert(QStringLiteral("id"), id);
        w.insert(QStringLiteral("title"), title);
        w.insert(QStringLiteral("appId"), readWindowClass(m_xcb, window));
        w.insert(QStringLiteral("active"), quint64(window) == active && !minimized);
        w.insert(QStringLiteral("minimized"), minimized);
        w.insert(QStringLiteral("maximized"), maximized);
        out.append(w);
    }
    xcb_flush(m_xcb);
    return out;
}

void WindowList::handleX11Events()
{
    bool relevant = false;
    while (xcb_generic_event_t *event = xcb_poll_for_event(m_xcb)) {
        const uint8_t type = event->response_type & 0x7f;
        if (type == XCB_PROPERTY_NOTIFY) {
            const auto *notify = reinterpret_cast<xcb_property_notify_event_t *>(event);
            if (notify->atom == x11Atom("_NET_CLIENT_LIST")
                || notify->atom == x11Atom("_NET_ACTIVE_WINDOW")
                || notify->atom == x11Atom("_NET_WM_STATE")
                || notify->atom == x11Atom("_NET_WM_NAME"))
                relevant = true;
        } else if (type == XCB_DESTROY_NOTIFY || type == XCB_UNMAP_NOTIFY
                   || type == XCB_MAP_NOTIFY) {
            relevant = true;
        }
        std::free(event);
    }
    if (relevant)
        refresh();
}

void WindowList::x11SendClientMessage(quint64 target, quint32 type, const quint32 values[5])
{
    const xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(m_xcb)).data;
    xcb_client_message_event_t event {};
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = xcb_window_t(target);
    event.type = type;
    std::memcpy(event.data.data32, values, sizeof(uint32_t) * 5);
    xcb_send_event(m_xcb, 0, screen->root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                   reinterpret_cast<const char *>(&event));
    xcb_flush(m_xcb);
}

void WindowList::x11Activate(quint64 window)
{
    const quint32 atom = x11Atom("_NET_ACTIVE_WINDOW");
    if (atom == 0)
        return;
    const quint32 values[5] = { kSourcePager, kCurrentTime, 0, 0, 0 };
    x11SendClientMessage(window, atom, values);
}

void WindowList::x11Close(quint64 window)
{
    const quint32 atom = x11Atom("_NET_CLOSE_WINDOW");
    if (atom == 0)
        return;
    const quint32 values[5] = { kCurrentTime, kSourcePager, 0, 0, 0 };
    x11SendClientMessage(window, atom, values);
}

void WindowList::x11Minimize(quint64 window)
{
    // WM_CHANGE_STATE with IconicState(3) is the classic iconify request; it
    // goes to the client window itself, not to the root. WM_CHANGE_STATE is
    // not one of the X protocol's predefined atoms, so there is no
    // XCB_ATOM_WM_CHANGE_STATE constant and it must be interned by name.
    const quint32 changeState = x11Atom("WM_CHANGE_STATE");
    if (changeState == 0)
        return;
    xcb_client_message_event_t event {};
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = xcb_window_t(window);
    event.type = changeState;
    event.data.data32[0] = 3; // IconicState
    xcb_send_event(m_xcb, 0, xcb_window_t(window), XCB_EVENT_MASK_NO_EVENT,
                   reinterpret_cast<const char *>(&event));
    xcb_flush(m_xcb);
}

void WindowList::x11ToggleMaximized(quint64 window, bool currentlyMaximized)
{
    Q_UNUSED(currentlyMaximized);
    const quint32 state = x11Atom("_NET_WM_STATE");
    const quint32 vert = x11Atom("_NET_WM_STATE_MAXIMIZED_VERT");
    const quint32 horz = x11Atom("_NET_WM_STATE_MAXIMIZED_HORZ");
    if (state == 0 || vert == 0 || horz == 0)
        return;
    const quint32 values[5] = { 2, vert, horz, kSourcePager, 0 }; // 2 = toggle
    x11SendClientMessage(window, state, values);
}

#else // !QT_CONFIG(xcb)

void WindowList::startX11() {}
void WindowList::stopX11() {}
QVariantList WindowList::readX11Windows() { return {}; }
void WindowList::handleX11Events() {}
void WindowList::x11SendClientMessage(quint64, quint32, const quint32[5]) {}
void WindowList::x11Activate(quint64) {}
void WindowList::x11Close(quint64) {}
void WindowList::x11Minimize(quint64) {}
void WindowList::x11ToggleMaximized(quint64, bool) {}
quint32 WindowList::x11Atom(const char *) { return 0; }

#endif

// ---------------------------------------------------------------------------
// Public actions, routed to whatever back end owns the window id
// ---------------------------------------------------------------------------

void WindowList::activate(const QString &id)
{
    if (m_xcb) {
        const auto it = m_x11ByWindowId.constFind(id);
        if (it != m_x11ByWindowId.constEnd()) {
            x11Activate(it.value());
            return;
        }
    }
    if (m_wlDisplay) {
        const auto it = m_waylandByWindowId.constFind(id);
        if (it != m_waylandByWindowId.constEnd() && m_wlSeat) {
            wl_proxy_marshal(static_cast<wl_proxy *>(it.value()), WLR_HANDLE_ACTIVATE, m_wlSeat);
            wl_display_flush(m_wlDisplay);
        }
    }
}

void WindowList::closeWindow(const QString &id)
{
    if (m_xcb) {
        const auto it = m_x11ByWindowId.constFind(id);
        if (it != m_x11ByWindowId.constEnd()) {
            x11Close(it.value());
            return;
        }
    }
    if (m_wlDisplay) {
        const auto it = m_waylandByWindowId.constFind(id);
        if (it != m_waylandByWindowId.constEnd()) {
            wl_proxy_marshal(static_cast<wl_proxy *>(it.value()), WLR_HANDLE_CLOSE);
            wl_display_flush(m_wlDisplay);
        }
    }
}

void WindowList::minimize(const QString &id)
{
    if (m_xcb) {
        const auto it = m_x11ByWindowId.constFind(id);
        if (it != m_x11ByWindowId.constEnd()) {
            x11Minimize(it.value());
            return;
        }
    }
    if (m_wlDisplay) {
        const auto it = m_waylandByWindowId.constFind(id);
        if (it != m_waylandByWindowId.constEnd()) {
            wl_proxy_marshal(static_cast<wl_proxy *>(it.value()), WLR_HANDLE_SET_MINIMIZED);
            wl_display_flush(m_wlDisplay);
        }
    }
}

void WindowList::toggleMaximized(const QString &id)
{
    bool currentlyMaximized = false;
    for (const QVariant &entry : m_windows) {
        const QVariantMap w = entry.toMap();
        if (w.value(QStringLiteral("id")) == id)
            currentlyMaximized = w.value(QStringLiteral("maximized")).toBool();
    }
    if (m_xcb) {
        const auto it = m_x11ByWindowId.constFind(id);
        if (it != m_x11ByWindowId.constEnd()) {
            x11ToggleMaximized(it.value(), currentlyMaximized);
            return;
        }
    }
    if (m_wlDisplay) {
        const auto it = m_waylandByWindowId.constFind(id);
        if (it != m_waylandByWindowId.constEnd()) {
            wl_proxy_marshal(static_cast<wl_proxy *>(it.value()),
                             currentlyMaximized ? WLR_HANDLE_UNSET_MAXIMIZED
                                                : WLR_HANDLE_SET_MAXIMIZED);
            wl_display_flush(m_wlDisplay);
        }
    }
}

// ---------------------------------------------------------------------------
// Wayland (wlr-foreign-toplevel)
// ---------------------------------------------------------------------------

void WindowList::startWayland()
{
    // A private connection: the dock only talks the foreign-toplevel protocol
    // and stays fully independent of Qt's own display connection.
    m_wlDisplay = wl_display_connect(nullptr);
    if (!m_wlDisplay)
        return;

    m_wlRegistry = wl_display_get_registry(m_wlDisplay);
    wl_proxy_add_listener(reinterpret_cast<wl_proxy *>(m_wlRegistry), kRegistryListener, this);

    // Two roundtrips: the first processes the registry globals (binds the
    // manager(s) and the seat), the second the initial toplevel events the
    // compositor sends right after the binds.
    wl_display_roundtrip(m_wlDisplay);
    wl_display_roundtrip(m_wlDisplay);

    if (!m_wlManager && !m_extManager) {
        // The compositor implements neither listing protocol (GNOME before 48,
        // for example). The X11 back end may still cover the session's
        // XWayland windows, so only this connection is closed here.
        stopWayland();
        return;
    }

    m_wlNotifier = new QSocketNotifier(wl_display_get_fd(m_wlDisplay), QSocketNotifier::Read, this);
    connect(m_wlNotifier, &QSocketNotifier::activated, this, &WindowList::waylandSocketReady);
    wl_display_flush(m_wlDisplay);
}

void WindowList::stopWayland()
{
    if (m_wlNotifier) {
        m_wlNotifier->setEnabled(false);
        m_wlNotifier->deleteLater();
        m_wlNotifier = nullptr;
    }
    if (m_wlDisplay) {
        wl_display_disconnect(m_wlDisplay);
        m_wlDisplay = nullptr;
    }
    m_wlRegistry = nullptr;
    m_wlManager = nullptr;
    m_wlSeat = nullptr;
    m_extManager = nullptr;
    m_waylandPending.clear();
    m_waylandByWindowId.clear();
    m_extPending.clear();
    m_extByWindowId.clear();
}

void WindowList::waylandSocketReady()
{
    if (!m_wlDisplay || !m_wlNotifier)
        return;
    m_wlNotifier->setEnabled(false);
    if (wl_display_get_error(m_wlDisplay) != 0) {
        stopWayland();
        if (!m_xcb)
            m_backend = QStringLiteral("none");
        setWindows(mergedWindowList());
        return;
    }
    // Standard cooperative read: claim the read, drain the socket, dispatch.
    while (wl_display_prepare_read(m_wlDisplay) != 0)
        wl_display_dispatch_pending(m_wlDisplay);
    if (wl_display_read_events(m_wlDisplay) < 0) {
        stopWayland();
        if (!m_xcb)
            m_backend = QStringLiteral("none");
        setWindows(mergedWindowList());
        return;
    }
    wl_display_dispatch_pending(m_wlDisplay);
    setWindows(mergedWindowList());
    if (m_wlNotifier)
        m_wlNotifier->setEnabled(true);
}

void WindowList::waylandRoundtrip()
{
    if (!m_wlDisplay)
        return;
    if (wl_display_roundtrip(m_wlDisplay) < 0) {
        // The connection died; the X11 back end (if any) keeps the dock alive.
        stopWayland();
        if (!m_xcb)
            m_backend = QStringLiteral("none");
    }
}

void WindowList::waylandEmit()
{
    // The poll/dispatch callers publish the snapshot; this hook exists for the
    // `done` event so grouped updates stay cheap.
    setWindows(waylandSnapshot());
}

void WindowList::waylandToplevelCreated(void *handle)
{
    wl_proxy_add_listener(static_cast<wl_proxy *>(handle), kToplevelListener, this);
    const QString id = QStringLiteral("w%1").arg(m_nextWindowId++);
    m_waylandByWindowId.insert(id, handle);
    QVariantMap w;
    w.insert(QStringLiteral("id"), id);
    w.insert(QStringLiteral("title"), QString());
    w.insert(QStringLiteral("appId"), QString());
    w.insert(QStringLiteral("active"), false);
    w.insert(QStringLiteral("minimized"), false);
    w.insert(QStringLiteral("maximized"), false);
    m_waylandPending.insert(handle, w);
}

void WindowList::waylandUpdate(void *handle, const QString &key, const QString &value)
{
    const auto it = m_waylandPending.find(handle);
    if (it == m_waylandPending.end())
        return;
    it->insert(key, value);
}

void WindowList::waylandState(void *handle, bool active, bool minimized, bool maximized)
{
    const auto it = m_waylandPending.find(handle);
    if (it == m_waylandPending.end())
        return;
    it->insert(QStringLiteral("active"), active && !minimized);
    it->insert(QStringLiteral("minimized"), minimized);
    it->insert(QStringLiteral("maximized"), maximized);
}

void WindowList::waylandToplevelClosed(void *handle)
{
    // v3 added an explicit destroy; the handle stays silent without it.
    if (m_wlManager && m_wlManagerVersion >= 3)
        wl_proxy_marshal(static_cast<wl_proxy *>(handle), WLR_HANDLE_DESTROY);
    m_waylandPending.remove(handle);
    for (auto it = m_waylandByWindowId.begin(); it != m_waylandByWindowId.end(); ++it) {
        if (it.value() == handle) {
            m_waylandByWindowId.erase(it);
            break;
        }
    }
    waylandEmit();
}

void WindowList::waylandFinished()
{
    // The compositor is done with the wlr protocol (manager retirement); the
    // ext list, if bound, keeps feeding the merged snapshot.
    m_waylandPending.clear();
    m_waylandByWindowId.clear();
    setWindows(mergedWindowList());
}

QVariantList WindowList::waylandSnapshot() const
{
    QVariantList out;
    out.reserve(m_waylandPending.size() + m_extPending.size());
    for (const QVariantMap &entry : m_waylandPending)
        out.append(entry);
    for (const QVariantMap &entry : m_extPending)
        out.append(entry);
    return out;
}

// ---------------------------------------------------------------------------
// Merged multi-protocol list
// ---------------------------------------------------------------------------

bool WindowList::windowListContains(const QVariantList &list, const QVariantMap &window)
{
    const QString title = window.value(QStringLiteral("title")).toString();
    if (title.isEmpty())
        return false; // untitled windows are never treated as duplicates
    const QString appId = window.value(QStringLiteral("appId")).toString();
    for (const QVariant &entry : list) {
        const QVariantMap other = entry.toMap();
        if (other.value(QStringLiteral("title")).toString() != title)
            continue;
        const QString otherApp = other.value(QStringLiteral("appId")).toString();
        if (appId.isEmpty() || otherApp.isEmpty())
            return true;
        if (appId.compare(otherApp, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QVariantList WindowList::mergedWindowList()
{
    QVariantList out;
    if (m_wlDisplay) {
        // 1. wlr toplevels: full control, and compositors that implement the
        //    protocol report their XWayland windows through it as well.
        out.reserve(m_waylandPending.size() + m_extPending.size() + 16);
        for (const QVariantMap &entry : m_waylandPending)
            out.append(entry);
        // 2. X11 windows that no wlr toplevel already represents. They win
        //    over the read-only ext reports of the same window, because
        //    activate/minimize/close actually work for them.
        if (m_xcb) {
            const QVariantList x11 = readX11Windows();
            for (const QVariant &entry : x11) {
                const QVariantMap w = entry.toMap();
                if (!windowListContains(out, w))
                    out.append(w);
            }
        }
        // 3. Read-only ext toplevels (GNOME's native Wayland windows) that
        //    nothing else shows.
        for (const QVariantMap &entry : m_extPending) {
            if (!windowListContains(out, entry))
                out.append(entry);
        }
        return out;
    }
    if (m_xcb)
        return readX11Windows();
    return {};
}
