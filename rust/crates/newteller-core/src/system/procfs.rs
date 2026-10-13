//! Readers for the plain-text kernel interfaces in `/proc`.
//!
//! Every function here is pure text parsing plus one file read, and every
//! parser is exposed separately so it can be unit tested without `/proc`.

use serde::{Deserialize, Serialize};

use crate::util;

/// CPU time counters of one CPU, in jiffies.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct CpuTimes {
    /// `user`.
    pub user: u64,
    /// `nice`.
    pub nice: u64,
    /// `system`.
    pub system: u64,
    /// `idle`.
    pub idle: u64,
    /// `iowait`.
    pub iowait: u64,
    /// `irq`.
    pub irq: u64,
    /// `softirq`.
    pub softirq: u64,
    /// `steal`.
    pub steal: u64,
}

impl CpuTimes {
    /// Sum of all counters.
    pub fn total(&self) -> u64 {
        self.user
            .saturating_add(self.nice)
            .saturating_add(self.system)
            .saturating_add(self.idle)
            .saturating_add(self.iowait)
            .saturating_add(self.irq)
            .saturating_add(self.softirq)
            .saturating_add(self.steal)
    }

    /// Everything that is not idle or waiting for I/O.
    pub fn busy(&self) -> u64 {
        self.total().saturating_sub(self.idle).saturating_sub(self.iowait)
    }

    /// Busy percentage between two samples; `0.0` when nothing elapsed.
    pub fn percent_since(&self, previous: &CpuTimes) -> f64 {
        let total = self.total().saturating_sub(previous.total());
        if total == 0 {
            return 0.0;
        }
        let busy = self.busy().saturating_sub(previous.busy());
        (busy as f64 / total as f64 * 100.0).clamp(0.0, 100.0)
    }
}

/// Parses `/proc/stat` into the aggregate line and one entry per core.
pub fn parse_cpu_stat(text: &str) -> Option<(CpuTimes, Vec<CpuTimes>)> {
    let mut aggregate = CpuTimes::default();
    let mut cores: Vec<CpuTimes> = Vec::new();
    let mut seen_aggregate = false;

    for line in text.lines() {
        let mut fields = line.split_whitespace();
        let Some(label) = fields.next() else { continue };
        if label == "cpu" {
            aggregate = times_from(fields);
            seen_aggregate = true;
        } else if label.starts_with("cpu") {
            cores.push(times_from(fields));
        } else if label == "intr" || label == "ctxt" {
            // Everything after the per-cpu lines is irrelevant here.
            break;
        }
    }

    seen_aggregate.then_some((aggregate, cores))
}

fn times_from<'a, I: Iterator<Item = &'a str>>(mut fields: I) -> CpuTimes {
    let mut values = [0u64; 8];
    for value in values.iter_mut() {
        match fields.next() {
            Some(field) => *value = field.parse::<u64>().unwrap_or(0),
            None => break,
        }
    }
    CpuTimes {
        user: values[0],
        nice: values[1],
        system: values[2],
        idle: values[3],
        iowait: values[4],
        irq: values[5],
        softirq: values[6],
        steal: values[7],
    }
}

/// Reads `/proc/stat`.
pub fn read_cpu_stat() -> Option<(CpuTimes, Vec<CpuTimes>)> {
    let text = util::read_capped(std::path::Path::new("/proc/stat"), 256 * 1024)?;
    parse_cpu_stat(&text)
}

/// Memory counters, in bytes.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct MemInfo {
    /// `MemTotal`.
    pub total: u64,
    /// `MemFree`.
    pub free: u64,
    /// `MemAvailable`, or an approximation on kernels that lack it.
    pub available: u64,
    /// `Buffers`.
    pub buffers: u64,
    /// `Cached`.
    pub cached: u64,
    /// `SReclaimable`.
    pub slab: u64,
    /// `SwapTotal`.
    pub swap_total: u64,
    /// `SwapFree`.
    pub swap_free: u64,
    /// True when the kernel reported `MemAvailable`.
    pub has_available: bool,
}

impl MemInfo {
    /// Memory the user cannot reclaim, in bytes.
    pub fn used(&self) -> u64 {
        self.total.saturating_sub(self.available.min(self.total))
    }

    /// Used percentage.
    pub fn percent(&self) -> f64 {
        if self.total == 0 {
            return 0.0;
        }
        (self.used() as f64 / self.total as f64 * 100.0).clamp(0.0, 100.0)
    }

    /// Used swap in bytes.
    pub fn swap_used(&self) -> u64 {
        self.swap_total.saturating_sub(self.swap_free.min(self.swap_total))
    }

    /// Used swap percentage.
    pub fn swap_percent(&self) -> f64 {
        if self.swap_total == 0 {
            return 0.0;
        }
        (self.swap_used() as f64 / self.swap_total as f64 * 100.0).clamp(0.0, 100.0)
    }
}

