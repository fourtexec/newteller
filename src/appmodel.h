#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "appentry.h"
#include "rustbridge.h"

class Settings;

// Application catalogue (XDG applications directories). Exposed to QML as `Apps`.
//
// Two back ends share this class:
//   * the Rust core: one parallel scan, a trigram index, fuzzy ranking, usage
//     boosting and /proc based "is it running" answers;
//   * a pure C++/Qt fallback, used when NEWTELLER_WITH_RUST=OFF, which keeps the
//     original behaviour (linear search, first directory wins).
// `rustBacked` tells QML which extras are meaningful.
class AppModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY categoriesChanged)
    Q_PROPERTY(QStringList runningIds READ runningIds NOTIFY runningChanged)
    Q_PROPERTY(bool rustBacked READ rustBacked CONSTANT)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        CommentRole,
        IconRole,
        ExecRole,
        CategoriesRole,
        GenericNameRole,
        KeywordsRole,
        TerminalRole,
        PathRole,
        ProgramRole,
        RunningRole
    };

    explicit AppModel(Settings *settings, QObject *parent = nullptr);
    ~AppModel() override = default;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_apps.size(); }
    bool rustBacked() const;
    QVariantList categories() const;
    QStringList runningIds() const { return m_runningIds; }

    Q_INVOKABLE void reload();

    // Classic search: a list of application maps, best match first. Kept for the
    // existing QML and for the fallback back end.
    Q_INVOKABLE QVariantList search(const QString &query, int limit = 50) const;
    // Rust search: the same list plus score, kind (exact/prefix/word/acronym/
    // substring/fuzzy/typo), field and a StyledText `highlight`. Empty without
    // Rust, so QML can fall back to `search()`.
    Q_INVOKABLE QVariantList searchDetailed(const QString &query, int limit = 50) const;
    Q_INVOKABLE QVariantMap searchResult(const QString &query, int limit = 50) const;

    Q_INVOKABLE QVariantMap byId(const QString &id) const;   // O(1)
    Q_INVOKABLE int indexOf(const QString &id) const;        // -1 when unknown
    Q_INVOKABLE QVariantList recent(int limit = 8) const;
    Q_INVOKABLE QVariantList launchArgs(const QString &id, const QStringList &files = {}) const;
    // Launches the application, passing `files` to its %f/%u field codes.
    Q_INVOKABLE bool launch(const QString &id, const QStringList &files = QStringList());
    Q_INVOKABLE bool launchAction(const QString &id, const QString &actionId);

    Q_INVOKABLE bool isRunning(const QString &id) const;
    Q_INVOKABLE void refreshRunning();

    Q_INVOKABLE int usageOf(const QString &id) const;
    Q_INVOKABLE void forgetUsage(const QString &id);
    Q_INVOKABLE void forgetRecent();

    Q_INVOKABLE QVariantMap stats() const;
    Q_INVOKABLE QVariantMap scanReport() const;

signals:
    void countChanged();
    void categoriesChanged();
    void runningChanged();
    void reloadFinished(int applications, double milliseconds);
    void launchFailed(const QString &id, const QString &reason);

private:
    static bool parseDesktop(const QString &path, AppEntry &out, const QString &locale);
    static QVariantMap toMap(const AppEntry &app);
    static void sortApps(QVector<AppEntry> &apps);
    void rememberRecent(const QString &id);
    void rememberUsage(const QString &id);
    void applyUsage();
    void rebuildRustIndex();
    void scanWithQt(const QString &locale);
    QStringList applicationDirs() const;

    Settings *m_settings;
    QVector<AppEntry> m_apps;
    QHash<QString, int> m_indexById; // id -> row, rebuilt on reload only
    QStringList m_runningIds;
    mutable QHash<int, QByteArray> m_roles;
    // Rust search index, owned through the RAII wrapper; invalid without Rust.
    Rust::Index m_index;
};
