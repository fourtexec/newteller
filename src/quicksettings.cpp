#include "quicksettings.h"
#include "settings.h"

#include <QCoreApplication>
#include <memory>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingReply>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QtDebug>

namespace {

constexpr auto kNmService = "org.freedesktop.NetworkManager";
constexpr auto kNmPath = "/org/freedesktop/NetworkManager";
constexpr auto kNmInterface = "org.freedesktop.NetworkManager";
constexpr auto kMprisPrefix = "org.mpris.MediaPlayer2.";
constexpr auto kMprisPath = "/org/mpris/MediaPlayer2";

// NetworkManager device states, in the order of the spec (NMState).
QString networkStateText(int state, bool wifiEnabled)
{
    if (state >= 70)
        return QCoreApplication::translate("QuickSettings", "Connected");
    if (state >= 60)
        return QCoreApplication::translate("QuickSettings", "Connecting...");
    if (state >= 30)
        return QCoreApplication::translate("QuickSettings", "Disconnecting...");
    if (!wifiEnabled)
        return QCoreApplication::translate("QuickSettings", "Wi-Fi off");
    return QCoreApplication::translate("QuickSettings", "Disconnected");
}

// MPRIS Metadata (a{sv}) as a map. qdbus_cast reads the dictionary itself;
// the hand-written loop it replaces used the writing QDBusArgument API,
// never advanced and froze the shell whenever a player changed tracks.
QVariantMap metadataMap(const QVariant &metadata)
{
    if (metadata.userType() == qMetaTypeId<QDBusArgument>())
        return qdbus_cast<QVariantMap>(metadata.value<QDBusArgument>());
    return metadata.toMap();
}

// Extracts a plain string from a D-Bus variant, looking through QDBusArgument
// wrappers (Metadata arrives as a{sv} with nested types). The argument is
// read through a const QDBusArgument: the non-const begin*() calls are the
// writing API, do nothing on a received message, and a loop over them
// never ends.
QString variantString(const QVariant &value)
{
    if (value.userType() == QMetaType::QStringList)
        return value.toStringList().join(QStringLiteral(", "));
    if (value.userType() == qMetaTypeId<QDBusVariant>())
        return variantString(qvariant_cast<QDBusVariant>(value).variant());
    if (value.userType() != qMetaTypeId<QDBusArgument>() && value.canConvert<QString>())
        return value.toString();
    if (value.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        if (argument.currentType() == QDBusArgument::ArrayType) {
            argument.beginArray();
            QString joined;
            while (!argument.atEnd()) {
                QString item;
                argument >> item;
                if (!joined.isEmpty())
                    joined += QStringLiteral(", ");
                joined += item;
            }
            argument.endArray();
            return joined;
        }
        if (argument.currentType() == QDBusArgument::StructureType) {
            argument.beginStructure();
            QString item;
            argument >> item;
            argument.endStructure();
            return item;
        }
    }
    return QString();
}

} // namespace

QuickSettings::QuickSettings(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    detectVolumeTool();
    detectBacklight();
    readVolume();
    readBrightness();
    setupNetwork();
    setupMedia();

    // Volume and brightness can change outside the shell (media keys,
    // function keys, other mixers). D-Bus sources update themselves.
    m_tickTimer.setInterval(8000);
    connect(&m_tickTimer, &QTimer::timeout, this, &QuickSettings::tick);
    m_tickTimer.start();
}

void QuickSettings::tick()
{
    readVolume();
    readBrightness();
}

void QuickSettings::refresh()
{
    readVolume();
    readBrightness();
}

// ---------------------------------------------------------------------------
// Volume (wpctl, then pactl)

void QuickSettings::detectVolumeTool()
{
    if (!QStandardPaths::findExecutable(QStringLiteral("wpctl")).isEmpty())
        m_volumeTool = VolumeTool::WpCtl;
    else if (!QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty())
        m_volumeTool = VolumeTool::Pactl;
    else
        m_volumeTool = VolumeTool::None;
}

