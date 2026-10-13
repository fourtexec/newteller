#include "rustbridge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QVariant>

#ifdef NEWTELLER_HAVE_RUST
#include <newteller_core.h>
#endif

namespace {

#ifdef NEWTELLER_HAVE_RUST

/// RAII owner of a `char *` returned by the Rust core.
class RustString {
public:
    explicit RustString(char *pointer) : m_pointer(pointer) {}
    ~RustString()
    {
        if (m_pointer != nullptr) {
            nt_free(m_pointer);
        }
    }
    RustString(const RustString &) = delete;
    RustString &operator=(const RustString &) = delete;

    bool isNull() const { return m_pointer == nullptr; }
    QString text() const { return m_pointer != nullptr ? QString::fromUtf8(m_pointer) : QString(); }
    QByteArray bytes() const
    {
        return m_pointer != nullptr ? QByteArray(m_pointer) : QByteArray();
    }

private:
    char *m_pointer;
};

/// The core speaks JSON over the ABI, which keeps it free of struct layout
/// problems between two toolchains.

QVariant fromJson(const QByteArray &text)
{
    if (text.isEmpty()) {
        return {};
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(text, &error);
    if (error.error != QJsonParseError::NoError || document.isNull()) {
        return {};
    }
    return document.isObject() ? QVariant(document.object().toVariantMap())
                               : QVariant(document.array().toVariantList());
}

QVariantList toList(const QByteArray &text)
{
    const QVariant value = fromJson(text);
    return value.typeId() == QMetaType::QVariantList ? value.toList() : QVariantList();
}

QVariantMap toMap(const QByteArray &text)
{
    const QVariant value = fromJson(text);
    return value.typeId() == QMetaType::QVariantMap ? value.toMap() : QVariantMap();
}

QByteArray compact(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray compact(const QJsonArray &array)
{
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

QJsonArray jsonArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QStringList toStringList(const QVariant &value)
{
    if (value.typeId() == QMetaType::QStringList) {
        return value.toStringList();
    }
    QStringList result;
    const QVariantList list = value.toList();
    result.reserve(list.size());
    for (const QVariant &item : list) {
        result << item.toString();
    }
    return result;
}

AppEntry entryFromJson(const QVariantMap &map)
{
    AppEntry entry;
    entry.id = map.value("id").toString();
    entry.name = map.value("name").toString();
    entry.genericName = map.value("genericName").toString();
    entry.comment = map.value("comment").toString();
    entry.exec = map.value("exec").toString();
    entry.tryExec = map.value("tryExec").toString();
    entry.path = map.value("path").toString();
    entry.icon = map.value("icon").toString();
    entry.categories = toStringList(map.value("categories"));
    entry.keywords = toStringList(map.value("keywords"));
    entry.mimeTypes = toStringList(map.value("mimeTypes"));
    entry.onlyShowIn = toStringList(map.value("onlyShowIn"));
    entry.notShowIn = toStringList(map.value("notShowIn"));
    entry.actions = map.value("actions").toList();
    entry.terminal = map.value("terminal").toBool();
    entry.noDisplay = map.value("noDisplay").toBool();
    entry.hidden = map.value("hidden").toBool();
    entry.startupNotify = map.value("startupNotify").toBool();
    entry.dbusActivatable = map.value("dbusActivatable").toBool();
    entry.startupWmClass = map.value("startupWMClass").toString();
    entry.sortKey = map.value("sortKey").toString();
    entry.priority = map.value("priority").toInt();
    return entry;
}

#else // !NEWTELLER_HAVE_RUST - every call below is a stub returning nothing.

AppEntry entryFromJson(const QVariantMap &) { return {}; }

#endif // NEWTELLER_HAVE_RUST

} // namespace

namespace Rust {

bool available()
{
#ifdef NEWTELLER_HAVE_RUST
    static const bool matched = (nt_abi_version() == NT_ABI_VERSION);
    return matched;
#else
    return false;
#endif
}

QString version()
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return {};
    }
    const RustString text(nt_version());
    return text.text();
#else
    return {};
#endif
}

QString lastError()
{
#ifdef NEWTELLER_HAVE_RUST
    const RustString text(nt_last_error());
    return text.text();
#else
    return {};
#endif
}

// ---------------------------------------------------------------------------
// Index
// ---------------------------------------------------------------------------

Index::~Index()
{
#ifdef NEWTELLER_HAVE_RUST
    if (m_handle != nullptr) {
        nt_index_free(m_handle);
        m_handle = nullptr;
    }
#endif
}

Index::Index(Index &&other) noexcept : m_handle(other.m_handle)
{
    other.m_handle = nullptr;
}

Index &Index::operator=(Index &&other) noexcept
{
    if (this != &other) {
#ifdef NEWTELLER_HAVE_RUST
        if (m_handle != nullptr) {
            nt_index_free(m_handle);
        }
#endif
        m_handle = other.m_handle;
        other.m_handle = nullptr;
    }
    return *this;
}

bool Index::scan(const ScanOptions &options)
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return false;
    }

