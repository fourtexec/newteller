#include "shellwindow.h"

#include "windowsystem.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QRect>
#include <QScreen>
#include <QVariant>

#if QT_CONFIG(xcb)
#include <xcb/xcb.h>
#endif

#include <cstdlib>
#include <cstring>

namespace {

// Dynamic property read by Qt's xcb plugin whenever it creates the native
// window or the window flags change. Its value is a QXcbWindow::WindowType
// bit; Qt then puts the matching _NET_WM_WINDOW_TYPE atom first in the list.
constexpr char kWindowTypeProperty[] = "_q_xcb_wm_window_type";
constexpr int kXcbTypeDesktop = 0x000002;
constexpr int kXcbTypeDock = 0x000004;

#if QT_CONFIG(xcb)
xcb_connection_t *x11Connection()
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    return x11 ? x11->connection() : nullptr;
}

xcb_atom_t internAtom(xcb_connection_t *connection, const char *name)
{
    const xcb_intern_atom_cookie_t cookie =
        xcb_intern_atom(connection, 0, uint16_t(std::strlen(name)), name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, nullptr);
    if (!reply)
        return XCB_ATOM_NONE;
    const xcb_atom_t atom = reply->atom;
    std::free(reply);
    return atom;
}

void setProperty32(xcb_connection_t *connection, xcb_window_t window, const char *name,
                   xcb_atom_t type, const void *data, uint32_t count)
{
    const xcb_atom_t property = internAtom(connection, name);
    if (property != XCB_ATOM_NONE)
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, property, type, 32, count, data);
}

// Native (X root window) geometry of a screen. Qt keeps a screen's top-left
// corner in device pixels and scales only its size.
QRect nativeGeometry(const QScreen *screen)
{
    const QRect g = screen->geometry();
    return QRect(g.topLeft(), g.size() * screen->devicePixelRatio());
}
#endif

} // namespace

ShellWindow::ShellWindow(QObject *attachee)
    : QObject(attachee)
    , m_window(qobject_cast<QWindow *>(attachee))
    , m_x11(WindowSystem::isX11())
{
    if (!m_window) {
        qWarning("ShellWindow: attached properties only apply to Window objects");
        return;
    }
    if (!m_x11)
        return;

    m_window->installEventFilter(this);
    connect(m_window, &QWindow::screenChanged, this, &ShellWindow::trackScreen);
    trackScreen(m_window->screen());
}

ShellWindow *ShellWindow::qmlAttachedProperties(QObject *object)
{
    return new ShellWindow(object);
}

void ShellWindow::setRole(Role role)
{
    if (m_role == role)
        return;
    m_role = role;
    applyWindowType();
    updateStrut();
    emit roleChanged();
}

void ShellWindow::applyWindowType()
{
    if (!m_window || !m_x11)
        return;

    int type = 0;
    switch (m_role) {
    case Normal:
        break;
    case Desktop:
        type = kXcbTypeDesktop;
        break;
    case Dock:
        type = kXcbTypeDock;
        break;
    }
    // Kept as a property so Qt applies it again if it recreates the window.
    m_window->setProperty(kWindowTypeProperty, type ? QVariant(type) : QVariant());

    // Normally the native window does not exist yet and Qt picks the type up
    // when it creates it. layer-shell-qt 6.3 creates the native window as soon
    // as its attached object appears, though, and Qt only re-reads the property
    // when the window flags change, so set the type directly in that case. The
    // window is not mapped yet, so the window manager still sees it in time.
    if (m_window->handle())
        applyNativeHints(true);
}

// Writes the X11 properties that have to be in place before the window is
// mapped: on native window creation (Qt has just written the window type
// itself), or when the role changes after that (writeType).
void ShellWindow::applyNativeHints(bool writeType)
{
#if QT_CONFIG(xcb)
    if (!m_window || !m_window->handle() || m_role == Normal)
        return;
    xcb_connection_t *connection = x11Connection();
    if (!connection)
        return;
    const xcb_window_t window = xcb_window_t(m_window->winId());

    if (writeType) {
        const xcb_atom_t types[] = {
            internAtom(connection, m_role == Desktop ? "_NET_WM_WINDOW_TYPE_DESKTOP" : "_NET_WM_WINDOW_TYPE_DOCK"),
            internAtom(connection, "_NET_WM_WINDOW_TYPE_NORMAL"),
        };
        setProperty32(connection, window, "_NET_WM_WINDOW_TYPE", XCB_ATOM_ATOM, types, 2);
    }

    // Show the desktop and the panels on every workspace.
    const uint32_t allDesktops = 0xFFFFFFFF;
    setProperty32(connection, window, "_NET_WM_DESKTOP", XCB_ATOM_CARDINAL, &allDesktops, 1);

    xcb_flush(connection);
#endif
}

