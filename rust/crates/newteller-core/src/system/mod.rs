//! System metrics for the panel and desktop widgets.
//!
//! [`Monitor`] keeps the previous samples, so CPU load and network throughput
//! are real rates instead of counters since boot. Everything that cannot change
//! during a session (CPU model, `uname`) is read once and cached.

pub mod disk;
pub mod power;
pub mod process;
pub mod procfs;

use std::collections::HashMap;
use std::time::Instant;

use serde::{Deserialize, Serialize};

use crate::util;
use procfs::{CpuTimes, MemInfo, NetDev};

/// Include disk usage (`statvfs` on every mount) in the snapshot.
pub const FLAG_DISKS: u32 = 1;
/// Include thermal zones.
pub const FLAG_THERMAL: u32 = 1 << 1;
/// Include the battery.
pub const FLAG_BATTERY: u32 = 1 << 2;
/// Include the process count.
pub const FLAG_PROCESSES: u32 = 1 << 3;
/// Include `uname`.
pub const FLAG_UNAME: u32 = 1 << 4;
/// Include one utilisation value per core.
pub const FLAG_PER_CORE: u32 = 1 << 5;
/// Bits 8..=15 of the flag word carry the thermal zone limit (0 = unlimited).
pub const FLAG_THERMAL_LIMIT_SHIFT: u32 = 8;

/// Which parts of the system to sample.
///
/// Skipping what a widget does not show is worth it: `statvfs` over a dozen
/// mounts and a full `/sys/class/power_supply` walk are the two most expensive
/// parts of a snapshot.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct SnapshotOptions {
    /// Sample disks.
    pub disks: bool,
    /// Sample thermal zones.
    pub thermal: bool,
    /// Sample the battery.
    pub battery: bool,
    /// Count processes.
    pub processes: bool,
    /// Read `uname` (cached after the first call).
    pub uname: bool,
    /// Report per-core utilisation.
    pub per_core: bool,
    /// Maximum number of thermal zones.
    pub thermal_limit: usize,
}

impl Default for SnapshotOptions {
    fn default() -> Self {
        SnapshotOptions {
            disks: false,
            thermal: false,
            battery: true,
            processes: true,
            uname: false,
            per_core: true,
            thermal_limit: 4,
        }
    }
}

impl SnapshotOptions {
    /// Everything, for the diagnostics view.
    pub fn full() -> SnapshotOptions {
        SnapshotOptions {
            disks: true,
            thermal: true,
            battery: true,
            processes: true,
            uname: true,
            per_core: true,
            thermal_limit: 16,
        }
    }

    /// Only what a small panel widget needs.
    pub fn light() -> SnapshotOptions {
        SnapshotOptions {
            disks: false,
            thermal: false,
            battery: true,
            processes: false,
            uname: false,
            per_core: false,
            thermal_limit: 0,
        }
    }

    /// Builds options from the bit field used by the C ABI.
    ///
    /// Bits 8..=15 carry the thermal zone limit (0 = unlimited), so the value
    /// survives the round trip through [`SnapshotOptions::to_flags`].
    pub fn from_flags(flags: u32) -> SnapshotOptions {
        SnapshotOptions {
            disks: flags & FLAG_DISKS != 0,
            thermal: flags & FLAG_THERMAL != 0,
            battery: flags & FLAG_BATTERY != 0,
            processes: flags & FLAG_PROCESSES != 0,
            uname: flags & FLAG_UNAME != 0,
            per_core: flags & FLAG_PER_CORE != 0,
            thermal_limit: ((flags >> FLAG_THERMAL_LIMIT_SHIFT) & 0xff) as usize,
        }
    }

    /// The bit field of these options, for the C ABI.
    pub fn to_flags(&self) -> u32 {
        let mut flags = 0u32;
        if self.disks {
            flags |= FLAG_DISKS;
        }
        if self.thermal {
            flags |= FLAG_THERMAL;
        }
        if self.battery {
            flags |= FLAG_BATTERY;
        }
        if self.processes {
            flags |= FLAG_PROCESSES;
        }
        if self.uname {
            flags |= FLAG_UNAME;
        }
        if self.per_core {
            flags |= FLAG_PER_CORE;
        }
        flags |= ((self.thermal_limit as u32) & 0xff) << FLAG_THERMAL_LIMIT_SHIFT;
        flags
    }
}

