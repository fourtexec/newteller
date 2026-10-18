#include "theme.h"
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
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

QVariantMap ThemeManager::themeValues(const QString &name) const
{
    const QString wanted = name.isEmpty() ? m_name : name;
    // Built-in default first, so partial themes report complete values.
    QVariantMap out = loadJson(QStringLiteral(":/themes/default.json"));
    if (wanted != QLatin1String("default")) {
        const QString userFile = Settings::configDir() + QStringLiteral("/themes/")
                                 + wanted + QStringLiteral(".json");
        const QString file = QFile::exists(userFile) ? userFile
                                                     : QStringLiteral(":/themes/") + wanted
                                                           + QStringLiteral(".json");
        const QVariantMap overrides = loadJson(file);
        for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
            out.insert(it.key(), it.value());
    }
    return out;
}

bool ThemeManager::hasUserTheme(const QString &name) const
{
    const QString wanted = name.isEmpty() ? m_name : name;
    return QFile::exists(Settings::configDir() + QStringLiteral("/themes/")
                         + wanted + QStringLiteral(".json"));
}

bool ThemeManager::saveUserTheme(const QString &name, const QVariantMap &values)
{
    const QString safe = name.trimmed();
    if (safe.isEmpty() || safe.contains(QLatin1Char('/')) || safe.contains(QLatin1Char('\\')))
        return false;
    const QString dir = Settings::configDir() + QStringLiteral("/themes");
    if (!QDir().mkpath(dir))
        return false;

    QJsonObject object;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        object.insert(it.key(), QJsonValue::fromVariant(it.value()));
    QFile file(dir + QLatin1Char('/') + safe + QStringLiteral(".json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    file.close();

    if (m_name == safe)
        reload();
    return true;
}

bool ThemeManager::deleteUserTheme(const QString &name)
{
    const QString file = Settings::configDir() + QStringLiteral("/themes/")
                         + name + QStringLiteral(".json");
    if (!QFile::exists(file))
        return false;
    const bool removed = QFile::remove(file);
    if (removed && m_name == name)
        reload();
    return removed;
}

QString ThemeManager::userThemesPath() const
{
    return Settings::configDir() + QStringLiteral("/themes");
}
