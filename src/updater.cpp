#include "updater.h"

#include "notifications.h"
#include "settings.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>

#include <unistd.h>

namespace {

const char kManifestUrl[] = "https://fourtexec.site/newteller/update.json";
const char kBuildInfo[] = "/usr/share/newteller/BUILDINFO";
// Release archives are tens of megabytes; anything far beyond that is not one.
const qint64 kMaxArchiveBytes = 1024LL * 1024 * 1024;
const int kNetworkTimeoutMs = 30000;
// The format of the update data this build understands ("schema").
const int kSchema = 1;
const int kMaxChanges = 15;

bool isHex(const QString &text, int length)
{
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]+$"));
    return text.size() == length && hex.match(text).hasMatch();
}

// key=value files: BUILDINFO, /etc/os-release (values may be quoted).
QHash<QString, QString> readKeyValues(const QString &path)
{
    QHash<QString, QString> values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return values;
    const QList<QByteArray> lines = file.read(64 * 1024).split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0 || line.startsWith(QLatin1Char('#')))
            continue;
        QString value = line.mid(equals + 1).trimmed();
        if (value.size() >= 2 && (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\'')))
            && value.endsWith(value.at(0)))
            value = value.mid(1, value.size() - 2);
        values.insert(line.left(equals).trimmed(), value);
    }
    return values;
}

} // namespace

Updater::Updater(Settings *settings, NotificationDaemon *notifications, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_notifications(notifications)
    , m_manifestUrl(QString::fromLatin1(kManifestUrl))
{
    // For tests and mirrors: another place to read the update data from.
    const QString otherUrl = qEnvironmentVariable("NEWTELLER_UPDATE_URL");
    if (!otherUrl.isEmpty())
        m_manifestUrl = QUrl::fromUserInput(otherUrl);
    readBuildInfo();

    m_installer->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_installer, &QProcess::readyRead, this, [this] {
        // Only the end is ever shown (the reason of a failure).
        m_installerOutput = (m_installerOutput + m_installer->readAll()).right(8192);
    });
    connect(m_installer, &QProcess::finished, this, &Updater::installerFinished);
    connect(m_installer, &QProcess::errorOccurred, this, [this](QProcess::ProcessError problem) {
        if (problem == QProcess::FailedToStart)
            failInstall(tr("pkexec could not be started."));
    });

    if (!m_settings)
        return;
    auto automatic = [this] {
        if (busy() || m_state == QLatin1String("installed"))
            return;
        if (m_settings->value(QStringLiteral("updates.autoCheck"), true).toBool())
            startCheck(true);
    };
    // Not at login, when the network is often still coming up.
    QTimer::singleShot(2 * 60 * 1000, this, automatic);
    m_autoTimer.setInterval(12 * 60 * 60 * 1000);
    connect(&m_autoTimer, &QTimer::timeout, this, automatic);
    m_autoTimer.start();

    if (m_notifications) {
        connect(m_notifications, &NotificationDaemon::ActionInvoked, this,
                [this](uint id, const QString &key) {
                    if (id != m_announcement || m_announcement == 0)
                        return;
                    m_announcement = 0;
                    if (key == QLatin1String("install"))
                        install();
                    else if (key == QLatin1String("skip"))
                        skip();
                });
        connect(m_notifications, &NotificationDaemon::NotificationClosed, this,
                [this](uint id, uint) {
                    if (id == m_announcement)
                        m_announcement = 0;
                });
    }
}

Updater::~Updater()
{
    if (m_download)
        m_download->abort();
    // A running installer is left alone: QProcess's destructor would kill it
    // (or wait for it) half way through. It does not need the shell to
    // finish, so the object is simply not destroyed on the way out.
    m_installer->disconnect(this);
    if (m_installer->state() == QProcess::NotRunning)
        delete m_installer;
}

bool Updater::busy() const
{
    return m_state == QLatin1String("checking") || m_state == QLatin1String("downloading")
           || m_state == QLatin1String("installing");
}

