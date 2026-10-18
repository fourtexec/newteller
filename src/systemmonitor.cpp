#include "systemmonitor.h"

#include "settings.h"

#include <QDateTime>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace {

// Number formatting that matches the Rust core (`1.5 GiB`, `3.2 MiB/s`).
QString bytesText(double bytes)
{
    if (!(bytes > 0.0))
        return QStringLiteral("0 B");
    static const char *const units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    int unit = 0;
    double value = bytes;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        return QStringLiteral("%1 B").arg(qRound64(value));
    return QStringLiteral("%1 %2").arg(value, 0, 'f', 1).arg(QLatin1String(units[unit]));
}

QString durationText(double seconds)
{
    if (!(seconds > 0.0))
        return QString();
    const qint64 total = qint64(seconds);
    const qint64 days = total / 86400;
    const qint64 hours = (total / 3600) % 24;
    const qint64 minutes = (total / 60) % 60;
    const qint64 secs = total % 60;
    if (days > 0)
        return QStringLiteral("%1 d %2:%3")
            .arg(days)
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
}

// "key: value kB" lines of /proc/meminfo -> map of key -> bytes.
QVariantMap readMemInfo()
{
    QVariantMap out;
    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QStringList parts = line.mid(colon + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;
        bool ok = false;
        const qint64 kib = parts.first().toLongLong(&ok);
        if (ok)
            out.insert(key, qlonglong(kib) * 1024);
    }
    return out;
}

// "/proc/stat" cpu lines -> {total, idle} jiffy pairs, one per logical core.
struct CpuJiffies {
    qint64 total = 0;
    qint64 idle = 0;
};

QList<CpuJiffies> readCpuJiffies()
{
    QList<CpuJiffies> out;
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.startsWith(QLatin1String("cpu"))) {
            const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() < 5)
                continue;
            CpuJiffies j;
            qint64 sum = 0;
            for (int i = 1; i < parts.size(); ++i)
                sum += parts.at(i).toLongLong();
            j.total = sum;
            j.idle = parts.at(4).toLongLong()
                     + (parts.size() > 5 ? parts.at(5).toLongLong() : 0);
            out.append(j);
        } else {
            break;
        }
    }
    return out;
}

double cpuPercentSince(qint64 total, qint64 idle, qint64 prevTotal, qint64 idleAtPrev)
{
    const qint64 totalDelta = total - prevTotal;
    const qint64 idleDelta = idle - idleAtPrev;
    if (totalDelta <= 0)
        return 0.0;
    const double busy = 100.0 * double(totalDelta - idleDelta) / double(totalDelta);
    return std::clamp(busy, 0.0, 100.0);
}

} // namespace

SystemMonitor::SystemMonitor(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_refreshInterval = settings->value(QStringLiteral("system.interval"), 2000).toInt();
    m_flags = flagsFromSettings();

    if (Rust::available() && m_metrics.isValid()) {
        m_available = true;
    } else {
        m_available = QFile::exists(QStringLiteral("/proc/stat"));
    }

    m_timer.setInterval(qMax(0, m_refreshInterval));
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &SystemMonitor::refresh);
    if (m_refreshInterval > 0)
        m_timer.start();

    refresh();
}

void SystemMonitor::setRefreshInterval(int milliseconds)
{
    milliseconds = qMax(0, milliseconds);
    if (m_refreshInterval == milliseconds)
        return;
    m_refreshInterval = milliseconds;
    if (milliseconds > 0)
        m_timer.start(milliseconds);
    else
        m_timer.stop();
    emit refreshIntervalChanged(milliseconds);
}

void SystemMonitor::setFlags(quint32 flags)
{
    if (m_flags == flags)
        return;
    m_flags = flags;
    refresh();
}

quint32 SystemMonitor::flagsFromSettings() const
{
    quint32 flags = Rust::Metrics::Battery | Rust::Metrics::Uname;
    if (m_settings->value(QStringLiteral("system.disks"), true).toBool())
        flags |= Rust::Metrics::Disks;
    if (m_settings->value(QStringLiteral("system.thermal"), true).toBool())
        flags |= Rust::Metrics::Thermal;
    if (m_settings->value(QStringLiteral("system.perCore"), false).toBool())
        flags |= Rust::Metrics::PerCore;
    return flags;
}

