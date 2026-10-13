#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVector>

class Settings;

// Discovers plugins: each is a directory with a manifest.json and a QML entry file.
// Search order: $NEWTELLER_PLUGIN_PATH, ~/.config/newteller/plugins, /usr/share/newteller/plugins.
// Exposed to QML as the context property `Plugins`.
class PluginManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)

public:
    explicit PluginManager(Settings *settings, QObject *parent = nullptr);

    QVariantList plugins() const;

    Q_INVOKABLE QVariantList ofType(const QString &type) const;
    Q_INVOKABLE void setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void rescan();

signals:
    void pluginsChanged();

private:
    QStringList searchPaths() const;
    QStringList disabledIds() const;

    Settings *m_settings;
    QVector<QVariantMap> m_plugins;
};
