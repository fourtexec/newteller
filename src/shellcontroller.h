#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

class Settings;

// Session actions, icon resolution and small system facts. Exposed to QML as
// the context property `Shell`.
class ShellController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString home READ home CONSTANT)
    Q_PROPERTY(QString desktopUrl READ desktopUrl CONSTANT)
    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString sessionType READ sessionType CONSTANT)
    Q_PROPERTY(QString hostname READ hostname CONSTANT)
    Q_PROPERTY(QString distro READ distro CONSTANT)
    Q_PROPERTY(QString kernelVersion READ kernelVersion CONSTANT)
    Q_PROPERTY(bool isWayland READ isWayland CONSTANT)
    // One shared clock for the panel and every clock widget.
    Q_PROPERTY(QDateTime now READ now NOTIFY nowChanged)

public:
    explicit ShellController(Settings *settings, QObject *parent = nullptr);

    QString version() const { return QStringLiteral(NEWTELLER_VERSION); }
    QString home() const;
    QString desktopUrl() const;
    QString userName() const;
    QString sessionType() const;
    QString hostname() const;
    QString distro() const;
    QString kernelVersion() const;
    bool isWayland() const;
    QDateTime now() const { return m_now; }

    // Resolution order: absolute path -> IconKit set on disk -> system icon theme (image://theme/).
    Q_INVOKABLE QUrl iconUrl(const QString &name) const;

    Q_INVOKABLE void openUrl(const QString &url) const;
    // Opens the built-in settings application. The QML lives in the executable's
    // resources, so it is always available and cannot be deleted like a plugin.
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void openConfig() const;
    // Opens the path in the file manager (or its handler for directories).
    Q_INVOKABLE void openPath(const QString &path) const;
    Q_INVOKABLE void runShell(const QString &command) const;
    // Starts `command` in the configured terminal emulator.
    Q_INVOKABLE void terminal(const QString &command) const;
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    // Desktop notification through org.freedesktop.Notifications (notify-send).
    Q_INVOKABLE void notify(const QString &title, const QString &body) const;
    // Text currently in the clipboard, for the desktop paste action.
    Q_INVOKABLE QString clipboardText() const;
    // Writes a text file (used by the desktop "Paste" action).
    Q_INVOKABLE bool saveTextFile(const QString &path, const QString &text) const;
    // Machine and configuration diagnostics, same shape as `newteller-ctl doctor`.
    Q_INVOKABLE QVariantMap diagnostics() const;

    Q_INVOKABLE void lock() const;
    Q_INVOKABLE void logout() const;
    Q_INVOKABLE void suspend() const;
    Q_INVOKABLE void reboot() const;
    Q_INVOKABLE void powerOff() const;

signals:
    void nowChanged();
    // Emitted by openSettings(); Main.qml shows the embedded settings window.
    void settingsRequested();

private:
    QString expandHome(const QString &path) const;

    Settings *m_settings;
    QTimer m_clock;
    QDateTime m_now;
    // icon name -> resolved file URL ("" = fall back to the theme), so the
    // bindings in TaskButton do not stat the same files every frame.
    mutable QHash<QString, QString> m_iconCache;
};
