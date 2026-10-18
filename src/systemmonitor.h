#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "rustbridge.h"

class Settings;

// Live system metrics for the panel widgets and the settings view. Exposed to
// QML as the context property `System`.
//
// One shared ticker serves every widget: the Rust core keeps the previous
// samples internally so rates (CPU %, network) are real, and a small /proc
// fallback covers builds without Rust. All getters read cached members that
// are refreshed together, so a widget layout never sees half-updated values.
class SystemMonitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(int refreshInterval READ refreshInterval WRITE setRefreshInterval NOTIFY refreshIntervalChanged)
    Q_PROPERTY(double cpuPercent READ cpuPercent NOTIFY snapshotUpdated)
    Q_PROPERTY(int cpuCount READ cpuCount NOTIFY snapshotUpdated)
    Q_PROPERTY(QString cpuModel READ cpuModel NOTIFY snapshotUpdated)
    Q_PROPERTY(double frequencyMhz READ frequencyMhz NOTIFY snapshotUpdated)
    Q_PROPERTY(double load1 READ load1 NOTIFY snapshotUpdated)
    Q_PROPERTY(double load5 READ load5 NOTIFY snapshotUpdated)
    Q_PROPERTY(double load15 READ load15 NOTIFY snapshotUpdated)
    Q_PROPERTY(QVariantList perCore READ perCore NOTIFY snapshotUpdated)
    Q_PROPERTY(double memoryPercent READ memoryPercent NOTIFY snapshotUpdated)
    Q_PROPERTY(QString memoryText READ memoryText NOTIFY snapshotUpdated)
    Q_PROPERTY(QString memoryUsedText READ memoryUsedText NOTIFY snapshotUpdated)
    Q_PROPERTY(QString memoryTotalText READ memoryTotalText NOTIFY snapshotUpdated)
    Q_PROPERTY(double swapPercent READ swapPercent NOTIFY snapshotUpdated)
    Q_PROPERTY(QString networkDownText READ networkDownText NOTIFY snapshotUpdated)
    Q_PROPERTY(QString networkUpText READ networkUpText NOTIFY snapshotUpdated)
    Q_PROPERTY(double networkDown READ networkDown NOTIFY snapshotUpdated)
    Q_PROPERTY(double networkUp READ networkUp NOTIFY snapshotUpdated)
    Q_PROPERTY(bool batteryPresent READ batteryPresent NOTIFY snapshotUpdated)
    Q_PROPERTY(double batteryPercent READ batteryPercent NOTIFY snapshotUpdated)
    Q_PROPERTY(QString batteryStatus READ batteryStatus NOTIFY snapshotUpdated)
    Q_PROPERTY(bool batteryCharging READ batteryCharging NOTIFY snapshotUpdated)
    Q_PROPERTY(bool batteryCritical READ batteryCritical NOTIFY snapshotUpdated)
    Q_PROPERTY(QString batteryTimeText READ batteryTimeText NOTIFY snapshotUpdated)
    Q_PROPERTY(QString temperatureText READ temperatureText NOTIFY snapshotUpdated)
    Q_PROPERTY(QVariantList thermalZones READ thermalZones NOTIFY snapshotUpdated)
    Q_PROPERTY(double diskPercent READ diskPercent NOTIFY snapshotUpdated)
    Q_PROPERTY(QString diskFreeText READ diskFreeText NOTIFY snapshotUpdated)
    Q_PROPERTY(QVariantList disks READ disks NOTIFY snapshotUpdated)
    Q_PROPERTY(int processes READ processes NOTIFY snapshotUpdated)
    Q_PROPERTY(int userProcesses READ userProcesses NOTIFY snapshotUpdated)
    Q_PROPERTY(QString uptimeText READ uptimeText NOTIFY snapshotUpdated)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY snapshotUpdated)