void QuickSettings::runToolAsync(const QStringList &arguments,
                                 std::function<void(bool, const QString &)> done)
{
    auto *process = new QProcess(this);
    auto finished = std::make_shared<bool>(false);
    auto finish = [process, done, finished](bool ok) {
        if (*finished)
            return;
        *finished = true;
        done(ok, ok ? QString::fromUtf8(process->readAllStandardOutput()).trimmed() : QString());
        process->deleteLater();
    };
    connect(process, &QProcess::finished, this, [process, finish](int code, QProcess::ExitStatus status) {
        Q_UNUSED(process);
        finish(status == QProcess::NormalExit && code == 0);
    });
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(false);
    });
    QTimer::singleShot(3000, process, [process] {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
    process->start(arguments.first(), arguments.mid(1));
}

void QuickSettings::applyVolume(int percent, bool muted)
{
    if (percent != m_volume || muted != m_volumeMuted) {
        m_volume = qBound(0, percent, 150);
        m_volumeMuted = muted;
        emit volumeChanged();
    }
}

// Every 8 s and after each change: asynchronous, so a slow or stuck audio
// server never freezes the panel (it used to wait up to 2.3 s for wpctl).
void QuickSettings::readVolume()
{
    if (m_volumeTool == VolumeTool::None || m_volumeReading)
        return;
    m_volumeReading = true;
    if (m_volumeTool == VolumeTool::WpCtl) {
        runToolAsync({QStringLiteral("wpctl"), QStringLiteral("get-volume"),
                      QStringLiteral("@DEFAULT_AUDIO_SINK@")},
                     [this](bool ok, const QString &out) {
                         m_volumeReading = false;
                         if (!ok)
                             return;
                         // "Volume: 0.45 [MUTED]"
                         const bool muted = out.contains(QLatin1String("MUTED"), Qt::CaseInsensitive);
                         const QStringList words = out.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                         bool parsed = false;
                         double linear = 0;
                         for (const QString &word : words) {
                             linear = word.toDouble(&parsed);
                             if (parsed)
                                 break;
                         }
                         if (parsed)
                             applyVolume(qRound(linear * 100.0), muted);
                     });
        return;
    }
    // pactl: the mute state, then the volume.
    runToolAsync({QStringLiteral("pactl"), QStringLiteral("get-sink-mute"),
                  QStringLiteral("@DEFAULT_SINK@")},
                 [this](bool, const QString &mutedOut) {
                     const bool muted = mutedOut.contains(QLatin1String("yes"), Qt::CaseInsensitive);
                     runToolAsync({QStringLiteral("pactl"), QStringLiteral("get-sink-volume"),
                                   QStringLiteral("@DEFAULT_SINK@")},
                                  [this, muted](bool ok, const QString &out) {
                                      m_volumeReading = false;
                                      if (!ok)
                                          return;
                                      // "Volume: front-left: 43690 /  67% / -11.00 dB, ..."
                                      static const QRegularExpression percentRegex(
                                          QStringLiteral("(\\d+)%"));
                                      const auto match = percentRegex.match(out);
                                      if (match.hasMatch())
                                          applyVolume(match.captured(1).toInt(), muted);
                                  });
                 });
}

void QuickSettings::setVolume(int percent)
{
    if (m_volumeTool == VolumeTool::None)
        return;
    const int clamped = qBound(0, percent, 150);
    if (m_volumeTool == VolumeTool::WpCtl) {
        QProcess::startDetached(
            QStringLiteral("wpctl"),
            {QStringLiteral("set-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@"),
             QString::number(clamped / 100.0, 'f', 3)});
    } else {
        QProcess::startDetached(
            QStringLiteral("pactl"),
            {QStringLiteral("set-sink-volume"), QStringLiteral("@DEFAULT_SINK@"),
             QStringLiteral("%1%").arg(clamped)});
    }
    m_volume = clamped;
    emit volumeChanged();
    // The mixer answers asynchronously; the next tick settles the real value.
    QTimer::singleShot(400, this, &QuickSettings::readVolume);
}

void QuickSettings::toggleMuted()
{
    if (m_volumeTool == VolumeTool::None)
        return;
    if (m_volumeTool == VolumeTool::WpCtl) {
        QProcess::startDetached(QStringLiteral("wpctl"),
                                {QStringLiteral("set-mute"),
                                 QStringLiteral("@DEFAULT_AUDIO_SINK@"), QStringLiteral("toggle")});
    } else {
        QProcess::startDetached(QStringLiteral("pactl"),
                                {QStringLiteral("set-sink-mute"), QStringLiteral("@DEFAULT_SINK@"),
                                 QStringLiteral("toggle")});
    }
    m_volumeMuted = !m_volumeMuted;
    emit volumeChanged();
    QTimer::singleShot(400, this, &QuickSettings::readVolume);
}

// ---------------------------------------------------------------------------
// Brightness (/sys/class/backlight)

void QuickSettings::detectBacklight()
{
    QDir root(QStringLiteral("/sys/class/backlight"));
    const QStringList devices = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    // "firmware" and "raw" backlights are the screen; "platform" ones often
    // belong to keyboards. Preference order matches upower's.
    for (const QString &type : {QStringLiteral("firmware"), QStringLiteral("raw"),
                                QStringLiteral("platform")}) {
        for (const QString &device : devices) {
            QFile typeFile(root.filePath(device + QStringLiteral("/type")));
            if (typeFile.open(QIODevice::ReadOnly)
                && typeFile.readAll().trimmed() == type.toLatin1()) {
                QFile maxFile(root.filePath(device + QStringLiteral("/max_brightness")));
                if (!maxFile.open(QIODevice::ReadOnly) || maxFile.readAll().trimmed().toInt() <= 0)
                    continue;
                m_backlightDevice = device;
                m_backlightMax = maxFile.readAll().trimmed().toInt();
                return;
            }
        }
    }
    // No `type` files (older kernels): fall back to the first entry.
    if (m_backlightDevice.isEmpty() && !devices.isEmpty()) {
        QFile maxFile(root.filePath(devices.first() + QStringLiteral("/max_brightness")));
        if (maxFile.open(QIODevice::ReadOnly)) {
            const int max = maxFile.readAll().trimmed().toInt();
            if (max > 0) {
                m_backlightDevice = devices.first();
                m_backlightMax = max;
            }
        }
    }
}

void QuickSettings::readBrightness()
{
    if (m_backlightDevice.isEmpty())
        return;
    QFile file(QStringLiteral("/sys/class/backlight/%1/brightness").arg(m_backlightDevice));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const int value = file.readAll().trimmed().toInt();
    const int percent = m_backlightMax > 0 ? qRound(value * 100.0 / m_backlightMax) : 0;
    if (percent != m_brightness) {
        m_brightness = qBound(0, percent, 100);
        emit brightnessChanged();
    }
}

void QuickSettings::setBrightness(int percent)
{
    if (m_backlightDevice.isEmpty())
        return;
    const int clamped = qBound(0, percent, 100);
    writeBrightness(clamped);
    m_brightness = clamped;
    emit brightnessChanged();
}

void QuickSettings::writeBrightness(int percent)
{
    const int value = m_backlightMax > 0 ? qRound(percent * m_backlightMax / 100.0) : 0;
    // GNOME's settings daemon applies the change for us (and handles PWM
    // quirks); when it is not there, try the raw sysfs write, which works
    // whenever the udev rules allow the user to write.
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.gnome.SettingsDaemon.Power"),
        QStringLiteral("/org/gnome/SettingsDaemon/Power"),
        QStringLiteral("org.gnome.SettingsDaemon.Power.Screen"), QStringLiteral("SetPercentage"));
    call << percent;
    // Asynchronous: a slow settings daemon must not stall the slider.
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 500), this);
    const QString device = m_backlightDevice;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, device, value](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (!w->isError())
            return;
        QFile file(QStringLiteral("/sys/class/backlight/%1/brightness").arg(device));
        if (file.open(QIODevice::WriteOnly))
            file.write(QByteArray::number(value));
        else
            m_brightnessWriteTried = true;
    });
}

