//! Disk usage from `/proc/self/mounts` and `statvfs`.

use std::path::Path;

use serde::{Deserialize, Serialize};

use crate::util;

/// Filesystems worth showing. Pseudo filesystems are noise in a panel widget.
pub const REAL_FILESYSTEMS: [&str; 17] = [
    "ext2", "ext3", "ext4", "btrfs", "xfs", "zfs", "jfs", "reiserfs", "f2fs", "vfat", "exfat",
    "ntfs", "ntfs3", "fuseblk", "overlay", "erofs", "bcachefs",
];

/// Mount points that are never interesting to a user.
pub const IGNORED_PREFIXES: [&str; 8] = [
    "/proc",
    "/sys",
    "/dev",
    "/run",
    "/snap",
    "/var/lib/docker",
    "/var/lib/containers",
    "/var/lib/snapd",
];

/// Usage of one mount point.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct DiskUsage {
    /// Mount point.
    pub mount: String,
    /// Device or source.
    pub device: String,
    /// Filesystem type.
    pub fs_type: String,
    /// Total size in bytes.
    pub total: u64,
    /// Used bytes, excluding the root reserve.
    pub used: u64,
    /// Bytes available to the user.
    pub available: u64,
    /// Used percentage, computed the way `df` does it.
    pub percent: f64,
    /// Human readable total, ready to print.
    pub total_text: String,
    /// Human readable available, ready to print.
    pub available_text: String,
}

impl Default for DiskUsage {
    fn default() -> Self {
        DiskUsage {
            mount: String::new(),
            device: String::new(),
            fs_type: String::new(),
            total: 0,
            used: 0,
            available: 0,
            percent: 0.0,
            total_text: String::new(),
            available_text: String::new(),
        }
    }
}

/// One line of `/proc/self/mounts`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Mount {
    /// Device or source.
    pub device: String,
    /// Mount point, with the octal escapes resolved.
    pub mount: String,
    /// Filesystem type.
    pub fs_type: String,
}

/// Reads the real mounts of the running system.
pub fn read_disks() -> Vec<DiskUsage> {
    read_disks_from(Path::new("/proc/self/mounts"))
}

/// Disk reader that can be pointed at any mounts file; used by the tests.
///
/// `statvfs` still runs against the live mount point, so a synthetic mounts file
/// only produces entries for paths that exist.
pub fn read_disks_from(mounts: &Path) -> Vec<DiskUsage> {
    let text = match util::read_capped(mounts, 512 * 1024) {
        Some(text) => text,
        None => return Vec::new(),
    };
    let mut out: Vec<DiskUsage> = Vec::new();

    for mount in parse_mounts(&text) {
        if !REAL_FILESYSTEMS.contains(&mount.fs_type.as_str()) {
            continue;
        }
        if IGNORED_PREFIXES.iter().any(|prefix| mount.mount.starts_with(prefix)) {
            continue;
        }
        // The same device can be bind mounted several times; keep the shortest
        // (usually the canonical) mount point.
        if mount.device.starts_with('/') {
            if let Some(existing) = out.iter().position(|entry| entry.device == mount.device) {
                if out[existing].mount.len() <= mount.mount.len() {
                    continue;
                }
                out.swap_remove(existing);
            }
        }
        let Some((total, used, available)) = usage_of(&mount.mount) else {
            continue;
        };
        if total == 0 {
            continue;
        }
        out.push(DiskUsage {
            mount: mount.mount,
            device: mount.device,
            fs_type: mount.fs_type,
            total,
            used,
            available,
            percent: percent_of(used, available),
            total_text: util::format_bytes(total),
            available_text: util::format_bytes(available),
        });
    }

    out.sort_by(|a, b| a.mount.cmp(&b.mount));
    out
}

/// Used percentage, following `df`: the root reserve is neither used nor
/// available, so it must not be counted as free space.
pub fn percent_of(used: u64, available: u64) -> f64 {
    let denominator = used.saturating_add(available);
    if denominator == 0 {
        return 0.0;
    }
    (used as f64 / denominator as f64 * 100.0).clamp(0.0, 100.0)
}

/// Parses a mounts file, resolving the octal escapes used for spaces and tabs.
pub fn parse_mounts(text: &str) -> Vec<Mount> {
    let mut out = Vec::new();
    for line in text.lines() {
        let fields: Vec<&str> = line.split_whitespace().collect();
        if fields.len() < 3 {
            continue;
        }
        out.push(Mount {
            device: unescape_mount(fields[0]),
            mount: unescape_mount(fields[1]),
            fs_type: unescape_mount(fields[2]),
        });
    }
    out
}