void SystemMonitor::refresh()
{
    m_flags = flagsFromSettings();
    if (Rust::available() && m_metrics.isValid()) {
        const QVariantMap map = m_metrics.snapshot(m_flags);
        if (!map.isEmpty()) {
            applySnapshot(map);
            return;
        }
    }
    sampleWithQt();
}

QVariantList SystemMonitor::processesList(int limit)
{
    if (Rust::available() && m_metrics.isValid())
        return m_metrics.processes(qMax(0, limit));
    return {};
}

void SystemMonitor::reset()
{
    if (Rust::available() && m_metrics.isValid())
        m_metrics.reset();
    m_prevTotalJiffies = 0;
    m_prevIdleJiffies = 0;
    m_prevCoreTotal.clear();
    m_prevCoreIdle.clear();
    m_prevNetRx = -1;
    m_prevNetTx = -1;
    m_prevNetMs = 0;
    refresh();
}

QString SystemMonitor::formatBytes(qint64 bytes) const
{
    if (Rust::available()) {
        const QString text = Rust::formatBytes(quint64(qMax<qint64>(0, bytes)));
        if (!text.isEmpty())
            return text;
    }
    return bytesText(double(bytes));
}

QString SystemMonitor::formatRate(double bytesPerSecond) const
{
    if (Rust::available()) {
        const QString text = Rust::formatRate(bytesPerSecond);
        if (!text.isEmpty())
            return text;
    }
    return bytesText(bytesPerSecond) + QStringLiteral("/s");
}

void SystemMonitor::applySnapshot(const QVariantMap &map)
{
    m_snapshot = map;

    const QVariantMap cpu = map.value(QStringLiteral("cpu")).toMap();
    m_cpuPercent = cpu.value(QStringLiteral("percent")).toDouble();
    m_cpuCount = cpu.value(QStringLiteral("count")).toInt();
    m_cpuModel = cpu.value(QStringLiteral("model")).toString();
    m_frequencyMhz = cpu.value(QStringLiteral("frequencyMhz")).toDouble();
    m_load1 = cpu.value(QStringLiteral("load1")).toDouble();
    m_load5 = cpu.value(QStringLiteral("load5")).toDouble();
    m_load15 = cpu.value(QStringLiteral("load15")).toDouble();
    m_perCore.clear();
    const QVariantList perCore = cpu.value(QStringLiteral("perCore")).toList();
    for (const QVariant &core : perCore)
        m_perCore.append(core.toDouble());

    const QVariantMap memory = map.value(QStringLiteral("memory")).toMap();
    m_memoryPercent = memory.value(QStringLiteral("percent")).toDouble();
    m_memoryText = memory.value(QStringLiteral("text")).toString();
    m_memoryUsedText = bytesText(memory.value(QStringLiteral("used")).toDouble());
    m_memoryTotalText = bytesText(memory.value(QStringLiteral("total")).toDouble());
    m_swapPercent = memory.value(QStringLiteral("swapPercent")).toDouble();

    const QVariantMap network = map.value(QStringLiteral("network")).toMap();
    m_networkDown = network.value(QStringLiteral("rxRate")).toDouble();
    m_networkUp = network.value(QStringLiteral("txRate")).toDouble();
    m_networkDownText = formatRate(m_networkDown);
    m_networkUpText = formatRate(m_networkUp);

    const QVariantMap battery = map.value(QStringLiteral("battery")).toMap();
    m_batteryPresent = battery.value(QStringLiteral("present")).toBool();
    m_batteryPercent = battery.value(QStringLiteral("percent")).toDouble();
    m_batteryStatus = battery.value(QStringLiteral("status")).toString();
    m_batteryCharging = battery.value(QStringLiteral("charging")).toBool();
    m_batteryCritical = m_batteryPresent && !m_batteryCharging && m_batteryPercent > 0.0
                       && m_batteryPercent <= 10.0;
    const double toEmpty = battery.value(QStringLiteral("timeToEmptySeconds")).toDouble();
    const double toFull = battery.value(QStringLiteral("timeToFullSeconds")).toDouble();
    if (m_batteryCharging && toFull > 0.0)
        m_batteryTimeText = durationText(toFull) + QStringLiteral(" to full");
    else if (!m_batteryCharging && toEmpty > 0.0)
        m_batteryTimeText = durationText(toEmpty) + QStringLiteral(" left");
    else
        m_batteryTimeText.clear();

    m_thermalZones.clear();
    double hottest = 0.0;
    const QVariantList thermal = map.value(QStringLiteral("thermal")).toList();
    for (const QVariant &zone : thermal) {
        const QVariantMap z = zone.toMap();
        m_thermalZones.append(z);
        hottest = qMax(hottest, z.value(QStringLiteral("celsius")).toDouble());
    }
    m_temperatureText = thermal.isEmpty()
        ? QString()
        : QStringLiteral("%1°C").arg(hottest, 0, 'f', 1);

    m_disks.clear();
    double weighted = 0.0;
    qint64 totalBytes = 0;
    qint64 freeBytes = 0;
    const QVariantList disks = map.value(QStringLiteral("disks")).toList();
    for (const QVariant &disk : disks) {
        const QVariantMap d = disk.toMap();
        m_disks.append(d);
        const qint64 total = qint64(d.value(QStringLiteral("total")).toDouble());
        const qint64 available = qint64(d.value(QStringLiteral("available")).toDouble());
        totalBytes += total;
        freeBytes += available;
        weighted += d.value(QStringLiteral("percent")).toDouble() * double(total);
    }
    m_diskPercent = totalBytes > 0 ? std::clamp(weighted / double(totalBytes), 0.0, 100.0) : 0.0;
    m_diskFreeText = bytesText(double(freeBytes)) + QStringLiteral(" free");

    m_processes = map.value(QStringLiteral("processes")).toInt();
    m_userProcesses = map.value(QStringLiteral("userProcesses")).toInt();
    const QVariantMap cpuAgain = map.value(QStringLiteral("cpu")).toMap();
    m_uptimeText = cpuAgain.value(QStringLiteral("uptimeText")).toString();

    m_available = map.value(QStringLiteral("available"), true).toBool();
    emit snapshotUpdated();
}

