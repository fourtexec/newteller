#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>

class Settings;

// NewTeller's own administrator prompt (the "UAC" of the session), exposed to
// QML as `Polkit`.
//
// It registers as the polkit authentication agent of the login session, so
// every request for administrator rights - pkexec ("Open with root"),
// installing software, changing system settings, mounting disks - shows the
// shell's dialog (AuthDialog.qml) instead of a foreign one. The password goes
// to polkit's setuid helper through libpolkit-agent-1; the shell never sees
// whether it was right except through polkit's answer.
//
// Built only when libpolkit-agent-1 is available (NEWTELLER_HAVE_POLKIT); the
// session falls back to an external agent (lxpolkit, polkit-gnome...) when the
// built-in one is disabled (`security.builtinAgent`), not compiled in, or
// cannot register (no logind session, another agent already registered).
class PolkitAgent : public QObject
{
    Q_OBJECT
    // Compiled with polkit support.
    Q_PROPERTY(bool available READ available CONSTANT)
    // Registered with polkit for this session.
    Q_PROPERTY(bool registered READ registered NOTIFY registeredChanged)
    // A request is waiting for the user.
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString actionId READ actionId NOTIFY changed)
    Q_PROPERTY(QString iconName READ iconName NOTIFY changed)
    // The program asking (pkexec: the command line), if polkit tells.
    Q_PROPERTY(QString program READ program NOTIFY changed)
    Q_PROPERTY(QVariantMap details READ details NOTIFY changed)
    // Accounts that may authorize the request (user names).
    Q_PROPERTY(QStringList identities READ identities NOTIFY changed)
    Q_PROPERTY(int identityIndex READ identityIndex NOTIFY changed)
    // What PAM asks for ("Password: "), and whether the answer may be shown.
    Q_PROPERTY(QString prompt READ prompt NOTIFY changed)
    Q_PROPERTY(bool echo READ echo NOTIFY changed)
    // Waiting for PAM after an answer.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString info READ info NOTIFY changed)

public:
    explicit PolkitAgent(Settings *settings, QObject *parent = nullptr);
    ~PolkitAgent() override;

    bool available() const;
    bool registered() const { return m_registered; }
    bool active() const { return m_active; }
    QString message() const { return m_message; }
    QString actionId() const { return m_actionId; }
    QString iconName() const { return m_iconName; }
    QString program() const { return m_program; }
    QVariantMap details() const { return m_details; }
    QStringList identities() const { return m_identities; }
    int identityIndex() const { return m_identityIndex; }
    QString prompt() const { return m_prompt; }
    bool echo() const { return m_echo; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QString info() const { return m_info; }

    // The answer to the current prompt (the password).
    Q_INVOKABLE void respond(const QString &response);
    // The user dismissed the dialog.
    Q_INVOKABLE void cancel();
    // Authenticate as another of the allowed accounts.
    Q_INVOKABLE void selectIdentity(int index);

    // Used by the GLib glue in polkitagent.cpp.
    struct Request;
    void begin(Request *request);
    void sessionRequest(const QString &prompt, bool echo);
    void sessionMessage(const QString &text, bool isError);
    void sessionCompleted(bool gained);
    void requestCancelled();

signals:
    void changed();
    void registeredChanged();
    // The outcome of a request, for the shell (e.g. a notification).
    void finished(bool authorized);

private:
    void registerAgent();
    void startSession();
    void finish(bool authorized, bool userCancelled);
    void reset();
    // Answers a request that never got the dialog, and frees it.
    void drop(Request *request, bool cancelled, const char *reason);
    // Shows the next waiting request, if any.
    void startNext();

    Settings *m_settings;
    bool m_registered = false;
    bool m_active = false;
    QString m_message;
    QString m_actionId;
    QString m_iconName;
    QString m_program;
    QVariantMap m_details;
    QStringList m_identities;
    int m_identityIndex = 0;
    QString m_prompt;
    bool m_echo = false;
    bool m_busy = false;
    QString m_error;
    QString m_info;
    int m_attempts = 0;

    // GLib objects (listener, registration handle, the running request),
    // kept out of the header so Qt and GLib headers never meet here.
    void *m_listener = nullptr;
    void *m_handle = nullptr;
    Request *m_request = nullptr;
    // Requests that arrived while the dialog was open, oldest first.
    QList<Request *> m_queue;
};
