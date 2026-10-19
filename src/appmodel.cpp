#include "appmodel.h"

#include "settings.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
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

QStringList listValue(QSettings &s, const QString &key)
{
    return textValue(s, key).split(QLatin1Char(';'), Qt::SkipEmptyParts);
}

// Case folded name used for sorting and comparisons.
QString foldName(const QString &name)
{
    return name.trimmed().toLower();
}

// QML sees argv as a list of variants; C++ callers convert it back to strings.
QVariantList toVariantList(const QStringList &values)
{
    QVariantList out;
    out.reserve(values.size());
    for (const QString &value : values)
        out.append(value);
    return out;
}

// id -> launch count, stored under apps.usage.
QHash<QString, int> usageFromSettings(Settings *settings)
{
    QHash<QString, int> usage;
    const QVariantMap map = settings->value(QStringLiteral("apps.usage")).toMap();
    usage.reserve(map.size());
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        const int count = it.value().toInt();
        if (count > 0) {
            usage.insert(it.key(), count);
        }
    }
    return usage;
}

} // namespace

QString AppEntry::program() const
{
    // Cheap scan: the program is the first unquoted token of Exec=.
    bool quoted = false;
    QString token;
    for (const QChar character : exec) {
        if (character == QLatin1Char('"')) {
            quoted = !quoted;
            continue;
        }
        if (!quoted && character.isSpace()) {
            if (!token.isEmpty()) {
                break;
            }
            continue;
        }
        token.append(character);
    }
    return token;
}

AppModel::AppModel(Settings *settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_settings(settings)
{
    m_roles = {
        {IdRole, "appId"},
        {NameRole, "name"},
        {CommentRole, "comment"},
        {IconRole, "icon"},
        {ExecRole, "exec"},
        {CategoriesRole, "categories"},
        {GenericNameRole, "genericName"},
        {KeywordsRole, "keywords"},
        {TerminalRole, "terminal"},
        {PathRole, "path"},
        {ProgramRole, "program"},
        {RunningRole, "running"},
    };
    // Applications started outside the shell have to show up in the dock too,
    // so the /proc scan repeats periodically; refreshRunning() is cheap when
    // nothing changed and the QML only rebuilds the dock on runningChanged.
    m_runningTimer.setInterval(1500);
    m_runningTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_runningTimer, &QTimer::timeout, this, &AppModel::refreshRunning);
    m_runningTimer.start();
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
    case GenericNameRole: return a.genericName;
    case KeywordsRole: return a.keywords;
    case TerminalRole: return a.terminal;
    case PathRole: return a.path;
    case ProgramRole: return a.program();
    case RunningRole: return m_runningIds.contains(a.id);
    default: return {};
    }
}

QHash<int, QByteArray> AppModel::roleNames() const
{
    // Built once in the constructor: QML asks for this on every view attach.
    return m_roles;
}

bool AppModel::rustBacked() const
{
    return Rust::available() && m_index.isValid();
}

QVariantList AppModel::categories() const
{
    if (m_index.isValid()) {
        return m_index.categories();
    }
    // Fallback: count the categories of the loaded applications.
    QHash<QString, int> counts;
    for (const AppEntry &app : m_apps) {
        for (const QString &category : app.categories) {
            counts[category] = counts.value(category) + 1;
        }
    }
    QVariantList out;
    out.reserve(counts.size());
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        out.append(QVariantMap{{QStringLiteral("name"), it.key()},
                               {QStringLiteral("count"), it.value()}});
    }
    std::sort(out.begin(), out.end(), [](const QVariant &left, const QVariant &right) {
        const int difference = right.toMap().value(QStringLiteral("count")).toInt()
                               - left.toMap().value(QStringLiteral("count")).toInt();
        return difference != 0 ? difference > 0
                               : left.toMap().value(QStringLiteral("name")).toString()
                                     < right.toMap().value(QStringLiteral("name")).toString();
    });
    return out;
}