// ---------------------------------------------------------------------------
// Network (NetworkManager on the system bus)

bool QuickSettings::networkAvailable() const
{
    return m_nmPresent;
}

void QuickSettings::setupNetwork()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected())
        return;

    // NM may appear later (service start after resume).
    auto *watcher = new QDBusServiceWatcher(QString::fromLatin1(kNmService), bus,
                                            QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &newOwner) {
                m_nmPresent = !newOwner.isEmpty();
                if (m_nmPresent)
                    readNetwork();
                emit networkChanged();
            });

    m_nmPresent = bus.interface()->isServiceRegistered(QString::fromLatin1(kNmService));
    if (!m_nmPresent)
        return;

    readNetwork();
    bus.connect(QString::fromLatin1(kNmService), QString::fromLatin1(kNmPath),
                QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("PropertiesChanged"), this,
                SLOT(networkPropertiesChanged(QString, QVariantMap, QStringList)));
}

// State and WirelessEnabled in one asynchronous GetAll: NetworkManager
// being slow or restarting no longer stalls the panel.
void QuickSettings::readNetwork()
{
    QDBusMessage call = QDBusMessage::createMethodCall(QString::fromLatin1(kNmService),
                                                       QString::fromLatin1(kNmPath),
                                                       QStringLiteral("org.freedesktop.DBus.Properties"),
                                                       QStringLiteral("GetAll"));
    call << QString::fromLatin1(kNmInterface);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError())
            return;
        const QVariantMap properties = reply.value();
        const QVariant stateVariant = properties.value(QStringLiteral("State"));
        const QVariant wifiVariant = properties.value(QStringLiteral("WirelessEnabled"));
        const int state = stateVariant.isValid() ? stateVariant.toInt() : -1;
        const bool wifi = wifiVariant.isValid() ? wifiVariant.toBool() : m_wifiEnabled;
        if (state >= 0 && (state != m_networkState || wifi != m_wifiEnabled)) {
            m_networkState = state;
            m_wifiEnabled = wifi;
            setNetworkStatus(state, wifi);
            emit networkChanged();
        }
    });
}