public:
    explicit SystemMonitor(Settings *settings, QObject *parent = nullptr);

    bool available() const { return m_available; }
    int refreshInterval() const { return m_refreshInterval; }
    void setRefreshInterval(int milliseconds);

    double cpuPercent() const { return m_cpuPercent; }
    int cpuCount() const { return m_cpuCount; }
    QString cpuModel() const { return m_cpuModel; }
    double frequencyMhz() const { return m_frequencyMhz; }
    double load1() const { return m_load1; }
    double load5() const { return m_load5; }
    double load15() const { return m_load15; }
    QVariantList perCore() const { return m_perCore; }
    double memoryPercent() const { return m_memoryPercent; }
    QString memoryText() const { return m_memoryText; }
    QString memoryUsedText() const { return m_memoryUsedText; }
    QString memoryTotalText() const { return m_memoryTotalText; }
    double swapPercent() const { return m_swapPercent; }
    QString networkDownText() const { return m_networkDownText; }
    QString networkUpText() const { return m_networkUpText; }
    double networkDown() const { return m_networkDown; }
    double networkUp() const { return m_networkUp; }
    bool batteryPresent() const { return m_batteryPresent; }
    double batteryPercent() const { return m_batteryPercent; }
    QString batteryStatus() const { return m_batteryStatus; }
    bool batteryCharging() const { return m_batteryCharging; }
    bool batteryCritical() const { return m_batteryCritical; }
    QString batteryTimeText() const { return m_batteryTimeText; }
    QString temperatureText() const { return m_temperatureText; }
    QVariantList thermalZones() const { return m_thermalZones; }
    double diskPercent() const { return m_diskPercent; }
    QString diskFreeText() const { return m_diskFreeText; }
    QVariantList disks() const { return m_disks; }
    int processes() const { return m_processes; }
    int userProcesses() const { return m_userProcesses; }
    QString uptimeText() const { return m_uptimeText; }
    QVariantMap snapshot() const { return m_snapshot; }

    // Takes one sample immediately (the timer keeps running as usual).
    Q_INVOKABLE void refresh();
    // The process list, largest memory users first, at most `limit` rows.
    Q_INVOKABLE QVariantList processesList(int limit);
    // Forgets the previous samples, for example after resume from suspend.
    Q_INVOKABLE void reset();
    Q_INVOKABLE QString formatBytes(qint64 bytes) const;
    Q_INVOKABLE QString formatRate(double bytesPerSecond) const;
    // Replaces the sampling flags; see the Rust::Metrics constants.
    Q_INVOKABLE void setFlags(quint32 flags);

signals:
    void snapshotUpdated();
    void refreshIntervalChanged(int milliseconds);

private:
    void applySnapshot(const QVariantMap &map);
    void clearSnapshot();
    // /proc fallback used when the Rust core is not compiled in.
    void sampleWithQt();
    quint32 flagsFromSettings() const;

    Settings *m_settings = nullptr;
    Rust::Metrics m_metrics;
    QTimer m_timer;
    QVariantMap m_snapshot;
    int m_refreshInterval = 2000;
    quint32 m_flags = 0;
    bool m_available = false;

    // Previous fallback samples, for the rates.
    qint64 m_prevTotalJiffies = 0;
    qint64 m_prevIdleJiffies = 0;
    QList<qint64> m_prevCoreTotal;
    QList<qint64> m_prevCoreIdle;
    qint64 m_prevNetRx = -1;
    qint64 m_prevNetTx = -1;
    qint64 m_prevNetMs = 0;

    // Cached members, refreshed together by applySnapshot().
    double m_cpuPercent = 0.0;
    int m_cpuCount = 0;
    QString m_cpuModel;
    double m_frequencyMhz = 0.0;
    double m_load1 = 0.0;
    double m_load5 = 0.0;
    double m_load15 = 0.0;
    QVariantList m_perCore;
    double m_memoryPercent = 0.0;
    QString m_memoryText;
    QString m_memoryUsedText;
    QString m_memoryTotalText;
    double m_swapPercent = 0.0;
    QString m_networkDownText;
    QString m_networkUpText;
    double m_networkDown = 0.0;
    double m_networkUp = 0.0;
    bool m_batteryPresent = false;
    double m_batteryPercent = 0.0;
    QString m_batteryStatus;
    bool m_batteryCharging = false;
    bool m_batteryCritical = false;
    QString m_batteryTimeText;
    QString m_temperatureText;
    QVariantList m_thermalZones;
    double m_diskPercent = 0.0;
    QString m_diskFreeText;
    QVariantList m_disks;
    int m_processes = 0;
    int m_userProcesses = 0;
    QString m_uptimeText;
};