QStringList AppModel::applicationDirs() const
{
    QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);

    // QStandardPaths skips XDG_DATA_DIRS entries in some configurations, so add
    // them explicitly; duplicates are dropped below and order decides priority.
    const QString dataDirs = qEnvironmentVariable("XDG_DATA_DIRS");
    if (!dataDirs.isEmpty()) {
        const QStringList parts = dataDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const QString candidate = QDir(part).filePath(QStringLiteral("applications"));
            if (!dirs.contains(candidate)) {
                dirs << candidate;
            }
        }
    }
    const QString flatpak = QStringLiteral("/var/lib/flatpak/exports/share/applications");
    if (!dirs.contains(flatpak)) {
        dirs << flatpak;
    }

    QStringList existing;
    existing.reserve(dirs.size());
    for (const QString &dir : dirs) {
        if (QDir(dir).exists() && !existing.contains(dir)) {
            existing << dir;
        }
    }
    return existing;
}

void AppModel::reload()
{
    QElapsedTimer timer;
    timer.start();

    const QString locale = QLocale::system().name();
    const int before = m_apps.size();

    beginResetModel();
    m_apps.clear();
    m_indexById.clear();

    if (Rust::available()) {
        rebuildRustIndex();
        if (m_index.isValid()) {
            m_apps = m_index.entries();
            sortApps(m_apps);
        }
    }
    if (m_apps.isEmpty()) {
        // No Rust, or the Rust scan produced nothing: use the Qt parser.
        m_index = Rust::Index(); // release the handle, if any
        scanWithQt(locale);
    }

    m_indexById.reserve(m_apps.size());
    for (int row = 0; row < m_apps.size(); ++row) {
        m_indexById.insert(m_apps.at(row).id, row);
    }
    endResetModel();

    applyUsage();
    refreshRunning();

    if (before != m_apps.size()) {
        emit countChanged();
    }
    emit categoriesChanged();
    emit reloadFinished(m_apps.size(), static_cast<double>(timer.elapsed()));
}

void AppModel::rebuildRustIndex()
{
    Rust::ScanOptions options;
    options.dirs = applicationDirs();
    options.locale = QLocale::system().name();
    options.desktopEnv = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    options.maxApps = m_settings->value(QStringLiteral("apps.maxApps"), 0).toInt();
    options.maxDepth = 3;
    options.includeHidden = false;
    if (!m_index.scan(options)) {
        qWarning("NewTeller: Rust application scan failed: %s", qPrintable(Rust::lastError()));
    }
}

void AppModel::scanWithQt(const QString &locale)
{
    QSet<QString> seen;

    const QStringList dirs = applicationDirs();
    for (int priority = dirs.size(); priority > 0; --priority) {
        const QString &dir = dirs.at(dirs.size() - priority);
        QDirIterator it(dir, {QStringLiteral("*.desktop")}, QDir::Files);
        while (it.hasNext()) {
            it.next();
            const QString id = it.fileName();
            // The first directory wins (the user directory is listed first), so a
            // user entry overrides the system one with the same name.
            if (seen.contains(id)) {
                continue;
            }
            AppEntry entry;
            entry.id = id;
            entry.priority = priority;
            if (parseDesktop(it.filePath(), entry, locale)) {
                seen.insert(id);
                m_apps.append(entry);
            }
        }
    }
    sortApps(m_apps);
}