void Updater::readBuildInfo()
{
    QString path = qEnvironmentVariable("NEWTELLER_BUILDINFO");
    if (path.isEmpty())
        path = QString::fromLatin1(kBuildInfo);
    const QHash<QString, QString> info = readKeyValues(path);
    const QString commit = info.value(QStringLiteral("commit"));
    if (isHex(commit, 40))
        m_installedCommit = commit;
    m_installedDate = info.value(QStringLiteral("date")).toLongLong();
    m_target = info.value(QStringLiteral("target"));
    // A build from source (the installation images build that way) has no
    // target written down; the system says which archive fits.
    if (m_target.isEmpty())
        m_target = detectTarget();
}

// The same table as scripts/platform.sh.
QString Updater::detectTarget()
{
#if defined(__x86_64__)
    const QHash<QString, QString> os = readKeyValues(QStringLiteral("/etc/os-release"));
    const QString id = os.value(QStringLiteral("ID"));
    const QString version = os.value(QStringLiteral("VERSION_ID"));
    if (id == QLatin1String("debian") && version == QLatin1String("13"))
        return QStringLiteral("debian13-x86_64");
    if (id == QLatin1String("ubuntu") && version == QLatin1String("26.04"))
        return QStringLiteral("ubuntu26.04-x86_64");
    if (id == QLatin1String("fedora") && version.toInt() >= 42)
        return QStringLiteral("fedora%1-x86_64").arg(version.toInt());
    const QStringList like = os.value(QStringLiteral("ID_LIKE")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (id == QLatin1String("arch") || like.contains(QLatin1String("arch")))
        return QStringLiteral("arch-x86_64");
#endif
    return QString();
}

QString Updater::formatDate(qint64 seconds)
{
    if (seconds <= 0)
        return QString();
    return QLocale().toString(QDateTime::fromSecsSinceEpoch(seconds).date(), QLocale::ShortFormat);
}

void Updater::setState(const QString &state, const QString &error)
{
    m_state = state;
    m_error = error;
    emit changed();
}

void Updater::check()
{
    startCheck(false);
}

void Updater::startCheck(bool automatic)
{
    // After an installation the files on disk are ahead of this process;
    // what it knows about itself is stale until the next login.
    if (busy() || m_state == QLatin1String("installed"))
        return;
    if (!m_manifestUrl.isValid()) {
        failCheck(tr("The address of the update data is not valid."));
        return;
    }
    setState(QStringLiteral("checking"));
    QNetworkRequest request(m_manifestUrl);
    request.setTransferTimeout(kNetworkTimeoutMs);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("NewTeller-Updater"));
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, automatic] { manifestReceived(reply, automatic); });
}

void Updater::failCheck(const QString &message)
{
    // What an earlier check found stays on offer.
    setState(m_archiveUrl.isValid() && m_latestCommit != m_installedCommit
                 ? QStringLiteral("available") : QStringLiteral("idle"),
             message);
    emit checked();
}