void QuickSettings::setNetworkStatus(int state, bool wifiEnabled)
{
    m_networkStatus = networkStateText(state, wifiEnabled);
}

void QuickSettings::networkPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                             const QStringList &)
{
    if (interface != QString::fromLatin1(kNmInterface))
        return;
    if (changed.contains(QStringLiteral("State"))
        || changed.contains(QStringLiteral("WirelessEnabled"))
        || changed.contains(QStringLiteral("Connectivity")))
        readNetwork();
}

void QuickSettings::toggleWifi()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    QDBusMessage call = QDBusMessage::createMethodCall(
        QString::fromLatin1(kNmService), QString::fromLatin1(kNmPath),
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"));
    call << QString::fromLatin1(kNmInterface) << QStringLiteral("WirelessEnabled")
         << QVariant::fromValue(!m_wifiEnabled);
    bus.asyncCall(call);
    m_wifiEnabled = !m_wifiEnabled;
    m_networkStatus = networkStateText(m_networkState, m_wifiEnabled);
    emit networkChanged();
    QTimer::singleShot(600, this, &QuickSettings::readNetwork);
}

// ---------------------------------------------------------------------------
// Media (MPRIS on the session bus)

void QuickSettings::setupMedia()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;

    // NameOwnerChanged with arg0namespace matches the whole MPRIS family:
    // every player registers org.mpris.MediaPlayer2.<something>.
    bus.connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"),
                this, SLOT(dbusNameOwnerChanged(QString, QString, QString)));
    scanMediaPlayers();
}

void QuickSettings::dbusNameOwnerChanged(const QString &name, const QString &oldOwner,
                                         const QString &newOwner)
{
    if (!name.startsWith(QString::fromLatin1(kMprisPrefix)))
        return;
    if (!newOwner.isEmpty() && oldOwner.isEmpty())
        addMediaPlayer(name, newOwner);
    else if (newOwner.isEmpty() && !oldOwner.isEmpty())
        removeMediaPlayer(name);
}

void QuickSettings::scanMediaPlayers()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.interface())
        return;
    const QStringList names = bus.interface()->registeredServiceNames().value();
    for (const QString &name : names) {
        if (name.startsWith(QString::fromLatin1(kMprisPrefix)))
            addMediaPlayer(name);
    }
}

void QuickSettings::addMediaPlayer(const QString &service, const QString &owner)
{
    if (m_mediaPlayers.contains(service))
        return;
    m_mediaPlayers.append(service);
    QString unique = owner;
    if (unique.isEmpty() && QDBusConnection::sessionBus().interface())
        unique = QDBusConnection::sessionBus().interface()->serviceOwner(service).value();
    m_mediaOwners.insert(service, unique);
    QDBusConnection::sessionBus().connect(service, QString::fromLatin1(kMprisPath),
                                          QStringLiteral("org.freedesktop.DBus.Properties"),
                                          QStringLiteral("PropertiesChanged"), this,
                                          SLOT(mediaPropertiesChanged(QString, QVariantMap,
                                                                      QStringList)));
    readMediaPlayer(service);
    pickMedia();
}