/// CPU state.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct CpuSnapshot {
    /// Utilisation of all cores together, in percent.
    pub percent: f64,
    /// Utilisation per core, empty unless requested.
    pub per_core: Vec<f64>,
    /// Number of logical cores.
    pub count: usize,
    /// Model name.
    pub model: String,
    /// Current frequency in MHz, when cpufreq reports it.
    pub frequency_mhz: f64,
    /// Maximum frequency in MHz, when cpufreq reports it.
    pub frequency_max_mhz: f64,
    /// 1 minute load average.
    pub load1: f64,
    /// 5 minute load average.
    pub load5: f64,
    /// 15 minute load average.
    pub load15: f64,
    /// Load per core, the number that says whether the machine is busy.
    pub load_per_core: f64,
    /// Seconds since boot.
    pub uptime_seconds: f64,
    /// Ready to print uptime, for example `3 d 04:05`.
    pub uptime_text: String,
}

/// Memory and swap.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct MemorySnapshot {
    /// Total bytes.
    pub total: u64,
    /// Used bytes, `total - available`.
    pub used: u64,
    /// Available bytes, as the kernel estimates them.
    pub available: u64,
    /// Free bytes.
    pub free: u64,
    /// Buffer cache bytes.
    pub buffers: u64,
    /// Page cache bytes.
    pub cached: u64,
    /// Used percentage.
    pub percent: f64,
    /// Total swap bytes.
    pub swap_total: u64,
    /// Used swap bytes.
    pub swap_used: u64,
    /// Used swap percentage.
    pub swap_percent: f64,
    /// Ready to print used / total, for example `3.2 GiB / 15.6 GiB`.
    pub text: String,
}

/// One network interface.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct InterfaceSnapshot {
    /// Interface name.
    pub name: String,
    /// Received bytes since boot.
    pub rx_bytes: u64,
    /// Transmitted bytes since boot.
    pub tx_bytes: u64,
    /// Receive rate in bytes per second.
    pub rx_rate: f64,
    /// Transmit rate in bytes per second.
    pub tx_rate: f64,
    /// True for the loopback interface.
    pub loopback: bool,
}

/// Network throughput.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct NetworkSnapshot {
    /// Receive rate of all real interfaces, bytes per second.
    pub rx_rate: f64,
    /// Transmit rate of all real interfaces, bytes per second.
    pub tx_rate: f64,
    /// Received bytes since boot, all real interfaces.
    pub rx_total: u64,
    /// Transmitted bytes since boot, all real interfaces.
    pub tx_total: u64,
    /// Every interface, including loopback.
    pub interfaces: Vec<InterfaceSnapshot>,
}

/// Output of `uname(2)`.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct Uname {
    /// `sysname`, for example `Linux`.
    pub sysname: String,
    /// `nodename`, the host name.
    pub nodename: String,
    /// `release`, the kernel version.
    pub release: String,
    /// `version`, the kernel build stamp.
    pub version: String,
    /// `machine`, for example `x86_64`.
    pub machine: String,
}

/// One sample of the whole system.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct Snapshot {
    /// False when the platform does not expose `/proc`.
    pub available: bool,
    /// Milliseconds since the epoch.
    pub timestamp_ms: u64,
    /// Milliseconds since the previous snapshot; rates are computed over it.
    pub interval_ms: f64,
    /// CPU.
    pub cpu: CpuSnapshot,
    /// Memory and swap.
    pub memory: MemorySnapshot,
    /// Network.
    pub network: NetworkSnapshot,
    /// Battery, absent systems report `present = false`.
    pub battery: power::Battery,
    /// Thermal zones, hottest first.
    pub thermal: Vec<power::ThermalZone>,
    /// Real filesystems.
    pub disks: Vec<disk::DiskUsage>,
    /// `uname`.
    pub uname: Uname,
    /// Number of processes.
    pub processes: usize,
    /// Number of running processes owned by this user.
    pub user_processes: usize,
    /// Cost of taking the snapshot.
    pub elapsed_ms: f64,
}

