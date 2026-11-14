// GLib and polkit come first: gio declares struct members called `signals`,
// which Qt's keyword macro would rewrite if Qt's headers were included before.
#ifdef NEWTELLER_HAVE_POLKIT
#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE 1
#include <polkitagent/polkitagent.h>
#include <pwd.h>
#include <unistd.h>
#include <vector>
#endif

#include "polkitagent.h"

#include "settings.h"

#include <QCoreApplication>
#include <QTimer>
#include <QtGlobal>

#ifdef NEWTELLER_HAVE_POLKIT

// ---------------------------------------------------------------------------
// GObject glue: a PolkitAgentListener subclass that forwards requests to the
// PolkitAgent QObject. Qt runs on the GLib main loop on Linux, so polkit's
// D-Bus calls and the helper's I/O are dispatched on the GUI thread.
// ---------------------------------------------------------------------------

struct PolkitAgent::Request
{
    GTask *task = nullptr;
    GCancellable *cancellable = nullptr;
    gulong cancelHandler = 0;
    QByteArray cookie;
    QVariantMap details;
    std::vector<PolkitIdentity *> identities;
    PolkitAgentSession *session = nullptr;
};

namespace {

struct NtPolkitListener
{
    PolkitAgentListener parent_instance;
    PolkitAgent *agent;
};

struct NtPolkitListenerClass
{
    PolkitAgentListenerClass parent_class;
};

void ntInitiate(PolkitAgentListener *listener, const gchar *actionId, const gchar *message,
                const gchar *iconName, PolkitDetails *details, const gchar *cookie,
                GList *identities, GCancellable *cancellable, GAsyncReadyCallback callback,
                gpointer userData);
gboolean ntInitiateFinish(PolkitAgentListener *listener, GAsyncResult *result, GError **error);

} // namespace

G_DEFINE_TYPE(NtPolkitListener, nt_polkit_listener, POLKIT_AGENT_TYPE_LISTENER)

static void nt_polkit_listener_init(NtPolkitListener *self)
{
    self->agent = nullptr;
}

static void nt_polkit_listener_class_init(NtPolkitListenerClass *klass)
{
    PolkitAgentListenerClass *listenerClass = POLKIT_AGENT_LISTENER_CLASS(klass);
    listenerClass->initiate_authentication = ntInitiate;
    listenerClass->initiate_authentication_finish = ntInitiateFinish;
}