bool AppModel::parseDesktop(const QString &path, AppEntry &out, const QString &lang)
{
    QSettings s(path, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("Desktop Entry"));

    if (textValue(s, QStringLiteral("Type")) != QLatin1String("Application"))
        return false;
    out.noDisplay = s.value(QStringLiteral("NoDisplay"), false).toBool();
    out.hidden = s.value(QStringLiteral("Hidden"), false).toBool();
    if (out.noDisplay || out.hidden)
        return false;

    // Localisation: Name[lang_COUNTRY], then Name[lang], then Name.
    QString name = textValue(s, QStringLiteral("Name[%1]").arg(lang));
    if (name.isEmpty())
        name = textValue(s, QStringLiteral("Name[%1]").arg(lang.section(QLatin1Char('_'), 0, 0)));
    if (name.isEmpty())
        name = textValue(s, QStringLiteral("Name"));
    if (name.isEmpty())
        return false;

    out.path = path;
    out.name = name;
    out.genericName = textValue(s, QStringLiteral("GenericName[%1]").arg(lang));
    if (out.genericName.isEmpty())
        out.genericName = textValue(s, QStringLiteral("GenericName"));
    out.comment = textValue(s, QStringLiteral("Comment[%1]").arg(lang));
    if (out.comment.isEmpty())
        out.comment = textValue(s, QStringLiteral("Comment"));
    out.exec = textValue(s, QStringLiteral("Exec"));
    out.tryExec = textValue(s, QStringLiteral("TryExec"));
    out.icon = textValue(s, QStringLiteral("Icon"));
    out.terminal = s.value(QStringLiteral("Terminal"), false).toBool();
    out.startupNotify = s.value(QStringLiteral("StartupNotify"), false).toBool();
    out.dbusActivatable = s.value(QStringLiteral("DBusActivatable"), false).toBool();
    out.startupWmClass = textValue(s, QStringLiteral("StartupWMClass"));
    out.categories = listValue(s, QStringLiteral("Categories"));
    out.keywords = listValue(s, QStringLiteral("Keywords[%1]").arg(lang));
    if (out.keywords.isEmpty())
        out.keywords = listValue(s, QStringLiteral("Keywords"));
    out.mimeTypes = listValue(s, QStringLiteral("MimeType"));
    out.onlyShowIn = listValue(s, QStringLiteral("OnlyShowIn"));
    out.notShowIn = listValue(s, QStringLiteral("NotShowIn"));
    out.sortKey = foldName(out.name);
    s.endGroup();

    // [Desktop Action name] groups, used for jump lists in the start menu.
    const QStringList groups = s.childGroups();
    for (const QString &group : groups) {
        if (!group.startsWith(QLatin1String("Desktop Action ")))
            continue;
        s.beginGroup(group);
        const QString actionName = textValue(s, QStringLiteral("Name[%1]").arg(lang));
        const QString actionExec = textValue(s, QStringLiteral("Exec"));
        if (!actionExec.isEmpty()) {
            QVariantMap action;
            action.insert(QStringLiteral("id"), group.section(QLatin1Char(' '), 2));
            action.insert(QStringLiteral("name"),
                          actionName.isEmpty() ? textValue(s, QStringLiteral("Name")) : actionName);
            action.insert(QStringLiteral("exec"), actionExec);
            action.insert(QStringLiteral("icon"), textValue(s, QStringLiteral("Icon")));
            out.actions.append(action);
        }
        s.endGroup();
    }

    return !out.exec.isEmpty();
}

void AppModel::sortApps(QVector<AppEntry> &apps)
{
    std::stable_sort(apps.begin(), apps.end(), [](const AppEntry &left, const AppEntry &right) {
        if (left.priority != right.priority)
            return left.priority > right.priority; // a user override beats a system entry
        const QString &leftKey = left.sortKey.isEmpty() ? foldName(left.name) : left.sortKey;
        const QString &rightKey = right.sortKey.isEmpty() ? foldName(right.name) : right.sortKey;
        if (leftKey != rightKey)
            return leftKey < rightKey;
        return left.id < right.id;
    });
    for (AppEntry &app : apps) {
        if (app.sortKey.isEmpty())
            app.sortKey = foldName(app.name);
    }
}

QVariantMap AppModel::toMap(const AppEntry &app)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), app.id);
    m.insert(QStringLiteral("name"), app.name);
    m.insert(QStringLiteral("genericName"), app.genericName);
    m.insert(QStringLiteral("comment"), app.comment);
    m.insert(QStringLiteral("icon"), app.icon);
    m.insert(QStringLiteral("exec"), app.exec);
    m.insert(QStringLiteral("path"), app.path);
    m.insert(QStringLiteral("program"), app.program());
    m.insert(QStringLiteral("categories"), app.categories);
    m.insert(QStringLiteral("keywords"), app.keywords);
    m.insert(QStringLiteral("actions"), app.actions);
    m.insert(QStringLiteral("terminal"), app.terminal);
    return m;
}

QVariantMap AppModel::byId(const QString &id) const
{
    const int row = m_indexById.value(id, -1);
    if (row < 0 || row >= m_apps.size())
        return {};
    return toMap(m_apps.at(row));
}

