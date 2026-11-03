#include "sessionservices.h"

#include "settings.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTime>
#include <QUrl>

namespace {

QString boolText(const QVariant &value)
{
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
}

// One line of compositor.conf; newlines would start a new key, so drop them.
QString confLine(const QString &key, const QString &value)
{
    QString clean = value;
    clean.remove(QLatin1Char('\n'));
    clean.remove(QLatin1Char('\r'));
    return key + QStringLiteral(" = ") + clean + QLatin1Char('\n');
}

bool inSession()
{
    return qEnvironmentVariable("NEWTELLER_SESSION") == QLatin1String("1");
}

QString validTime(const QString &text, const QString &fallback)
{
    const QTime time = QTime::fromString(text.trimmed(), QStringLiteral("HH:mm"));
    return time.isValid() ? time.toString(QStringLiteral("HH:mm")) : fallback;
}

} // namespace

SessionServices::SessionServices(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_reconnect.setInterval(2000);
    connect(&m_reconnect, &QTimer::timeout, this, &SessionServices::connectCompositor);

    // Sliders write many values in a row: batch them.
    m_configDebounce.setSingleShot(true);
    m_configDebounce.setInterval(250);
    connect(&m_configDebounce, &QTimer::timeout, this, [this] {
        writeCompositorConfig();
        writeLockConfig();
    });
    m_idleDebounce.setSingleShot(true);
    m_idleDebounce.setInterval(400);
    connect(&m_idleDebounce, &QTimer::timeout, this, &SessionServices::restartIdle);
    m_nightDebounce.setSingleShot(true);
    m_nightDebounce.setInterval(400);
    connect(&m_nightDebounce, &QTimer::timeout, this, &SessionServices::restartNightLight);

    connect(&m_socket, &QLocalSocket::readyRead, this, &SessionServices::readCompositor);
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        m_reconnect.stop();
        m_buffer.clear();
        sendCommand(QStringLiteral("watch"));
        emit compositorChanged();
    });
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        emit compositorChanged();
        if (!m_reconnect.isActive())
            m_reconnect.start();
    });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this] {
        if (!m_reconnect.isActive())
            m_reconnect.start();
    });
    connect(&m_nightLight, &QProcess::stateChanged, this, [this] { emit nightLightChanged(); });

    connect(m_settings, &Settings::changed, this, &SessionServices::settingChanged);

    writeCompositorConfig();
    writeLockConfig();
    connectCompositor();
    if (inSession()) {
        restartIdle();
        restartNightLight();
        startPolkitAgent();
    }
}

SessionServices::~SessionServices()
{
    for (QProcess *process : {&m_idle, &m_nightLight, &m_polkit}) {
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(500))
                process->kill();
        }
    }
}

bool SessionServices::compositorConnected() const
{
    return m_socket.state() == QLocalSocket::ConnectedState;
}

QString SessionServices::findProgram(const QString &name)
{
    return QStandardPaths::findExecutable(name);
}

bool SessionServices::lockAvailable() const
{
    return !findProgram(QStringLiteral("swaylock")).isEmpty();
}

bool SessionServices::idleAvailable() const
{
    return !findProgram(QStringLiteral("swayidle")).isEmpty();
}

bool SessionServices::nightLightAvailable() const
{
    return !findProgram(QStringLiteral("wlsunset")).isEmpty();
}

bool SessionServices::nightLightRunning() const
{
    return m_nightLight.state() != QProcess::NotRunning;
}

QString SessionServices::displaySettingsTool() const
{
    return findProgram(QStringLiteral("wdisplays")).isEmpty() ? QString()
                                                               : QStringLiteral("wdisplays");
}

// ---------------------------------------------------------------------------
// Compositor connection
// ---------------------------------------------------------------------------

void SessionServices::connectCompositor()
{
    const QString path = qEnvironmentVariable("NEWTELLER_IPC");
    if (path.isEmpty()) {
        m_reconnect.stop();
        return;
    }
    if (m_socket.state() != QLocalSocket::UnconnectedState)
        return;
    m_socket.connectToServer(path);
}