namespace {

QString userName(PolkitIdentity *identity)
{
    const uid_t uid = static_cast<uid_t>(polkit_unix_user_get_uid(POLKIT_UNIX_USER(identity)));
    if (const passwd *entry = getpwuid(uid))
        return QString::fromLocal8Bit(entry->pw_name);
    return QStringLiteral("uid %1").arg(uid);
}

void onRequest(PolkitAgentSession *, gchar *request, gboolean echoOn, gpointer data)
{
    static_cast<PolkitAgent *>(data)->sessionRequest(QString::fromUtf8(request), echoOn);
}

void onShowError(PolkitAgentSession *, gchar *text, gpointer data)
{
    static_cast<PolkitAgent *>(data)->sessionMessage(QString::fromUtf8(text), true);
}

void onShowInfo(PolkitAgentSession *, gchar *text, gpointer data)
{
    static_cast<PolkitAgent *>(data)->sessionMessage(QString::fromUtf8(text), false);
}

void onCompleted(PolkitAgentSession *, gboolean gained, gpointer data)
{
    static_cast<PolkitAgent *>(data)->sessionCompleted(gained);
}

void onCancelled(GCancellable *, gpointer data)
{
    static_cast<PolkitAgent *>(data)->requestCancelled();
}

void ntInitiate(PolkitAgentListener *listener, const gchar *actionId, const gchar *message,
                const gchar *iconName, PolkitDetails *details, const gchar *cookie,
                GList *identities, GCancellable *cancellable, GAsyncReadyCallback callback,
                gpointer userData)
{
    auto *self = reinterpret_cast<NtPolkitListener *>(listener);
    GTask *task = g_task_new(listener, cancellable, callback, userData);

    std::vector<PolkitIdentity *> users;
    for (GList *item = identities; item; item = item->next) {
        auto *identity = static_cast<PolkitIdentity *>(item->data);
        // Groups cannot type a password; polkit lists their members as users.
        if (POLKIT_IS_UNIX_USER(identity))
            users.push_back(static_cast<PolkitIdentity *>(g_object_ref(identity)));
    }
    if (!self->agent || users.empty()) {
        for (PolkitIdentity *identity : users)
            g_object_unref(identity);
        g_task_return_new_error(task, POLKIT_ERROR, POLKIT_ERROR_FAILED,
                                "No account can authorize this request");
        g_object_unref(task);
        return;
    }

    auto *request = new PolkitAgent::Request;
    request->task = task;
    request->cookie = QByteArray(cookie);
    request->identities = std::move(users);
    if (cancellable)
        request->cancellable = static_cast<GCancellable *>(g_object_ref(cancellable));

    QVariantMap detailMap;
    if (details) {
        gchar **keys = polkit_details_get_keys(details);
        for (gchar **key = keys; key && *key; ++key) {
            const gchar *value = polkit_details_lookup(details, *key);
            detailMap.insert(QString::fromUtf8(*key), QString::fromUtf8(value ? value : ""));
        }
        g_strfreev(keys);
    }
    detailMap.insert(QStringLiteral("action"), QString::fromUtf8(actionId));
    detailMap.insert(QStringLiteral("message"), QString::fromUtf8(message));
    detailMap.insert(QStringLiteral("icon"), QString::fromUtf8(iconName ? iconName : ""));

    request->details = detailMap;
    self->agent->begin(request);
}

gboolean ntInitiateFinish(PolkitAgentListener *, GAsyncResult *result, GError **error)
{
    return g_task_propagate_boolean(G_TASK(result), error);
}

} // namespace

#endif // NEWTELLER_HAVE_POLKIT

// ---------------------------------------------------------------------------
// PolkitAgent
// ---------------------------------------------------------------------------

PolkitAgent::PolkitAgent(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    const bool inSession = qEnvironmentVariable("NEWTELLER_SESSION") == QLatin1String("1");
    if (inSession && m_settings->value(QStringLiteral("security.builtinAgent"), true).toBool())
        registerAgent();
    // SessionServices starts an external agent only when this one is absent.
    QCoreApplication::instance()->setProperty("newteller.builtinPolkitAgent", m_registered);
}

PolkitAgent::~PolkitAgent()
{
#ifdef NEWTELLER_HAVE_POLKIT
    while (!m_queue.isEmpty())
        drop(m_queue.takeFirst(), true, "The authentication agent is going away");
    if (m_request)
        finish(false, true);
    if (m_handle)
        polkit_agent_listener_unregister(m_handle);
    if (m_listener)
        g_object_unref(m_listener);
#endif
}

bool PolkitAgent::available() const
{
#ifdef NEWTELLER_HAVE_POLKIT
    return true;
#else
    return false;
#endif
}

void PolkitAgent::registerAgent()
{
#ifdef NEWTELLER_HAVE_POLKIT
    GError *error = nullptr;
    PolkitSubject *subject = polkit_unix_session_new_for_process_sync(getpid(), nullptr, &error);
    if (!subject) {
        qWarning("NewTeller: no login session for the administrator prompt: %s",
                 error ? error->message : "unknown error");
        // Any other agent would fail the same way (and lxpolkit shows an
        // "Error" window for it), so SessionServices starts none.
        QCoreApplication::instance()->setProperty(
            "newteller.polkitAgentError",
            QStringLiteral("no login session: %1")
                .arg(QString::fromUtf8(error ? error->message : "unknown error")));
        g_clear_error(&error);
        return;
    }
    auto *listener = static_cast<NtPolkitListener *>(g_object_new(nt_polkit_listener_get_type(), nullptr));
    listener->agent = this;
    m_handle = polkit_agent_listener_register(POLKIT_AGENT_LISTENER(listener),
                                              POLKIT_AGENT_REGISTER_FLAGS_NONE, subject,
                                              "/org/newteller/PolicyKit1/AuthenticationAgent",
                                              nullptr, &error);
    g_object_unref(subject);
    if (!m_handle) {
        // Usually another agent already serves this session.
        qWarning("NewTeller: could not register the administrator prompt: %s",
                 error ? error->message : "unknown error");
        if (error && g_strrstr(error->message, "already exists"))
            QCoreApplication::instance()->setProperty("newteller.polkitAgentElsewhere", true);
        QCoreApplication::instance()->setProperty(
            "newteller.polkitAgentError",
            QString::fromUtf8(error ? error->message : "unknown error"));
        g_clear_error(&error);
        g_object_unref(listener);
        return;
    }
    m_listener = listener;
    m_registered = true;
    emit registeredChanged();
#endif
}

