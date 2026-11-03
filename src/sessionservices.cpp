#include "sessionservices.h"

#include "settings.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QList>

#include <algorithm>
#include <QSet>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTime>
#include <QUrl>
#include <QDateTime>

#include <csignal>
#include <sys/prctl.h>

namespace {

struct AutostartEntry {
    QString file;
    QString name;
    QString exec;
    QString path;
};

// The [Desktop Entry] group of a .desktop file, unlocalised keys only.
QHash<QString, QString> readDesktopEntry(const QString &fileName)
{
    QHash<QString, QString> keys;
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return keys;
    bool inEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('['))) {
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inEntry)
            continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        const QString key = line.left(equals).trimmed();
        if (key.contains(QLatin1Char('[')))
            continue;
        keys.insert(key, line.mid(equals + 1).trimmed());
    }
    return keys;
}

bool listContains(const QString &list, const QString &desktop)
{
    const QStringList items = list.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &item : items) {
        if (item.trimmed().compare(desktop, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

// Exec without the field codes that make no sense at login (%f, %u, ...).
QString stripFieldCodes(const QString &exec)
{
    QString out;
    for (int i = 0; i < exec.size(); ++i) {
        if (exec.at(i) == QLatin1Char('%') && i + 1 < exec.size()) {
            if (exec.at(i + 1) == QLatin1Char('%'))
                out += QLatin1Char('%');
            ++i;
            continue;
        }
        out += exec.at(i);
    }
    return out.simplified();
}

// The XDG autostart entries for this session, user entries overriding
// system ones of the same file name (XDG Autostart specification).
QList<AutostartEntry> collectAutostart()
{
    QStringList directories;
    QString configHome = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (configHome.isEmpty())
        configHome = QDir::homePath() + QStringLiteral("/.config");
    directories << configHome + QStringLiteral("/autostart");
    QString configDirs = qEnvironmentVariable("XDG_CONFIG_DIRS");
    if (configDirs.isEmpty())
        configDirs = QStringLiteral("/etc/xdg");
    for (const QString &dir : configDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts))
        directories << dir + QStringLiteral("/autostart");

    QList<AutostartEntry> entries;
    QSet<QString> seen;
    const QString desktop = QStringLiteral("NewTeller");
    for (const QString &directory : directories) {
        const QStringList files = QDir(directory).entryList({QStringLiteral("*.desktop")},
                                                            QDir::Files, QDir::Name);
        for (const QString &name : files) {
            if (seen.contains(name))
                continue;
            seen.insert(name);
            const QString path = directory + QLatin1Char('/') + name;
            const QHash<QString, QString> keys = readDesktopEntry(path);
            if (keys.value(QStringLiteral("Type"), QStringLiteral("Application"))
                != QLatin1String("Application"))
                continue;
            if (keys.value(QStringLiteral("Hidden")) == QLatin1String("true"))
                continue;
            if (keys.value(QStringLiteral("X-GNOME-Autostart-enabled")) == QLatin1String("false"))
                continue;
            const QString onlyShowIn = keys.value(QStringLiteral("OnlyShowIn"));
            if (!onlyShowIn.isEmpty() && !listContains(onlyShowIn, desktop))
                continue;
            if (listContains(keys.value(QStringLiteral("NotShowIn")), desktop))
                continue;
            const QString tryExec = keys.value(QStringLiteral("TryExec"));
            if (!tryExec.isEmpty() && QStandardPaths::findExecutable(tryExec).isEmpty()
                && !QFileInfo(tryExec).isExecutable())
                continue;
            const QString exec = stripFieldCodes(keys.value(QStringLiteral("Exec")));
            if (exec.isEmpty())
                continue;
            // Never start a second shell (newteller-autostart.desktop is for
            // running NewTeller inside another desktop).
            const QString program = QProcess::splitCommand(exec).value(0);
            if (QFileInfo(program).fileName() == QLatin1String("newteller"))
                continue;
            entries.append({path, keys.value(QStringLiteral("Name"), name), exec,
                            keys.value(QStringLiteral("Path"))});
        }
    }
    return entries;
}

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

    // The helpers die with the shell: when a crashed shell is restarted by
    // the compositor, the new one starts its own, without duplicates.
    for (QProcess *process : {&m_idle, &m_nightLight, &m_polkit}) {
        process->setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
    }

    connect(m_settings, &Settings::changed, this, &SessionServices::settingChanged);

    writeCompositorConfig();
    writeLockConfig();
    connectCompositor();
    readPowerProfile();
    if (inSession()) {
        restartIdle();
        restartNightLight();
        startPolkitAgent();
        applyAppearance();
        // Give the panel (and its tray watcher) a moment to come up, so tray
        // applications find a StatusNotifierWatcher when they start.
        QTimer::singleShot(1500, this, &SessionServices::runAutostart);
    }
}

void SessionServices::setTheme(QObject *theme)
{
    m_theme = theme;
    if (!m_theme)
        return;
    // Title bars that follow the theme are redrawn with it; the lock screen
    // ring takes the accent.
    connect(m_theme, SIGNAL(themeChanged()), &m_configDebounce, SLOT(start()));
    m_configDebounce.start();
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
                // <id>\x1e<app id>\x1e<title>
                const QStringList fields = entry.split(QChar(0x1e));
                QVariantMap item;
                item.insert(QStringLiteral("id"), fields.value(0).toInt());
                item.insert(QStringLiteral("appId"), fields.value(1));
                item.insert(QStringLiteral("title"), fields.value(2));
                m_switcherEntries.append(item);
            }
        }
        emit switcherChanged();
    } else if (line.startsWith(QLatin1String("key "))) {
        emit mediaKey(line.mid(4).trimmed());
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

QString SessionServices::soundSettingsTool() const
{
    for (const char *tool : {"pavucontrol", "pwvucontrol", "pavucontrol-qt"}) {
        if (!findProgram(QString::fromLatin1(tool)).isEmpty())
            return QString::fromLatin1(tool);
    }
    return QString();
}

void SessionServices::openSoundSettings()
{
    const QString tool = soundSettingsTool();
    if (!tool.isEmpty())
        QProcess::startDetached(tool, {});
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
        || key.startsWith(QLatin1String("workspaces.")) || key.startsWith(QLatin1String("decorations."))
        || key.startsWith(QLatin1String("shortcuts.")) || key.startsWith(QLatin1String("switcher."))
        || key.startsWith(QLatin1String("lock.")) || key == QLatin1String("windows.focusFollowsMouse")
        || key == QLatin1String("windows.placement") || key == QLatin1String("windows.snapping")
        || key.startsWith(QLatin1String("mouse.")) || key.startsWith(QLatin1String("cursor."))
        || key == QLatin1String("desktop.wallpaper")) {
        m_configDebounce.start();
    }
    if (inSession() && (all || key.startsWith(QLatin1String("apps."))
                        || key.startsWith(QLatin1String("cursor."))))
        applyAppearance();
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
    text += confLine(QStringLiteral("workspaces.wrap"), boolText(value("workspaces.wrap")));

    // Title bars drawn by the compositor.
    text += confLine(QStringLiteral("decorations.server"), boolText(value("decorations.server")));
    text += confLine(QStringLiteral("decorations.titleHeight"),
                     QString::number(qBound(16, value("decorations.titleHeight").toInt(), 64)));
    text += confLine(QStringLiteral("decorations.border"),
                     QString::number(qBound(0, value("decorations.border").toInt(), 8)));
    text += confLine(QStringLiteral("decorations.buttonsLeft"), boolText(value("decorations.buttonsLeft")));
    text += confLine(QStringLiteral("decorations.titleCentered"),
                     boolText(value("decorations.titleCentered")));
    text += confLine(QStringLiteral("decorations.showMinimize"), boolText(value("decorations.showMinimize")));
    text += confLine(QStringLiteral("decorations.showMaximize"), boolText(value("decorations.showMaximize")));
    QString font = value("decorations.font").toString().trimmed();
    QString activeBackground = value("decorations.activeBackground").toString();
    QString activeForeground = value("decorations.activeForeground").toString();
    QString inactiveBackground = value("decorations.inactiveBackground").toString();
    QString inactiveForeground = value("decorations.inactiveForeground").toString();
    if (value("decorations.followTheme").toBool() && m_theme) {
        auto themeColor = [this](const char *name) {
            return m_theme->property(name).value<QColor>().name(QColor::HexRgb);
        };
        activeBackground = themeColor("surface");
        activeForeground = themeColor("foreground");
        inactiveBackground = themeColor("background");
        inactiveForeground = themeColor("muted");
        if (font.isEmpty()) {
            font = m_theme->property("fontFamily").toString() + QStringLiteral(" Bold ")
                   + QString::number(qMax(8, m_theme->property("fontSize").toInt()));
        }
    }
    if (!font.isEmpty())
        text += confLine(QStringLiteral("decorations.font"), font);
    text += confLine(QStringLiteral("decorations.activeBackground"), activeBackground);
    text += confLine(QStringLiteral("decorations.activeForeground"), activeForeground);
    text += confLine(QStringLiteral("decorations.inactiveBackground"), inactiveBackground);
    text += confLine(QStringLiteral("decorations.inactiveForeground"), inactiveForeground);
    text += confLine(QStringLiteral("decorations.closeHover"), value("decorations.closeHover").toString());

    // Window behaviour.
    text += confLine(QStringLiteral("windows.focusFollowsMouse"),
                     boolText(value("windows.focusFollowsMouse")));
    text += confLine(QStringLiteral("windows.placement"), value("windows.placement").toString());
    text += confLine(QStringLiteral("windows.snapping"), boolText(value("windows.snapping")));
    text += confLine(QStringLiteral("switcher.allWorkspaces"), boolText(value("switcher.allWorkspaces")));

    // The user's shortcuts: [{keys, command}].
    const QVariantList shortcuts = value("shortcuts.custom").toList();
    int index = 0;
    for (const QVariant &item : shortcuts) {
        const QVariantMap shortcut = item.toMap();
        QString keys = shortcut.value(QStringLiteral("keys")).toString().trimmed();
        QString command = shortcut.value(QStringLiteral("command")).toString().trimmed();
        keys.remove(QLatin1Char('|'));
        if (keys.isEmpty() || command.isEmpty())
            continue;
        text += confLine(QStringLiteral("bind.%1").arg(index++), keys + QLatin1Char('|') + command);
    }
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
    if (!m_settings->value(QStringLiteral("lock.useWallpaper")).toBool())
        image.clear();
    const QString path = Settings::configDir() + QStringLiteral("/lock.conf");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QString text = QStringLiteral("# Written by the NewTeller shell for newteller-lock.\n");
    if (!image.isEmpty())
        text += QStringLiteral("image=") + image + QLatin1Char('\n');
    const QString lockColor = m_settings->value(QStringLiteral("lock.color")).toString();
    if (QColor::isValidColorName(lockColor))
        text += QStringLiteral("color=") + QColor(lockColor).name(QColor::HexRgb).mid(1) + QLatin1Char('\n');
    if (m_theme) {
        text += QStringLiteral("accent=")
                + m_theme->property("accent").value<QColor>().name(QColor::HexRgb).mid(1)
                + QLatin1Char('\n');
    }
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

QVariantList SessionServices::autostartEntries() const
{
    QVariantList list;
    for (const AutostartEntry &entry : collectAutostart()) {
        QVariantMap item;
        item.insert(QStringLiteral("name"), entry.name);
        item.insert(QStringLiteral("file"), entry.file);
        item.insert(QStringLiteral("exec"), entry.exec);
        list.append(item);
    }
    return list;
}

// GTK, libadwaita, Firefox and Chromium read the dark/light preference and
// the cursor from org.gnome.desktop.interface (directly or through the
// settings portal), so that is where the choice goes.
void SessionServices::applyAppearance()
{
    const QString gsettings = findProgram(QStringLiteral("gsettings"));
    if (gsettings.isEmpty())
        return;
    const QString schema = QStringLiteral("org.gnome.desktop.interface");
    const QString scheme = m_settings->value(QStringLiteral("apps.colorScheme")).toString();
    if (scheme == QLatin1String("dark")) {
        QProcess::startDetached(gsettings, {QStringLiteral("set"), schema,
                                            QStringLiteral("color-scheme"), QStringLiteral("prefer-dark")});
        QProcess::startDetached(gsettings, {QStringLiteral("set"), schema,
                                            QStringLiteral("gtk-theme"), QStringLiteral("Adwaita-dark")});
    } else if (scheme == QLatin1String("light")) {
        QProcess::startDetached(gsettings, {QStringLiteral("set"), schema,
                                            QStringLiteral("color-scheme"), QStringLiteral("prefer-light")});
        QProcess::startDetached(gsettings, {QStringLiteral("set"), schema,
                                            QStringLiteral("gtk-theme"), QStringLiteral("Adwaita")});
    }
    auto setKey = [&](const QString &key, const QString &value) {
        QProcess::startDetached(gsettings, {QStringLiteral("set"), schema, key, value});
    };
    const QString gtkTheme = m_settings->value(QStringLiteral("apps.gtkTheme")).toString().trimmed();
    if (!gtkTheme.isEmpty())
        setKey(QStringLiteral("gtk-theme"), gtkTheme);
    const QString iconTheme = m_settings->value(QStringLiteral("apps.iconTheme")).toString().trimmed();
    if (!iconTheme.isEmpty())
        setKey(QStringLiteral("icon-theme"), iconTheme);
    const QString appFont = m_settings->value(QStringLiteral("apps.font")).toString().trimmed();
    if (!appFont.isEmpty())
        setKey(QStringLiteral("font-name"), appFont);
    const double textScale = m_settings->value(QStringLiteral("apps.textScale")).toDouble();
    if (textScale >= 0.5 && textScale <= 3.0)
        setKey(QStringLiteral("text-scaling-factor"), QString::number(textScale, 'f', 2));
    const QString cursorTheme = m_settings->value(QStringLiteral("cursor.theme")).toString().trimmed();
    if (!cursorTheme.isEmpty()) {
        QProcess::startDetached(gsettings, {QStringLiteral("set"), schema,
                                            QStringLiteral("cursor-theme"), cursorTheme});
    }
    const int cursorSize = m_settings->value(QStringLiteral("cursor.size")).toInt();
    if (cursorSize >= 8) {
        QProcess::startDetached(gsettings, {QStringLiteral("set"), schema,
                                            QStringLiteral("cursor-size"),
                                            QString::number(cursorSize)});
    }
}

void SessionServices::readPowerProfile()
{
    const QString tool = findProgram(QStringLiteral("powerprofilesctl"));
    if (tool.isEmpty())
        return;
    // Asynchronous: the daemon may take a moment to answer on a cold boot.
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        const QString current = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        process->deleteLater();
        if (current.isEmpty())
            return;
        m_powerProfiles = {QStringLiteral("power-saver"), QStringLiteral("balanced"),
                           QStringLiteral("performance")};
        if (current != m_powerProfile || m_powerProfiles.isEmpty()) {
            m_powerProfile = current;
        }
        emit powerProfileChanged();
    });
    process->start(tool, {QStringLiteral("get")});
}

