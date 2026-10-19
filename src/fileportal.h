#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class QDBusPendingCallWatcher;

// Chooses files through the freedesktop FileChooser portal
// (org.freedesktop.portal.FileChooser on org.freedesktop.portal.Desktop).
//
// The portal opens the file manager / file chooser that is already installed
// in the system (the GTK or Qt dialog of the running desktop), so the shell
// does not have to ship its own. Every desktop with xdg-desktop-portal has
// it; when the service is missing the QML falls back to the built-in picker.
class FilePortal : public QObject {
    Q_OBJECT
    // Whether the portal service is registered on the session bus.
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit FilePortal(QObject *parent = nullptr);

    bool available() const;

    // Opens the system file chooser. One request at a time; a second call
    // while one is open cancels the previous request silently.
    // `imagesOnly` adds an image filter for convenience (the user can still
    // switch to "All files" inside the dialog).
    Q_INVOKABLE void pickFile(const QString &title, const QString &startFolder,
                              bool imagesOnly);

signals:
    // Selected local path; empty when the dialog was dismissed.
    void filePicked(const QString &path);
    // The portal could not be used at all (call failed); the QML opens its
    // integrated picker instead.
    void pickFailed();

private Q_SLOTS:
    void portalResponse(uint response, const QVariantMap &results);
    void callFinished(QDBusPendingCallWatcher *watcher);

private:
    QString expandFolder(const QString &path) const;

    bool m_waiting = false;
    QString m_requestPath;
};