void PolkitAgent::begin(Request *request)
{
#ifdef NEWTELLER_HAVE_POLKIT
    if (m_request) {
        // One dialog at a time; the others wait their turn. Two programs
        // asking at once (an updater and a pkexec command) used to make
        // the second one fail straight away.
        if (m_queue.size() >= 8)
            drop(request, false, "Too many authentication requests are waiting");
        else
            m_queue.append(request);
        return;
    }
    // The caller gave up while it was waiting.
    if (request->cancellable && g_cancellable_is_cancelled(request->cancellable)) {
        drop(request, true, "The request was cancelled");
        return;
    }
    const QVariantMap details = request->details;
    m_request = request;
    m_actionId = details.value(QStringLiteral("action")).toString();
    m_message = details.value(QStringLiteral("message")).toString();
    m_iconName = details.value(QStringLiteral("icon")).toString();
    m_details = details;
    m_details.remove(QStringLiteral("action"));
    m_details.remove(QStringLiteral("message"));
    m_details.remove(QStringLiteral("icon"));
    // pkexec reports the command line; other callers sometimes the program.
    m_program = m_details.value(QStringLiteral("command_line"),
                                m_details.value(QStringLiteral("program"))).toString();

    m_identities.clear();
    m_identityIndex = 0;
    const uid_t me = getuid();
    for (size_t i = 0; i < request->identities.size(); ++i) {
        PolkitIdentity *identity = request->identities[i];
        m_identities << userName(identity);
        if (static_cast<uid_t>(polkit_unix_user_get_uid(POLKIT_UNIX_USER(identity))) == me)
            m_identityIndex = int(i);
    }
    m_error.clear();
    m_info.clear();
    m_prompt.clear();
    m_attempts = 0;
    m_active = true;

    if (request->cancellable)
        request->cancelHandler = g_cancellable_connect(request->cancellable, G_CALLBACK(onCancelled),
                                                       this, nullptr);
    startSession();
    emit changed();
#else
    Q_UNUSED(request);
#endif
}

void PolkitAgent::drop(Request *request, bool cancelled, const char *reason)
{
#ifdef NEWTELLER_HAVE_POLKIT
    g_task_return_new_error(request->task, POLKIT_ERROR,
                            cancelled ? POLKIT_ERROR_CANCELLED : POLKIT_ERROR_FAILED, "%s", reason);
    g_object_unref(request->task);
    for (PolkitIdentity *identity : request->identities)
        g_object_unref(identity);
    if (request->cancellable)
        g_object_unref(request->cancellable);
    delete request;
#else
    Q_UNUSED(request);
    Q_UNUSED(cancelled);
    Q_UNUSED(reason);
#endif
}

void PolkitAgent::startNext()
{
    while (!m_request && !m_queue.isEmpty())
        begin(m_queue.takeFirst());
}