void SessionServices::setPowerProfile(const QString &profile)
{
    const QString tool = findProgram(QStringLiteral("powerprofilesctl"));
    if (tool.isEmpty() || !m_powerProfiles.contains(profile))
        return;
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        process->deleteLater();
        readPowerProfile();
    });
    process->start(tool, {QStringLiteral("set"), profile});
}

// ---------------------------------------------------------------------------
// Wi-Fi (nmcli)
// ---------------------------------------------------------------------------

namespace {

// nmcli -t escapes ':' and '\\' inside fields with a backslash.
QStringList splitTerse(const QString &line)
{
    QStringList fields;
    QString current;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('\\') && i + 1 < line.size()) {
            current += line.at(++i);
        } else if (c == QLatin1Char(':')) {
            fields << current;
            current.clear();
        } else {
            current += c;
        }
    }
    fields << current;
    return fields;
}

} // namespace

bool SessionServices::wifiAvailable() const
{
    return !findProgram(QStringLiteral("nmcli")).isEmpty();
}

void SessionServices::scanWifi()
{
    const QString nmcli = findProgram(QStringLiteral("nmcli"));
    if (nmcli.isEmpty() || m_wifiBusy)
        return;
    m_wifiBusy = true;
    emit wifiChanged();
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        process->deleteLater();
        QHash<QString, QVariantMap> best;
        const QStringList lines = QString::fromUtf8(process->readAllStandardOutput())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList f = splitTerse(line);
            if (f.size() < 4 || f.at(1).isEmpty())
                continue;
            QVariantMap network;
            network.insert(QStringLiteral("active"), f.at(0) == QLatin1String("*"));
            network.insert(QStringLiteral("ssid"), f.at(1));
            network.insert(QStringLiteral("signal"), f.at(2).toInt());
            const QString security = f.at(3).trimmed();
            network.insert(QStringLiteral("secure"),
                           !security.isEmpty() && security != QLatin1String("--"));
            // One row per name: the active access point, else the strongest.
            const QVariantMap seen = best.value(f.at(1));
            if (seen.isEmpty() || network.value(QStringLiteral("active")).toBool()
                || (!seen.value(QStringLiteral("active")).toBool()
                    && network.value(QStringLiteral("signal")).toInt()
                           > seen.value(QStringLiteral("signal")).toInt())) {
                best.insert(f.at(1), network);
            }
        }
        QList<QVariantMap> sorted = best.values();
        std::sort(sorted.begin(), sorted.end(), [](const QVariantMap &a, const QVariantMap &b) {
            if (a.value(QStringLiteral("active")).toBool() != b.value(QStringLiteral("active")).toBool())
                return a.value(QStringLiteral("active")).toBool();
            return a.value(QStringLiteral("signal")).toInt() > b.value(QStringLiteral("signal")).toInt();
        });
        m_wifiNetworks.clear();
        for (const QVariantMap &network : sorted)
            m_wifiNetworks.append(network);
        m_wifiBusy = false;
        emit wifiChanged();
    });
    process->start(nmcli, {QStringLiteral("-t"), QStringLiteral("-f"),
                           QStringLiteral("IN-USE,SSID,SIGNAL,SECURITY"),
                           QStringLiteral("device"), QStringLiteral("wifi"),
                           QStringLiteral("list"), QStringLiteral("--rescan"),
                           QStringLiteral("auto")});
}

