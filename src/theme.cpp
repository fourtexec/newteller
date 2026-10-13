#include "theme.h"
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

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
}

QColor ThemeManager::color(const char *key) const
{
    return QColor(m_t.value(QString::fromLatin1(key)).toString());
}

void ThemeManager::reload()
{
    const QString name = m_settings->value(QStringLiteral("theme.name"), QStringLiteral("default")).toString();

    // Base is always the built-in default, so a partial theme still looks complete.
    QVariantMap theme = loadJson(QStringLiteral(":/themes/default.json"));
    if (name != QLatin1String("default")) {
        const QString userFile = Settings::configDir() + QStringLiteral("/themes/") + name + QStringLiteral(".json");
        const QString file = QFile::exists(userFile) ? userFile : QStringLiteral(":/themes/") + name + QStringLiteral(".json");
        const QVariantMap overrides = loadJson(file);
        for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
            theme.insert(it.key(), it.value());
    }

    m_t = theme;
    m_name = name;
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
    return names;
}
