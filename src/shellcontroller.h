#pragma once

#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPair>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

class NotificationDaemon;
class Settings;

// Session actions, icon resolution and small system facts. Exposed to QML as
// the context property `Shell`.
class ShellController : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.newteller.Shell")
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString home READ home CONSTANT)
    Q_PROPERTY(QString desktopUrl READ desktopUrl CONSTANT)
    // XDG user folders: localized names (~/Изображения) come from user-dirs.dirs.
    Q_PROPERTY(QString picturesPath READ picturesPath CONSTANT)
    Q_PROPERTY(QString documentsPath READ documentsPath CONSTANT)
    Q_PROPERTY(QString downloadsPath READ downloadsPath CONSTANT)
    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString sessionType READ sessionType CONSTANT)
    Q_PROPERTY(QString hostname READ hostname CONSTANT)
    Q_PROPERTY(QString distro READ distro CONSTANT)
    Q_PROPERTY(QString kernelVersion READ kernelVersion CONSTANT)
    Q_PROPERTY(bool isWayland READ isWayland CONSTANT)
    // One shared clock for the panel and every clock widget.
    Q_PROPERTY(QDateTime now READ now NOTIFY nowChanged)

public:
    explicit ShellController(Settings *settings, NotificationDaemon *notifications = nullptr,
                             QObject *parent = nullptr);

    QString version() const { return QStringLiteral(NEWTELLER_VERSION); }
    QString home() const;
    QString desktopUrl() const;
    QString picturesPath() const;
    QString documentsPath() const;
    QString downloadsPath() const;
    QString userName() const;
    QString sessionType() const;
    QString hostname() const;
    QString distro() const;
    QString kernelVersion() const;
    bool isWayland() const;
    QDateTime now() const { return m_now; }

    // Resolution order: absolute path -> icons compiled into the binary ->
    // IconKit set on disk (settings: icons.path) -> the kit bundled with the
    // installation -> system icon theme (image://theme/).
    Q_INVOKABLE QUrl iconUrl(const QString &name) const;
    // Like iconUrl(), but IconKit glyph icons are recoloured to `color` (the
    // lucide set draws with stroke="currentColor", which renders black - the
    // recolour makes them follow the theme foreground, i.e. white on dark
    // themes). System theme application icons are returned unchanged.
    Q_INVOKABLE QUrl tintedIcon(const QString &name, const QColor &color) const;

    Q_INVOKABLE void openUrl(const QString &url) const;
    // Opens the built-in settings application. The QML lives in the executable's
    // resources, so it is always available and cannot be deleted like a plugin.
    Q_INVOKABLE void openSettings();
    // Session-level entry points used by newteller-compositor keybindings and
    // by `gdbus call` from scripts. The QML root listens to the signals.
    Q_INVOKABLE void showStartMenu();
    // Opens the start menu, or closes it when it is open (a tap of Super).
    Q_INVOKABLE void toggleStartMenu();
    Q_INVOKABLE void showRunDialog();
    // Super+N and Super+A from the compositor.
    Q_INVOKABLE void toggleNotifications();
    Q_INVOKABLE void toggleQuickSettings();
    // Super+V: the clipboard history.
    Q_INVOKABLE void toggleClipboard();
    // Super+Tab: the window overview.
    Q_INVOKABLE void toggleTaskView();
    // Ctrl+Shift+Esc / Ctrl+Alt+Esc: the force-quit dialog.
    Q_INVOKABLE void showForceQuit();
    Q_INVOKABLE void openConfig() const;
    // Opens the path in the file manager (or its handler for directories).
    Q_INVOKABLE void openPath(const QString &path) const;
    Q_INVOKABLE void runShell(const QString &command) const;
    // Starts `command` in the configured terminal emulator.
    Q_INVOKABLE void terminal(const QString &command) const;
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    // Desktop notification through org.freedesktop.Notifications (notify-send).
    Q_INVOKABLE void notify(const QString &title, const QString &body) const;
    // Gets through do-not-disturb and stays until closed (critical battery).
    Q_INVOKABLE void notifyUrgent(const QString &title, const QString &body,
                                  const QString &icon = QString()) const;
    // Text currently in the clipboard, for the desktop paste action.
    Q_INVOKABLE QString clipboardText() const;
    // Writes a text file (used by the desktop "Paste" action).
    Q_INVOKABLE bool saveTextFile(const QString &path, const QString &text) const;
    // Moves a file or folder (path or file:// URL) to the trash, where it can
    // be restored; says so on screen when that fails.
    Q_INVOKABLE bool moveToTrash(const QString &path) const;
    // Creates "name" in the folder (path or file:// URL), or "name 2", "name 3"
    // when that exists; returns the new folder's path, empty on failure.
    Q_INVOKABLE QString createFolder(const QString &parent, const QString &name) const;
    // Renames a file or folder (path or file:// URL) within its folder; says
    // why on screen when it cannot (name taken, invalid, no permission).
    Q_INVOKABLE bool renameFile(const QString &path, const QString &newName) const;
    // Machine and configuration diagnostics, same shape as `newteller-ctl doctor`.
    Q_INVOKABLE QVariantMap diagnostics() const;

    // --- file helpers used by the wallpaper pickers ------------------------
    // Expands "~", strips file:// URLs and percent escapes, and returns an
    // absolute path. Paths that are not existing directories (a deleted
    // folder, a file, a non-local URL) fall back to home, so the caller always
    // gets a folder it can open.
    Q_INVOKABLE QString folderPath(const QString &path) const;
    // Whether `path` is a readable image in a format Qt can load. The desktop
    // only ever receives wallpapers that pass this check.
    Q_INVOKABLE bool isImageFile(const QString &path) const;
    Q_INVOKABLE bool fileExists(const QString &path) const;

    Q_INVOKABLE void lock() const;
    Q_INVOKABLE void logout() const;
    Q_INVOKABLE void suspend() const;
    Q_INVOKABLE void reboot() const;
    Q_INVOKABLE void powerOff() const;

signals:
    void nowChanged();
    // Emitted by openSettings(); Main.qml shows the embedded settings window.
    void settingsRequested();
    void startMenuRequested();
    void startMenuToggleRequested();
    void runDialogRequested();
    void notificationsToggleRequested();
    void quickSettingsToggleRequested();
    void clipboardToggleRequested();
    void taskViewToggleRequested();
    void forceQuitRequested();

private:
    void openOrSay(const QUrl &url) const;
    QString expandHome(const QString &path) const;
    static QUrl themeIconUrl(const QString &name);

    Settings *m_settings;
    NotificationDaemon *m_notifications;
    QTimer m_clock;
    QDateTime m_now;
    // icon name -> resolved file URL ("" = fall back to the theme), so the
    // bindings in TaskButton do not stat the same files every frame.
    mutable QHash<QString, QString> m_iconCache;
    // "name@color" -> recoloured SVG file URL (Shell.tintedIcon).
    mutable QHash<QString, QString> m_tintCache;
    // "Moved to the trash" notifications with Undo: id -> {trashed, original}.
    mutable QHash<uint, QPair<QString, QString>> m_trashUndo;
};