    QJsonObject request;
    request["dirs"] = jsonArray(options.dirs);
    request["locale"] = options.locale;
    request["desktopEnv"] = options.desktopEnv;
    request["maxApps"] = qMax(0, options.maxApps);
    request["maxDepth"] = qMax(1, options.maxDepth);
    request["includeHidden"] = options.includeHidden;
    request["expandHome"] = true;
    const QByteArray bytes = compact(request);

    if (m_handle != nullptr) {
        nt_index_free(m_handle);
        m_handle = nullptr;
    }
    m_handle = nt_index_from_scan(bytes.constData());
    return m_handle != nullptr;
#else
    Q_UNUSED(options);
    return false;
#endif
}

bool Index::build(const QByteArray &appsJson)
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return false;
    }
    if (m_handle != nullptr) {
        nt_index_free(m_handle);
        m_handle = nullptr;
    }
    m_handle = nt_index_new(appsJson.constData());
    return m_handle != nullptr;
#else
    Q_UNUSED(appsJson);
    return false;
#endif
}

int Index::count() const
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return 0;
    }
    const intptr_t value = nt_index_len(m_handle);
    return value > 0 ? static_cast<int>(value) : 0;
#else
    return 0;
#endif
}

QVector<AppEntry> Index::entries() const
{
#ifdef NEWTELLER_HAVE_RUST
    QVector<AppEntry> entries;
    if (!available() || m_handle == nullptr) {
        return entries;
    }
    const RustString text(nt_index_apps(m_handle));
    const QVariantList list = toList(text.bytes());
    entries.reserve(list.size());
    for (const QVariant &item : list) {
        entries << entryFromJson(item.toMap());
    }
    return entries;
#else
    return {};
#endif
}

QVector<AppAction> Index::actionsOf(const QVariantList &actions)
{
    QVector<AppAction> result;
    result.reserve(actions.size());
    for (const QVariant &item : actions) {
        const QVariantMap map = item.toMap();
        AppAction action;
        action.id = map.value("id").toString();
        action.name = map.value("name").toString();
        action.exec = map.value("exec").toString();
        action.icon = map.value("icon").toString();
        result << action;
    }
    return result;
}

QVariantList Index::search(const QString &query,
                           int limit,
                           const QString &highlight,
                           bool fuzzy) const
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available() || m_handle == nullptr) {
        return {};
    }
    const QByteArray queryBytes = query.toUtf8();
    const QByteArray highlightBytes = highlight.toUtf8();

    quint32 flags = NT_SEARCH_KEYWORDS | NT_SEARCH_META;
    if (fuzzy) {
        flags |= NT_SEARCH_FUZZY;
    }
    if (!highlightBytes.isEmpty()) {
        flags |= NT_SEARCH_HIGHLIGHT;
    }

    const RustString text(nt_index_search(m_handle,
                                          queryBytes.constData(),
                                          static_cast<uint32_t>(qMax(0, limit)),
                                          flags,
                                          highlightBytes.isEmpty() ? nullptr : highlightBytes.constData()));
    return toMap(text.bytes()).value("hits").toList();
#else
    Q_UNUSED(query);
    Q_UNUSED(limit);
    Q_UNUSED(highlight);
    Q_UNUSED(fuzzy);
    return {};
#endif
}

void Index::setUsage(const QHash<QString, int> &usage)
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available() || m_handle == nullptr || usage.isEmpty()) {
        return;
    }
    QJsonObject object;
    for (auto it = usage.constBegin(); it != usage.constEnd(); ++it) {
        object[it.key()] = it.value();
    }
    const QByteArray bytes = compact(object);
    nt_index_set_usage(m_handle, bytes.constData());
#else
    Q_UNUSED(usage);
#endif
}

QVariantList Index::running() const
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available() || m_handle == nullptr) {
        return {};
    }
    const RustString text(nt_index_running(m_handle));
    return toList(text.bytes());
#else
    return {};
#endif
}