void SessionServices::connectWifi(const QString &ssid, const QString &password)
{
    const QString nmcli = findProgram(QStringLiteral("nmcli"));
    if (nmcli.isEmpty() || ssid.isEmpty())
        return;
    m_wifiBusy = true;
    emit wifiChanged();
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this, [this, process, ssid](int code) {
        const QString output = QString::fromUtf8(process->readAll()).trimmed();
        process->deleteLater();
        m_wifiBusy = false;
        emit wifiChanged();
        emit wifiConnectFinished(code == 0, ssid,
                                 output.section(QLatin1Char('\n'), -1).trimmed());
        scanWifi();
    });
    // --ask reads the password from stdin, so it never appears in the
    // process list the way `password <secret>` on the command line would.
    QStringList args;
    if (!password.isEmpty())
        args << QStringLiteral("--ask");
    args << QStringLiteral("device") << QStringLiteral("wifi") << QStringLiteral("connect") << ssid;
    process->start(nmcli, args);
    if (!password.isEmpty()) {
        process->write(password.toUtf8() + '\n');
    }
    process->closeWriteChannel();
}

void SessionServices::disconnectWifi()
{
    const QString nmcli = findProgram(QStringLiteral("nmcli"));
    if (nmcli.isEmpty())
        return;
    for (const QVariant &item : std::as_const(m_wifiNetworks)) {
        const QVariantMap network = item.toMap();
        if (!network.value(QStringLiteral("active")).toBool())
            continue;
        auto *process = new QProcess(this);
        connect(process, &QProcess::finished, this, [this, process] {
            process->deleteLater();
            scanWifi();
        });
        process->start(nmcli, {QStringLiteral("connection"), QStringLiteral("down"),
                               QStringLiteral("id"),
                               network.value(QStringLiteral("ssid")).toString()});
        return;
    }
}

