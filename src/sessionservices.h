#pragma once

#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariant>

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
    Q_PROPERTY(int workspace READ workspace NOTIFY workspacesChanged)
    Q_PROPERTY(int workspaceCount READ workspaceCount NOTIFY workspacesChanged)
    // Bit n set: desktop n has windows.
    Q_PROPERTY(int workspaceOccupied READ workspaceOccupied NOTIFY workspacesChanged)
    // Alt+Tab overlay: the windows (appId, title) in most-recently-used order
    // and the selected one; -1 while the switcher is closed.
    Q_PROPERTY(QVariantList switcherEntries READ switcherEntries NOTIFY switcherChanged)
    Q_PROPERTY(int switcherIndex READ switcherIndex NOTIFY switcherChanged)

public:
    explicit SessionServices(Settings *settings, QObject *parent = nullptr);
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
    int workspace() const { return m_workspace; }
    int workspaceCount() const { return m_workspaceCount; }
    int workspaceOccupied() const { return m_workspaceOccupied; }
    QVariantList switcherEntries() const { return m_switcherEntries; }
    int switcherIndex() const { return m_switcherIndex; }

    Q_INVOKABLE void switchWorkspace(int index);
    // Moves the focused window to another desktop.
    Q_INVOKABLE void moveToWorkspace(int index);
    Q_INVOKABLE void nextKeyboardLayout();
    Q_INVOKABLE void setKeyboardLayout(int index);
    Q_INVOKABLE void lock();
    Q_INVOKABLE void screenOff();
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
    QProcess m_idle;
    QProcess m_nightLight;
    QProcess m_polkit;
    QString m_polkitAgent;
    QString m_lastCompositorConfig;
};