void Updater::manifestReceived(QNetworkReply *reply, bool automatic)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        failCheck(tr("Could not read the update data from %1: %2").arg(source(), reply->errorString()));
        return;
    }
    const QJsonObject manifest = QJsonDocument::fromJson(reply->read(1024 * 1024)).object();
    // A site that moved on to a format this build cannot read says so,
    // instead of being misread.
    if (manifest.value(QLatin1String("schema")).toInt(kSchema) > kSchema) {
        failCheck(tr("This build of NewTeller is too old for the update data of %1. "
                     "Install the newest build by hand once.").arg(source()));
        return;
    }
    const QJsonObject targets = manifest.value(QLatin1String("targets")).toObject();
    if (targets.isEmpty()) {
        failCheck(tr("The update data from %1 could not be understood.").arg(source()));
        return;
    }
    m_lastChecked = QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat);
    if (m_target.isEmpty() || !targets.contains(m_target)) {
        failCheck(tr("There is no build for this system (%1) yet.")
                      .arg(m_target.isEmpty() ? tr("unknown") : m_target));
        return;
    }
    const QJsonObject entry = targets.value(m_target).toObject();
    const QString commit = entry.value(QLatin1String("commit")).toString();
    const QString hash = entry.value(QLatin1String("sha256")).toString().toLower();
    const QUrl url(entry.value(QLatin1String("url")).toString());
    // Links are only followed over https; a test manifest read from a local
    // file may point at local files.
    const bool schemeAllowed = url.scheme() == QLatin1String("https")
                               || m_manifestUrl.scheme() != QLatin1String("https");
    if (!isHex(commit, 40) || !isHex(hash, 64) || !url.isValid() || !schemeAllowed) {
        failCheck(tr("The update data from %1 could not be understood.").arg(source()));
        return;
    }
    m_latestCommit = commit;
    m_latestDate = static_cast<qint64>(entry.value(QLatin1String("date")).toDouble());
    m_notes = entry.value(QLatin1String("notes")).toString().left(300);
    m_archiveUrl = url;
    m_archiveHash = hash;
    m_archiveSize = static_cast<qint64>(entry.value(QLatin1String("size")).toDouble());

    // The site can lag behind a build installed by hand: an offer that is
    // older than what runs here is not an update.
    const bool older = m_installedDate > 0 && m_latestDate > 0 && m_latestDate <= m_installedDate;
    const bool available = commit != m_installedCommit && !older;

    // "changes" lists the newest commits of the project, newest first; the
    // ones between the installed build and the offered one are what the
    // update brings.
    m_changes.clear();
    const QJsonArray changes = manifest.value(QLatin1String("changes")).toArray();
    bool inRange = false;
    for (const QJsonValue &value : changes) {
        const QJsonObject change = value.toObject();
        const QString changeCommit = change.value(QLatin1String("commit")).toString();
        const qint64 changeDate = static_cast<qint64>(change.value(QLatin1String("date")).toDouble());
        if (changeCommit == commit)
            inRange = true;
        if (!m_installedCommit.isEmpty() && changeCommit == m_installedCommit)
            break;
        if (m_installedDate > 0 && changeDate > 0 && changeDate <= m_installedDate)
            break;
        const QString title = change.value(QLatin1String("title")).toString().left(200);
        if (!available || !inRange || title.isEmpty() || !isHex(changeCommit, 40))
            continue;
        m_changes.append(QVariantMap{{QStringLiteral("title"), title},
                                     {QStringLiteral("commit"), changeCommit.left(10)},
                                     {QStringLiteral("date"), formatDate(changeDate)}});
        if (m_changes.size() >= kMaxChanges)
            break;
    }
    setState(available ? QStringLiteral("available") : QStringLiteral("current"));
    emit checked();
    // A build from source has no commit to compare with: the offer is shown
    // in the settings, but nobody is nagged about it.
    if (available && automatic && !m_installedCommit.isEmpty())
        announce();
}

void Updater::announce()
{
    if (m_announcedCommit == m_latestCommit || skipped() || !m_notifications || !m_settings
        || !m_settings->value(QStringLiteral("notifications.enabled"), true).toBool())
        return;
    m_announcedCommit = m_latestCommit;
    const QString body = m_notes.isEmpty()
                             ? tr("Build %1 can be installed from Settings, Updates.").arg(m_latestCommit.left(10))
                             : m_notes;
    m_announcement = m_notifications->sendWithActions(
        QStringLiteral("NewTeller"), tr("A NewTeller update is available"), body,
        QStringLiteral("download"),
        {QStringLiteral("install"), tr("Install"), QStringLiteral("skip"), tr("Skip this build")}, false);
}

bool Updater::skipped() const
{
    return m_settings && !m_latestCommit.isEmpty()
           && m_settings->value(QStringLiteral("updates.skippedBuild")).toString() == m_latestCommit;
}

void Updater::skip()
{
    if (!m_settings || m_latestCommit.isEmpty())
        return;
    m_settings->setValue(QStringLiteral("updates.skippedBuild"), m_latestCommit);
    emit changed();
}

void Updater::install()
{
    if (m_state != QLatin1String("available"))
        return;
    if (::geteuid() != 0 && QStandardPaths::findExecutable(QStringLiteral("pkexec")).isEmpty()) {
        failInstall(tr("pkexec is not installed, so the update cannot ask for the administrator password."));
        return;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("newteller-update-install")).isEmpty()) {
        failInstall(tr("newteller-update-install is missing; reinstall NewTeller from a release archive."));
        return;
    }
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                           + QStringLiteral("/newteller/updates");
    // Old downloads are not kept.
    QDir(folder).removeRecursively();
    if (!QDir().mkpath(folder)) {
        failInstall(tr("The folder for the download could not be created."));
        return;
    }
    m_file.setFileName(folder + QStringLiteral("/newteller-%1.zip").arg(m_latestCommit));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        failInstall(tr("The folder for the download could not be created."));
        return;
    }
    m_hash.reset();
    m_cancelled = false;
    m_progress = 0;
    emit progressChanged();
    setState(QStringLiteral("downloading"));

    QNetworkRequest request(m_archiveUrl);
    // No data for this long means the connection is gone; the whole
    // download may take as long as it needs.
    request.setTransferTimeout(kNetworkTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("NewTeller-Updater"));
    QNetworkReply *reply = m_network.get(request);
    m_download = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] { saveDownloaded(reply); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const qint64 expected = total > 0 ? total : m_archiveSize;
        m_progress = expected > 0 ? qBound(0.0, double(received) / double(expected), 1.0) : 0;
        emit progressChanged();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { downloadFinished(reply); });
}

