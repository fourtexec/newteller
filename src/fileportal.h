#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QTimer>
#include <QDBusObjectPath>
#include <QDBusPendingReply>

// Chooses files through the freedesktop FileChooser portal
// (org.freedesktop.portal.FileChooser on org.freedesktop.portal.Desktop).
//
// The portal opens the file manager / file chooser that is already installed
// in the system (the GTK or Qt dialog of the running desktop), so the shell
// does not have to ship its own. Every desktop with xdg-desktop-portal has
// it; when the service is missing the QML falls back to the built-in picker.
//
// The portal protocol is asynchronous: OpenFile() returns the object path of a
// request object, and the portal emits Response(uint, a{sv}) on that path once
// the user is done (or the request is aborted). The path is derived from the
// caller's unique bus name plus a handle token, so it can be predicted and
// subscribed to *before* the call is made — which is required, because the
// portal is allowed to answer immediately. If the portal answers with a
// different path (it is allowed to sanitise the token) the subscription is
// moved to the path from the reply.
//
// At most one request is in flight. A second pickFile() closes the previous
// request instead of piling up dialogs, and every request ends with exactly
// one signal (filePicked or pickFailed), so the QML never has to guess.
class FilePortal : public QObject {
    Q_OBJECT
    // Whether the portal can be used: the service is on the bus or at least
    // installed and therefore startable on demand.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
    explicit FilePortal(QObject *parent = nullptr);
    ~FilePortal() override;

    bool available() const;

    // Opens the system file chooser. `imagesOnly` adds an image filter for
    // convenience (the user can still switch to "All files" inside the
    // dialog). An empty `startFolder` lets the portal decide.
    Q_INVOKABLE void pickFile(const QString &title, const QString &startFolder,
                              bool imagesOnly);

signals:
    // Selected local path; empty when the dialog was dismissed or the portal
    // returned something that is not a readable file.
    void filePicked(const QString &path);
    // The portal could not be used at all (call failed, no answer, no bus);
    // the QML opens its integrated picker instead.
    void pickFailed();
    void availableChanged();

private Q_SLOTS:
    void portalResponse(uint response, const QVariantMap &results);
    void responseTimedOut();

private:
    // Handles the reply to OpenFile(): an error means "no portal", success
    // moves the Response subscription to the request object it reports.
    void callFinished(const QDBusPendingReply<QDBusObjectPath> &reply);

    // Subscribes the portalResponse() slot to the Response signal of `path`.
    bool subscribe(const QString &path);
    // Drops the subscription and the in-flight bookkeeping.
    void resetRequest();
    // Ends the current request: emits one of the two signals exactly once.
    void finish(bool failed, const QString &path = QString());
    QString expandFolder(const QString &path) const;
    static bool serviceFileInstalled();

    bool m_waiting = false;
    // Counter for unique handle tokens (two requests must never share one).
    uint m_requestSequence = 0;
    // Path the Response signal is expected on (may be empty before the call).
    QString m_requestPath;
    // A request that never answers must not wedge the shell forever.
    QTimer m_timeout;
};