/// Resolves `\040` (space), `\011` (tab), `\012` (newline) and `\134`
/// (backslash) in a mounts field.
pub fn unescape_mount(input: &str) -> String {
    if !input.contains('\\') {
        return input.to_string();
    }
    let bytes = input.as_bytes();
    let mut out = String::with_capacity(input.len());
    let mut index = 0usize;
    while index < bytes.len() {
        if bytes[index] == b'\\' && index + 3 < bytes.len() {
            let octal = &input[index + 1..index + 4];
            if octal.bytes().all(|b| b.is_ascii_digit()) {
                if let Ok(value) = u8::from_str_radix(octal, 8) {
                    out.push(value as char);
                    index += 4;
                    continue;
                }
            }
        }
        // Bytes of a UTF-8 sequence are copied one char at a time by decoding at
        // every step, which keeps multi-byte characters intact.
        match input[index..].chars().next() {
            Some(c) => {
                out.push(c);
                index += c.len_utf8();
            }
            None => break,
        }
    }
    out
}

#[cfg(unix)]
fn usage_of(path: &str) -> Option<(u64, u64, u64)> {
    use std::ffi::CString;

    let c_path = CString::new(path).ok()?;
    let mut stats: libc::statvfs = unsafe { std::mem::zeroed() };
    let rc = unsafe { libc::statvfs(c_path.as_ptr(), &mut stats) };
    if rc != 0 {
        return None;
    }
    let block = if stats.f_frsize != 0 {
        stats.f_frsize as u64
    } else {
        stats.f_bsize as u64
    };
    if block == 0 {
        return None;
    }
    let total = stats.f_blocks as u64 * block;
    let free = stats.f_bfree as u64 * block;
    let available = stats.f_bavail as u64 * block;
    Some((total, total.saturating_sub(free), available))
}

#[cfg(not(unix))]
fn usage_of(_path: &str) -> Option<(u64, u64, u64)> {
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    const MOUNTS: &str = "\
sysfs /sys sysfs rw,nosuid 0 0
proc /proc proc rw 0 0
/dev/nvme0n1p2 / ext4 rw,relatime 0 0
/dev/nvme0n1p2 /home/user ext4 rw,relatime 0 0
tmpfs /run tmpfs rw 0 0
/dev/sda1 /boot/efi vfat rw 0 0
/dev/loop0 /snap/core/1 unixfs rw 0 0
overlay /var/lib/docker/overlay2/x/merged overlay rw 0 0
/dev/sdb1 /mnt/My\\040Disk ext4 rw 0 0
";

    #[test]
    fn parses_and_unescapes_mounts() {
        let mounts = parse_mounts(MOUNTS);
        assert_eq!(mounts.len(), 9);
        assert_eq!(mounts[8].mount, "/mnt/My Disk");
        assert_eq!(mounts[2].fs_type, "ext4");
    }

    #[test]
    fn filters_pseudo_filesystems_and_duplicates() {
        let root = std::env::temp_dir().join(format!("newteller-mounts-{}", std::process::id()));
        std::fs::create_dir_all(&root).unwrap();
        let file = root.join("mounts");
        std::fs::write(&file, MOUNTS).unwrap();

        let disks = read_disks_from(&file);
        // Only mounts that exist on the test machine survive `statvfs`, but the
        // pseudo filesystems must never be among them.
        assert!(disks.iter().all(|d| REAL_FILESYSTEMS.contains(&d.fs_type.as_str())));
        assert!(disks.iter().all(|d| !d.mount.starts_with("/proc")));
        assert!(disks.iter().all(|d| !d.mount.starts_with("/snap")));
        let mut devices = disks.iter().map(|d| d.device.clone()).collect::<Vec<_>>();
        let before = devices.len();
        devices.sort();
        devices.dedup();
        assert_eq!(before, devices.len(), "a device must appear only once");

        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn percent_ignores_the_root_reserve() {
        assert_eq!(percent_of(0, 0), 0.0);
        assert_eq!(percent_of(50, 50), 50.0);
        // 90 used, 10 available: df would say 90%, not 72% of the raw size.
        assert_eq!(percent_of(90, 10), 90.0);
    }

    #[test]
    fn unescaping_keeps_utf8_intact() {
        assert_eq!(unescape_mount("/mnt/Grüße"), "/mnt/Grüße");
        assert_eq!(unescape_mount("/a\\011b"), "/a\tb");
        assert_eq!(unescape_mount("/trailing\\"), "/trailing\\");
    }
}