/// `name -> (rx bytes, tx bytes)` per interface, kept between samples.
type NetCounters = HashMap<String, (u64, u64)>;

/// Keeps the previous samples needed for rates.
#[derive(Debug)]
pub struct Monitor {
    started: Instant,
    previous_cpu: Option<(Instant, CpuTimes, Vec<CpuTimes>)>,
    previous_net: Option<(Instant, NetCounters)>,
    model: Option<procfs::CpuModel>,
    uname: Option<Uname>,
}

impl Default for Monitor {
    fn default() -> Self {
        Monitor::new()
    }
}

impl Monitor {
    /// A monitor without samples; the first snapshot reports zero rates.
    pub fn new() -> Monitor {
        Monitor {
            started: Instant::now(),
            previous_cpu: None,
            previous_net: None,
            model: None,
            uname: None,
        }
    }

    /// Seconds since the monitor was created.
    pub fn age(&self) -> f64 {
        self.started.elapsed().as_secs_f64()
    }

    /// Takes one sample.
    pub fn snapshot(&mut self, options: &SnapshotOptions) -> Snapshot {
        let started = Instant::now();
        let mut snapshot = Snapshot {
            timestamp_ms: util::now_ms(),
            ..Snapshot::default()
        };

        // ---- CPU -----------------------------------------------------------
        if let Some((total, cores)) = procfs::read_cpu_stat() {
            snapshot.available = true;
            let interval = match &self.previous_cpu {
                Some((at, ..)) => at.elapsed().as_secs_f64(),
                None => 0.0,
            };
            snapshot.cpu.percent = match &self.previous_cpu {
                Some((_, previous_total, _)) => total.percent_since(previous_total),
                None => 0.0,
            };
            snapshot.cpu.count = if cores.is_empty() {
                std::thread::available_parallelism().map(|n| n.get()).unwrap_or(1)
            } else {
                cores.len()
            };
            if options.per_core {
                let previous_cores = self
                    .previous_cpu
                    .as_ref()
                    .map(|(_, _, cores)| cores.as_slice())
                    .unwrap_or(&[]);
                snapshot.cpu.per_core = cores
                    .iter()
                    .enumerate()
                    .map(|(index, core)| match previous_cores.get(index) {
                        Some(previous) => core.percent_since(previous),
                        None => 0.0,
                    })
                    .collect();
            }
            snapshot.cpu.uptime_seconds = procfs::read_uptime().unwrap_or(0.0);
            snapshot.cpu.uptime_text = format_duration(snapshot.cpu.uptime_seconds);
            self.previous_cpu = Some((Instant::now(), total, cores));
            snapshot.interval_ms = interval * 1000.0;
        }

        let model = self
            .model
            .get_or_insert_with(procfs::read_cpu_model)
            .clone();
        snapshot.cpu.model = model.model;
        snapshot.cpu.frequency_mhz = model.frequency_mhz;
        snapshot.cpu.frequency_max_mhz = model.frequency_max_mhz;
        if snapshot.cpu.count == 0 {
            snapshot.cpu.count = model.online;
        }
        if let Some(load) = procfs::read_loadavg() {
            snapshot.cpu.load1 = load[0];
            snapshot.cpu.load5 = load[1];
            snapshot.cpu.load15 = load[2];
            if snapshot.cpu.count > 0 {
                snapshot.cpu.load_per_core = load[0] / snapshot.cpu.count as f64;
            }
        }

        // ---- Memory --------------------------------------------------------
        if let Some(info) = procfs::read_meminfo() {
            snapshot.memory = memory_snapshot(&info);
        }

        // ---- Network -------------------------------------------------------
        let devices = procfs::read_netdev();
        if !devices.is_empty() {
            let now = Instant::now();
            let elapsed = match &self.previous_net {
                Some((at, _)) => at.elapsed().as_secs_f64().max(0.001),
                None => 0.0,
            };
            let previous = self
                .previous_net
                .as_ref()
                .map(|(_, map)| map.clone())
                .unwrap_or_default();

            let mut interfaces = Vec::with_capacity(devices.len());
            let mut current: HashMap<String, (u64, u64)> = HashMap::with_capacity(devices.len());
            let mut rx_rate = 0.0f64;
            let mut tx_rate = 0.0f64;
            let mut rx_total = 0u64;
            let mut tx_total = 0u64;

            for device in &devices {
                let (previous_rx, previous_tx) = previous
                    .get(&device.name)
                    .copied()
                    .unwrap_or((device.rx_bytes, device.tx_bytes));
                let device_rx_rate = rate(device.rx_bytes, previous_rx, elapsed);
                let device_tx_rate = rate(device.tx_bytes, previous_tx, elapsed);
                if !device.is_loopback() {
                    rx_rate += device_rx_rate;
                    tx_rate += device_tx_rate;
                    rx_total = rx_total.saturating_add(device.rx_bytes);
                    tx_total = tx_total.saturating_add(device.tx_bytes);
                }
                current.insert(device.name.clone(), (device.rx_bytes, device.tx_bytes));
                interfaces.push(interface_snapshot(device, device_rx_rate, device_tx_rate));
            }

            self.previous_net = Some((now, current));
            snapshot.network = NetworkSnapshot {
                rx_rate,
                tx_rate,
                rx_total,
                tx_total,
                interfaces,
            };
        }

        // ---- Optional parts ------------------------------------------------
        if options.battery {
            snapshot.battery = power::read_battery();
        }
        if options.thermal {
            snapshot.thermal = power::read_thermal(options.thermal_limit);
        }
        if options.disks {
            snapshot.disks = disk::read_disks();
        }
        if options.uname {
            snapshot.uname = self.uname.get_or_insert_with(read_uname).clone();
        }
        if options.processes {
            snapshot.processes = process::count();
            snapshot.user_processes = process::list_light().len();
        }

        snapshot.elapsed_ms = started.elapsed().as_secs_f64() * 1000.0;
        snapshot
    }

