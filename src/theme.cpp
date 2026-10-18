#include "theme.h"
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

namespace {

QVariantMap loadJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object().toVariantMap() : QVariantMap{};
}

} // namespace

ThemeManager::ThemeManager(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    reload();
    connect(settings, &Settings::changed, this, [this](const QString &key) {
        if (key.isEmpty() || key == QLatin1String("theme.name"))
            reload();
    });
    // theme.autoReload: editing the theme file in a text editor re-applies it.
    connect(settings, &Settings::externalChange, this, [this](const QString &path) {
        if (!m_settings->value(QStringLiteral("theme.autoReload"), true).toBool())
            return;
        if (!m_filePath.isEmpty() && path == m_filePath)
            reload();
    });
}

QColor ThemeManager::color(const QString &key) const
{
    const auto cached = m_cache.constFind(key);
    if (cached != m_cache.constEnd())
        return cached.value();
    const QColor parsed(m_t.value(key).toString());
    m_cache.insert(key, parsed);
    return parsed;
}

void ThemeManager::invalidateCache()
{
    m_cache.clear();
}

void ThemeManager::reload()
{
    const QString name = m_settings->value(QStringLiteral("theme.name"), QStringLiteral("default")).toString();

    // Base is always the built-in default, so a partial theme still looks complete.
    QVariantMap theme = loadJson(QStringLiteral(":/themes/default.json"));
    QString file;
    if (name != QLatin1String("default")) {
        const QString userFile = Settings::configDir() + QStringLiteral("/themes/") + name + QStringLiteral(".json");
        file = QFile::exists(userFile) ? userFile : QStringLiteral(":/themes/") + name + QStringLiteral(".json");
        const QVariantMap overrides = loadJson(file);
        for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
            theme.insert(it.key(), it.value());
    }

    m_t = theme;
    m_name = name;
    m_filePath = file;
    invalidateCache();
    emit themeChanged();
}

QStringList ThemeManager::availableThemes() const
{
    QStringList names;
    const QStringList dirs{Settings::configDir() + QStringLiteral("/themes"), QStringLiteral(":/themes")};
    for (const QString &dir : dirs) {
        const QStringList files = QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files);
        for (const QString &f : files) {
            const QString base = QFileInfo(f).completeBaseName();
            if (!names.contains(base))
                names << base;
        }
    }
    names.sort();
    return names;
}

QColor ThemeManager::withAlpha(const QColor &color, qreal alpha) const
{
    QColor out(color);
    out.setAlphaF(qBound(0.0, alpha, 1.0));
    return out;
}

QColor ThemeManager::mix(const QColor &a, const QColor &b, qreal t) const
{
    t = qBound(0.0, t, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}