void SessionServices::sendCommand(const QString &command)
{
    if (!compositorConnected())
        return;
    m_socket.write(command.toUtf8() + '\n');
    m_socket.flush();
}

void SessionServices::readCompositor()
{
    m_buffer += m_socket.readAll();
    int newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_buffer.left(newline)).trimmed();
        m_buffer.remove(0, newline + 1);
        if (line.startsWith(QLatin1String("event ")))
            handleEvent(line.mid(6));
    }
    if (m_buffer.size() > 65536)
        m_buffer.clear();
}

void SessionServices::handleEvent(const QString &line)
{
    if (line.startsWith(QLatin1String("layout "))) {
        // layout <index> <name>|<name>|...
        const QString rest = line.mid(7);
        const int space = rest.indexOf(QLatin1Char(' '));
        const int index = (space < 0 ? rest : rest.left(space)).toInt();
        const QStringList names = space < 0 ? QStringList()
                                            : rest.mid(space + 1).split(QLatin1Char('|'),
                                                                        Qt::SkipEmptyParts);
        if (names != m_layouts || index != m_layout) {
            m_layouts = names;
            m_layout = index;
            emit keyboardChanged();
        }
    } else if (line.startsWith(QLatin1String("workspace "))) {
        // workspace <current> <count> <occupied mask>
        const QStringList parts = line.mid(10).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 3) {
            const int current = parts.at(0).toInt();
            const int count = qMax(1, parts.at(1).toInt());
            const int occupied = int(parts.at(2).toUInt());
            if (current != m_workspace || count != m_workspaceCount
                || occupied != m_workspaceOccupied) {
                m_workspace = current;
                m_workspaceCount = count;
                m_workspaceOccupied = occupied;
                emit workspacesChanged();
            }
        }
    } else if (line.startsWith(QLatin1String("switcher "))) {
        // switcher <selected> <appId>\x1e<title>\x1f<appId>\x1e<title>...
        const QString rest = line.mid(9);
        const int space = rest.indexOf(QLatin1Char(' '));
        m_switcherIndex = (space < 0 ? rest : rest.left(space)).toInt();
        m_switcherEntries.clear();
        if (m_switcherIndex >= 0 && space >= 0) {
            const QStringList entries = rest.mid(space + 1).split(QChar(0x1f), Qt::SkipEmptyParts);
            for (const QString &entry : entries) {
                const int separator = entry.indexOf(QChar(0x1e));
                QVariantMap item;
                item.insert(QStringLiteral("appId"), separator < 0 ? entry : entry.left(separator));
                item.insert(QStringLiteral("title"), separator < 0 ? QString() : entry.mid(separator + 1));
                m_switcherEntries.append(item);
            }
        }
        emit switcherChanged();
    } else if (line.startsWith(QLatin1String("locked "))) {
        const bool locked = line.mid(7).trimmed() == QLatin1String("1");
        if (locked != m_locked) {
            m_locked = locked;
            emit lockedChanged();
        }
    }
}

// ---------------------------------------------------------------------------
// Keyboard layouts
// ---------------------------------------------------------------------------

QString SessionServices::keyboardLayoutName() const
{
    if (m_layout < 0 || m_layout >= m_layouts.size())
        return QString();
    return layoutDisplayName(m_layouts.at(m_layout));
}

QString SessionServices::keyboardLayoutLabel() const
{
    if (m_layout < 0 || m_layout >= m_layouts.size())
        return QString();
    const QString code = m_layouts.at(m_layout).trimmed().toLower();
    if (code == QLatin1String("us") || code == QLatin1String("gb"))
        return QStringLiteral("EN");
    if (code.size() <= 3 && !code.contains(QLatin1Char(' ')))
        return code.toUpper();
    // A full xkb name ("English (US)") when no codes are configured.
    return code.left(2).toUpper();
}

