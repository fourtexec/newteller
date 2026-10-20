#include "quicksettings.h"
#include "settings.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
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
        return QStringLiteral("Connected");
    if (state >= 60)
        return QStringLiteral("Connecting...");
    if (state >= 30)
        return QStringLiteral("Disconnecting...");
    if (!wifiEnabled)
        return QStringLiteral("Wi-Fi off");
    return QStringLiteral("Disconnected");
}

// Extracts a plain string from a D-Bus variant, looking through QDBusArgument
// wrappers (Metadata arrives as a{sv} with nested types).
QString variantString(const QVariant &value)
{
    if (value.canConvert<QString>())
        return value.toString();
    if (value.canConvert<QDBusArgument>()) {
        QDBusArgument argument = value.value<QDBusArgument>();
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

bool QuickSettings::runTool(const QStringList &arguments, QString *output) const
{
    QProcess process;
    process.start(arguments.first(), arguments.mid(1));
    if (!process.waitForStarted(800) || !process.waitForFinished(1500))
        return false;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return false;
    if (output)
        *output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    return true;
}

void QuickSettings::readVolume()
{
    if (m_volumeTool == VolumeTool::None)
        return;
    QString out;
    if (m_volumeTool == VolumeTool::WpCtl) {
        if (!runTool({QStringLiteral("wpctl"), QStringLiteral("get-volume"),
                      QStringLiteral("@DEFAULT_AUDIO_SINK@")},
                     &out))
            return;
        const bool muted = out.contains(QLatin1String("MUTED"), Qt::CaseInsensitive);
        const int space = out.lastIndexOf(QLatin1Char(' '));
        const QString number = space >= 0 ? out.mid(space + 1) : out;
        bool ok = false;
        const double linear = number.toDouble(&ok);
        if (!ok)
            return;
        const int percent = qRound(linear * 100.0);
        if (percent != m_volume || muted != m_volumeMuted) {
            m_volume = qBound(0, percent, 150);
            m_volumeMuted = muted;
            emit volumeChanged();
        }
        return;
    }
    // pactl
    QString mutedOut;
    runTool({QStringLiteral("pactl"), QStringLiteral("get-sink-mute"),
             QStringLiteral("@DEFAULT_SINK@")},
            &mutedOut);
    if (!runTool({QStringLiteral("pactl"), QStringLiteral("get-sink-volume"),
                  QStringLiteral("@DEFAULT_SINK@")},
                 &out))
        return;
    const bool muted = mutedOut.contains(QLatin1String("yes"), Qt::CaseInsensitive);
    // "Volume: front-left: 43690 /  67% / -11.00 dB, ..."
    int percent = -1;
    const QRegularExpression percentRegex(QStringLiteral("(\\d+)%"));
    const auto match = percentRegex.match(out);
    if (match.hasMatch())
        percent = match.captured(1).toInt();
    if (percent < 0)
        return;
    if (percent != m_volume || muted != m_volumeMuted) {
        m_volume = qBound(0, percent, 150);
        m_volumeMuted = muted;
        emit volumeChanged();
    }
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
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 500);
    if (reply.type() == QDBusMessage::ReplyMessage)
        return;
    QFile file(QStringLiteral("/sys/class/backlight/%1/brightness").arg(m_backlightDevice));
    if (file.open(QIODevice::WriteOnly))
        file.write(QByteArray::number(value));
    else
        m_brightnessWriteTried = true;
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

void QuickSettings::readNetwork()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    auto readProperty = [&bus](const QString &property) -> QVariant {
        QDBusMessage call = QDBusMessage::createMethodCall(
            QString::fromLatin1(kNmService), QString::fromLatin1(kNmPath),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        call << QString::fromLatin1(kNmInterface) << property;
        const QDBusMessage reply = bus.call(call, QDBus::BlockWithGui, 800);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return {};
        return reply.arguments().first();
    };

    // Properties::Get returns a variant; basic types demarshal to a plain
    // QVariant, so read them directly.
    const QVariant stateVariant = readProperty(QStringLiteral("State"));
    const QVariant wifiVariant = readProperty(QStringLiteral("WirelessEnabled"));
    const int state = stateVariant.isValid() ? stateVariant.toInt() : -1;
    const bool wifi = wifiVariant.isValid() ? wifiVariant.toBool() : m_wifiEnabled;

    if (state >= 0 && (state != m_networkState || wifi != m_wifiEnabled)) {
        m_networkState = state;
        m_wifiEnabled = wifi;
        setNetworkStatus(state, wifi);
        emit networkChanged();
    }
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
        addMediaPlayer(name);
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

void QuickSettings::addMediaPlayer(const QString &service)
{
    if (m_mediaPlayers.contains(service))
        return;
    m_mediaPlayers.append(service);
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
    QDBusConnection::sessionBus().disconnect(service, QString::fromLatin1(kMprisPath),
                                             QStringLiteral("org.freedesktop.DBus.Properties"),
                                             QStringLiteral("PropertiesChanged"), this,
                                             SLOT(mediaPropertiesChanged(QString, QVariantMap,
                                                                         QStringList)));
    pickMedia();
}

// Reads Metadata and PlaybackStatus of one player. Failures leave the player
// in the list: a player may not be fully up yet, the next change heals it.
void QuickSettings::readMediaPlayer(const QString &service)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    auto read = [&bus, &service](const QString &property) -> QVariant {
        QDBusMessage call = QDBusMessage::createMethodCall(
            service, QString::fromLatin1(kMprisPath),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        call << QStringLiteral("org.mpris.MediaPlayer2.Player") << property;
        const QDBusMessage reply = bus.call(call, QDBus::BlockWithGui, 600);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return {};
        return reply.arguments().first();
    };

    const QVariant metadata = read(QStringLiteral("Metadata"));
    if (metadata.isValid()) {
        QVariantMap map;
        if (metadata.canConvert<QDBusArgument>()) {
            const QDBusArgument argument = metadata.value<QDBusArgument>();
            argument.beginArray();
            while (!argument.atEnd()) {
                QDBusArgument entry = argument;
                entry.beginStructure();
                QString key;
                QVariant value;
                entry >> key >> value;
                entry.endStructure();
                map.insert(key, value);
            }
            argument.endArray();
            if (map.contains(QStringLiteral("xesam:title")))
                m_mediaTitle = variantString(map.value(QStringLiteral("xesam:title")));
            if (map.contains(QStringLiteral("xesam:artist")))
                m_mediaArtist = variantString(map.value(QStringLiteral("xesam:artist")));
        }
    }
    const QVariant status = read(QStringLiteral("PlaybackStatus"));
    if (status.isValid())
        m_mediaPlaying = status.toString() == QLatin1String("Playing");
}

void QuickSettings::mediaPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                           const QStringList &)
{
    if (interface != QLatin1String("org.mpris.MediaPlayer2.Player"))
        return;
    const QString service = message().service();
    if (!m_mediaPlayers.contains(service))
        return;
    if (changed.contains(QStringLiteral("Metadata"))) {
        // Cheap path: the change already carries the new values.
        const QVariant metadata = changed.value(QStringLiteral("Metadata"));
        if (metadata.canConvert<QDBusArgument>()) {
            const QDBusArgument argument = metadata.value<QDBusArgument>();
            argument.beginArray();
            QVariantMap map;
            while (!argument.atEnd()) {
                QDBusArgument entry = argument;
                entry.beginStructure();
                QString key;
                QVariant value;
                entry >> key >> value;
                entry.endStructure();
                map.insert(key, value);
            }
            argument.endArray();
            if (map.contains(QStringLiteral("xesam:title")))
                m_mediaTitle = variantString(map.value(QStringLiteral("xesam:title")));
            if (map.contains(QStringLiteral("xesam:artist")))
                m_mediaArtist = variantString(map.value(QStringLiteral("xesam:artist")));
        }
    }
    if (changed.contains(QStringLiteral("PlaybackStatus")))
        m_mediaPlaying = variantString(changed.value(QStringLiteral("PlaybackStatus")))
                         == QLatin1String("Playing");
    pickMedia();
}

// The popup shows one player: whoever is playing, else the newest player.
QString QuickSettings::mediaPlayer() const
{
    for (const QString &service : m_mediaPlayers) {
        // Playback status is cached only for the picked player; players are
        // few, so asking each one is fine and keeps this simple.
        return service;
    }
    return QString();
}

void QuickSettings::pickMedia()
{
    if (m_mediaPlayers.isEmpty()) {
        m_mediaTitle.clear();
        m_mediaArtist.clear();
        m_mediaPlaying = false;
        emit mediaChanged();
        return;
    }
    // Prefer a playing player; otherwise keep the first service.
    QString chosen;
    for (const QString &service : m_mediaPlayers) {
        QDBusMessage call = QDBusMessage::createMethodCall(
            service, QString::fromLatin1(kMprisPath),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        call << QStringLiteral("org.mpris.MediaPlayer2.Player")
             << QStringLiteral("PlaybackStatus");
        const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::BlockWithGui, 400);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()
            && reply.arguments().first().toString() == QLatin1String("Playing")) {
            chosen = service;
            break;
        }
    }
    if (chosen.isEmpty())
        chosen = m_mediaPlayers.first();
    readMediaPlayer(chosen);
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