// ---------------------------------------------------------------------------
// Synchronous queries (overview, displays)
// ---------------------------------------------------------------------------

QString SessionServices::query(const QString &command) const
{
    const QString path = qEnvironmentVariable("NEWTELLER_IPC");
    if (path.isEmpty())
        return QString();
    QLocalSocket socket;
    socket.connectToServer(path);
    if (!socket.waitForConnected(300))
        return QString();
    socket.write(command.toUtf8() + '\n');
    socket.flush();
    QByteArray data;
    while (!data.contains('\n') && socket.waitForReadyRead(2000))
        data += socket.readAll();
    return QString::fromUtf8(data).trimmed();
}

QVariantList SessionServices::windows() const
{
    QVariantList list;
    const QString reply = query(QStringLiteral("windows"));
    if (!reply.startsWith(QLatin1String("ok")))
        return list;
    const QStringList entries = reply.mid(3).split(QChar(0x1f), Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        const QStringList f = entry.split(QChar(0x1e));
        if (f.size() < 5)
            continue;
        QVariantMap window;
        window.insert(QStringLiteral("id"), f.at(0).toInt());
        window.insert(QStringLiteral("workspace"), f.at(1).toInt());
        window.insert(QStringLiteral("appId"), f.at(2));
        window.insert(QStringLiteral("title"), f.at(3));
        window.insert(QStringLiteral("focused"), f.at(4).contains(QLatin1Char('f')));
        window.insert(QStringLiteral("minimized"), f.at(4).contains(QLatin1Char('m')));
        window.insert(QStringLiteral("maximized"), f.at(4).contains(QLatin1Char('x')));
        list.append(window);
    }
    return list;
}