void ShellWindow::trackScreen(QScreen *screen)
{
    // Struts are measured from the edges of the whole X screen, which changes
    // when any monitor is added, removed or resized.
    disconnect(m_screenConnection);
    if (screen)
        m_screenConnection = connect(screen, &QScreen::virtualGeometryChanged, this, &ShellWindow::updateStrut);
    updateStrut();
}

// Reserves the screen edge a dock sits on, so that maximised windows and
// window placement leave room for it.
void ShellWindow::updateStrut()
{
#if QT_CONFIG(xcb)
    if (!m_window || !m_x11 || !m_window->handle())
        return;
    QScreen *screen = m_window->screen();
    xcb_connection_t *connection = x11Connection();
    if (!screen || !connection)
        return;
    const WId window = m_window->winId();

    if (m_role != Dock) {
        if (m_strutWindow == window) {
            for (const char *name : { "_NET_WM_STRUT_PARTIAL", "_NET_WM_STRUT" }) {
                const xcb_atom_t property = internAtom(connection, name);
                if (property != XCB_ATOM_NONE)
                    xcb_delete_property(connection, xcb_window_t(window), property);
            }
            xcb_flush(connection);
        }
        m_strutWindow = 0;
        return;
    }

    // Everything below is in X root window (device) pixels.
    const QRect screenRect = nativeGeometry(screen);
    QRect root;
    for (const QScreen *sibling : screen->virtualSiblings())
        root |= nativeGeometry(sibling);
    const QRect logical = m_window->geometry();
    const QPoint origin = screen->geometry().topLeft();
    const qreal ratio = screen->devicePixelRatio();
    const QRect win(origin + (logical.topLeft() - origin) * ratio, logical.size() * ratio);
    if (win.isEmpty())
        return;

    const auto size = [](int value) { return quint32(qMax(0, value)); };
    std::array<quint32, 12> strut {};
    // A bar wider than it is tall reserves the top or bottom edge, otherwise
    // the left or right edge; whichever half of the screen it is in.
    if (win.width() >= win.height()) {
        const bool top = win.center().y() < screenRect.center().y();
        const int edge = top ? 2 : 3;
        strut[edge] = top ? size(win.bottom() + 1 - root.top()) : size(root.bottom() + 1 - win.top());
        strut[4 + 2 * edge] = size(win.left());
        strut[5 + 2 * edge] = size(win.right());
    } else {
        const bool left = win.center().x() < screenRect.center().x();
        const int edge = left ? 0 : 1;
        strut[edge] = left ? size(win.right() + 1 - root.left()) : size(root.right() + 1 - win.left());
        strut[4 + 2 * edge] = size(win.top());
        strut[5 + 2 * edge] = size(win.bottom());
    }

    if (window == m_strutWindow && strut == m_strut)
        return;
    setProperty32(connection, xcb_window_t(window), "_NET_WM_STRUT_PARTIAL", XCB_ATOM_CARDINAL, strut.data(), 12);
    // Older window managers only know the four-value form.
    setProperty32(connection, xcb_window_t(window), "_NET_WM_STRUT", XCB_ATOM_CARDINAL, strut.data(), 4);
    xcb_flush(connection);
    m_strut = strut;
    m_strutWindow = window;
#endif
}

bool ShellWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window) {
        switch (event->type()) {
        case QEvent::PlatformSurface:
            if (static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType()
                == QPlatformSurfaceEvent::SurfaceCreated) {
                applyNativeHints(false);
            }
            break;
        case QEvent::Expose:
        case QEvent::Move:
        case QEvent::Resize:
            updateStrut();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}