QStringList Index::runningIds() const
{
    QStringList ids;
    const QVariantList running = Index::running();
    ids.reserve(running.size());
    for (const QVariant &item : running) {
        const QString id = item.toMap().value("id").toString();
        if (!id.isEmpty()) {
            ids << id;
        }
    }
    return ids;
}

QVariantList Index::categories() const
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available() || m_handle == nullptr) {
        return {};
    }
    const RustString text(nt_index_categories(m_handle));
    return toList(text.bytes());
#else
    return {};
#endif
}

QVariantMap Index::stats() const
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available() || m_handle == nullptr) {
        return {};
    }
    const RustString text(nt_index_stats(m_handle));
    return toMap(text.bytes());
#else
    return {};
#endif
}

QVariantMap Index::report() const
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available() || m_handle == nullptr) {
        return {};
    }
    const RustString text(nt_index_scan_report(m_handle));
    return toMap(text.bytes());
#else
    return {};
#endif
}

QStringList Index::execArgv(const QString &exec,
                            const QString &icon,
                            const QString &name,
                            const QString &desktopFile,
                            const QStringList &files,
                            bool terminal,
                            const QString &terminalCommand)
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return {};
    }
    QJsonObject request;
    request["exec"] = exec;
    request["icon"] = icon;
    request["name"] = name;
    request["desktopFile"] = desktopFile;
    request["files"] = jsonArray(files);
    request["terminal"] = terminal;
    if (!terminalCommand.isEmpty()) {
        request["terminalCommand"] = terminalCommand;
    }
    const QByteArray bytes = compact(request);

    const RustString text(nt_exec_argv(bytes.constData()));
    QStringList argv;
    const QVariantList list = toList(text.bytes());
    argv.reserve(list.size());
    for (const QVariant &item : list) {
        argv << item.toString();
    }
    return argv;
#else
    Q_UNUSED(exec);
    Q_UNUSED(icon);
    Q_UNUSED(name);
    Q_UNUSED(desktopFile);
    Q_UNUSED(files);
    Q_UNUSED(terminal);
    Q_UNUSED(terminalCommand);
    return {};
#endif
}

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------

Metrics::Metrics()
{
#ifdef NEWTELLER_HAVE_RUST
    if (available()) {
        m_handle = nt_metrics_new();
    }
#endif
}

Metrics::~Metrics()
{
#ifdef NEWTELLER_HAVE_RUST
    if (m_handle != nullptr) {
        nt_metrics_free(m_handle);
        m_handle = nullptr;
    }
#endif
}

QVariantMap Metrics::snapshot(quint32 flags) const
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available() || m_handle == nullptr) {
        return {};
    }
    const RustString text(nt_metrics_snapshot(m_handle, flags));
    return toMap(text.bytes());
#else
    Q_UNUSED(flags);
    return {};
#endif
}

QVariantList Metrics::processes(int limit) const
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available() || m_handle == nullptr) {
        return {};
    }
    const RustString text(nt_metrics_processes(m_handle, static_cast<uint32_t>(qMax(0, limit))));
    return toList(text.bytes());
#else
    Q_UNUSED(limit);
    return {};
#endif
}

void Metrics::reset() const
{
#ifdef NEWTELLER_HAVE_RUST
    if (available() && m_handle != nullptr) {
        nt_metrics_reset(m_handle);
    }
#endif
}

// ---------------------------------------------------------------------------
// Watcher
// ---------------------------------------------------------------------------

Watcher::Watcher(int debounceMs)
{
#ifdef NEWTELLER_HAVE_RUST
    if (available()) {
        m_handle = nt_watcher_new(static_cast<uint32_t>(qMax(0, debounceMs)));
    }
#else
    Q_UNUSED(debounceMs);
#endif
}

Watcher::~Watcher()
{
#ifdef NEWTELLER_HAVE_RUST
    if (m_handle != nullptr) {
        nt_watcher_free(m_handle);
        m_handle = nullptr;
    }
#endif
}

int Watcher::descriptor() const
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available() || m_handle == nullptr) {
        return -1;
    }
    const int32_t value = nt_watcher_fd(m_handle);
    return value >= 0 ? static_cast<int>(value) : -1;
#else
    return -1;
#endif
}

bool Watcher::add(const QString &path, bool recursive)
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available() || m_handle == nullptr) {
        return false;
    }
    const QByteArray bytes = path.toUtf8();
    return nt_watcher_add(m_handle, bytes.constData(), recursive) == 0;
#else
    Q_UNUSED(path);
    Q_UNUSED(recursive);
    return false;
#endif
}