QVariantMap AppModel::matchWindowApp(const QString &appId, const QString &title) const
{
    Q_UNUSED(title); // reserved: future window-title based matching

    // Candidates in order of trust: the wayland app_id, the X11 WM_CLASS,
    // each with and without a .desktop suffix.
    QStringList candidates;
    if (!appId.isEmpty()) {
        candidates << appId;
        if (appId.endsWith(QLatin1String(".desktop")))
            candidates << appId.chopped(8);
    }

    // 1. Exact catalogue ids.
    for (const QString &candidate : candidates) {
        const int row = m_indexById.value(candidate, -1);
        if (row >= 0)
            return toMap(m_apps.at(row));
    }

    // 2. startupWMClass and program name, compared case-insensitively.
    for (const QString &candidate : candidates) {
        const QString needle = candidate.toLower();
        for (const AppEntry &app : m_apps) {
            if (app.startupWmClass.compare(needle, Qt::CaseInsensitive) == 0)
                return toMap(app);
            if (app.program().compare(needle, Qt::CaseInsensitive) == 0)
                return toMap(app);
            const QString program = app.program();
            const int slash = program.lastIndexOf(QLatin1Char('/'));
            if (slash >= 0 && program.mid(slash + 1).compare(needle, Qt::CaseInsensitive) == 0)
                return toMap(app);
        }
    }
    return {};
}

int AppModel::indexOf(const QString &id) const
{
    return m_indexById.value(id, -1);
}

QVariantList AppModel::searchDetailed(const QString &query, int limit) const
{
    if (!m_index.isValid())
        return {};

    const bool fuzzy = m_settings->value(QStringLiteral("startmenu.fuzzy"), true).toBool();
    const QString highlight = m_settings->value(QStringLiteral("startmenu.highlight"),
                                                QStringLiteral("#ffffff")).toString();
    return m_index.search(query.trimmed(), limit, highlight, fuzzy);
}

QVariantMap AppModel::searchResult(const QString &query, int limit) const
{
    if (!m_index.isValid())
        return {};
    const bool fuzzy = m_settings->value(QStringLiteral("startmenu.fuzzy"), true).toBool();
    const QString highlight = m_settings->value(QStringLiteral("startmenu.highlight"),
                                                QStringLiteral("#ffffff")).toString();
    // The full result object, for the diagnostics window and CLI parity.
    QVariantMap out;
    const QVariantList hits = m_index.search(query.trimmed(), limit, highlight, fuzzy);
    out.insert(QStringLiteral("hits"), hits);
    out.insert(QStringLiteral("count"), hits.size());
    out.insert(QStringLiteral("query"), query);
    out.insert(QStringLiteral("fuzzy"), fuzzy);
    return out;
}

QVariantList AppModel::search(const QString &query, int limit) const
{
    // Rust path: ranked hits, each expanded to the full application map so the
    // existing QML keeps working and gains score/kind/highlight for free.
    const QVariantList hits = searchDetailed(query, limit);
    if (!hits.isEmpty() || (m_index.isValid() && !query.trimmed().isEmpty())) {
        QVariantList out;
        out.reserve(hits.size());
        for (const QVariant &hit : hits) {
            const QVariantMap hitMap = hit.toMap();
            QVariantMap app = byId(hitMap.value(QStringLiteral("id")).toString());
            if (app.isEmpty())
                continue;
            app.insert(QStringLiteral("score"), hitMap.value(QStringLiteral("score")));
            app.insert(QStringLiteral("kind"), hitMap.value(QStringLiteral("kind")));
            app.insert(QStringLiteral("field"), hitMap.value(QStringLiteral("field")));
            app.insert(QStringLiteral("highlight"), hitMap.value(QStringLiteral("highlight")));
            app.insert(QStringLiteral("usage"), hitMap.value(QStringLiteral("usage")));
            out.append(app);
        }
        if (!out.isEmpty() || !query.trimmed().isEmpty())
            return out;
        // An empty query means "list everything", which the index answers with
        // the most used applications only when usage data exists.
    }

    // Qt fallback: three tiers, stable within a tier, same ranking as before.
    const QString q = query.trimmed().toLower();
    QList<QPair<int, const AppEntry *>> matches;
    matches.reserve(m_apps.size());
    for (const AppEntry &a : m_apps) {
        int score = -1;
        if (q.isEmpty()) {
            score = 0;
        } else {
            const QString n = foldName(a.name);
            if (n.startsWith(q))
                score = 0;
            else if (n.contains(q))
                score = 1;
            else if (foldName(a.comment).contains(q)
                     || foldName(a.keywords.join(QLatin1Char(' '))).contains(q))
                score = 2;
        }
        if (score >= 0)
            matches.append({score, &a});
    }
    std::stable_sort(matches.begin(), matches.end(),
                     [](const auto &x, const auto &y) { return x.first < y.first; });

    QVariantList out;
    out.reserve(qMin(limit, static_cast<int>(matches.size())));
    for (int i = 0; i < matches.size() && out.size() < limit; ++i)
        out.append(toMap(*matches.at(i).second));
    return out;
}

