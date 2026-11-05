#include "audiodevices.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <memory>

AudioDevices::AudioDevices(QObject *parent)
    : QObject(parent)
    , m_pactl(QStandardPaths::findExecutable(QStringLiteral("pactl")))
{
    refresh();
}

void AudioDevices::run(const QStringList &arguments,
                       std::function<void(bool, const QByteArray &)> done)
{
    if (m_pactl.isEmpty()) {
        done(false, {});
        return;
    }
    auto *process = new QProcess(this);
    auto finished = std::make_shared<bool>(false);
    auto finish = [process, done, finished](bool ok) {
        if (*finished)
            return;
        *finished = true;
        done(ok, ok ? process->readAllStandardOutput() : QByteArray());
        process->deleteLater();
    };
    connect(process, &QProcess::finished, this,
            [finish](int code, QProcess::ExitStatus status) {
                finish(status == QProcess::NormalExit && code == 0);
            });
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(false);
    });
    // A sound server that does not answer is given up on.
    QTimer::singleShot(3000, process, [process] {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
    process->start(m_pactl, arguments);
}

void AudioDevices::refresh()
{
    if (m_reading || m_pactl.isEmpty())
        return;
    m_reading = true;
    run({QStringLiteral("get-default-sink")}, [this](bool, const QByteArray &out) {
        const QString defaultName = QString::fromUtf8(out).trimmed();
        run({QStringLiteral("-f"), QStringLiteral("json"), QStringLiteral("list"),
             QStringLiteral("sinks")},
            [this, defaultName](bool ok, const QByteArray &sinks) {
                if (!ok) {
                    m_reading = false;
                    if (m_available) {
                        m_available = false;
                        emit changed();
                    }
                    return;
                }
                const bool wasAvailable = m_available;
                const QVariantList oldOutputs = m_outputs;
                const QVariantList oldStreams = m_streams;
                readOutputs(sinks, defaultName);
                run({QStringLiteral("-f"), QStringLiteral("json"), QStringLiteral("list"),
                     QStringLiteral("sink-inputs")},
                    [this, wasAvailable, oldOutputs, oldStreams](bool ok, const QByteArray &inputs) {
                        m_reading = false;
                        if (ok)
                            readStreams(inputs);
                        // Unchanged data keeps the rows (and a slider being
                        // dragged) as they are.
                        if (m_available != wasAvailable || m_outputs != oldOutputs
                            || m_streams != oldStreams)
                            emit changed();
                    });
            });
    });
}

// "volume": {"front-left": {"value_percent": "45%", ...}, ...}: the loudest
// channel, as the sliders of other desktops show it.
int AudioDevices::volumePercent(const QVariant &volume)
{
    int best = 0;
    const QVariantMap channels = volume.toMap();
    for (auto it = channels.cbegin(); it != channels.cend(); ++it) {
        QString text = it.value().toMap().value(QStringLiteral("value_percent")).toString();
        text.remove(QLatin1Char('%'));
        best = qMax(best, text.trimmed().toInt());
    }
    return best;
}

void AudioDevices::readOutputs(const QByteArray &json, const QString &defaultName)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    // pactl older than 16 ignores -f json and prints text.
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        m_available = false;
        m_outputs.clear();
        return;
    }
    m_available = true;
    QVariantList outputs;
    for (const QJsonValue &value : doc.array()) {
        const QVariantMap sink = value.toObject().toVariantMap();
        const QString name = sink.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;
        QString description = sink.value(QStringLiteral("description")).toString();
        if (description.isEmpty())
            description = name;
        QVariantMap out;
        out.insert(QStringLiteral("name"), name);
        out.insert(QStringLiteral("description"), description);
        out.insert(QStringLiteral("isDefault"), name == defaultName);
        out.insert(QStringLiteral("volume"), volumePercent(sink.value(QStringLiteral("volume"))));
        out.insert(QStringLiteral("muted"), sink.value(QStringLiteral("mute")).toBool());
        out.insert(QStringLiteral("bluetooth"), name.startsWith(QLatin1String("bluez")));
        outputs.append(out);
    }
    m_outputs = outputs;
}

void AudioDevices::readStreams(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    QVariantList streams;
    for (const QJsonValue &value : doc.array()) {
        const QVariantMap input = value.toObject().toVariantMap();
        const QVariantMap props = input.value(QStringLiteral("properties")).toMap();
        // Event sounds (a click, a notification) come and go; not worth a row.
        if (props.value(QStringLiteral("media.role")).toString() == QLatin1String("event"))
            continue;
        QString app = props.value(QStringLiteral("application.name")).toString();
        if (app.isEmpty())
            app = props.value(QStringLiteral("application.process.binary")).toString();
        QVariantMap stream;
        stream.insert(QStringLiteral("index"), input.value(QStringLiteral("index")).toInt());
        stream.insert(QStringLiteral("app"), app);
        stream.insert(QStringLiteral("title"), props.value(QStringLiteral("media.name")).toString());
        stream.insert(QStringLiteral("icon"),
                      props.value(QStringLiteral("application.icon_name")).toString());
        stream.insert(QStringLiteral("volume"), volumePercent(input.value(QStringLiteral("volume"))));
        stream.insert(QStringLiteral("muted"), input.value(QStringLiteral("mute")).toBool());
        streams.append(stream);
    }
    m_streams = streams;
}

void AudioDevices::setDefaultOutput(const QString &name)
{
    if (name.isEmpty())
        return;
    run({QStringLiteral("set-default-sink"), name}, [this, name](bool ok, const QByteArray &) {
        if (!ok) {
            refresh();
            return;
        }
        // What is playing follows the new output (PulseAudio keeps streams
        // where they are otherwise).
        for (const QVariant &entry : std::as_const(m_streams)) {
            const QString index = QString::number(entry.toMap().value(QStringLiteral("index")).toInt());
            run({QStringLiteral("move-sink-input"), index, name}, [](bool, const QByteArray &) {});
        }
        QTimer::singleShot(300, this, &AudioDevices::refresh);
    });
}

void AudioDevices::setStreamVolume(int index, int percent)
{
    const int clamped = qBound(0, percent, 150);
    run({QStringLiteral("set-sink-input-volume"), QString::number(index),
         QStringLiteral("%1%").arg(clamped)},
        [](bool, const QByteArray &) {});
    for (QVariant &entry : m_streams) {
        QVariantMap stream = entry.toMap();
        if (stream.value(QStringLiteral("index")).toInt() == index) {
            stream.insert(QStringLiteral("volume"), clamped);
            entry = stream;
        }
    }
}

void AudioDevices::toggleStreamMuted(int index)
{
    run({QStringLiteral("set-sink-input-mute"), QString::number(index), QStringLiteral("toggle")},
        [this](bool, const QByteArray &) { refresh(); });
}