QString SessionServices::layoutDisplayName(const QString &code) const
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("us"), QStringLiteral("English (US)")},
        {QStringLiteral("gb"), QStringLiteral("English (UK)")},
        {QStringLiteral("ru"), QStringLiteral("Russian")},
        {QStringLiteral("ua"), QStringLiteral("Ukrainian")},
        {QStringLiteral("by"), QStringLiteral("Belarusian")},
        {QStringLiteral("kz"), QStringLiteral("Kazakh")},
        {QStringLiteral("de"), QStringLiteral("German")},
        {QStringLiteral("fr"), QStringLiteral("French")},
        {QStringLiteral("es"), QStringLiteral("Spanish")},
        {QStringLiteral("it"), QStringLiteral("Italian")},
        {QStringLiteral("pl"), QStringLiteral("Polish")},
        {QStringLiteral("cz"), QStringLiteral("Czech")},
        {QStringLiteral("pt"), QStringLiteral("Portuguese")},
        {QStringLiteral("br"), QStringLiteral("Portuguese (Brazil)")},
        {QStringLiteral("tr"), QStringLiteral("Turkish")},
        {QStringLiteral("gr"), QStringLiteral("Greek")},
        {QStringLiteral("il"), QStringLiteral("Hebrew")},
        {QStringLiteral("ara"), QStringLiteral("Arabic")},
        {QStringLiteral("jp"), QStringLiteral("Japanese")},
        {QStringLiteral("cn"), QStringLiteral("Chinese")},
        {QStringLiteral("kr"), QStringLiteral("Korean")},
        {QStringLiteral("ge"), QStringLiteral("Georgian")},
        {QStringLiteral("am"), QStringLiteral("Armenian")},
        {QStringLiteral("se"), QStringLiteral("Swedish")},
        {QStringLiteral("fi"), QStringLiteral("Finnish")},
        {QStringLiteral("no"), QStringLiteral("Norwegian")},
        {QStringLiteral("dk"), QStringLiteral("Danish")},
        {QStringLiteral("nl"), QStringLiteral("Dutch")},
    };
    const QString key = code.trimmed().toLower();
    return names.value(key, code);
}

void SessionServices::switchWorkspace(int index)
{
    sendCommand(QStringLiteral("workspace %1").arg(index));
}

void SessionServices::moveToWorkspace(int index)
{
    sendCommand(QStringLiteral("move-to-workspace %1").arg(index));
}

void SessionServices::nextKeyboardLayout()
{
    sendCommand(QStringLiteral("layout next"));
}

void SessionServices::setKeyboardLayout(int index)
{
    sendCommand(QStringLiteral("layout %1").arg(index));
}

// ---------------------------------------------------------------------------
// Lock, blanking, logout
// ---------------------------------------------------------------------------

void SessionServices::lock()
{
    if (compositorConnected()) {
        sendCommand(QStringLiteral("lock"));
        return;
    }
    if (QProcess::startDetached(QStringLiteral("newteller-lock"), {}))
        return;
    QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("lock-session")});
}

void SessionServices::screenOff()
{
    sendCommand(QStringLiteral("dpms off"));
}

void SessionServices::openDisplaySettings()
{
    const QString tool = displaySettingsTool();
    if (!tool.isEmpty())
        QProcess::startDetached(tool, {});
}

void SessionServices::logout()
{
    if (compositorConnected()) {
        sendCommand(QStringLiteral("exit"));
        return;
    }
    QProcess::startDetached(QStringLiteral("loginctl"),
                            {QStringLiteral("terminate-session"),
                             qEnvironmentVariable("XDG_SESSION_ID")});
}

// ---------------------------------------------------------------------------
// Settings -> compositor.conf, lock.conf and the helper processes
// ---------------------------------------------------------------------------

void SessionServices::settingChanged(const QString &key)
{
    const bool all = key.isEmpty();
    if (all || key.startsWith(QLatin1String("keyboard.")) || key.startsWith(QLatin1String("touchpad."))
        || key.startsWith(QLatin1String("workspaces."))
        || key.startsWith(QLatin1String("mouse.")) || key.startsWith(QLatin1String("cursor."))
        || key == QLatin1String("desktop.wallpaper")) {
        m_configDebounce.start();
    }
    if (inSession() && (all || key.startsWith(QLatin1String("power."))))
        m_idleDebounce.start();
    if (inSession() && (all || key.startsWith(QLatin1String("nightLight."))))
        m_nightDebounce.start();
}