void QuickSettings::removeMediaPlayer(const QString &service)
{
    if (!m_mediaPlayers.removeOne(service))
        return;
    m_mediaOwners.remove(service);
    m_playerStates.remove(service);
    QDBusConnection::sessionBus().disconnect(service, QString::fromLatin1(kMprisPath),
                                             QStringLiteral("org.freedesktop.DBus.Properties"),
                                             QStringLiteral("PropertiesChanged"), this,
                                             SLOT(mediaPropertiesChanged(QString, QVariantMap,
                                                                         QStringList)));
    pickMedia();
}

// Reads every property of one player without blocking (GetAll); the
// answer updates its cached state and the choice of player.
void QuickSettings::readMediaPlayer(const QString &service)
{
    QDBusMessage call = QDBusMessage::createMethodCall(service, QString::fromLatin1(kMprisPath),
                                                       QStringLiteral("org.freedesktop.DBus.Properties"),
                                                       QStringLiteral("GetAll"));
    call << QStringLiteral("org.mpris.MediaPlayer2.Player");
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, service](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError() || !m_mediaPlayers.contains(service))
            return;
        applyPlayerProperties(service, reply.value());
        pickMedia();
    });
}

void QuickSettings::applyPlayerProperties(const QString &service, const QVariantMap &properties)
{
    PlayerState &state = m_playerStates[service];
    if (properties.contains(QStringLiteral("Metadata"))) {
        const QVariantMap map = metadataMap(properties.value(QStringLiteral("Metadata")));
        state.title = variantString(map.value(QStringLiteral("xesam:title")));
        state.artist = variantString(map.value(QStringLiteral("xesam:artist")));
        // Only schemes an Image can load: players also send data or
        // private ones the panel cannot read.
        const QString art = variantString(map.value(QStringLiteral("mpris:artUrl"))).trimmed();
        const QString scheme = QUrl(art).scheme();
        state.artUrl = (scheme == QLatin1String("file") || scheme == QLatin1String("https")
                        || scheme == QLatin1String("http"))
                           ? art
                           : QString();
    }
    if (properties.contains(QStringLiteral("PlaybackStatus")))
        state.playing = variantString(properties.value(QStringLiteral("PlaybackStatus")))
                        == QLatin1String("Playing");
}

void QuickSettings::mediaPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                           const QStringList &)
{
    if (interface != QLatin1String("org.mpris.MediaPlayer2.Player"))
        return;
    // The sender is the player's unique name (":1.42"), not its
    // org.mpris.MediaPlayer2.<player> name.
    const QString sender = message().service();
    const QString service = m_mediaPlayers.contains(sender) ? sender : m_mediaOwners.key(sender);
    if (service.isEmpty())
        return;
    applyPlayerProperties(service, changed);
    pickMedia();
}

// The popup shows and controls one player: one that is playing, else the
// newest one.
QString QuickSettings::mediaPlayer() const
{
    return m_currentPlayer;
}

void QuickSettings::pickMedia()
{
    QString chosen;
    for (const QString &service : std::as_const(m_mediaPlayers)) {
        if (m_playerStates.value(service).playing)
            chosen = service;
    }
    if (chosen.isEmpty() && !m_mediaPlayers.isEmpty())
        chosen = m_mediaPlayers.constLast();
    const PlayerState state = m_playerStates.value(chosen);
    m_currentPlayer = chosen;
    m_mediaTitle = state.title;
    m_mediaArtist = state.artist;
    m_mediaArtUrl = state.artUrl;
    m_mediaPlaying = state.playing;
    emit mediaChanged();
}

void QuickSettings::callPlayer(const QString &method)
{
    const QString service = mediaPlayer();
    if (service.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(
        service, QString::fromLatin1(kMprisPath),
        QStringLiteral("org.mpris.MediaPlayer2.Player"), method);
    QDBusConnection::sessionBus().asyncCall(call);
    // Status follows shortly; the PropertiesChanged signal settles the rest.
    QTimer::singleShot(300, this, [this] {
        const QString current = mediaPlayer();
        if (!current.isEmpty())
            readMediaPlayer(current);
        emit mediaChanged();
    });
}

void QuickSettings::mediaPlayPause()
{
    callPlayer(QStringLiteral("PlayPause"));
}

void QuickSettings::mediaNext()
{
    callPlayer(QStringLiteral("Next"));
}

void QuickSettings::mediaPrevious()
{
    callPlayer(QStringLiteral("Previous"));
}
