#pragma once

#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QHash>
#include <QVariant>
#include <QMap>
#include <QSet>
#include <QMetaType>

// a{sa{sv}}: NetworkManager connection settings.
typedef QMap<QString, QVariantMap> NMSettingsMap;
Q_DECLARE_METATYPE(NMSettingsMap)

class Settings;

// The desktop services around the compositor, exposed to QML as `Session`.
//
// * Talks to newteller-compositor over its control socket ($NEWTELLER_IPC):
//   keyboard layout indicator and switching, locking, screen blanking.
// * Mirrors the input settings of shell.json (keyboard.*, touchpad.*,
//   mouse.*, cursor.*) into compositor.conf and asks the compositor to reload.
// * Runs the idle manager (swayidle: lock and blank after a timeout, lock
//   before suspend), the night light (wlsunset) and a polkit agent, each
//   only when the program is installed.
class SessionServices : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool compositorConnected READ compositorConnected NOTIFY compositorChanged)
    Q_PROPERTY(QStringList keyboardLayouts READ keyboardLayouts NOTIFY keyboardChanged)
    Q_PROPERTY(int keyboardLayout READ keyboardLayout NOTIFY keyboardChanged)
    Q_PROPERTY(QString keyboardLayoutLabel READ keyboardLayoutLabel NOTIFY keyboardChanged)
    Q_PROPERTY(QString keyboardLayoutName READ keyboardLayoutName NOTIFY keyboardChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged)
    Q_PROPERTY(bool lockAvailable READ lockAvailable CONSTANT)
    Q_PROPERTY(bool idleAvailable READ idleAvailable CONSTANT)
    Q_PROPERTY(bool nightLightAvailable READ nightLightAvailable CONSTANT)
    Q_PROPERTY(bool nightLightRunning READ nightLightRunning NOTIFY nightLightChanged)
    Q_PROPERTY(QString polkitAgent READ polkitAgent CONSTANT)
    Q_PROPERTY(QString displaySettingsTool READ displaySettingsTool CONSTANT)
    // The sound mixer for output/input devices (pavucontrol), if installed.
    Q_PROPERTY(QString soundSettingsTool READ soundSettingsTool CONSTANT)
    Q_PROPERTY(int workspace READ workspace NOTIFY workspacesChanged)
    Q_PROPERTY(int workspaceCount READ workspaceCount NOTIFY workspacesChanged)
    // Bit n set: desktop n has windows.
    Q_PROPERTY(int workspaceOccupied READ workspaceOccupied NOTIFY workspacesChanged)
    // Alt+Tab overlay: the windows (appId, title) in most-recently-used order
    // and the selected one; -1 while the switcher is closed.
    Q_PROPERTY(QVariantList switcherEntries READ switcherEntries NOTIFY switcherChanged)
    Q_PROPERTY(int switcherIndex READ switcherIndex NOTIFY switcherChanged)
    // power-profiles-daemon: "power-saver", "balanced", "performance"; empty
    // when the daemon is not installed.
    Q_PROPERTY(QString powerProfile READ powerProfile NOTIFY powerProfileChanged)
    Q_PROPERTY(QStringList powerProfiles READ powerProfiles NOTIFY powerProfileChanged)
    // Wi-Fi networks in range (NetworkManager): ssid, signal (0-100), secure,
    // active. wifiStatus explains why there are none (no NetworkManager, no
    // adapter, missing firmware, switched off); empty when Wi-Fi works.
    Q_PROPERTY(bool wifiAvailable READ wifiAvailable NOTIFY wifiChanged)
    Q_PROPERTY(QString wifiStatus READ wifiStatus NOTIFY wifiChanged)
    Q_PROPERTY(QVariantList wifiNetworks READ wifiNetworks NOTIFY wifiChanged)
    Q_PROPERTY(bool wifiBusy READ wifiBusy NOTIFY wifiChanged)
    // Ids of the windows that stopped answering the compositor's pings.
    Q_PROPERTY(QVariantList hungWindows READ hungWindows NOTIFY hungWindowsChanged)

