#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class Settings;

// One launchable application parsed from an XDG .desktop file.
struct AppEntry {
    QString id;        // desktop file name, e.g. "firefox-esr.desktop"
    QString name;
    QString comment;
    QString exec;
    QString icon;
    QStringList categories;
    QStringList keywords;
    bool terminal = false;
};

// Application catalogue (XDG applications directories). Exposed to QML as `Apps`.
class AppModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, CommentRole, IconRole, ExecRole, CategoriesRole };

    explicit AppModel(Settings *settings, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void reload();
    Q_INVOKABLE QVariantList search(const QString &query, int limit = 50) const;
    Q_INVOKABLE QVariantMap byId(const QString &id) const;
    Q_INVOKABLE QVariantList recent(int limit = 8) const;
    Q_INVOKABLE bool launch(const QString &id);

private:
    static bool parseDesktop(const QString &path, AppEntry &out);
    static QVariantMap toMap(const AppEntry &app);
    void rememberRecent(const QString &id);

    Settings *m_settings;
    QVector<AppEntry> m_apps;
};