/// Parses `/proc/meminfo`. Values there are in kibibytes.
pub fn parse_meminfo(text: &str) -> Option<MemInfo> {
    let mut info = MemInfo::default();
    let mut seen_total = false;
    let mut available_from_kernel = false;

    for line in text.lines() {
        let Some((key, rest)) = line.split_once(':') else { continue };
        let value = rest
            .split_whitespace()
            .next()
            .and_then(|v| v.parse::<u64>().ok())
            .unwrap_or(0)
            .saturating_mul(1024);
        match key.trim() {
            "MemTotal" => {
                info.total = value;
                seen_total = true;
            }
            "MemFree" => info.free = value,
            "MemAvailable" => {
                info.available = value;
                available_from_kernel = true;
            }
            "Buffers" => info.buffers = value,
            "Cached" => info.cached = value,
            "SReclaimable" => info.slab = value,
            "SwapTotal" => info.swap_total = value,
            "SwapFree" => info.swap_free = value,
            _ => {}
        }
    }

    if !seen_total {
        return None;
    }
    if !available_from_kernel {
        info.available = info.free + info.buffers + info.cached;
    }
    info.has_available = available_from_kernel;
    Some(info)
}

/// Reads `/proc/meminfo`.
pub fn read_meminfo() -> Option<MemInfo> {
    let text = util::read_capped(std::path::Path::new("/proc/meminfo"), 64 * 1024)?;
    parse_meminfo(&text)
}

/// Counters of one network interface, as reported by `/proc/net/dev`.
#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct NetDev {
    /// Interface name.
    pub name: String,
    /// Received bytes.
    pub rx_bytes: u64,
    /// Received packets.
    pub rx_packets: u64,
    /// Receive errors.
    pub rx_errors: u64,
    /// Transmitted bytes.
    pub tx_bytes: u64,
    /// Transmitted packets.
    pub tx_packets: u64,
    /// Transmit errors.
    pub tx_errors: u64,
}

impl NetDev {
    /// True for `lo` and other virtual loopback interfaces.
    pub fn is_loopback(&self) -> bool {
        self.name == "lo" || self.name == "lo0"
    }
}

/// Parses `/proc/net/dev`.
pub fn parse_netdev(text: &str) -> Vec<NetDev> {
    let mut out = Vec::new();
    for line in text.lines() {
        let Some((name, rest)) = line.split_once(':') else { continue };
        let name = name.trim().to_string();
        if name.is_empty() {
            continue;
        }
        let fields: Vec<&str> = rest.split_whitespace().collect();
        let field = |index: usize| {
            fields
                .get(index)
                .and_then(|v| v.parse::<u64>().ok())
                .unwrap_or(0)
        };
        out.push(NetDev {
            name,
            rx_bytes: field(0),
            rx_packets: field(1),
            rx_errors: field(2),
            tx_bytes: field(8),
            tx_packets: field(9),
            tx_errors: field(10),
        });
    }
    out
}

/// Reads `/proc/net/dev`.
pub fn read_netdev() -> Vec<NetDev> {
    match util::read_capped(std::path::Path::new("/proc/net/dev"), 128 * 1024) {
        Some(text) => parse_netdev(&text),
        None => Vec::new(),
    }
}

/// Parses `/proc/loadavg` into the 1, 5 and 15 minute averages.
pub fn parse_loadavg(text: &str) -> Option<[f64; 3]> {
    let mut fields = text.split_whitespace();
    let one = fields.next()?.parse::<f64>().ok()?;
    let five = fields.next()?.parse::<f64>().ok()?;
    let fifteen = fields.next()?.parse::<f64>().ok()?;
    Some([one, five, fifteen])
}

/// Reads `/proc/loadavg`.
pub fn read_loadavg() -> Option<[f64; 3]> {
    let text = util::read_to_string_lossy(std::path::Path::new("/proc/loadavg"))?;
    parse_loadavg(&text)
}

/// Parses `/proc/uptime` (seconds since boot, idle seconds of all CPUs).
pub fn parse_uptime(text: &str) -> Option<f64> {
    text.split_whitespace().next()?.parse::<f64>().ok()
}

/// Reads `/proc/uptime`.
pub fn read_uptime() -> Option<f64> {
    let text = util::read_to_string_lossy(std::path::Path::new("/proc/uptime"))?;
    parse_uptime(&text)
}

/// Model name and current frequency of the first CPU.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct CpuModel {
    /// `model name` from `/proc/cpuinfo`, when present.
    pub model: String,
    /// Current frequency in MHz, from cpufreq when available.
    pub frequency_mhz: f64,
    /// Maximum frequency in MHz, from cpufreq when available.
    pub frequency_max_mhz: f64,
    /// Online CPUs, from `/sys/devices/system/cpu/online`.
    pub online: usize,
}