QString SessionServices::compositorConfigText() const
{
    auto value = [this](const char *key) {
        return m_settings->value(QString::fromLatin1(key));
    };
    QString text = QStringLiteral(
        "# Written by the NewTeller shell from shell.json; edit the settings\n"
        "# there (Settings -> Keyboard, Mouse & touchpad), not this file.\n");

    const QString layouts = value("keyboard.layouts").toString().simplified().remove(QLatin1Char(' '));
    if (!layouts.isEmpty()) {
        text += confLine(QStringLiteral("keyboard.layout"), layouts);
        text += confLine(QStringLiteral("keyboard.variant"),
                         value("keyboard.variants").toString().simplified().remove(QLatin1Char(' ')));
        QStringList options;
        const QString switchKeys = value("keyboard.switchKeys").toString().trimmed();
        if (!switchKeys.isEmpty())
            options << switchKeys;
        const QString extra = value("keyboard.options").toString().simplified().remove(QLatin1Char(' '));
        if (!extra.isEmpty())
            options << extra;
        text += confLine(QStringLiteral("keyboard.options"), options.join(QLatin1Char(',')));
    }
    text += confLine(QStringLiteral("keyboard.repeatRate"),
                     QString::number(qBound(1, value("keyboard.repeatRate").toInt(), 100)));
    text += confLine(QStringLiteral("keyboard.repeatDelay"),
                     QString::number(qBound(100, value("keyboard.repeatDelay").toInt(), 2000)));
    text += confLine(QStringLiteral("keyboard.numlock"), boolText(value("keyboard.numlock")));
    text += confLine(QStringLiteral("touchpad.tapToClick"), boolText(value("touchpad.tapToClick")));
    text += confLine(QStringLiteral("touchpad.naturalScroll"), boolText(value("touchpad.naturalScroll")));
    text += confLine(QStringLiteral("touchpad.disableWhileTyping"),
                     boolText(value("touchpad.disableWhileTyping")));
    text += confLine(QStringLiteral("touchpad.speed"),
                     QString::number(qBound(-1.0, value("touchpad.speed").toDouble(), 1.0), 'f', 2));
    text += confLine(QStringLiteral("mouse.naturalScroll"), boolText(value("mouse.naturalScroll")));
    text += confLine(QStringLiteral("mouse.speed"),
                     QString::number(qBound(-1.0, value("mouse.speed").toDouble(), 1.0), 'f', 2));
    text += confLine(QStringLiteral("mouse.leftHanded"), boolText(value("mouse.leftHanded")));
    const QString cursorTheme = value("cursor.theme").toString().trimmed();
    if (!cursorTheme.isEmpty())
        text += confLine(QStringLiteral("cursor.theme"), cursorTheme);
    text += confLine(QStringLiteral("cursor.size"),
                     QString::number(qBound(8, value("cursor.size").toInt(), 256)));
    text += confLine(QStringLiteral("workspaces.count"),
                     QString::number(qBound(1, value("workspaces.count").toInt(), 9)));
    return text;
}

void SessionServices::writeCompositorConfig()
{
    const QString text = compositorConfigText();
    if (text == m_lastCompositorConfig)
        return;
    const QString path = Settings::configDir() + QStringLiteral("/compositor.conf");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    file.write(text.toUtf8());
    if (!file.commit())
        return;
    const bool first = m_lastCompositorConfig.isEmpty();
    m_lastCompositorConfig = text;
    // The compositor reads the file itself at start; later edits need a nudge.
    if (!first)
        sendCommand(QStringLiteral("reload"));
}

void SessionServices::writeLockConfig()
{
    QString image = m_settings->value(QStringLiteral("desktop.wallpaper")).toString();
    if (image.startsWith(QLatin1String("file://")))
        image = QUrl(image).toLocalFile();
    if (image.startsWith(QLatin1String("~/")))
        image = QDir::homePath() + image.mid(1);
    if (!image.isEmpty() && !QFileInfo(image).isFile())
        image.clear();
    const QString path = Settings::configDir() + QStringLiteral("/lock.conf");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QString text = QStringLiteral("# Written by the NewTeller shell for newteller-lock.\n");
    if (!image.isEmpty())
        text += QStringLiteral("image=") + image + QLatin1Char('\n');
    file.write(text.toUtf8());
    file.commit();
}