bool Watcher::remove(const QString &path)
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available() || m_handle == nullptr) {
        return false;
    }
    const QByteArray bytes = path.toUtf8();
    return nt_watcher_remove(m_handle, bytes.constData()) == 0;
#else
    Q_UNUSED(path);
    return false;
#endif
}

QVariantList Watcher::poll()
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available() || m_handle == nullptr) {
        return {};
    }
    const RustString text(nt_watcher_poll(m_handle));
    return toList(text.bytes());
#else
    return {};
#endif
}

qint64 Watcher::pendingInMs() const
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available() || m_handle == nullptr) {
        return -1;
    }
    return static_cast<qint64>(nt_watcher_pending_in_ms(m_handle));
#else
    return -1;
#endif
}

// ---------------------------------------------------------------------------
// files, configuration, formatting
// ---------------------------------------------------------------------------

bool writeFileAtomic(const QString &path, const QByteArray &data, bool backup)
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return false;
    }
    const QByteArray bytes = path.toUtf8();
    quint32 flags = NT_WRITE_FSYNC | NT_WRITE_CREATE_PARENTS;
    if (backup) {
        flags |= NT_WRITE_BACKUP;
    }
    const int32_t result = nt_write_file_atomic(bytes.constData(),
                                                reinterpret_cast<const uint8_t *>(data.constData()),
                                                static_cast<size_t>(data.size()),
                                                flags);
    return result == 0;
#else
    Q_UNUSED(path);
    Q_UNUSED(data);
    Q_UNUSED(backup);
    return false;
#endif
}

QByteArray readFile(const QString &path)
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return {};
    }
    const QByteArray bytes = path.toUtf8();
    const RustString text(nt_read_file(bytes.constData()));
    return text.bytes();
#else
    Q_UNUSED(path);
    return {};
#endif
}

QVariantMap loadConfig(const QString &path)
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return {};
    }
    const QByteArray bytes = path.toUtf8();
    const RustString text(nt_load_config(bytes.constData()));
    return toMap(text.bytes());
#else
    Q_UNUSED(path);
    return {};
#endif
}

QVariantList configSchema()
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return {};
    }
    const RustString text(nt_config_schema());
    return toList(text.bytes());
#else
    return {};
#endif
}

QVariantMap configDefaults()
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return {};
    }
    const RustString text(nt_config_defaults());
    return toMap(text.bytes());
#else
    return {};
#endif
}

QVariantList validateConfig(const QVariantMap &config)
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return {};
    }
    const QByteArray bytes = compact(QJsonObject::fromVariantMap(config));
    const RustString text(nt_config_validate(bytes.constData()));
    return toList(text.bytes());
#else
    Q_UNUSED(config);
    return {};
#endif
}

QVariantMap diagnostics()
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return {};
    }
    const RustString text(nt_diagnostics());
    return toMap(text.bytes());
#else
    return {};
#endif
}

QString resolvePath(const QString &path)
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return {};
    }
    const QByteArray bytes = path.toUtf8();
    const RustString text(nt_resolve_path(bytes.constData()));
    const QString resolved = text.text();
    return resolved.isEmpty() ? path : resolved;
#else
    Q_UNUSED(path);
    return {};
#endif
}

QString formatBytes(quint64 bytes)
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return {};
    }
    const RustString text(nt_format_bytes(bytes));
    return text.text();
#else
    Q_UNUSED(bytes);
    return {};
#endif
}

QString formatRate(double bytesPerSecond)
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return {};
    }
    const RustString text(nt_format_rate(bytesPerSecond));
    return text.text();
#else
    Q_UNUSED(bytesPerSecond);
    return {};
#endif
}

QString formatDuration(double seconds)
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return {};
    }
    const RustString text(nt_format_duration(seconds));
    return text.text();
#else
    Q_UNUSED(seconds);
    return {};
#endif
}

int fuzzyMatch(const QString &query, const QString &candidate, bool fuzzy)
{
#ifdef NEWTELLER_HAVE_RUST
    if (!available()) {
        return 0;
    }
    const QByteArray queryBytes = query.toUtf8();
    const QByteArray candidateBytes = candidate.toUtf8();
    const uint32_t flags = fuzzy ? NT_SEARCH_FUZZY : 0u;
    const int32_t score = nt_fuzzy_match(queryBytes.constData(), candidateBytes.constData(), flags);
    return score > 0 ? static_cast<int>(score) : 0;
#else
    Q_UNUSED(query);
    Q_UNUSED(candidate);
    Q_UNUSED(fuzzy);
    return 0;
#endif
}

} // namespace Rust