/// Reads CPU model information. Missing values stay empty or zero.
pub fn read_cpu_model() -> CpuModel {
    let mut out = CpuModel::default();
    if let Some(text) = util::read_capped(std::path::Path::new("/proc/cpuinfo"), 128 * 1024) {
        for line in text.lines() {
            let Some((key, value)) = line.split_once(':') else { continue };
            let key = key.trim();
            let value = value.trim();
            if out.model.is_empty() && (key == "model name" || key == "Model" || key == "cpu model") {
                out.model = value.to_string();
            }
            if out.frequency_mhz == 0.0 && key == "cpu MHz" {
                out.frequency_mhz = value.parse::<f64>().unwrap_or(0.0);
            }
            if !out.model.is_empty() && out.frequency_mhz > 0.0 {
                break;
            }
        }
    }

    let cpufreq = std::path::Path::new("/sys/devices/system/cpu/cpu0/cpufreq");
    if out.frequency_mhz == 0.0 {
        if let Some(khz) = read_number(&cpufreq.join("scaling_cur_freq")) {
            out.frequency_mhz = khz as f64 / 1000.0;
        }
    }
    if let Some(khz) = read_number(&cpufreq.join("scaling_max_freq")) {
        out.frequency_max_mhz = khz as f64 / 1000.0;
    }
    if let Some(text) = util::read_to_string_lossy(std::path::Path::new("/sys/devices/system/cpu/online")) {
        out.online = parse_cpu_range(text.trim());
    }
    out
}

/// Parses a CPU list such as `0-3,8-11` into a count.
pub fn parse_cpu_range(text: &str) -> usize {
    let mut count = 0usize;
    for part in text.split(',') {
        let part = part.trim();
        if part.is_empty() {
            continue;
        }
        match part.split_once('-') {
            Some((from, to)) => {
                let from = from.parse::<usize>().unwrap_or(0);
                let to = to.parse::<usize>().unwrap_or(from);
                count += to.saturating_sub(from) + 1;
            }
            None => count += 1,
        }
    }
    count
}

fn read_number(path: &std::path::Path) -> Option<u64> {
    let text = util::read_to_string_lossy(path)?;
    text.trim().parse::<u64>().ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    const STAT: &str = "cpu  1000 20 300 5000 40 0 10 5 0 0 0\n\
                        cpu0 500 10 150 2500 20 0 5 3 0 0 0\n\
                        cpu1 500 10 150 2500 20 0 5 2 0 0 0\n\
                        intr 12345 0 0\n\
                        ctxt 999\n";

    #[test]
    fn parses_cpu_stat() {
        let (total, cores) = parse_cpu_stat(STAT).expect("parses");
        assert_eq!(cores.len(), 2);
        assert_eq!(total.user, 1000);
        assert_eq!(total.total(), 1000 + 20 + 300 + 5000 + 40 + 0 + 10 + 5);
        assert_eq!(total.busy(), total.total() - 5000 - 40);
        assert_eq!(total.percent_since(&CpuTimes::default()) > 0.0, true);
    }

    #[test]
    fn cpu_percent_is_clamped() {
        let before = CpuTimes { idle: 100, ..CpuTimes::default() };
        let after = CpuTimes { idle: 100, user: 100, ..CpuTimes::default() };
        assert_eq!(after.percent_since(&before), 100.0);
        assert_eq!(before.percent_since(&before), 0.0);
    }

    const MEMINFO: &str = "MemTotal:       16384000 kB\n\
                           MemFree:         1000000 kB\n\
                           MemAvailable:    8000000 kB\n\
                           Buffers:          200000 kB\n\
                           Cached:          4000000 kB\n\
                           SwapCached:            0 kB\n\
                           SwapTotal:       2000000 kB\n\
                           SwapFree:        1500000 kB\n\
                           SReclaimable:     300000 kB\n";

    #[test]
    fn parses_meminfo_and_derives_usage() {
        let info = parse_meminfo(MEMINFO).expect("parses");
        assert_eq!(info.total, 16384000 * 1024);
        assert!(info.has_available);
        assert_eq!(info.used(), info.total - info.available);
        assert!((info.percent() - 51.17).abs() < 0.1);
        assert_eq!(info.swap_used(), 500000 * 1024);
    }

    #[test]
    fn approximates_available_on_old_kernels() {
        let info = parse_meminfo("MemTotal: 1000 kB\nMemFree: 100 kB\nBuffers: 50 kB\nCached: 200 kB\n").unwrap();
        assert!(!info.has_available);
        assert_eq!(info.available, 350 * 1024);
    }

    const NETDEV: &str = "Inter-|   Receive                                                |  Transmit\n\
                          face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n\
                          \x20   lo: 1000      10    0    0    0     0          0         0    1000      10    0    0    0     0       0          0\n\
                          eth0: 2000      20    1    0    0     0          0         0    3000      30    2    0    0     0       0          0\n";

    #[test]
    fn parses_netdev() {
        let devices = parse_netdev(NETDEV);
        assert_eq!(devices.len(), 2);
        assert!(devices[0].is_loopback());
        assert_eq!(devices[1].name, "eth0");
        assert_eq!(devices[1].rx_bytes, 2000);
        assert_eq!(devices[1].tx_bytes, 3000);
        assert_eq!(devices[1].rx_errors, 1);
    }

    #[test]
    fn parses_scalars() {
        assert_eq!(parse_loadavg("0.52 0.58 0.59 1/234 5678"), Some([0.52, 0.58, 0.59]));
        assert_eq!(parse_uptime("12345.67 89012.34"), Some(12345.67));
        assert_eq!(parse_cpu_range("0-3,8"), 5);
        assert_eq!(parse_cpu_range(""), 0);
    }
}
