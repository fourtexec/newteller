#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class NotificationDaemon;
class Settings;

// Screen recording with wf-recorder, which captures through the
// wlr-screencopy protocol the NewTeller compositor serves. Exposed to QML as
// `Recorder`: quick settings start it, a pill on the panel shows the time and
// stops it, Super+Alt+R toggles it. Videos go to Screencasts in the videos
// folder (~/Videos, or its localized name) unless recorder.folder names
// another, as recorder.format (mp4 or mkv).
class ScreenRecorder : public QObject {
    Q_OBJECT
    // wf-recorder is installed.
    Q_PROPERTY(bool available READ available CONSTANT)
    // slurp is installed too, so an area can be picked.
    Q_PROPERTY(bool areaAvailable READ areaAvailable CONSTANT)
    // Picking an area or recording.
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY stateChanged)
    // Seconds since the recording started.
    Q_PROPERTY(int elapsed READ elapsed NOTIFY elapsedChanged)

public:
    ScreenRecorder(Settings *settings, NotificationDaemon *notifications,
                   QObject *parent = nullptr);
    ~ScreenRecorder() override;

    bool available() const { return !m_recorder.isEmpty(); }
    bool areaAvailable() const { return available() && !m_slurp.isEmpty(); }
    bool busy() const { return m_picking || m_recording; }
    bool recording() const { return m_recording; }
    int elapsed() const;

    // `screen` is the output to record (a QScreen name, empty for the
    // primary one); `area` asks for a rectangle with slurp first.
    Q_INVOKABLE void start(bool area = false, const QString &screen = QString());
    Q_INVOKABLE void stop();
    // Super+Alt+R: the whole primary screen, or stop.
    Q_INVOKABLE void toggle();

    // Where the next video goes (the folder is created); empty when the
    // folder cannot be made.
    QString outputFile() const;

signals:
    void stateChanged();
    void elapsedChanged();

private:
    void begin(const QString &geometry, const QString &screen);
    void recorderFinished(int exitCode, QProcess::ExitStatus status);
    void say(const QString &title, const QString &body) const;

    Settings *m_settings;
    NotificationDaemon *m_notifications;
    QString m_recorder;
    QString m_slurp;
    QProcess m_process;
    QProcess m_picker;
    QTimer m_ticker;
    QElapsedTimer m_clock;
    QString m_file;
    bool m_picking = false;
    bool m_recording = false;
    bool m_stopping = false;
    // Notification id -> the video it announces (Open / Show in folder).
    QHash<uint, QString> m_announced;
};
