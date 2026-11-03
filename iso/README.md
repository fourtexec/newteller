# NewTeller installation images

`iso/build-iso.sh` builds a bootable ISO for **Debian 13**, **Ubuntu 26.04**,
**Arch Linux** or **Fedora**. The ISO boots straight into a live NewTeller
session (no password) and opens the **NewTeller installer**, which installs
that distribution with NewTeller as its only desktop.

```sh
sudo iso/build-iso.sh debian     # on a Debian or Ubuntu machine
sudo iso/build-iso.sh ubuntu     # on a Debian or Ubuntu machine
sudo iso/build-iso.sh arch       # on any Linux (downloads Arch's bootstrap root)
sudo iso/build-iso.sh fedora     # on a Fedora machine
```

The image lands in `iso/out/newteller-<distro>-<date>-x86_64.iso` with a
`.sha256` next to it. A build needs about 10 GB of free space, a network
connection and 20-60 minutes. Options: `--output DIR`, `--work DIR`,
`--keep-work`; `EXTRA_PACKAGES="firefox-esr gimp"` adds packages to the image.

Host tools: `debootstrap` and `squashfs-tools` (Debian/Ubuntu images);
`curl`, `zstd` and `squashfs-tools` (Arch); `dnf`, `squashfs-tools` and
`e2fsprogs` (Fedora). Everything else (compiling NewTeller, GRUB, the ISO
tools) runs inside the image being built, so the host stays clean. A
privileged container of the matching distribution works as a host too:

```sh
podman run --rm -it --privileged -v "$PWD:/src" debian:13 \
    bash -c 'apt-get update && apt-get install -y debootstrap squashfs-tools && /src/iso/build-iso.sh debian'
```

## Writing it to a USB stick

- **Rufus** (Windows): choose the ISO; when Rufus asks how to write it,
  pick **"Write in DD Image mode"**.
- **balenaEtcher**, **Ventoy**, GNOME Disks, or
  `sudo dd if=newteller-….iso of=/dev/sdX bs=4M status=progress oflag=sync`.

The stick boots on UEFI and legacy BIOS machines. Turn **Secure Boot off** to
start the live system (its boot loader is not signed). The installed Debian,
Ubuntu and Fedora systems use their distribution's signed shim and can run
with Secure Boot on again; the installed Arch system needs it off.

## What is on the image

- the distribution's kernel, firmware, NetworkManager (Wi-Fi), PipeWire,
  BlueZ, SDDM as the login screen, the `foot` terminal;
- NewTeller, compiled from this source tree inside the image (the build
  dependencies are removed again afterwards);
- the live user `live` without a password, logged in automatically, allowed
  to administrate without a prompt;
- the installer (`iso/installer`, Python + PySide6/QML), started on login and
  available on the desktop and in the start menu as "Install NewTeller".

## The installer

Steps, in the spirit of Ubuntu's installer: **Welcome** (language),
**Keyboard** (layout with a test field; a second layout gets Alt+Shift),
**Disk** (erase a whole disk and install; the live medium itself is never
offered), **Location** (time zone), **Account** (name, computer name, user
name, password, automatic login), **Summary** (with an explicit "everything
on the disk will be erased" confirmation), **Installing** (progress and the
full log), then **Restart now**.

`newteller-install-backend` does the work as root (through pkexec):

1. partitions the disk: GPT with a 1 GiB EFI system partition on UEFI
   machines, MBR on BIOS machines; ext4 root;
2. copies the live image's pristine root file system (not the running
   overlay) with rsync, plus the Wi-Fi networks joined in the live session;
3. writes fstab (UUIDs), a swap file (up to 4 GiB), host name, time zone,
   locale and keyboard (`/etc/default/keyboard`, which NewTeller reads);
4. removes the live user, its autologin and sudo/polkit rules and the
   installer itself, creates your account (administrator: `sudo` group on
   Debian/Ubuntu, `wheel` on Arch/Fedora), optionally with automatic login;
5. rebuilds the initramfs without the live hooks (`update-initramfs`,
   `mkinitcpio -P`, `dracut`) and installs GRUB for UEFI (with the signed
   shim where the distribution has one) or BIOS.

## How each image boots

| Image | Live root | Kernel command line |
| --- | --- | --- |
| Debian, Ubuntu | `/live/filesystem.squashfs` (live-boot) | `boot=live` |
| Arch | `/arch/x86_64/airootfs.sfs` (archiso hooks) | `archisobasedir=arch archisolabel=NEWTELLER_ARCH` |
| Fedora | `/LiveOS/squashfs.img` with `LiveOS/rootfs.img` (dracut dmsquash-live) | `root=live:CDLABEL=… rd.live.image rd.live.overlay.overlayfs=1` |

The boot menu (GRUB, BIOS and UEFI, made with the image's own
`grub-mkrescue`) offers the live system, the same with safe graphics
(`nomodeset`) and the firmware settings.

## Files

| Path | What it is |
| --- | --- |
| `build-iso.sh` | Entry point: options, the build steps in order. |
| `lib/common.sh` | chroot handling, building NewTeller in the image, the live session, GRUB menu, ISO. |
| `lib/debian.sh` | Debian 13 and Ubuntu 26.04 (debootstrap, apt, live-boot). |
| `lib/arch.sh` | Arch Linux (bootstrap tarball, pacman, mkinitcpio-archiso). |
| `lib/fedora.sh` | Fedora (dnf --installroot, dracut dmsquash-live). |
| `installer/newteller-installer` | The installer application (Python, PySide6). |
| `installer/Installer.qml` | Its interface. |
| `installer/newteller-install-backend` | The root part that installs to the disk. |
