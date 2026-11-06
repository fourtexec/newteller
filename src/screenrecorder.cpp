#include "screenrecorder.h"

#include "notifications.h"
#include "settings.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QUrl>

#include <csignal>
#include <sys/types.h>

ScreenRecorder::ScreenRecorder(Settings *settings, NotificationDaemon *notifications,
                               QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_notifications(notifications)
    , m_recorder(QStandardPaths::findExecutable(QStringLiteral("wf-recorder")))
    , m_slurp(QStandardPaths::findExecutable(QStringLiteral("slurp")))
{
    m_ticker.setInterval(1000);
    connect(&m_ticker, &QTimer::timeout, this, &ScreenRecorder::elapsedChanged);

    // wf-recorder prints its progress there; nobody reads it, so it is
    // dropped instead of filling the pipe until the recorder blocks.
    m_process.setStandardOutputFile(QProcess::nullDevice());
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_process, &QProcess::finished, this, &ScreenRecorder::recorderFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_recording = false;
        m_ticker.stop();
        emit stateChanged();
        say(tr("Screen recording failed"), tr("wf-recorder could not be started."));
    });

    // slurp prints the picked area as "X,Y WxH"; Escape exits with an error.
    connect(&m_picker, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        m_picking = false;
        const QString geometry = QString::fromUtf8(m_picker.readAllStandardOutput()).trimmed();
        if (status != QProcess::NormalExit || exitCode != 0 || geometry.isEmpty()) {
            emit stateChanged();
            return;
        }
        begin(geometry, QString());
    });

    if (m_notifications) {
        connect(m_notifications, &NotificationDaemon::ActionInvoked, this,
                [this](uint id, const QString &key) {
                    const QString file = m_announced.take(id);
                    if (file.isEmpty())
                        return;
                    if (key == QLatin1String("open"))
                        QDesktopServices::openUrl(QUrl::fromLocalFile(file));
                    else if (key == QLatin1String("folder"))
                        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath()));
                });
        connect(m_notifications, &NotificationDaemon::NotificationClosed, this,
                [this](uint id, uint) { m_announced.remove(id); });
    }
}

ScreenRecorder::~ScreenRecorder()
{
    // Logging out while recording: let wf-recorder finish the file so the
    // video is still playable.
    if (m_process.state() != QProcess::NotRunning) {
        ::kill(static_cast<pid_t>(m_process.processId()), SIGINT);
        if (!m_process.waitForFinished(3000))
            m_process.kill();
    }
    if (m_picker.state() != QProcess::NotRunning)
        m_picker.kill();
}

int ScreenRecorder::elapsed() const
{
    return m_recording && m_clock.isValid() ? int(m_clock.elapsed() / 1000) : 0;
}

void ScreenRecorder::start(bool area, const QString &screen)
{
    if (busy())
        return;
    if (!available()) {
        say(tr("Screen recording is not available"),
            tr("Install wf-recorder to record the screen."));
        return;
    }
    if (area) {
        if (m_slurp.isEmpty()) {
            say(tr("Screen recording is not available"),
                tr("Recording an area needs slurp."));
            return;
        }
        m_picking = true;
        emit stateChanged();
        m_picker.start(m_slurp, {});
        return;
    }
    begin(QString(), screen);
}

void ScreenRecorder::begin(const QString &geometry, const QString &screen)
{
    m_file = outputFile();
    if (m_file.isEmpty()) {
        emit stateChanged();
        say(tr("Screen recording failed"), tr("The videos folder could not be created."));
        return;
    }
    QStringList arguments{QStringLiteral("-f"), m_file};
    if (!geometry.isEmpty()) {
        arguments << QStringLiteral("-g") << geometry;
    } else {
        // Always name the output: with several screens wf-recorder would
        // otherwise ask on its standard input which one to record.
        QString name = screen;
        if (name.isEmpty() && QGuiApplication::primaryScreen())
            name = QGuiApplication::primaryScreen()->name();
        if (!name.isEmpty())
            arguments << QStringLiteral("-o") << name;
    }
    // The default microphone (PipeWire / PulseAudio source).
    if (m_settings->value(QStringLiteral("recorder.audio"), false).toBool())
        arguments << QStringLiteral("--audio");
    m_stopping = false;
    m_process.start(m_recorder, arguments);
    m_process.closeWriteChannel();
    m_recording = true;
    m_clock.start();
    m_ticker.start();
    emit stateChanged();
    emit elapsedChanged();
}