QString SessionServices::thumbnail(int id, int size)
{
    const QString reply = query(QStringLiteral("thumbnail %1 %2").arg(id).arg(size));
    if (!reply.startsWith(QLatin1String("ok ")))
        return QString();
    // A new query string each time: QML caches images by URL.
    return QUrl::fromLocalFile(reply.mid(3)).toString() + QStringLiteral("?v=")
           + QString::number(++m_thumbnailSerial);
}

QString SessionServices::cachedThumbnail(int id) const
{
    const QString path = qEnvironmentVariable("XDG_RUNTIME_DIR")
                         + QStringLiteral("/newteller-thumbs/%1.png").arg(id);
    if (!QFileInfo::exists(path))
        return QString();
    return QUrl::fromLocalFile(path).toString() + QStringLiteral("?v=")
           + QString::number(QFileInfo(path).lastModified().toMSecsSinceEpoch());
}

void SessionServices::activateWindow(int id)
{
    query(QStringLiteral("activate %1").arg(id));
}

void SessionServices::closeWindow(int id)
{
    query(QStringLiteral("close %1").arg(id));
}

void SessionServices::moveWindow(int id, int workspace)
{
    query(QStringLiteral("move-window %1 %2").arg(id).arg(workspace));
}

QVariantList SessionServices::outputs() const
{
    QVariantList list;
    const QString reply = query(QStringLiteral("outputs-detail"));
    if (!reply.startsWith(QLatin1String("ok")))
        return list;
    const QStringList entries = reply.mid(3).split(QChar(0x1f), Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        const QStringList f = entry.split(QChar(0x1e));
        if (f.size() < 8)
            continue;
        QVariantMap output;
        output.insert(QStringLiteral("name"), f.at(0));
        output.insert(QStringLiteral("description"), f.at(1).trimmed());
        output.insert(QStringLiteral("enabled"), f.at(2) == QLatin1String("on"));
        const QString mode = f.at(3);
        output.insert(QStringLiteral("width"), mode.section(QLatin1Char('x'), 0, 0).toInt());
        output.insert(QStringLiteral("height"),
                      mode.section(QLatin1Char('x'), 1).section(QLatin1Char('@'), 0, 0).toInt());
        output.insert(QStringLiteral("refresh"), mode.section(QLatin1Char('@'), 1).toInt());
        output.insert(QStringLiteral("x"), f.at(4).section(QLatin1Char(','), 0, 0).toInt());
        output.insert(QStringLiteral("y"), f.at(4).section(QLatin1Char(','), 1).toInt());
        output.insert(QStringLiteral("scale"), f.at(5).toDouble());
        output.insert(QStringLiteral("transform"), f.at(6).toInt());
        QVariantList modes;
        for (const QString &item : f.at(7).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            QVariantMap m;
            m.insert(QStringLiteral("width"), item.section(QLatin1Char('x'), 0, 0).toInt());
            m.insert(QStringLiteral("height"),
                     item.section(QLatin1Char('x'), 1).section(QLatin1Char('@'), 0, 0).toInt());
            m.insert(QStringLiteral("refresh"), item.section(QLatin1Char('@'), 1).toInt());
            modes.append(m);
        }
        output.insert(QStringLiteral("modes"), modes);
        list.append(output);
    }
    return list;
}

