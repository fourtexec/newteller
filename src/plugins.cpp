#include "plugins.h"
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUrl>
#include <QtDebug>
#include <algorithm>

PluginManager::PluginManager(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    rescan();
    connect(settings, &Settings::changed, this, [this](const QString &key) {
        if (key.isEmpty() || key == QLatin1String("plugins.disabled"))
            rebuildCache();
    });
    // plugins.autoRescan: a new or edited plugin applies without a restart.
    connect(settings, &Settings::externalChange, this, [this](const QString &path) {
        if (!m_settings->value(QStringLiteral("plugins.autoRescan"), true).toBool())
            return;
        for (const QString &root : searchPaths()) {
            if (path.startsWith(root)) {
                rescan();
                return;
            }
        }
    });
}

QStringList PluginManager::searchPaths() const
{
    QStringList paths;
    const QString env = qEnvironmentVariable("NEWTELLER_PLUGIN_PATH");
    if (!env.isEmpty())
        paths += env.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    paths << Settings::configDir() + QStringLiteral("/plugins");
    paths << QStringLiteral("/usr/share/newteller/plugins");
    return paths;
}

QStringList PluginManager::disabledIds() const
{
    return m_settings->value(QStringLiteral("plugins.disabled")).toStringList();
}

void PluginManager::rescan()
{
    m_plugins.clear();
    QSet<QString> seen;

    for (const QString &root : searchPaths()) {
        const QDir dir(root);
        const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &sub : subdirs) {
            const QString pluginDir = dir.filePath(sub);
            QFile manifest(pluginDir + QStringLiteral("/manifest.json"));
            if (!manifest.open(QIODevice::ReadOnly))
                continue;
            QJsonParseError error;
            const QJsonDocument doc = QJsonDocument::fromJson(manifest.readAll(), &error);
            if (!doc.isObject()) {
                qWarning() << "newteller: invalid plugin manifest" << manifest.fileName()
                           << (error.error == QJsonParseError::NoError ? "not an object"
                                                                       : error.errorString());
                continue;
            }

            const QVariantMap m = doc.object().toVariantMap();
            const QString id = m.value(QStringLiteral("id")).toString();
            const QString type = m.value(QStringLiteral("type")).toString();
            const QString entry = m.value(QStringLiteral("entry")).toString();
            if (id.isEmpty() || type.isEmpty() || entry.isEmpty()) {
                qWarning() << "newteller: plugin manifest" << manifest.fileName()
                           << "needs id, type and entry";
                continue;
            }
            if (seen.contains(id))
                continue; // earlier search path wins
            seen.insert(id);

            QVariantMap p;
            p.insert(QStringLiteral("id"), id);
            p.insert(QStringLiteral("name"), m.value(QStringLiteral("name"), sub));
            p.insert(QStringLiteral("version"), m.value(QStringLiteral("version"), QStringLiteral("0.0.0")));
            p.insert(QStringLiteral("description"), m.value(QStringLiteral("description")).toString());
            p.insert(QStringLiteral("settings"), m.value(QStringLiteral("settings")).toMap());
            p.insert(QStringLiteral("type"), type);
            p.insert(QStringLiteral("order"), m.value(QStringLiteral("order"), 0));
            p.insert(QStringLiteral("x"), m.value(QStringLiteral("x"), 40));
            p.insert(QStringLiteral("y"), m.value(QStringLiteral("y"), 40));
            p.insert(QStringLiteral("path"), pluginDir);
            p.insert(QStringLiteral("entry"), QUrl::fromLocalFile(pluginDir + QLatin1Char('/') + entry).toString());
            if (!QFile::exists(pluginDir + QLatin1Char('/') + entry))
                qWarning() << "newteller: plugin" << id << "entry file is missing:" << entry;
            m_plugins.append(p);
        }
    }

    std::stable_sort(m_plugins.begin(), m_plugins.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("order")).toInt() < b.value(QStringLiteral("order")).toInt();
    });
    rebuildCache();
}

void PluginManager::rebuildCache()
{
    const QStringList disabled = disabledIds();
    QVariantList out;
    out.reserve(m_plugins.size());
    for (QVariantMap p : m_plugins) {
        p.insert(QStringLiteral("enabled"), !disabled.contains(p.value(QStringLiteral("id")).toString()));
        out.append(p);
    }
    m_cached = out;
    emit pluginsChanged();
}

QVariantList PluginManager::ofType(const QString &type) const
{
    QVariantList out;
    for (const QVariant &v : m_cached) {
        const QVariantMap p = v.toMap();
        if (p.value(QStringLiteral("type")).toString() == type && p.value(QStringLiteral("enabled")).toBool())
            out.append(p);
    }
    return out;
}

QVariantMap PluginManager::at(const QString &id) const
{
    for (const QVariant &v : m_cached) {
        const QVariantMap p = v.toMap();
        if (p.value(QStringLiteral("id")).toString() == id)
            return p;
    }
    return {};
}

void PluginManager::setEnabled(const QString &id, bool enabled)
{
    QStringList disabled = disabledIds();
    if (enabled)
        disabled.removeAll(id);
    else if (!disabled.contains(id))
        disabled.append(id);
    m_settings->setValue(QStringLiteral("plugins.disabled"), disabled);
}