    /// Takes a sample and serialises it, as used by the C ABI.
    pub fn snapshot_json(&mut self, options: &SnapshotOptions) -> crate::Result<String> {
        Ok(serde_json::to_string(&self.snapshot(options))?)
    }

    /// Drops the cached samples, so the next snapshot reports zero rates again.
    /// Called after the system wakes up from suspend, when the deltas would
    /// otherwise span the whole sleep.
    pub fn reset(&mut self) {
        self.previous_cpu = None;
        self.previous_net = None;
    }
}

fn memory_snapshot(info: &MemInfo) -> MemorySnapshot {
    MemorySnapshot {
        total: info.total,
        used: info.used(),
        available: info.available,
        free: info.free,
        buffers: info.buffers,
        cached: info.cached,
        percent: info.percent(),
        swap_total: info.swap_total,
        swap_used: info.swap_used(),
        swap_percent: info.swap_percent(),
        text: format!(
            "{} / {}",
            util::format_bytes(info.used()),
            util::format_bytes(info.total)
        ),
    }
}

fn interface_snapshot(device: &NetDev, rx_rate: f64, tx_rate: f64) -> InterfaceSnapshot {
    InterfaceSnapshot {
        name: device.name.clone(),
        rx_bytes: device.rx_bytes,
        tx_bytes: device.tx_bytes,
        rx_rate,
        tx_rate,
        loopback: device.is_loopback(),
    }
}

/// Bytes per second between two counters, ignoring counter wraps and the first
/// sample.
fn rate(current: u64, previous: u64, elapsed: f64) -> f64 {
    if elapsed <= 0.0 || current < previous {
        return 0.0;
    }
    ((current - previous) as f64 / elapsed).max(0.0)
}

/// Formats seconds as `3 d 04:05`, `04:05` or `0:05`.
pub fn format_duration(seconds: f64) -> String {
    if !seconds.is_finite() || seconds < 0.0 {
        return String::new();
    }
    let total = seconds as u64;
    let days = total / 86_400;
    let hours = (total % 86_400) / 3_600;
    let minutes = (total % 3_600) / 60;
    let secs = total % 60;
    if days > 0 {
        format!("{days} d {hours:02}:{minutes:02}")
    } else if hours > 0 {
        format!("{hours:02}:{minutes:02}:{secs:02}")
    } else {
        format!("{minutes}:{secs:02}")
    }
}

