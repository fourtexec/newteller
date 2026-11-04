#include "clipboardhistory.h"

#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

#include <csignal>
#include <sys/prctl.h>

namespace {

// Separates the entries wl-paste prints: one record separator after each.
constexpr char kSeparator = '\x1e';
// A huge paste (a file's worth of text) is not worth keeping in a menu.
constexpr int kMaxEntryLength = 64 * 1024;

} // namespace

ClipboardHistory::ClipboardHistory(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_available = !QStandardPaths::findExecutable(QStringLiteral("wl-paste")).isEmpty()
                  && !QStandardPaths::findExecutable(QStringLiteral("wl-copy")).isEmpty()
                  && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");

    m_watcher.setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
    connect(&m_watcher, &QProcess::readyReadStandardOutput, this, &ClipboardHistory::readWatcher);

    connect(m_settings, &Settings::changed, this, [this](const QString &key) {
        if (!key.isEmpty() && !key.startsWith(QLatin1String("clipboard.")))
            return;
        if (enabled())
            start();
        else
            stop();
        trim();
        if (persistent())
            save();
        else
            QFile::remove(storePath());
    });

    if (persistent())
        load();
    if (enabled())
        start();
}

ClipboardHistory::~ClipboardHistory()
{
    stop();
}

bool ClipboardHistory::enabled() const
{
    return m_available && m_settings->value(QStringLiteral("clipboard.historyEnabled")).toBool();
}

bool ClipboardHistory::persistent() const
{
    return m_settings->value(QStringLiteral("clipboard.persistHistory")).toBool();
}

QString ClipboardHistory::storePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/newteller/clipboard.json");
}

void ClipboardHistory::start()
{
    if (m_watcher.state() != QProcess::NotRunning)
        return;
    // For every new text selection wl-paste runs the command with the text
    // on stdin; the command echoes it followed by the separator. Passwords
    // that a password manager marks as secret (x-kde-passwordManagerHint;
    // wl-paste reports CLIPBOARD_STATE=sensitive) never enter the history.
    m_watcher.start(QStringLiteral("wl-paste"),
                    {QStringLiteral("--no-newline"), QStringLiteral("--type"), QStringLiteral("text"),
                     QStringLiteral("--watch"), QStringLiteral("sh"), QStringLiteral("-c"),
                     QStringLiteral("if [ \"${CLIPBOARD_STATE:-}\" = sensitive ]; then "
                                    "cat >/dev/null; else cat; printf '\\036'; fi")});
}

void ClipboardHistory::stop()
{
    if (m_watcher.state() == QProcess::NotRunning)
        return;
    m_watcher.terminate();
    if (!m_watcher.waitForFinished(500))
        m_watcher.kill();
}

void ClipboardHistory::readWatcher()
{
    m_buffer += m_watcher.readAllStandardOutput();
    int separator;
    while ((separator = m_buffer.indexOf(kSeparator)) >= 0) {
        const QString text = QString::fromUtf8(m_buffer.left(separator));
        m_buffer.remove(0, separator + 1);
        add(text);
    }
    if (m_buffer.size() > kMaxEntryLength * 4)
        m_buffer.clear();
}

void ClipboardHistory::add(const QString &text)
{
    if (text.trimmed().isEmpty() || text.size() > kMaxEntryLength)
        return;
    if (!m_ownCopy.isNull() && text == m_ownCopy) {
        m_ownCopy.clear();
        return;
    }
    m_items.removeAll(text);
    m_items.prepend(text);
    trim();
    emit itemsChanged();
    if (persistent())
        save();
}

void ClipboardHistory::trim()
{
    const int limit = qBound(1, m_settings->value(QStringLiteral("clipboard.historySize")).toInt(), 1000);
    if (m_items.size() > limit) {
        m_items = m_items.mid(0, limit);
        emit itemsChanged();
    }
}

void ClipboardHistory::copy(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    const QString text = m_items.at(index);
    m_items.removeAt(index);
    m_items.prepend(text);
    emit itemsChanged();
    m_ownCopy = text;
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    process->start(QStringLiteral("wl-copy"), {});
    process->write(text.toUtf8());
    process->closeWriteChannel();
    if (persistent())
        save();
}

void ClipboardHistory::remove(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    m_items.removeAt(index);
    emit itemsChanged();
    if (persistent())
        save();
}

void ClipboardHistory::clear()
{
    m_items.clear();
    emit itemsChanged();
    QFile::remove(storePath());
}

void ClipboardHistory::load()
{
    QFile file(storePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue &value : array) {
        if (value.isString())
            m_items.append(value.toString());
    }
    trim();
    emit itemsChanged();
}

void ClipboardHistory::save() const
{
    const QString path = storePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(QJsonArray::fromStringList(m_items)).toJson(QJsonDocument::Compact));
    file.commit();
}
