#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QtDBus/QDBusContext>

class QDBusServiceWatcher;
class Settings;

// The engine behind the panel's quick settings popup: volume, brightness,
// network status and the media player that is playing right now.
//
// Every part is optional and fails soft:
//   * volume      - `wpctl` (WirePlumber, every modern Wayland session) or
//                   `pactl` (PulseAudio) when installed; hidden otherwise
//   * brightness  - /sys/class/backlight is read directly; writes go through
//                   GNOME's SettingsDaemon.Power when it is there, then fall
//                   back to a direct sysfs write (works when the udev rule
//                   allows it); hidden when no backlight exists
//   * network     - NetworkManager on the system bus, read only except the
//                   Wi-Fi toggle; hidden when NM is not there
//   * media       - the first org.mpris.MediaPlayer2.* player on the session
//                   bus, preferring whichever is playing
//
// Exposed to QML as the context property `Quick`. Nothing here polls the
// network: NetworkManager and MPRIS report changes over D-Bus, and volume and
// brightness are re-read on a slow timer because both can change outside the
// shell (media keys, function keys).
class QuickSettings : public QObject, protected QDBusContext {
    Q_OBJECT
    // Volume ------------------------------------------------------------------
    Q_PROPERTY(bool volumeAvailable READ volumeAvailable NOTIFY volumeChanged)
    Q_PROPERTY(int volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool volumeMuted READ volumeMuted NOTIFY volumeChanged)
    // Brightness --------------------------------------------------------------
    Q_PROPERTY(bool brightnessAvailable READ brightnessAvailable CONSTANT)
    Q_PROPERTY(int brightness READ brightness NOTIFY brightnessChanged)
    // Network -----------------------------------------------------------------
    Q_PROPERTY(bool networkAvailable READ networkAvailable NOTIFY networkChanged)
    Q_PROPERTY(int networkState READ networkState NOTIFY networkChanged)
    Q_PROPERTY(QString networkStatus READ networkStatus NOTIFY networkChanged)
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY networkChanged)
    // Media (MPRIS) -----------------------------------------------------------
    Q_PROPERTY(bool mediaAvailable READ mediaAvailable NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaPlayer READ mediaPlayer NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaArtist READ mediaArtist NOTIFY mediaChanged)
    Q_PROPERTY(bool mediaPlaying READ mediaPlaying NOTIFY mediaChanged)

public:
    explicit QuickSettings(Settings *settings, QObject *parent = nullptr);

    bool volumeAvailable() const { return m_volumeTool != VolumeTool::None; }
    int volume() const { return m_volume; }
    bool volumeMuted() const { return m_volumeMuted; }

    bool brightnessAvailable() const { return !m_backlightDevice.isEmpty(); }
    int brightness() const { return m_brightness; }

    bool networkAvailable() const;
    int networkState() const { return m_networkState; }
    QString networkStatus() const { return m_networkStatus; }
    bool wifiEnabled() const { return m_wifiEnabled; }

    bool mediaAvailable() const { return !m_mediaPlayers.isEmpty(); }
    QString mediaPlayer() const;
    QString mediaTitle() const { return m_mediaTitle; }
    QString mediaArtist() const { return m_mediaArtist; }
    bool mediaPlaying() const { return m_mediaPlaying; }

    // Volume ------------------------------------------------------------------
    Q_INVOKABLE void setVolume(int percent);
    Q_INVOKABLE void toggleMuted();
    // Brightness --------------------------------------------------------------
    Q_INVOKABLE void setBrightness(int percent);
    // Network -----------------------------------------------------------------
    Q_INVOKABLE void toggleWifi();
    // Media -------------------------------------------------------------------
    Q_INVOKABLE void mediaPlayPause();
    Q_INVOKABLE void mediaNext();
    Q_INVOKABLE void mediaPrevious();
    // Rereads volume and brightness right away (the popup calls this on open).
    Q_INVOKABLE void refresh();

signals:
    void volumeChanged();
    void brightnessChanged();
    void networkChanged();
    void mediaChanged();

private slots:
    void tick();
    void dbusNameOwnerChanged(const QString &name, const QString &oldOwner,
                              const QString &newOwner);
    void networkPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                  const QStringList &invalidated);
    void mediaPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                const QStringList &invalidated);

private:
    enum class VolumeTool { None, WpCtl, Pactl };

    void detectVolumeTool();
    void readVolume();
    void detectBacklight();
    void readBrightness();
    void writeBrightness(int percent);
    void setupNetwork();
    void readNetwork();
    void setNetworkStatus(int state, bool wifiEnabled);
    void setupMedia();
    void scanMediaPlayers();
    void addMediaPlayer(const QString &service);
    void removeMediaPlayer(const QString &service);
    void readMediaPlayer(const QString &service);
    void pickMedia();
    void callPlayer(const QString &method);
    bool runTool(const QStringList &arguments, QString *output) const;

    Settings *m_settings = nullptr;
    QTimer m_tickTimer;

    // Volume
    VolumeTool m_volumeTool = VolumeTool::None;
    int m_volume = 0;
    bool m_volumeMuted = false;
    // Brightness
    QString m_backlightDevice;
    int m_backlightMax = 0;
    int m_brightness = 0;
    bool m_brightnessWriteTried = false;
    // Network
    bool m_nmPresent = false;
    int m_networkState = 0;
    bool m_wifiEnabled = true;
    QString m_networkStatus;
    // Media
    QStringList m_mediaPlayers; // org.mpris.MediaPlayer2.* services
    QString m_mediaTitle;
    QString m_mediaArtist;
    bool m_mediaPlaying = false;
};