/// Formats a rate in bytes per second.
pub fn format_rate(bytes_per_second: f64) -> String {
    if !bytes_per_second.is_finite() || bytes_per_second <= 0.0 {
        return "0 B/s".to_string();
    }
    format!("{}/s", util::format_bytes(bytes_per_second as u64))
}

/// `uname(2)`, or empty strings where it is unavailable.
pub fn read_uname() -> Uname {
    #[cfg(unix)]
    {
        let mut info: libc::utsname = unsafe { std::mem::zeroed() };
        if unsafe { libc::uname(&mut info) } != 0 {
            return Uname::default();
        }
        Uname {
            sysname: chars_to_string(&info.sysname),
            nodename: chars_to_string(&info.nodename),
            release: chars_to_string(&info.release),
            version: chars_to_string(&info.version),
            machine: chars_to_string(&info.machine),
        }
    }
    #[cfg(not(unix))]
    {
        Uname::default()
    }
}

#[cfg(unix)]
fn chars_to_string(field: &[libc::c_char]) -> String {
    let bytes: Vec<u8> = field
        .iter()
        .take_while(|&&value| value != 0)
        .map(|&value| value as u8)
        .collect();
    String::from_utf8_lossy(&bytes).into_owned()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn options_round_trip_through_flags() {
        let options = SnapshotOptions::full();
        let flags = options.to_flags();
        let back = SnapshotOptions::from_flags(flags);
        assert!(back.disks && back.thermal && back.battery && back.uname && back.per_core);

        let light = SnapshotOptions::light();
        assert!(!light.disks);
        assert!(light.battery);
        let back = SnapshotOptions::from_flags(light.to_flags());
        assert_eq!(back, light);
    }

    #[test]
    fn rates_need_two_samples() {
        assert_eq!(rate(1000, 0, 0.0), 0.0);
        assert_eq!(rate(1000, 2000, 1.0), 0.0, "a wrapped counter is not a rate");
        assert_eq!(rate(1500, 500, 2.0), 500.0);
    }

    #[test]
    fn durations_are_readable() {
        assert_eq!(format_duration(0.0), "0:00");
        assert_eq!(format_duration(65.0), "1:05");
        assert_eq!(format_duration(3_661.0), "01:01:01");
        assert_eq!(format_duration(90_061.0), "1 d 01:01");
        assert_eq!(format_rate(1536.0), "1.5 KiB/s");
    }

    #[test]
    fn memory_snapshot_derives_text() {
        let info = MemInfo {
            total: 8 * 1024 * 1024 * 1024,
            available: 4 * 1024 * 1024 * 1024,
            ..MemInfo::default()
        };
        let memory = memory_snapshot(&info);
        assert_eq!(memory.percent, 50.0);
        assert_eq!(memory.text, "4.0 GiB / 8.0 GiB");
    }

    #[test]
    fn a_snapshot_is_available_on_linux_and_serialises() {
        let mut monitor = Monitor::new();
        let first = monitor.snapshot(&SnapshotOptions::light());
        let second = monitor.snapshot(&SnapshotOptions::light());
        // Give the counters something to count on a busy machine.
        std::thread::sleep(std::time::Duration::from_millis(20));
        let third = monitor.snapshot(&SnapshotOptions::light());

        let json = serde_json::to_string(&third).expect("serialises");
        assert!(json.contains("\"cpu\""));
        assert!(json.contains("uptimeText") || json.contains("\"memory\""));
        assert!(first.cpu.percent >= 0.0 && first.cpu.percent <= 100.0);
        assert!(second.cpu.percent >= 0.0);
        assert!(third.memory.total > 0 || !cfg!(target_os = "linux"));
        assert!(third.cpu.count > 0 || !cfg!(target_os = "linux"));
        monitor.reset();
        assert_eq!(monitor.snapshot(&SnapshotOptions::light()).cpu.percent, 0.0);
    }

    #[test]
    fn uname_is_filled_on_unix() {
        let info = read_uname();
        if cfg!(unix) {
            assert!(!info.sysname.is_empty());
            assert!(!info.machine.is_empty());
        }
    }
}
