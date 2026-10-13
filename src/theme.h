#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantMap>

class Settings;

// Loads a JSON theme. Built-in themes live in the Qt resource ":/themes/",
// user themes in ~/.config/newteller/themes/. Missing keys fall back to default.
// Exposed to QML as the context property `Theme`.
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
    QColor background() const { return color("background"); }
    QColor foreground() const { return color("foreground"); }
    QColor accent() const { return color("accent"); }
    QColor surface() const { return color("surface"); }
    QColor border() const { return color("border"); }
    QColor muted() const { return color("muted"); }
    qreal opacity() const { return m_t.value(QStringLiteral("opacity"), 0.92).toReal(); }
    int radius() const { return m_t.value(QStringLiteral("radius"), 10).toInt(); }
    int fontSize() const { return m_t.value(QStringLiteral("fontSize"), 10).toInt(); }
    QString fontFamily() const { return m_t.value(QStringLiteral("fontFamily"), QStringLiteral("Sans")).toString(); }

    Q_INVOKABLE QStringList availableThemes() const;

public slots:
    void reload();

signals:
    void themeChanged();

private:
    QColor color(const char *key) const;

    Settings *m_settings;
    QVariantMap m_t;
    QString m_name;
};
