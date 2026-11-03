#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

class Settings;

// Clipboard history (QML: Clipboard), opened with Super+V.
//
// `wl-paste --watch` (wl-clipboard, over the compositor's wlr-data-control)
// reports every text copied in the session; the newest entries are kept,
// up to clipboard.historySize. Picking one puts it back on the clipboard
// with wl-copy. The history stays in memory unless clipboard.persistHistory
// is on, in which case it is saved to ~/.local/share/newteller/clipboard.json
// (readable by the user only).
class ClipboardHistory : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QStringList items READ items NOTIFY itemsChanged)

public:
    explicit ClipboardHistory(Settings *settings, QObject *parent = nullptr);
    ~ClipboardHistory() override;

    bool available() const { return m_available; }
    QStringList items() const { return m_items; }

    Q_INVOKABLE void copy(int index);
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void clear();

signals:
    void itemsChanged();

private:
    void start();
    void stop();
    void readWatcher();
    void add(const QString &text);
    void trim();
    void load();
    void save() const;
    QString storePath() const;
    bool enabled() const;
    bool persistent() const;

    Settings *m_settings;
    bool m_available = false;
    QProcess m_watcher;
    QByteArray m_buffer;
    QStringList m_items;
    // The text we just put back ourselves; its echo is not a new entry.
    QString m_ownCopy;
};