QVariantList AppModel::recent(int limit) const
{
    QVariantList out;
    const int wanted = m_settings->value(QStringLiteral("apps.recentLimit"), limit).toInt();
    const QStringList ids = m_settings->value(QStringLiteral("apps.recent")).toStringList();
    const int effective = qMax(0, qMin(wanted > 0 ? wanted : limit, static_cast<int>(ids.size())));
    out.reserve(effective);
    for (int i = 0; i < effective; ++i) {
        const QVariantMap m = byId(ids.at(i));
        if (!m.isEmpty())
            out.append(m);
    }
    return out;
}

void AppModel::rememberRecent(const QString &id)
{
    const int limit = m_settings->value(QStringLiteral("apps.recentLimit"), 8).toInt();
    QStringList ids = m_settings->value(QStringLiteral("apps.recent")).toStringList();
    ids.removeAll(id);
    ids.prepend(id);
    while (ids.size() > qMax(1, limit) * 2)
        ids.removeLast();
    m_settings->setValue(QStringLiteral("apps.recent"), ids);
}

void AppModel::rememberUsage(const QString &id)
{
    QVariantMap usage = m_settings->value(QStringLiteral("apps.usage")).toMap();
    usage.insert(id, usage.value(id).toInt() + 1);
    m_settings->setValue(QStringLiteral("apps.usage"), usage);
    applyUsage();
}

int AppModel::usageOf(const QString &id) const
{
    return m_settings->value(QStringLiteral("apps.usage")).toMap().value(id).toInt();
}

void AppModel::forgetUsage(const QString &id)
{
    QVariantMap usage = m_settings->value(QStringLiteral("apps.usage")).toMap();
    if (usage.remove(id)) {
        m_settings->setValue(QStringLiteral("apps.usage"), usage);
        applyUsage();
    }
}

void AppModel::forgetRecent()
{
    m_settings->setValue(QStringLiteral("apps.recent"), QStringList());
    m_settings->setValue(QStringLiteral("apps.usage"), QVariantMap());
    applyUsage();
}

void AppModel::applyUsage()
{
    if (!m_index.isValid())
        return;
    m_index.setUsage(usageFromSettings(m_settings));
}

bool AppModel::isRunning(const QString &id) const
{
    return m_runningIds.contains(id);
}

void AppModel::refreshRunning()
{
    if (!m_index.isValid()) {
        if (!m_runningIds.isEmpty()) {
            m_runningIds.clear();
            emit runningChanged();
        }
        return;
    }

    const QStringList previous = m_runningIds;
    m_runningIds = m_index.runningIds();
    if (previous == m_runningIds)
        return;

    // Tell the views only about the rows whose running badge actually changed.
    QSet<QString> touched;
    for (const QString &id : previous) {
        if (!m_runningIds.contains(id))
            touched.insert(id);
    }
    for (const QString &id : m_runningIds) {
        if (!previous.contains(id))
            touched.insert(id);
    }
    for (const QString &id : touched) {
        const int row = m_indexById.value(id, -1);
        if (row >= 0 && row < m_apps.size()) {
            const QModelIndex index = createIndex(row, 0);
            emit dataChanged(index, index, {RunningRole});
        }
    }
    emit runningChanged();
}

