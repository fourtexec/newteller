//! Battery and thermal sensors from `/sys`.
//!
//! Two different supply drivers are handled: the "charge" ones (µAh / µA) and
//! the "energy" ones (µWh / µW). Several batteries are aggregated, which is
//! what a laptop with a removable bay needs.

use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

use crate::util;

/// Aggregated battery state.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct Battery {
    /// True when at least one battery was found.
    pub present: bool,
    /// Number of batteries that were aggregated.
    pub count: usize,
    /// Charge in percent, 0..100.
    pub percent: f64,
    /// `status` of the first battery: Charging, Discharging, Full,
    /// Not charging, Unknown.
    pub status: String,
    /// True while the battery is charging.
    pub charging: bool,
    /// True when running from the battery (the panel then shows the estimate).
    pub on_battery: bool,
    /// Seconds until empty while discharging; `0` when unknown.
    pub time_to_empty_seconds: f64,
    /// Seconds until full while charging; `0` when unknown.
    pub time_to_full_seconds: f64,
    /// Technology, for example `Li-ion`.
    pub technology: String,
    /// Model name of the first battery.
    pub model_name: String,
    /// Capacity level reported by the driver, for example `Normal`.
    pub capacity_level: String,
    /// True when the charge is at or below the critical level (5%).
    pub critical: bool,
}

impl Default for Battery {
    fn default() -> Self {
        Battery {
            present: false,
            count: 0,
            percent: 0.0,
            status: "Unknown".to_string(),
            charging: false,
            on_battery: false,
            time_to_empty_seconds: 0.0,
            time_to_full_seconds: 0.0,
            technology: String::new(),
            model_name: String::new(),
            capacity_level: String::new(),
            critical: false,
        }
    }
}

/// One power supply, as reported by its sysfs directory.
#[derive(Debug, Clone, Default)]
struct Supply {
    name: String,
    kind: String,
    status: String,
    technology: String,
    model_name: String,
    capacity_level: String,
    capacity_percent: f64,
    now: f64,
    full: f64,
    rate: f64,
    online: bool,
    present: bool,
}

/// Reads and aggregates every battery under `/sys/class/power_supply`.
pub fn read_battery() -> Battery {
    read_battery_from(Path::new("/sys/class/power_supply"))
}

/// Battery reader that can be pointed at any sysfs root; used by the tests.
pub fn read_battery_from(root: &Path) -> Battery {
    let mut aggregate = Battery::default();
    let mut total_now = 0.0f64;
    let mut total_full = 0.0f64;
    let mut total_rate = 0.0f64;
    let mut percent_sum = 0.0f64;
    let mut percent_count = 0usize;

    for supply in read_supplies(root) {
        if supply.kind != "Battery" && supply.kind != "UPS" {
            // Mains adapters carry the `online` flag that says whether we are on
            // AC power.
            if supply.kind == "Mains" && supply.online {
                aggregate.on_battery = false;
            }
            continue;
        }
        if !supply.present && supply.capacity_percent == 0.0 && supply.status.is_empty() {
            continue;
        }

        aggregate.present = true;
        aggregate.count += 1;
        if aggregate.status == "Unknown" && !supply.status.is_empty() {
            aggregate.status = supply.status.clone();
        }
        if supply.status == "Charging" {
            aggregate.charging = true;
        }
        if aggregate.technology.is_empty() {
            aggregate.technology = supply.technology.clone();
        }
        if aggregate.model_name.is_empty() {
            aggregate.model_name = supply.model_name.clone();
        }
        if aggregate.capacity_level.is_empty() {
            aggregate.capacity_level = supply.capacity_level.clone();
        }

        total_now += supply.now;
        total_full += supply.full;
        total_rate += supply.rate;
        if supply.capacity_percent > 0.0 {
            percent_sum += supply.capacity_percent;
            percent_count += 1;
        }
    }

    if !aggregate.present {
        return aggregate;
    }

    aggregate.percent = if total_full > 0.0 {
        (total_now / total_full * 100.0).clamp(0.0, 100.0)
    } else if percent_count > 0 {
        (percent_sum / percent_count as f64).clamp(0.0, 100.0)
    } else {
        0.0
    };
    // Only an explicit `Discharging` counts as "on battery": a full battery on
    // AC power and a device without a status file must not trigger the
    // low-battery warning in the panel.
    aggregate.on_battery = aggregate.status == "Discharging";
    aggregate.critical = aggregate.percent <= 5.0 && aggregate.on_battery;

    if total_rate > 0.0 {
        if aggregate.status == "Discharging" && total_now > 0.0 {
            aggregate.time_to_empty_seconds = total_now / total_rate * 3600.0;
        } else if aggregate.status == "Charging" && total_full > total_now {
            aggregate.time_to_full_seconds = (total_full - total_now) / total_rate * 3600.0;
        }
    }

    aggregate
}

