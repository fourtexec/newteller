#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class Settings;

// Session actions and icon resolution. Exposed to QML as the context property `Shell`.
class ShellController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString home READ home CONSTANT)
    Q_PROPERTY(QString desktopUrl READ desktopUrl CONSTANT)
    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString sessionType READ sessionType CONSTANT)

public:
    explicit ShellController(Settings *settings, QObject *parent = nullptr);

    QString home() const;
    QString desktopUrl() const;
    QString userName() const;
    QString sessionType() const;

    // Resolution order: absolute path -> IconKit set on disk -> system icon theme (image://theme/).
    Q_INVOKABLE QUrl iconUrl(const QString &name) const;

    Q_INVOKABLE void openUrl(const QString &url) const;
    Q_INVOKABLE void openConfig() const;
    Q_INVOKABLE void runShell(const QString &command) const;

    Q_INVOKABLE void lock() const;
    Q_INVOKABLE void logout() const;
    Q_INVOKABLE void suspend() const;
    Q_INVOKABLE void reboot() const;
    Q_INVOKABLE void powerOff() const;

private:
    QString expandHome(const QString &path) const;

    Settings *m_settings;
};
