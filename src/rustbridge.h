#pragma once

// Thin, RAII wrapper around the C ABI of the Rust core
// (rust/crates/newteller-core, header include/newteller_core.h).
//
// Everything in this file degrades gracefully: when the project is configured
// with -DNEWTELLER_WITH_RUST=OFF, or when the loaded library reports a
// different ABI version, `Rust::available()` is false and every call returns an
// empty result instead of touching a null pointer. Callers then use their C++
// fallbacks, so the shell keeps working without a Rust toolchain.
//
// Ownership rules, mirrored from the header:
//   * every `char *` from the core is released by RustString with nt_free();
//   * handles are created and released here only, never copied;
//   * none of this is thread safe - call it from the GUI thread, like Qt.

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "appentry.h" // AppEntry

// Opaque handles; the real declarations live in newteller_core.h.
struct NtIndex;
struct NtMetrics;
struct NtWatcher;

namespace Rust {

/// True when the Rust core was compiled in and its ABI matches the header.
bool available();
/// Version string of the core, empty when unavailable.
QString version();
/// Reason of the last failure, for qWarning().
QString lastError();

/// Options of one application scan.
struct ScanOptions {
    QStringList dirs;      ///< Application directories, most important first.
    QString locale;        ///< LANG style locale; empty reads the environment.
    QString desktopEnv;    ///< XDG_CURRENT_DESKTOP; empty reads the environment.
    int maxApps = 0;       ///< 0 means no limit.
    int maxDepth = 3;      ///< Recursion depth inside each directory.
    bool includeHidden = false; ///< Keep NoDisplay entries.
};

/// One desktop action, from a `[Desktop Action ...]` group.
struct AppAction {
    QString id;
    QString name;
    QString exec;
    QString icon;
};

/// Application catalogue plus the search structures built from it.
///
/// Movable but not copyable: it owns a Rust handle.
class Index {
public:
    Index() = default;
    ~Index();
    Index(const Index &) = delete;
    Index &operator=(const Index &) = delete;
    Index(Index &&other) noexcept;
    Index &operator=(Index &&other) noexcept;

    /// Scans the directories and builds the index. Returns false on failure.
    bool scan(const ScanOptions &options);
    /// Builds the index from a JSON array of desktop entries.
    bool build(const QByteArray &appsJson);

    bool isValid() const { return m_handle != nullptr; }
    int count() const;

    /// Every application, in display order.
    QVector<AppEntry> entries() const;
    /// Actions of one application, from its `actions` list.
    static QVector<AppAction> actionsOf(const QVariantList &actions);

    /// Runs a query. Returns a list of maps with the keys documented in
    /// newteller_core.h: id, name, icon, comment, category, score, kind, field,
    /// highlight, usage.
    QVariantList search(const QString &query, int limit, const QString &highlight, bool fuzzy) const;

    /// Launch counters, used to bias the ranking.
    void setUsage(const QHash<QString, int> &usage);

    /// Running applications: list of {id, count, pids}.
    QVariantList running() const;
    /// Ids of the running applications, for the cheap QML lookups.
    QStringList runningIds() const;
    /// Categories with their application counts.
    QVariantList categories() const;
    /// Index size and build cost.
    QVariantMap stats() const;
    /// Report of the scan that built the index.
    QVariantMap report() const;

    /// Expands an Exec line into argv, applying the quoting and field code
    /// rules of the desktop entry specification.
    static QStringList execArgv(const QString &exec,
                                const QString &icon,
                                const QString &name,
                                const QString &desktopFile,
                                const QStringList &files,
                                bool terminal,
                                const QString &terminalCommand);

private:
    NtIndex *m_handle = nullptr;
};

/// System metrics sampler. Keeps the previous samples internally, so the second
/// and later snapshots report real rates.
class Metrics {
public:
    Metrics();
    ~Metrics();
    Metrics(const Metrics &) = delete;
    Metrics &operator=(const Metrics &) = delete;

    bool isValid() const { return m_handle != nullptr; }
    /// One sample as a QVariantMap, empty when unavailable.
    QVariantMap snapshot(quint32 flags) const;
    /// The process list.
    QVariantList processes(int limit) const;
    /// Forget the previous samples, for example after resume from suspend.
    void reset() const;

    // Flag values, kept in sync with newteller_core.h.
    static constexpr quint32 Disks = 1;
    static constexpr quint32 Thermal = 2;
    static constexpr quint32 Battery = 4;
    static constexpr quint32 Processes = 8;
    static constexpr quint32 Uname = 16;
    static constexpr quint32 PerCore = 32;

private:
    NtMetrics *m_handle = nullptr;
};

/// Debounced inotify watcher driven by a QSocketNotifier on the C++ side.
class Watcher {
public:
    explicit Watcher(int debounceMs);
    ~Watcher();
    Watcher(const Watcher &) = delete;
    Watcher &operator=(const Watcher &) = delete;

    bool isValid() const { return m_handle != nullptr; }
    /// Descriptor for QSocketNotifier, -1 when unavailable.
    int descriptor() const;
    bool add(const QString &path, bool recursive);
    bool remove(const QString &path);
    /// Events whose debounce window expired: list of {path, kind, isDir, mask}.
    QVariantList poll();
    /// Milliseconds until the next event is ready, -1 when none is pending.
    qint64 pendingInMs() const;

private:
    NtWatcher *m_handle = nullptr;
};

// --- files and configuration ---------------------------------------------

/// Atomic write: temporary file, fsync, rename, optional `.bak` backup.
bool writeFileAtomic(const QString &path, const QByteArray &data, bool backup = true);
/// Reads a file, empty when it cannot be read.
QByteArray readFile(const QString &path);
/// Loads a configuration file, falling back to its backup:
/// {value: object, source: "file"|"backup"|"defaults", error: string|null}.
QVariantMap loadConfig(const QString &path);
/// The schema of shell.json: list of {key, kind, default, doc}.
QVariantList configSchema();
/// The default configuration object.
QVariantMap configDefaults();
/// Problems in a configuration object: list of {key, level, message}.
QVariantList validateConfig(const QVariantMap &config);
/// Machine and configuration diagnostics.
QVariantMap diagnostics();
/// Expands `~/` and returns an absolute path.
QString resolvePath(const QString &path);

// --- formatting, so C++ and the CLI print numbers identically ------------

QString formatBytes(quint64 bytes);
QString formatRate(double bytesPerSecond);
QString formatDuration(double seconds);

/// Score of one candidate against a query, same ranking as Index::search.
/// 0 means no match. Exposed to QML for plugins.
int fuzzyMatch(const QString &query, const QString &candidate, bool fuzzy);

} // namespace Rust