QString SessionServices::setOutput(const QString &name, bool enabled, int width, int height,
                                   int refresh, int x, int y, double scale, int transform)
{
    if (name.contains(QLatin1Char(' ')) || name.isEmpty())
        return QStringLiteral("unknown screen");
    const QString reply = query(QStringLiteral("output-set %1 %2 %3x%4@%5 %6 %7 %8 %9")
                                    .arg(name, enabled ? QStringLiteral("on") : QStringLiteral("off"))
                                    .arg(width).arg(height).arg(refresh).arg(x).arg(y)
                                    .arg(scale, 0, 'f', 2).arg(transform));
    if (reply == QLatin1String("ok"))
        return QString();
    if (reply.isEmpty())
        return QStringLiteral("The compositor did not answer.");
    return reply.startsWith(QLatin1String("error ")) ? reply.mid(6) : reply;
}

namespace {

QStringList dataDirectories(const QString &sub)
{
    QStringList dirs{QDir::homePath() + QStringLiteral("/.local/share/") + sub,
                     QDir::homePath() + QLatin1Char('/') + QLatin1Char('.') + sub};
    QString dataDirs = qEnvironmentVariable("XDG_DATA_DIRS");
    if (dataDirs.isEmpty())
        dataDirs = QStringLiteral("/usr/local/share:/usr/share");
    for (const QString &dir : dataDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts))
        dirs << dir + QLatin1Char('/') + sub;
    return dirs;
}