fn read_supplies(root: &Path) -> Vec<Supply> {
    let mut out = Vec::new();
    let entries = match std::fs::read_dir(root) {
        Ok(entries) => entries,
        Err(_) => return out,
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if !path.is_dir() {
            continue;
        }
        out.push(read_supply(&path));
    }
    // Stable order: the panel should not flicker when sysfs is unordered.
    out.sort_by(|a, b| a.name.cmp(&b.name));
    out
}

fn read_supply(dir: &Path) -> Supply {
    let name = dir
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default();
    let mut supply = Supply {
        name,
        ..Supply::default()
    };

    supply.kind = read_string(&dir.join("type")).unwrap_or_default();
    supply.status = read_string(&dir.join("status")).unwrap_or_default();
    supply.technology = read_string(&dir.join("technology")).unwrap_or_default();
    supply.model_name = read_string(&dir.join("model_name")).unwrap_or_default();
    supply.capacity_level = read_string(&dir.join("capacity_level")).unwrap_or_default();
    supply.online = read_string(&dir.join("online")).as_deref() == Some("1");
    supply.present = match read_string(&dir.join("present")) {
        Some(value) => value == "1",
        // Most batteries have no `present` file; a readable `type` is enough.
        None => !supply.kind.is_empty(),
    };

    if let Some(value) = read_string(&dir.join("capacity")) {
        supply.capacity_percent = value.parse::<f64>().unwrap_or(0.0);
    }

    // Charge based drivers report µAh and µA, energy based ones µWh and µW.
    let (now, full, rate) = if let Some(now) = read_number(&dir.join("charge_now")) {
        (
            now,
            read_number(&dir.join("charge_full"))
                .or_else(|| read_number(&dir.join("charge_full_design")))
                .unwrap_or(0.0),
            read_number(&dir.join("current_now")).unwrap_or(0.0),
        )
    } else if let Some(now) = read_number(&dir.join("energy_now")) {
        (
            now,
            read_number(&dir.join("energy_full"))
                .or_else(|| read_number(&dir.join("energy_full_design")))
                .unwrap_or(0.0),
            read_number(&dir.join("power_now")).unwrap_or(0.0),
        )
    } else {
        (0.0, 0.0, 0.0)
    };
    supply.now = now;
    supply.full = full;
    supply.rate = rate;
    supply
}

fn read_string(path: &Path) -> Option<String> {
    util::read_to_string_lossy(path).map(|text| text.trim().to_string())
}

fn read_number(path: &Path) -> Option<f64> {
    read_string(path)?.parse::<f64>().ok()
}

/// One thermal zone.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct ThermalZone {
    /// Zone name, for example `x86_pkg_temp`.
    pub name: String,
    /// Temperature in degrees Celsius.
    pub celsius: f64,
    /// sysfs path, for diagnostics.
    pub path: String,
}

impl Default for ThermalZone {
    fn default() -> Self {
        ThermalZone {
            name: String::new(),
            celsius: 0.0,
            path: String::new(),
        }
    }
}

/// Reads the thermal zones, hottest first, at most `limit` of them.
pub fn read_thermal(limit: usize) -> Vec<ThermalZone> {
    read_thermal_from(Path::new("/sys/class/thermal"), limit)
}

