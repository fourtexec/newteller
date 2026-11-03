#pragma once

#include <QDBusObjectPath>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <functional>

// Bluetooth through BlueZ on the system bus (QML: Bluetooth).
//
// The first adapter is used. Devices come from BlueZ's object manager and
// are re-read (debounced) whenever BlueZ reports a change. Pairing uses
// BlueZ's default "just works" exchange, which covers headphones, mice,
// speakers and most keyboards; devices that need a PIN can still be paired
// with blueman.
class Bluetooth : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool powered READ powered NOTIFY changed)
    Q_PROPERTY(bool discovering READ discovering NOTIFY changed)
    // path, address, name, icon, paired, connected, trusted
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    // Something is being connected or paired: path of the device.
    Q_PROPERTY(QString busyDevice READ busyDevice NOTIFY changed)

public:
    explicit Bluetooth(QObject *parent = nullptr);

    bool available() const { return !m_adapter.isEmpty(); }
    bool powered() const { return m_powered; }
    bool discovering() const { return m_discovering; }
    QVariantList devices() const { return m_devices; }
    QString busyDevice() const { return m_busy; }

    Q_INVOKABLE void setPowered(bool on);
    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE void stopDiscovery();
    Q_INVOKABLE void connectDevice(const QString &path);
    Q_INVOKABLE void disconnectDevice(const QString &path);
    // Pairs, trusts and connects a new device.
    Q_INVOKABLE void pairDevice(const QString &path);
    Q_INVOKABLE void forgetDevice(const QString &path);

signals:
    void changed();
    // A connect or pair finished; `error` is empty on success.
    void operationFinished(const QString &path, const QString &error);

private slots:
    void scheduleRefresh();

private:
    void refresh();
    void call(const QString &path, const QString &interface, const QString &method,
              const QVariantList &arguments, const QString &busyPath,
              std::function<void()> then = nullptr);

    QString m_adapter;
    bool m_powered = false;
    bool m_discovering = false;
    QVariantList m_devices;
    QString m_busy;
    QTimer m_refresh;
};