QVariantList AppModel::launchArgs(const QString &id, const QStringList &files) const
{
    const int row = indexOf(id);
    if (row < 0 || row >= m_apps.size())
        return {};
    const AppEntry &app = m_apps.at(row);

    const QString terminalCommand = m_settings->value(
        QStringLiteral("apps.terminalCommand"), QStringLiteral("x-terminal-emulator -e")).toString();

    if (Rust::available()) {
        // Full spec behaviour: quoting, %i/%c/%k expansion, %u/%f file lists,
        // terminal wrapping and environment aware program lookup.
        const QStringList argv = Rust::Index::execArgv(app.exec, app.icon, app.name, app.path,
                                                       files, app.terminal, terminalCommand);
        if (!argv.isEmpty())
            return toVariantList(argv);
    }

    // Fallback: drop field codes and prepend the terminal emulator.
    QStringList args;
    for (const QString &token : QProcess::splitCommand(app.exec)) {
        if (kFieldCodes.contains(token))
            continue;
        QString clean = token;
        clean.replace(QStringLiteral("%%"), QStringLiteral("%"));
        args << clean;
    }
    if (args.isEmpty())
        return {};

    if (!files.isEmpty())
        args += files;

    if (app.terminal) {
        const QStringList wrapper = terminalCommand.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        args = wrapper + args;
    }

    return toVariantList(args);
}

bool AppModel::launch(const QString &id, const QStringList &files)
{
    const QVariantList args = launchArgs(id, files);
    if (args.isEmpty()) {
        emit launchFailed(id, QStringLiteral("no such application or empty Exec line"));
        return false;
    }

    QStringList argv;
    argv.reserve(args.size());
    for (const QVariant &arg : args)
        argv << arg.toString();

    const QString program = argv.takeFirst();
    if (program.isEmpty()) {
        emit launchFailed(id, QStringLiteral("empty program"));
        return false;
    }

    const bool ok = QProcess::startDetached(program, argv);
    if (!ok) {
        emit launchFailed(id, QStringLiteral("QProcess::startDetached failed for %1").arg(program));
        return false;
    }

    rememberRecent(id);
    rememberUsage(id);
    // The process needs a moment to appear in /proc before the badge can show.
    QMetaObject::invokeMethod(this, "refreshRunning", Qt::QueuedConnection);
    return true;
}

bool AppModel::launchAction(const QString &id, const QString &actionId)
{
    const int row = indexOf(id);
    if (row < 0 || row >= m_apps.size())
        return false;
    const AppEntry &app = m_apps.at(row);

    for (const QVariant &variant : app.actions) {
        const QVariantMap action = variant.toMap();
        if (action.value(QStringLiteral("id")).toString() != actionId)
            continue;
        const QString exec = action.value(QStringLiteral("exec")).toString();
        if (exec.isEmpty())
            return false;

        const QString terminalCommand = m_settings->value(
            QStringLiteral("apps.terminalCommand"), QStringLiteral("x-terminal-emulator -e")).toString();
        QStringList argv = Rust::available()
                               ? Rust::Index::execArgv(exec,
                                                       action.value(QStringLiteral("icon")).toString(),
                                                       action.value(QStringLiteral("name")).toString(),
                                                       app.path, {}, app.terminal, terminalCommand)
                               : QStringList();
        if (argv.isEmpty()) {
            for (const QString &token : QProcess::splitCommand(exec)) {
                if (!kFieldCodes.contains(token))
                    argv << token;
            }
        }
        if (argv.isEmpty())
            return false;

        const QString program = argv.takeFirst();
        const bool ok = QProcess::startDetached(program, argv);
        if (ok) {
            rememberRecent(id);
            rememberUsage(id);
        }
        return ok;
    }
    return false;
}

QVariantMap AppModel::stats() const
{
    QVariantMap out;
    if (m_index.isValid()) {
        out = m_index.stats();
    }
    out.insert(QStringLiteral("apps"), m_apps.size());
    out.insert(QStringLiteral("rust"), rustBacked());
    out.insert(QStringLiteral("running"), m_runningIds.size());
    out.insert(QStringLiteral("dirs"), applicationDirs());
    return out;
}

QVariantMap AppModel::scanReport() const
{
    if (!m_index.isValid())
        return {};
    return m_index.report();
}