// Moves what has arrived into the file and the running hash.
void Updater::saveDownloaded(QNetworkReply *reply)
{
    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty() || !m_file.isOpen())
        return;
    m_hash.addData(chunk);
    if (m_file.write(chunk) != chunk.size() || m_file.size() > kMaxArchiveBytes)
        reply->abort();
}

void Updater::cancel()
{
    if (m_state == QLatin1String("downloading") && m_download) {
        m_cancelled = true;
        m_download->abort();
    }
}

void Updater::downloadFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    // Whatever arrived together with the end of the reply.
    if (reply->error() == QNetworkReply::NoError)
        saveDownloaded(reply);
    const bool tooLarge = m_file.size() > kMaxArchiveBytes;
    const bool writeFailed = m_file.error() != QFileDevice::NoError;
    m_file.close();
    if (m_cancelled) {
        m_file.remove();
        setState(QStringLiteral("available"));
        return;
    }
    if (reply->error() != QNetworkReply::NoError || writeFailed || tooLarge) {
        m_file.remove();
        failInstall(writeFailed ? tr("The download could not be saved (is the disk full?).")
                                : tr("The download failed: %1").arg(reply->errorString()));
        return;
    }
    if (QString::fromLatin1(m_hash.result().toHex()) != m_archiveHash) {
        m_file.remove();
        failInstall(tr("The downloaded file does not match its checksum and was deleted."));
        return;
    }
    runInstaller();
}

void Updater::runInstaller()
{
    m_installerOutput.clear();
    setState(QStringLiteral("installing"));
    // pkexec asks for the administrator password through the shell's own
    // polkit agent. The helper checks the hash again as root, on its own
    // copy, so the file cannot be swapped between the check and the run.
    const QString helper = QStandardPaths::findExecutable(QStringLiteral("newteller-update-install"));
    if (::geteuid() == 0) {
        // Already root (newteller --install-update under sudo): nobody to ask.
        m_installer->start(helper, {m_file.fileName(), m_archiveHash});
        return;
    }
    m_installer->start(QStandardPaths::findExecutable(QStringLiteral("pkexec")),
                       {helper, m_file.fileName(), m_archiveHash});
}

void Updater::installerFinished(int exitCode, QProcess::ExitStatus status)
{
    m_file.remove();
    if (status == QProcess::NormalExit && exitCode == 0) {
        setState(QStringLiteral("installed"));
        say(tr("NewTeller was updated"), tr("Log out and back in to start the new version."));
        return;
    }
    // pkexec: 126 = the prompt was dismissed, 127 = not authorized.
    if (status == QProcess::NormalExit && (exitCode == 126 || exitCode == 127)) {
        failInstall(tr("The update was not installed: the administrator password was not given."));
        return;
    }
    QString reason;
    const QList<QByteArray> lines = m_installerOutput.split('\n');
    for (auto line = lines.crbegin(); line != lines.crend() && reason.isEmpty(); ++line)
        reason = QString::fromUtf8(*line).trimmed();
    failInstall(reason.isEmpty() ? tr("The installer failed.")
                                 : tr("The installer failed: %1").arg(reason.left(300)));
}

void Updater::failInstall(const QString &message)
{
    setState(QStringLiteral("available"), message);
    say(tr("The NewTeller update failed"), message);
}

void Updater::say(const QString &title, const QString &body) const
{
    if (m_notifications && m_settings
        && m_settings->value(QStringLiteral("notifications.enabled"), true).toBool())
        m_notifications->send(QStringLiteral("NewTeller"), title, body, QStringLiteral("download"));
}
