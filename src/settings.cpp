#include "settings.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDebug>

QString Settings::configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/newteller");
}

QString Settings::configFile()
{
    return configDir() + QStringLiteral("/shell.json");
}

Settings::Settings(QObject *parent)
    : QObject(parent)
{
    m_defaults.insert(QStringLiteral("panel.height"), 48);
    m_defaults.insert(QStringLiteral("panel.clockFormat"), QStringLiteral("HH:mm"));
    m_defaults.insert(QStringLiteral("panel.pinned"),
                      QStringList{QStringLiteral("org.gnome.Terminal.desktop"),
                                  QStringLiteral("firefox-esr.desktop"),
                                  QStringLiteral("org.gnome.Nautilus.desktop")});
    m_defaults.insert(QStringLiteral("startmenu.width"), 440);
    m_defaults.insert(QStringLiteral("startmenu.height"), 580);
    m_defaults.insert(QStringLiteral("theme.name"), QStringLiteral("default"));
    m_defaults.insert(QStringLiteral("desktop.wallpaper"), QString());
    m_defaults.insert(QStringLiteral("icons.path"), QStringLiteral("~/iconkit/icons"));
    m_defaults.insert(QStringLiteral("icons.set"), QStringLiteral("lucide"));
    m_defaults.insert(QStringLiteral("plugins.disabled"), QStringList());
    m_defaults.insert(QStringLiteral("apps.recent"), QStringList());

    load();

    // Pick up edits made in a text editor while the shell is running.
    if (QFile::exists(configFile()))
        m_watcher.addPath(configFile());
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        if (QFile::exists(configFile()) && !m_watcher.files().contains(configFile()))
            m_watcher.addPath(configFile()); // editors often replace the file
        load();
        emit changed(QString());
    });
}

void Settings::load()
{
    QVariantMap merged = m_defaults;

    QFile file(configFile());
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            const QVariantMap user = doc.object().toVariantMap();
            for (auto it = user.cbegin(); it != user.cend(); ++it)
                merged.insert(it.key(), it.value());
        } else {
            qWarning() << "newteller: invalid settings file" << configFile() << err.errorString();
        }
    }

    m_values = merged;
    emit valuesChanged();
}

void Settings::save() const
{
    QDir().mkpath(configDir());
    QFile file(configFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "newteller: cannot write" << configFile();
        return;
    }
    const QJsonObject obj = QJsonObject::fromVariantMap(m_values);
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

QVariant Settings::value(const QString &key, const QVariant &fallback) const
{
    return m_values.value(key, fallback);
}

void Settings::setValue(const QString &key, const QVariant &value)
{
    if (m_values.value(key) == value)
        return;
    m_values.insert(key, value);
    save();

    if (!m_watcher.files().contains(configFile()) && QFile::exists(configFile()))
        m_watcher.addPath(configFile());

    emit valuesChanged();
    emit changed(key);
}
