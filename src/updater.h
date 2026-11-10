#pragma once

#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QUrl>

class NotificationDaemon;
class Settings;

// Updates of NewTeller itself. Exposed to QML as `Updater`.
//
// The update data is one small JSON file on the project's site
// (https://fourtexec.site/newteller/update.json). It holds no builds, only
// where they are: for every target (debian13-x86_64, arch-x86_64, ...) the
// commit, its date, the link to the release archive and the archive's
// SHA-256. The archive is downloaded from that link, checked against the
// hash and handed to newteller-update-install (through pkexec), which runs
// the archive's own install.sh.
class Updater : public QObject {
    Q_OBJECT
    // idle, checking, current, available, downloading, installing, installed
    Q_PROPERTY(QString state READ state NOTIFY changed)
    // Why the last check or installation failed; empty when it did not.
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // Where the update data comes from (the host name, for the interface).
    Q_PROPERTY(QString source READ source CONSTANT)
    // The running build (BUILDINFO of the release archive); the commit is
    // empty for a build from source.
    Q_PROPERTY(QString installedCommit READ installedCommit CONSTANT)
    Q_PROPERTY(QString installedDate READ installedDate CONSTANT)
    Q_PROPERTY(QString target READ target CONSTANT)
    // The build the site offers for this target (after a check).
    Q_PROPERTY(QString latestCommit READ latestCommit NOTIFY changed)
    Q_PROPERTY(QString latestDate READ latestDate NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QString lastChecked READ lastChecked NOTIFY changed)
    // 0..1 while downloading.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)

public:
    // Both may be null (newteller --check-update runs without a session).
    explicit Updater(Settings *settings = nullptr, NotificationDaemon *notifications = nullptr,
                     QObject *parent = nullptr);
    ~Updater() override;

    QString state() const { return m_state; }
    QString error() const { return m_error; }
    bool busy() const;
    QString source() const { return m_manifestUrl.host().isEmpty() ? m_manifestUrl.toString()
                                                                    : m_manifestUrl.host(); }
    QString installedCommit() const { return m_installedCommit; }
    QString installedDate() const { return formatDate(m_installedDate); }
    QString target() const { return m_target; }
    QString latestCommit() const { return m_latestCommit; }
    QString latestDate() const { return formatDate(m_latestDate); }
    QString notes() const { return m_notes; }
    QString lastChecked() const { return m_lastChecked; }
    double progress() const { return m_progress; }

    // Asks the site for the update data.
    Q_INVOKABLE void check();
    // Downloads the offered build and installs it (asks for the
    // administrator password).
    Q_INVOKABLE void install();
    // Stops a download.
    Q_INVOKABLE void cancel();

signals:
    void changed();
    void progressChanged();
    // A check ended, successfully or not.
    void checked();

private:
    void readBuildInfo();
    static QString detectTarget();
    static QString formatDate(qint64 seconds);
    void startCheck(bool automatic);
    void manifestReceived(QNetworkReply *reply, bool automatic);
    void failCheck(const QString &message);
    void downloadFinished(QNetworkReply *reply);
    void failInstall(const QString &message);
    void runInstaller();
    void installerFinished(int exitCode, QProcess::ExitStatus status);
    void setState(const QString &state, const QString &error = QString());
    void announce();
    void say(const QString &title, const QString &body) const;

    Settings *m_settings;
    NotificationDaemon *m_notifications;
    QNetworkAccessManager m_network;
    QUrl m_manifestUrl;
    QString m_state = QStringLiteral("idle");
    QString m_error;
    QString m_installedCommit;
    qint64 m_installedDate = 0;
    QString m_target;
    QString m_latestCommit;
    qint64 m_latestDate = 0;
    QString m_notes;
    QString m_lastChecked;
    QUrl m_archiveUrl;
    QString m_archiveHash;
    qint64 m_archiveSize = 0;
    double m_progress = 0;
    bool m_cancelled = false;
    QPointer<QNetworkReply> m_download;
    QFile m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    QProcess *m_installer = new QProcess;
    QByteArray m_installerOutput;
    QTimer m_autoTimer;
    // The build already announced with a notification, and that notification.
    QString m_announcedCommit;
    uint m_announcement = 0;
};