void PolkitAgent::startSession()
{
#ifdef NEWTELLER_HAVE_POLKIT
    if (!m_request)
        return;
    if (m_request->session) {
        g_signal_handlers_disconnect_by_data(m_request->session, this);
        polkit_agent_session_cancel(m_request->session);
        g_object_unref(m_request->session);
        m_request->session = nullptr;
    }
    PolkitIdentity *identity = m_request->identities.at(size_t(qBound(0, m_identityIndex,
                                                                      int(m_request->identities.size()) - 1)));
    m_request->session = polkit_agent_session_new(identity, m_request->cookie.constData());
    g_signal_connect(m_request->session, "request", G_CALLBACK(onRequest), this);
    g_signal_connect(m_request->session, "show-error", G_CALLBACK(onShowError), this);
    g_signal_connect(m_request->session, "show-info", G_CALLBACK(onShowInfo), this);
    g_signal_connect(m_request->session, "completed", G_CALLBACK(onCompleted), this);
    m_busy = true;
    polkit_agent_session_initiate(m_request->session);
#endif
}

void PolkitAgent::sessionRequest(const QString &prompt, bool echo)
{
    m_prompt = prompt.trimmed();
    m_echo = echo;
    m_busy = false;
    emit changed();
}

void PolkitAgent::sessionMessage(const QString &text, bool isError)
{
    if (isError)
        m_error = text.trimmed();
    else
        m_info = text.trimmed();
    emit changed();
}

void PolkitAgent::sessionCompleted(bool gained)
{
    if (!m_request)
        return;
    if (gained) {
        // Leave the session's own signal handler before freeing it.
        QTimer::singleShot(0, this, [this] { finish(true, false); });
        return;
    }
    // Wrong password (or PAM refused): ask again, like every other prompt.
    ++m_attempts;
    m_error = tr("That password did not work. Try again.");
    m_busy = false;
    emit changed();
    QTimer::singleShot(0, this, [this] { startSession(); });
}

void PolkitAgent::requestCancelled()
{
    // The caller gave up (the program that asked has quit). Leave the
    // cancellable's callback before disconnecting from it.
    QTimer::singleShot(0, this, [this] {
        if (m_request)
            finish(false, false);
    });
}

void PolkitAgent::respond(const QString &response)
{
#ifdef NEWTELLER_HAVE_POLKIT
    if (!m_request || !m_request->session || m_busy)
        return;
    m_busy = true;
    m_error.clear();
    emit changed();
    polkit_agent_session_response(m_request->session, response.toUtf8().constData());
#else
    Q_UNUSED(response);
#endif
}

void PolkitAgent::cancel()
{
    if (m_request)
        finish(false, true);
}

void PolkitAgent::selectIdentity(int index)
{
    if (!m_request || index < 0 || index >= m_identities.size() || index == m_identityIndex)
        return;
    m_identityIndex = index;
    m_error.clear();
    m_prompt.clear();
    emit changed();
    startSession();
}

void PolkitAgent::finish(bool authorized, bool userCancelled)
{
#ifdef NEWTELLER_HAVE_POLKIT
    Request *request = m_request;
    if (!request)
        return;
    m_request = nullptr;
    if (request->session) {
        g_signal_handlers_disconnect_by_data(request->session, this);
        if (!authorized)
            polkit_agent_session_cancel(request->session);
        g_object_unref(request->session);
    }
    if (request->cancellable) {
        if (request->cancelHandler)
            g_cancellable_disconnect(request->cancellable, request->cancelHandler);
        g_object_unref(request->cancellable);
    }
    if (authorized)
        g_task_return_boolean(request->task, TRUE);
    else
        g_task_return_new_error(request->task, POLKIT_ERROR, POLKIT_ERROR_CANCELLED,
                                userCancelled ? "The user dismissed the dialog"
                                              : "The request was cancelled");
    g_object_unref(request->task);
    for (PolkitIdentity *identity : request->identities)
        g_object_unref(identity);
    delete request;
#else
    Q_UNUSED(userCancelled);
#endif
    reset();
    emit finished(authorized);
    // The next caller's dialog, once this one has closed.
    if (!m_queue.isEmpty())
        QTimer::singleShot(0, this, &PolkitAgent::startNext);
}

void PolkitAgent::reset()
{
    m_active = false;
    m_busy = false;
    m_prompt.clear();
    m_error.clear();
    m_info.clear();
    m_message.clear();
    m_actionId.clear();
    m_iconName.clear();
    m_program.clear();
    m_details.clear();
    m_identities.clear();
    m_identityIndex = 0;
    emit changed();
}
