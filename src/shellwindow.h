#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QWindow>
#include <QtQml/qqml.h>

#include <array>

class QScreen;

// Attached property that tells an X11 window manager what a shell window is:
//
//     import NewTeller 1.0
//     Window { ShellWindow.role: ShellWindow.Dock }
//
// On X11 (native, or XWayland on compositors without wlr-layer-shell) it sets
// _NET_WM_WINDOW_TYPE before the window is mapped, keeps the window on every
// workspace and, for docks, reserves the screen edge the window sits on
// (_NET_WM_STRUT_PARTIAL) so that maximised windows leave room for it.
// On Wayland it does nothing; the org.kde.layershell attached properties
// place the windows there.
class ShellWindow : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Role role READ role WRITE setRole NOTIFY roleChanged)

public:
    enum Role {
        Normal,  // Ordinary window (default).
        Desktop, // Below all other windows; used for the desktop background.
        Dock,    // Above normal windows; reserves its screen edge (panels).
    };
    Q_ENUM(Role)

    explicit ShellWindow(QObject *attachee);

    Role role() const { return m_role; }
    void setRole(Role role);

    static ShellWindow *qmlAttachedProperties(QObject *object);

signals:
    void roleChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyWindowType();
    void applyNativeHints(bool writeType);
    void trackScreen(QScreen *screen);
    void updateStrut();

    QPointer<QWindow> m_window;
    Role m_role = Normal;
    bool m_x11 = false;
    QMetaObject::Connection m_screenConnection;
    // Last strut written (left, right, top, bottom, then start/end pairs),
    // and the native window it was written to; 0 when none was written.
    std::array<quint32, 12> m_strut {};
    WId m_strutWindow = 0;
};

QML_DECLARE_TYPEINFO(ShellWindow, QML_HAS_ATTACHED_PROPERTIES)
