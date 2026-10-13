#include "appmodel.h"
#include "settings.h"

#include <QDir>
#include <QDirIterator>
#include <QLocale>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>

namespace {

// Field codes from the Desktop Entry spec that are dropped when launching.
const QStringList kFieldCodes{
    QStringLiteral("%f"), QStringLiteral("%F"), QStringLiteral("%u"), QStringLiteral("%U"),
    QStringLiteral("%d"), QStringLiteral("%D"), QStringLiteral("%n"), QStringLiteral("%N"),
    QStringLiteral("%i"), QStringLiteral("%c"), QStringLiteral("%k"), QStringLiteral("%v"),
    QStringLiteral("%m")};

// QSettings may split comma-containing values into a list; joining restores them.
QString textValue(QSettings &s, const QString &key)
{
    return s.value(key).toStringList().join(QLatin1Char(','));
}

} // namespace

AppModel::AppModel(Settings *settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_settings(settings)
{
    reload();
}

int AppModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_apps.size();
}

QVariant AppModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_apps.size())
        return {};
    const AppEntry &a = m_apps.at(index.row());
    switch (role) {
    case IdRole: return a.id;
    case NameRole: return a.name;
    case CommentRole: return a.comment;
    case IconRole: return a.icon;
    case ExecRole: return a.exec;
    case CategoriesRole: return a.categories;
    default: return {};
    }
}

QHash<int, QByteArray> AppModel::roleNames() const
{
    return {
        {IdRole, "appId"},
        {NameRole, "name"},
        {CommentRole, "comment"},
        {IconRole, "icon"},
        {ExecRole, "exec"},
        {CategoriesRole, "categories"},
    };
}

void AppModel::reload()
{
    beginResetModel();
    m_apps.clear();

    QSet<QString> seen;
    const QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &dir : dirs) {
        if (!QDir(dir).exists())
            continue;
        QDirIterator it(dir, {QStringLiteral("*.desktop")}, QDir::Files);
        while (it.hasNext()) {
            it.next();
            const QString id = it.fileName();
            // The first directory wins (user dir is listed first), so user entries override system ones.
            if (seen.contains(id))
                continue;
            seen.insert(id);
            AppEntry entry;
            entry.id = id;
            if (parseDesktop(it.filePath(), entry))
                m_apps.append(entry);
        }
    }

    std::sort(m_apps.begin(), m_apps.end(), [](const AppEntry &a, const AppEntry &b) {
        return a.name.toLower() < b.name.toLower();
    });
    endResetModel();
}

bool AppModel::parseDesktop(const QString &path, AppEntry &out)
{
    QSettings s(path, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("Desktop Entry"));

    if (s.value(QStringLiteral("Type")).toString() != QLatin1String("Application"))
        return false;
    if (s.value(QStringLiteral("NoDisplay"), false).toBool() || s.value(QStringLiteral("Hidden"), false).toBool())
        return false;

    const QLocale loc = QLocale::system();
    const QString lang = loc.name().section(QLatin1Char('_'), 0, 0);
    QString name = textValue(s, QStringLiteral("Name[%1]").arg(loc.name()));
    if (name.isEmpty())
        name = textValue(s, QStringLiteral("Name[%1]").arg(lang));
    if (name.isEmpty())
        name = textValue(s, QStringLiteral("Name"));
    if (name.isEmpty())
        return false;

    out.name = name;
    out.comment = textValue(s, QStringLiteral("Comment[%1]").arg(lang));
    if (out.comment.isEmpty())
        out.comment = textValue(s, QStringLiteral("Comment"));
    out.exec = textValue(s, QStringLiteral("Exec"));
    out.icon = textValue(s, QStringLiteral("Icon"));
    out.terminal = s.value(QStringLiteral("Terminal"), false).toBool();
    out.categories = textValue(s, QStringLiteral("Categories")).split(QLatin1Char(';'), Qt::SkipEmptyParts);
    out.keywords = textValue(s, QStringLiteral("Keywords")).split(QLatin1Char(';'), Qt::SkipEmptyParts);
    return !out.exec.isEmpty();
}

QVariantMap AppModel::toMap(const AppEntry &app)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), app.id);
    m.insert(QStringLiteral("name"), app.name);
    m.insert(QStringLiteral("comment"), app.comment);
    m.insert(QStringLiteral("icon"), app.icon);
    m.insert(QStringLiteral("exec"), app.exec);
    m.insert(QStringLiteral("categories"), app.categories);
    return m;
}

QVariantList AppModel::search(const QString &query, int limit) const
{
    const QString q = query.trimmed().toLower();

    // Score: 0 = name starts with query, 1 = name contains, 2 = comment/keywords contain.
    QList<QPair<int, const AppEntry *>> hits;
    for (const AppEntry &a : m_apps) {
        int score = -1;
        if (q.isEmpty()) {
            score = 0;
        } else {
            const QString n = a.name.toLower();
            if (n.startsWith(q))
                score = 0;
            else if (n.contains(q))
                score = 1;
            else if (a.comment.toLower().contains(q) || a.keywords.join(QLatin1Char(' ')).toLower().contains(q))
                score = 2;
        }
        if (score >= 0)
            hits.append({score, &a});
    }

    std::stable_sort(hits.begin(), hits.end(), [](const auto &x, const auto &y) { return x.first < y.first; });

    QVariantList out;
    for (int i = 0; i < hits.size() && out.size() < limit; ++i)
        out.append(toMap(*hits[i].second));
    return out;
}

QVariantMap AppModel::byId(const QString &id) const
{
    for (const AppEntry &a : m_apps)
        if (a.id == id)
            return toMap(a);
    return {};
}

QVariantList AppModel::recent(int limit) const
{
    QVariantList out;
    const QStringList ids = m_settings->value(QStringLiteral("apps.recent")).toStringList();
    for (const QString &id : ids) {
        if (out.size() >= limit)
            break;
        const QVariantMap m = byId(id);
        if (!m.isEmpty())
            out.append(m);
    }
    return out;
}

void AppModel::rememberRecent(const QString &id)
{
    QStringList ids = m_settings->value(QStringLiteral("apps.recent")).toStringList();
    ids.removeAll(id);
    ids.prepend(id);
    while (ids.size() > 20)
        ids.removeLast();
    m_settings->setValue(QStringLiteral("apps.recent"), ids);
}

bool AppModel::launch(const QString &id)
{
    for (const AppEntry &a : m_apps) {
        if (a.id != id)
            continue;

        // Expand the Exec line into argv, dropping field codes like %U.
        QStringList args;
        for (const QString &token : QProcess::splitCommand(a.exec)) {
            if (kFieldCodes.contains(token))
                continue;
            args << QString(token).replace(QStringLiteral("%%"), QStringLiteral("%"));
        }
        if (args.isEmpty())
            return false;

        if (a.terminal)
            args.prepend({QStringLiteral("x-terminal-emulator"), QStringLiteral("-e")});

        const QString program = args.takeFirst();
        const bool ok = QProcess::startDetached(program, args);
        if (ok)
            rememberRecent(id);
        return ok;
    }
    return false;
}
