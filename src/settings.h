#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QVariantMap>

// Persistent key/value configuration stored as JSON in
// ~/.config/newteller/shell.json. Keys are flat and dotted, e.g. "panel.height".
// Exposed to QML as the context property `Settings`.
class Settings : public QObject {
    Q_OBJECT
    // Full merged map (defaults + user file). Reading it from QML creates bindings
    // that update automatically when any value changes.
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)

public:
    explicit Settings(QObject *parent = nullptr);

    static QString configDir();
    static QString configFile();

    QVariantMap values() const { return m_values; }

    Q_INVOKABLE QVariant value(const QString &key, const QVariant &fallback = QVariant()) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE QString configPath() const { return configFile(); }

signals:
    void valuesChanged();
    void changed(const QString &key); // empty key = whole file reloaded

private:
    void load();
    void save() const;

    QVariantMap m_defaults;
    QVariantMap m_values;
    QFileSystemWatcher m_watcher;
};