public:
    explicit SessionServices(Settings *settings, QObject *parent = nullptr);
    // The shell theme: title bars can follow its colours and font.
    void setTheme(QObject *theme);
    ~SessionServices() override;

    bool compositorConnected() const;
    QStringList keyboardLayouts() const { return m_layouts; }
    int keyboardLayout() const { return m_layout; }
    QString keyboardLayoutLabel() const;
    QString keyboardLayoutName() const;
    bool locked() const { return m_locked; }
    bool lockAvailable() const;
    bool idleAvailable() const;
    bool nightLightAvailable() const;
    bool nightLightRunning() const;
    QString polkitAgent() const { return m_polkitAgent; }
    QString displaySettingsTool() const;
    QString soundSettingsTool() const;
    Q_INVOKABLE void openSoundSettings();
    int workspace() const { return m_workspace; }
    int workspaceCount() const { return m_workspaceCount; }
    int workspaceOccupied() const { return m_workspaceOccupied; }
    QVariantList switcherEntries() const { return m_switcherEntries; }
    int switcherIndex() const { return m_switcherIndex; }
    QString powerProfile() const { return m_powerProfile; }
    QStringList powerProfiles() const { return m_powerProfiles; }
    Q_INVOKABLE void setPowerProfile(const QString &profile);

    // --- windows (overview) ---
    // Every window, most recent first: id, workspace, appId, title,
    // focused, minimized, maximized.
    Q_INVOKABLE QVariantList windows() const;
    // A fresh thumbnail of the window as a file:// URL ("" when the
    // compositor could not read it back).
    Q_INVOKABLE QString thumbnail(int id, int size = 320);
    // file:// URL of the thumbnail the compositor wrote for Alt+Tab.
    Q_INVOKABLE QString cachedThumbnail(int id) const;
    // Blur behind a shell surface (layer namespace on a screen), in surface
    // coordinates. `owner` identifies the QML window, so a window sharing
    // the namespace cannot clear another one's region.
    Q_INVOKABLE void setBlur(const QString &owner, const QString &scope, const QString &screen,
                             int x, int y, int width, int height, int radius);
    Q_INVOKABLE void clearBlur(const QString &owner, const QString &scope, const QString &screen);
    Q_INVOKABLE void activateWindow(int id);
    Q_INVOKABLE void closeWindow(int id);
    Q_INVOKABLE void moveWindow(int id, int workspace);
    // Force quit: kills the process behind the window. "" on success, else
    // the reason.
    Q_INVOKABLE QString killWindow(int id);
    QVariantList hungWindows() const;

    // --- displays ---
    // name, description, enabled, width, height, refresh (mHz), x, y,
    // scale, transform, modes [{width, height, refresh}].
    Q_INVOKABLE QVariantList outputs() const;
    // Returns "" on success, else the reason.
    Q_INVOKABLE QString setOutput(const QString &name, bool enabled, int width, int height,
                                  int refresh, int x, int y, double scale, int transform);

    // --- installed themes, for the appearance pickers ---
    Q_INVOKABLE QStringList gtkThemes() const;
    Q_INVOKABLE QStringList iconThemes() const;
    Q_INVOKABLE QStringList cursorThemes() const;
    bool wifiAvailable() const;
    QString wifiStatus() const { return m_wifiStatus; }
    QVariantList wifiNetworks() const { return m_wifiNetworks; }
    bool wifiBusy() const { return m_wifiBusy; }
    Q_INVOKABLE void scanWifi();
    // Joins a network; `password` may be empty for open or saved networks.
    Q_INVOKABLE void connectWifi(const QString &ssid, const QString &password);
    Q_INVOKABLE void disconnectWifi();

    Q_INVOKABLE void switchWorkspace(int index);
    // Moves the focused window to another desktop.
    Q_INVOKABLE void moveToWorkspace(int index);
    Q_INVOKABLE void nextKeyboardLayout();
    Q_INVOKABLE void setKeyboardLayout(int index);
    Q_INVOKABLE void lock();
    Q_INVOKABLE void screenOff();
    // Keeps the screen awake (org.freedesktop.ScreenSaver inhibitions); the
    // compositor drops it with the connection, so it is sent again after a
    // reconnect.
    void setIdleInhibited(bool inhibited);
    Q_INVOKABLE void openDisplaySettings();
    // Ends the session through the compositor (falls back to loginctl).
    Q_INVOKABLE void logout();
    // XDG autostart entries that apply to this session (name, file, exec),
    // for the settings page.
    Q_INVOKABLE QVariantList autostartEntries() const;
    // Human-readable name for a layout code ("ru" -> "Russian").
    Q_INVOKABLE QString layoutDisplayName(const QString &code) const;

    // The text written to compositor.conf for the current settings; public
    // so the result is easy to check.
    QString compositorConfigText() const;

signals:
    void compositorChanged();
    void keyboardChanged();
    void lockedChanged();
    void nightLightChanged();
    void workspacesChanged();
    void switcherChanged();
    // A volume/brightness/media key the compositor handed to the shell:
    // volume-up, volume-down, mute, mic-mute, brightness-up,
    // brightness-down, play-pause, next, previous.
    void mediaKey(const QString &name);
    void powerProfileChanged();
    void wifiChanged();
    // The outcome of connectWifi(); `message` explains a failure.
    void wifiConnectFinished(bool ok, const QString &ssid, const QString &message);
    // A mouse button went down anywhere (layout coordinates): open cards
    // close when it is outside them.
    void pointerPressed(int x, int y);
    void hungWindowsChanged();
    // A window stopped responding (the force-quit dialog offers to end it).
    void windowHung(int id);

private:
    void connectCompositor();
    void sendCommand(const QString &command);
    void readCompositor();
    void handleEvent(const QString &line);
    void settingChanged(const QString &key);
    void writeCompositorConfig();
    void writeLockConfig();
    void restartIdle();
    void restartNightLight();
    void startPolkitAgent();
    void runAutostart();
    void applyAppearance();
    QString query(const QString &command) const;
    void readPowerProfile();
    QString wifiDevice();
    void readAccessPoints();
    static QString findProgram(const QString &name);

    Settings *m_settings;
    QLocalSocket m_socket;
    QTimer m_reconnect;
    QTimer m_configDebounce;
    QTimer m_idleDebounce;
    QTimer m_nightDebounce;
    QByteArray m_buffer;
    QStringList m_layouts;
    int m_layout = 0;
    bool m_locked = false;
    int m_workspace = 0;
    int m_workspaceCount = 1;
    int m_workspaceOccupied = 0;
    QVariantList m_switcherEntries;
    int m_switcherIndex = -1;
    QString m_powerProfile;
    QStringList m_powerProfiles;
    QVariantList m_wifiNetworks;
    bool m_wifiBusy = false;
    QString m_wifiStatus;
    QSet<int> m_hung;
    QProcess m_idle;
    QProcess m_nightLight;
    QProcess m_polkit;
    QString m_polkitAgent;
    QString m_lastCompositorConfig;
    QObject *m_theme = nullptr;
    int m_thumbnailSerial = 0;
    // scope|screen -> owner and the last region sent, to skip repeats and to
    // send them again after the compositor connection comes back.
    QHash<QString, QString> m_blurOwners;
    QHash<QString, QString> m_blurCommands;
    bool m_idleInhibited = false;
};