void SystemMonitor::clearSnapshot()
{
    m_snapshot = {};
    m_cpuPercent = 0.0;
    m_perCore.clear();
    m_networkDown = 0.0;
    m_networkUp = 0.0;
    m_networkDownText = formatRate(0.0);
    m_networkUpText = formatRate(0.0);
    emit snapshotUpdated();
}

// Minimal /proc reader for builds without the Rust core: CPU, memory, network
// rates, load average and uptime. Battery, thermal and disks stay empty.
void SystemMonitor::sampleWithQt()
{
    QVariantMap map;
    QVariantMap cpu;

    const QList<CpuJiffies> jiffies = readCpuJiffies();
    if (!jiffies.isEmpty()) {
        const CpuJiffies total = jiffies.first();
        cpu.insert(QStringLiteral("percent"),
                   cpuPercentSince(total.total, total.idle, m_prevTotalJiffies, m_prevIdleJiffies));
        m_prevTotalJiffies = total.total;
        m_prevIdleJiffies = total.idle;

        QVariantList perCore;
        const int cores = jiffies.size() - 1;
        cpu.insert(QStringLiteral("count"), cores);
        if (m_prevCoreTotal.size() != cores) {
            m_prevCoreTotal = QList<qint64>(cores, 0);
            m_prevCoreIdle = QList<qint64>(cores, 0);
        }
        for (int i = 0; i < cores; ++i) {
            const CpuJiffies &core = jiffies.at(i + 1);
            perCore.append(cpuPercentSince(core.total, core.idle,
                                           m_prevCoreTotal.at(i), m_prevCoreIdle.at(i)));
            m_prevCoreTotal[i] = core.total;
            m_prevCoreIdle[i] = core.idle;
        }
        cpu.insert(QStringLiteral("perCore"), perCore);
    }

    QFile loadavg(QStringLiteral("/proc/loadavg"));
    if (loadavg.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList parts = QString::fromUtf8(loadavg.readAll())
                                      .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 3) {
            m_load1 = parts.at(0).toDouble();
            m_load5 = parts.at(1).toDouble();
            m_load15 = parts.at(2).toDouble();
        }
    }
    cpu.insert(QStringLiteral("load1"), m_load1);
    cpu.insert(QStringLiteral("load5"), m_load5);
    cpu.insert(QStringLiteral("load15"), m_load15);
    cpu.insert(QStringLiteral("model"), m_cpuModel);

    QFile uptime(QStringLiteral("/proc/uptime"));
    if (uptime.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString text = QString::fromUtf8(uptime.readAll());
        const double seconds = text.section(QLatin1Char(' '), 0, 0).toDouble();
        cpu.insert(QStringLiteral("uptimeSeconds"), seconds);
        cpu.insert(QStringLiteral("uptimeText"), durationText(seconds));
    }
    map.insert(QStringLiteral("cpu"), cpu);

    const QVariantMap mem = readMemInfo();
    QVariantMap memory;
    const qint64 total = mem.value(QStringLiteral("MemTotal")).toLongLong();
    const qint64 available = mem.value(QStringLiteral("MemAvailable")).toLongLong();
    const qint64 swapTotal = mem.value(QStringLiteral("SwapTotal")).toLongLong();
    const qint64 swapFree = mem.value(QStringLiteral("SwapFree")).toLongLong();
    const qint64 used = qMax<qint64>(0, total - available);
    memory.insert(QStringLiteral("total"), total);
    memory.insert(QStringLiteral("used"), used);
    memory.insert(QStringLiteral("available"), available);
    memory.insert(QStringLiteral("percent"),
                  total > 0 ? std::clamp(100.0 * double(used) / double(total), 0.0, 100.0) : 0.0);
    memory.insert(QStringLiteral("text"),
                  bytesText(double(used)) + QStringLiteral(" / ") + bytesText(double(total)));
    memory.insert(QStringLiteral("swapTotal"), swapTotal);
    memory.insert(QStringLiteral("swapUsed"), qMax<qint64>(0, swapTotal - swapFree));
    memory.insert(QStringLiteral("swapPercent"),
                  swapTotal > 0
                      ? std::clamp(100.0 * double(swapTotal - swapFree) / double(swapTotal), 0.0, 100.0)
                      : 0.0);
    map.insert(QStringLiteral("memory"), memory);

    QVariantMap network;
    qint64 rx = 0;
    qint64 tx = 0;
    QFile netdev(QStringLiteral("/proc/net/dev"));
    if (netdev.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&netdev);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon <= 0)
                continue;
            const QString name = line.left(colon).trimmed();
            if (name == QLatin1String("lo"))
                continue;
            const QStringList parts = line.mid(colon + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() < 9)
                continue;
            rx += parts.at(0).toLongLong();
            tx += parts.at(8).toLongLong();
        }
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_prevNetRx >= 0 && now > m_prevNetMs) {
        const double seconds = double(now - m_prevNetMs) / 1000.0;
        network.insert(QStringLiteral("rxRate"),
                       std::max(0.0, double(rx - m_prevNetRx) / seconds));
        network.insert(QStringLiteral("txRate"),
                       std::max(0.0, double(tx - m_prevNetTx) / seconds));
    } else {
        network.insert(QStringLiteral("rxRate"), 0.0);
        network.insert(QStringLiteral("txRate"), 0.0);
    }
    m_prevNetRx = rx;
    m_prevNetTx = tx;
    m_prevNetMs = now;
    map.insert(QStringLiteral("network"), network);

    QVariantMap battery;
    battery.insert(QStringLiteral("present"), false);
    map.insert(QStringLiteral("battery"), battery);
    map.insert(QStringLiteral("thermal"), QVariantList());
    map.insert(QStringLiteral("disks"), QVariantList());

    map.insert(QStringLiteral("processes"), m_processes);
    map.insert(QStringLiteral("userProcesses"), m_userProcesses);
    map.insert(QStringLiteral("available"), true);

    applySnapshot(map);
}