QStringList themesWith(const QString &sub, const QString &marker)
{
    QStringList names;
    for (const QString &dir : dataDirectories(sub)) {
        const QStringList entries = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &entry : entries) {
            if (!names.contains(entry) && QFileInfo::exists(dir + QLatin1Char('/') + entry + marker))
                names << entry;
        }
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

} // namespace

QStringList SessionServices::gtkThemes() const
{
    return themesWith(QStringLiteral("themes"), QStringLiteral("/gtk-3.0"));
}

QStringList SessionServices::iconThemes() const
{
    QStringList names;
    for (const QString &name : themesWith(QStringLiteral("icons"), QStringLiteral("/index.theme"))) {
        // Cursor-only themes also have an index.theme; skip them.
        bool hasIcons = false;
        for (const QString &dir : dataDirectories(QStringLiteral("icons"))) {
            const QStringList subdirs = QDir(dir + QLatin1Char('/') + name)
                                            .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            if (subdirs.size() > (subdirs.contains(QStringLiteral("cursors")) ? 1 : 0)) {
                hasIcons = true;
                break;
            }
        }
        if (hasIcons && name != QLatin1String("default"))
            names << name;
    }
    return names;
}

QStringList SessionServices::cursorThemes() const
{
    return themesWith(QStringLiteral("icons"), QStringLiteral("/cursors"));
}

void SessionServices::runAutostart()
{
    for (const AutostartEntry &entry : collectAutostart()) {
        QStringList args = QProcess::splitCommand(entry.exec);
        if (args.isEmpty())
            continue;
        const QString program = args.takeFirst();
        // nm-applet only shows up in a StatusNotifier tray in indicator mode;
        // its stock autostart entry asks for the old X11 tray icon.
        if (QFileInfo(program).fileName() == QLatin1String("nm-applet")
            && !args.contains(QLatin1String("--indicator")))
            args << QStringLiteral("--indicator");
        QProcess::startDetached(program, args, entry.path);
    }
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
