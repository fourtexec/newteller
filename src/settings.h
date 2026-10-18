#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QSocketNotifier>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

#include "rustbridge.h"

// Persistent key/value configuration stored as JSON in
// ~/.config/newteller/shell.json. Keys are flat and dotted, e.g. "panel.height".
// Exposed to QML as the context property `Settings`.
//
// Writes are debounced (one atomic save shortly after the last change, with a
// `.bak` backup), reads are resilient (a corrupt file falls back to the
// backup), and the file is watched so edits made in a text editor apply
// without a restart.
class Settings : public QObject {
    Q_OBJECT
    // Full merged map (defaults + user file). Reading it from QML creates bindings
    // that update automatically when any value changes.
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)

public:
    explicit Settings(QObject *parent = nullptr);
    ~Settings() override;

    static QString configDir();
    static QString configFile();

    QVariantMap values() const { return m_values; }
    bool dirty() const { return m_dirty; }

    Q_INVOKABLE QVariant value(const QString &key, const QVariant &fallback = QVariant()) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void setValues(const QVariantMap &values);
    // Removes the user override so the key falls back to its default.
    Q_INVOKABLE void reset(const QString &key);
    Q_INVOKABLE bool has(const QString &key) const;
    Q_INVOKABLE QStringList keys() const;
    Q_INVOKABLE QVariantMap defaults() const { return m_defaults; }
    // The schema of shell.json: list of {key, kind, default, doc}.
    Q_INVOKABLE QVariantList schema() const;
    // Problems in the current configuration: list of {key, level, message}.
    Q_INVOKABLE QVariantList validate() const;
    Q_INVOKABLE QString configPath() const { return configFile(); }
    // Re-reads the file, discarding unsaved changes.
    Q_INVOKABLE void reload();
    // Writes pending changes immediately.
    Q_INVOKABLE void flush();

signals:
    void valuesChanged();
    void changed(const QString &key); // empty key = whole file reloaded
    void dirtyChanged();
    // A file inside the config directory changed on disk (path is absolute).
    void externalChange(const QString &path);

private:
    void load();
    void scheduleSave();
    bool writeBack();
    void startWatching();
    void handleWatchEvents();

    QVariantMap m_defaults;
    QVariantMap m_values;
    QString m_lastWritten; // file body of the last save, to ignore our own writes
    QTimer m_saveTimer;
    bool m_dirty = false;

    // Preferred: the Rust inotify watcher, driven by a socket notifier.
    std::unique_ptr<Rust::Watcher> m_watcher;
    QSocketNotifier *m_notifier = nullptr;
    QTimer m_debounceTimer;
    // Fallback: QFileSystemWatcher.
    QFileSystemWatcher m_fsWatcher;
};