void SessionServices::restartIdle()
{
    if (m_idle.state() != QProcess::NotRunning) {
        m_idle.terminate();
        if (!m_idle.waitForFinished(1000))
            m_idle.kill();
    }
    const QString swayidle = findProgram(QStringLiteral("swayidle"));
    if (swayidle.isEmpty())
        return;

    const int lockMinutes = m_settings->value(QStringLiteral("power.lockAfter")).toInt();
    const int offMinutes = m_settings->value(QStringLiteral("power.screenOffAfter")).toInt();
    const bool lockOnSleep = m_settings->value(QStringLiteral("power.lockOnSleep")).toBool();

    // -w: before-sleep waits for the locker, so the lock screen is already
    // up when the machine wakes.
    QStringList args{QStringLiteral("-w")};
    if (lockMinutes > 0) {
        args << QStringLiteral("timeout") << QString::number(lockMinutes * 60)
             << QStringLiteral("newteller-lock");
    }
    if (offMinutes > 0) {
        args << QStringLiteral("timeout") << QString::number(offMinutes * 60)
             << QStringLiteral("newteller-compositor --msg dpms off")
             << QStringLiteral("resume") << QStringLiteral("newteller-compositor --msg dpms on");
    }
    if (lockOnSleep)
        args << QStringLiteral("before-sleep") << QStringLiteral("newteller-lock");
    // `loginctl lock-session` (and unlock-session) from anywhere.
    args << QStringLiteral("lock") << QStringLiteral("newteller-lock")
         << QStringLiteral("unlock") << QStringLiteral("pkill -USR1 -x swaylock");
    m_idle.start(swayidle, args);
}

void SessionServices::restartNightLight()
{
    if (m_nightLight.state() != QProcess::NotRunning) {
        m_nightLight.terminate();
        if (!m_nightLight.waitForFinished(1000))
            m_nightLight.kill();
    }
    if (!m_settings->value(QStringLiteral("nightLight.enabled")).toBool())
        return;
    const QString wlsunset = findProgram(QStringLiteral("wlsunset"));
    if (wlsunset.isEmpty())
        return;
    const int temperature = qBound(1500, m_settings->value(QStringLiteral("nightLight.temperature")).toInt(), 6000);
    const QString schedule = m_settings->value(QStringLiteral("nightLight.schedule")).toString();
    QStringList args{QStringLiteral("-t"), QString::number(temperature)};
    if (schedule == QLatin1String("always")) {
        // Day and night at (almost) the same temperature: always warm.
        args << QStringLiteral("-T") << QString::number(temperature + 1);
    } else {
        args << QStringLiteral("-T") << QStringLiteral("6500")
             << QStringLiteral("-s")
             << validTime(m_settings->value(QStringLiteral("nightLight.from")).toString(),
                          QStringLiteral("20:00"))
             << QStringLiteral("-S")
             << validTime(m_settings->value(QStringLiteral("nightLight.to")).toString(),
                          QStringLiteral("07:00"));
    }
    m_nightLight.start(wlsunset, args);
}

void SessionServices::startPolkitAgent()
{
    // The first agent that is installed; a second agent in the same session
    // simply fails to register and exits.
    const QStringList candidates{
        QStringLiteral("/usr/bin/lxpolkit"),
        QStringLiteral("/usr/libexec/polkit-mate-authentication-agent-1"),
        QStringLiteral("/usr/lib/x86_64-linux-gnu/polkit-mate/polkit-mate-authentication-agent-1"),
        QStringLiteral("/usr/libexec/polkit-gnome-authentication-agent-1"),
        QStringLiteral("/usr/lib/policykit-1-gnome/polkit-gnome-authentication-agent-1"),
        QStringLiteral("/usr/lib/x86_64-linux-gnu/libexec/polkit-kde-authentication-agent-1"),
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo(candidate).isExecutable()) {
            m_polkitAgent = candidate;
            m_polkit.start(candidate, {});
            return;
        }
    }
}