void ScreenRecorder::stop()
{
    if (m_picking) {
        m_picker.kill();
        return;
    }
    if (!m_recording || m_process.state() == QProcess::NotRunning)
        return;
    // SIGINT is wf-recorder's "finish the file and quit"; a recorder that
    // does not react within a few seconds is killed.
    m_stopping = true;
    ::kill(static_cast<pid_t>(m_process.processId()), SIGINT);
    QTimer::singleShot(5000, this, [this] {
        if (m_stopping && m_process.state() != QProcess::NotRunning)
            m_process.kill();
    });
}

void ScreenRecorder::toggle()
{
    if (busy())
        stop();
    else
        start(false);
}

void ScreenRecorder::recorderFinished(int exitCode, QProcess::ExitStatus status)
{
    const bool asked = m_stopping;
    const int seconds = elapsed();
    m_stopping = false;
    m_recording = false;
    m_ticker.stop();
    emit stateChanged();
    emit elapsedChanged();

    const QFileInfo info(m_file);
    const bool saved = info.exists() && info.size() > 0;
    if (!saved || (!asked && (status != QProcess::NormalExit || exitCode != 0))) {
        // The last line wf-recorder printed says why (no screencopy, an
        // encoder or audio device missing).
        QString reason = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
        reason = reason.section(QLatin1Char('\n'), -1).trimmed();
        if (reason.isEmpty())
            reason = tr("wf-recorder stopped unexpectedly.");
        if (!saved)
            QFile::remove(m_file);
        say(tr("Screen recording failed"), reason);
        return;
    }
    const QString body = tr("%1 (%2:%3) in %4")
                             .arg(info.fileName())
                             .arg(seconds / 60)
                             .arg(seconds % 60, 2, 10, QLatin1Char('0'))
                             .arg(QDir::toNativeSeparators(info.absolutePath()));
    if (m_notifications
        && m_settings->value(QStringLiteral("notifications.enabled"), true).toBool()) {
        const uint id = m_notifications->sendWithActions(
            QStringLiteral("NewTeller"), tr("Screen recording saved"), body,
            QStringLiteral("video"),
            {QStringLiteral("open"), tr("Play"), QStringLiteral("folder"), tr("Show in folder")},
            false, 10000);
        m_announced.insert(id, info.absoluteFilePath());
        return;
    }
    say(tr("Screen recording saved"), body);
}

QString ScreenRecorder::outputFile() const
{
    QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (videos.isEmpty())
        videos = QDir::homePath() + QStringLiteral("/Videos");
    const QString folder = videos + QStringLiteral("/Screencasts");
    if (!QDir().mkpath(folder))
        return QString();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString file = QStringLiteral("%1/Screencast_%2.mp4").arg(folder, stamp);
    // wf-recorder asks before overwriting; never let it.
    for (int n = 2; QFileInfo::exists(file); ++n)
        file = QStringLiteral("%1/Screencast_%2-%3.mp4").arg(folder, stamp).arg(n);
    return file;
}

void ScreenRecorder::say(const QString &title, const QString &body) const
{
    if (m_notifications
        && m_settings->value(QStringLiteral("notifications.enabled"), true).toBool()) {
        m_notifications->send(QStringLiteral("NewTeller"), title, body, QStringLiteral("video"));
        return;
    }
    QProcess::startDetached(QStringLiteral("notify-send"),
                            {QStringLiteral("--app-name=NewTeller"), title, body});
}