/// Thermal reader that can be pointed at any sysfs root; used by the tests.
pub fn read_thermal_from(root: &Path, limit: usize) -> Vec<ThermalZone> {
    let mut zones = Vec::new();
    let entries = match std::fs::read_dir(root) {
        Ok(entries) => entries,
        Err(_) => return zones,
    };
    for entry in entries.flatten() {
        let path: PathBuf = entry.path();
        if !path.is_dir() {
            continue;
        }
        let Some(name) = path.file_name().map(|n| n.to_string_lossy().into_owned()) else {
            continue;
        };
        if !name.starts_with("thermal_zone") {
            continue;
        }
        let Some(milli) = read_number(&path.join("temp")) else {
            continue;
        };
        let type_name = read_string(&path.join("type")).unwrap_or_else(|| name.clone());
        zones.push(ThermalZone {
            name: type_name,
            celsius: milli / 1000.0,
            path: path.to_string_lossy().into_owned(),
        });
    }
    zones.sort_by(|a, b| {
        b.celsius
            .partial_cmp(&a.celsius)
            .unwrap_or(std::cmp::Ordering::Equal)
            .then_with(|| a.name.cmp(&b.name))
    });
    if limit > 0 && zones.len() > limit {
        zones.truncate(limit);
    }
    zones
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    fn write(path: &Path, body: &str) {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent).unwrap();
        }
        let mut file = std::fs::File::create(path).unwrap();
        file.write_all(body.as_bytes()).unwrap();
    }

    fn temp_root(tag: &str) -> PathBuf {
        let root = std::env::temp_dir().join(format!("newteller-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        std::fs::create_dir_all(&root).unwrap();
        root
    }

    #[test]
    fn aggregates_a_discharging_battery() {
        let root = temp_root("battery");
        let bat = root.join("BAT0");
        write(&bat.join("type"), "Battery\n");
        write(&bat.join("status"), "Discharging\n");
        write(&bat.join("capacity"), "42\n");
        write(&bat.join("charge_now"), "21000000\n");
        write(&bat.join("charge_full"), "50000000\n");
        write(&bat.join("current_now"), "1000000\n");
        write(&bat.join("technology"), "Li-ion\n");
        write(&bat.join("model_name"), "DELL XYZ\n");
        write(&root.join("AC").join("type"), "Mains\n");
        write(&root.join("AC").join("online"), "0\n");

        let battery = read_battery_from(&root);
        assert!(battery.present);
        assert_eq!(battery.count, 1);
        assert!((battery.percent - 42.0).abs() < 0.01);
        assert_eq!(battery.status, "Discharging");
        assert!(battery.on_battery);
        assert!(!battery.charging);
        assert!(!battery.critical);
        // 21 Ah left at 1 A -> 21 hours.
        assert!((battery.time_to_empty_seconds - 21.0 * 3600.0).abs() < 1.0);
        assert_eq!(battery.technology, "Li-ion");
        assert_eq!(battery.model_name, "DELL XYZ");
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn estimates_time_to_full_while_charging() {
        let root = temp_root("charging");
        let bat = root.join("BAT0");
        write(&bat.join("type"), "Battery\n");
        write(&bat.join("status"), "Charging\n");
        write(&bat.join("energy_now"), "20000000\n");
        write(&bat.join("energy_full"), "60000000\n");
        write(&bat.join("power_now"), "20000000\n");

        let battery = read_battery_from(&root);
        assert!(battery.charging);
        assert!((battery.percent - 33.333).abs() < 0.01);
        // 40 Wh missing at 20 W -> 2 hours.
        assert!((battery.time_to_full_seconds - 7200.0).abs() < 1.0);
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn reports_no_battery_on_a_desktop() {
        let root = temp_root("nobattery");
        write(&root.join("AC").join("type"), "Mains\n");
        write(&root.join("AC").join("online"), "1\n");
        let battery = read_battery_from(&root);
        assert!(!battery.present);
        assert!(!battery.on_battery);
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn reads_thermal_zones_hottest_first() {
        let root = temp_root("thermal");
        write(&root.join("thermal_zone0").join("type"), "acpitz\n");
        write(&root.join("thermal_zone0").join("temp"), "45000\n");
        write(&root.join("thermal_zone1").join("type"), "x86_pkg_temp\n");
        write(&root.join("thermal_zone1").join("temp"), "61000\n");
        write(&root.join("cooling_device0").join("type"), "fan\n");

        let zones = read_thermal_from(&root, 0);
        assert_eq!(zones.len(), 2);
        assert_eq!(zones[0].name, "x86_pkg_temp");
        assert!((zones[0].celsius - 61.0).abs() < 0.001);

        let limited = read_thermal_from(&root, 1);
        assert_eq!(limited.len(), 1);
        let _ = std::fs::remove_dir_all(&root);
    }
}
