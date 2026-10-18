#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class Settings;

// Loads a JSON theme. Built-in themes live in the Qt resource ":/themes/",
// user themes in ~/.config/newteller/themes/. Missing keys fall back to default.
// Exposed to QML as the context property `Theme`.
//
// Parsed colours are cached; with `theme.autoReload` a change to the theme
// file applies without a restart.
class ThemeManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name NOTIFY themeChanged)
    Q_PROPERTY(QColor background READ background NOTIFY themeChanged)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY themeChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
    Q_PROPERTY(QColor surface READ surface NOTIFY themeChanged)
    Q_PROPERTY(QColor border READ border NOTIFY themeChanged)
    Q_PROPERTY(QColor muted READ muted NOTIFY themeChanged)
    Q_PROPERTY(qreal opacity READ opacity NOTIFY themeChanged)
    Q_PROPERTY(int radius READ radius NOTIFY themeChanged)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY themeChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY themeChanged)

public:
    explicit ThemeManager(Settings *settings, QObject *parent = nullptr);

    QString name() const { return m_name; }
    QColor background() const { return color(QStringLiteral("background")); }
    QColor foreground() const { return color(QStringLiteral("foreground")); }
    QColor accent() const { return color(QStringLiteral("accent")); }
    QColor surface() const { return color(QStringLiteral("surface")); }
    QColor border() const { return color(QStringLiteral("border")); }
    QColor muted() const { return color(QStringLiteral("muted")); }
    qreal opacity() const { return m_t.value(QStringLiteral("opacity"), 0.92).toReal(); }
    int radius() const { return m_t.value(QStringLiteral("radius"), 10).toInt(); }
    int fontSize() const { return m_t.value(QStringLiteral("fontSize"), 10).toInt(); }
    QString fontFamily() const { return m_t.value(QStringLiteral("fontFamily"), QStringLiteral("Sans")).toString(); }

    Q_INVOKABLE QStringList availableThemes() const;
    // theme.background with another alpha (0..1), for QML overlays.
    Q_INVOKABLE QColor withAlpha(const QColor &color, qreal alpha) const;
    // Linear blend of two colours, t in 0..1 (0 = a, 1 = b).
    Q_INVOKABLE QColor mix(const QColor &a, const QColor &b, qreal t) const;

    // --- theme file management (used by the built-in settings application) ---
    // The full key/value map of one theme, defaults filled in.
    Q_INVOKABLE QVariantMap themeValues(const QString &name) const;
    // True when the user has an override file for the theme in the config dir.
    Q_INVOKABLE bool hasUserTheme(const QString &name) const;
    // Writes ~/.config/newteller/themes/<name>.json and applies it.
    Q_INVOKABLE bool saveUserTheme(const QString &name, const QVariantMap &values);
    // Removes the user override; the theme falls back to the built-in file.
    Q_INVOKABLE bool deleteUserTheme(const QString &name);
    // The directory user themes live in (shown in the settings).
    Q_INVOKABLE QString userThemesPath() const;

public slots:
    void reload();

signals:
    void themeChanged();

private:
    QColor color(const QString &key) const;
    void invalidateCache();

    Settings *m_settings;
    QVariantMap m_t;
    mutable QHash<QString, QColor> m_cache;
    QString m_name;
    QString m_filePath; // the file the current theme came from, for watching
};
