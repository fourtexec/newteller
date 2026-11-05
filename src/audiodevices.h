#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <functional>

// Sound outputs and the applications playing, for the sound card in quick
// settings. Exposed to QML as `Audio`.
//
// Everything goes through `pactl -f json` (PulseAudio 16+, and PipeWire
// through pipewire-pulse), run asynchronously: a stuck sound server never
// freezes the shell. Without pactl, or with one too old for JSON output,
// `available` stays false and the card offers the external sound settings
// instead.
class AudioDevices : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // [{ name, description, isDefault, volume (0..150), muted, bluetooth }]
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY changed)
    // [{ index, app, title, icon, volume, muted }]
    Q_PROPERTY(QVariantList streams READ streams NOTIFY changed)

public:
    explicit AudioDevices(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QVariantList outputs() const { return m_outputs; }
    QVariantList streams() const { return m_streams; }

    // Reads the outputs and the playing applications again.
    Q_INVOKABLE void refresh();
    // Makes `name` the default output and moves what is playing there.
    Q_INVOKABLE void setDefaultOutput(const QString &name);
    Q_INVOKABLE void setStreamVolume(int index, int percent);
    Q_INVOKABLE void toggleStreamMuted(int index);

signals:
    void changed();

private:
    void run(const QStringList &arguments, std::function<void(bool, const QByteArray &)> done);
    void readOutputs(const QByteArray &json, const QString &defaultName);
    void readStreams(const QByteArray &json);
    static int volumePercent(const QVariant &volume);

    QString m_pactl;
    bool m_available = false;
    bool m_reading = false;
    QVariantList m_outputs;
    QVariantList m_streams;
};
