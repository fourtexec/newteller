#!/usr/bin/env bash
# Build a bootable NewTeller ISO: a live system with the NewTeller desktop and
# its installer, which starts by itself and installs the system (NewTeller as
# the only desktop) to a disk.
#
#   sudo iso/build-iso.sh debian|ubuntu|arch|fedora [options]
#
# Options:
#   --output DIR     where the .iso (and its .sha256) goes   [iso/out]
#   --work DIR       scratch space, ~10 GB                    [iso/work/<distro>]
#   --keep-work      keep the scratch root after a successful build
#
# Environment:
#   EXTRA_PACKAGES   more packages for the image ("firefox-esr gimp")
#   MIRROR, SUITE    Debian/Ubuntu mirror and suite
#   ARCH_MIRROR      Arch mirror (https://geo.mirror.pkgbuild.com)
#   FEDORA_RELEASE   Fedora release to build (default: the host's)
#
# Hosts:
#   debian, ubuntu   a Debian or Ubuntu machine with debootstrap, squashfs-tools
#   arch             any Linux with curl, zstd and squashfs-tools
#   fedora           a Fedora machine with dnf, squashfs-tools and e2fsprogs
# A matching container works too (podman/docker run --privileged).
#
# Write the result to a USB stick with Rufus (choose "DD image" mode when it
# asks), balenaEtcher, Ventoy or `dd`. The live system boots on BIOS and UEFI;
# turn Secure Boot off for the live medium (the installed Debian, Ubuntu and
# Fedora systems boot with Secure Boot through their signed shim).
set -Eeuo pipefail

REPO=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
# shellcheck source=iso/lib/common.sh
source "$REPO/iso/lib/common.sh"

usage() {
    sed -n '2,/^set -E/p' "${BASH_SOURCE[0]}" | sed '$d; s/^# \{0,1\}//'
    exit "${1:-0}"
}

DISTRO=${1:-}
[[ -n $DISTRO && $DISTRO != -h && $DISTRO != --help ]] || usage 0
shift
OUTPUT="$REPO/iso/out"
WORK=''
KEEP_WORK=no
while [[ $# -gt 0 ]]; do
    case $1 in
        --output) OUTPUT=$(realpath -m -- "$2"); shift 2 ;;
        --work) WORK=$(realpath -m -- "$2"); shift 2 ;;
        --keep-work) KEEP_WORK=yes; shift ;;
        -h|--help) usage 0 ;;
        *) echo "unknown option: $1" >&2; usage 1 ;;
    esac
done
EXTRA_PACKAGES=${EXTRA_PACKAGES:-}

case $DISTRO in
    debian|ubuntu) source "$REPO/iso/lib/debian.sh" ;;
    arch) source "$REPO/iso/lib/arch.sh" ;;
    fedora) source "$REPO/iso/lib/fedora.sh" ;;
    *) echo "unknown distribution: $DISTRO (debian, ubuntu, arch or fedora)" >&2; exit 1 ;;
esac

[[ $EUID -eq 0 ]] || die 'Run as root (sudo): the build mounts file systems and uses chroot.'
[[ $(uname -m) == x86_64 ]] || die 'The images are built for x86_64.'
need mount umount chroot tar realpath sha256sum
distro_check_host

WORK=${WORK:-$REPO/iso/work/$DISTRO}
ROOTFS="$WORK/rootfs"

# Never delete a root that still has something mounted inside.
if [[ -d $WORK ]]; then
    if findmnt -rno TARGET | grep -q "^$WORK/"; then
        die "Something is still mounted under $WORK; unmount it first (umount -R $ROOTFS)."
    fi
    log "Removing the previous build in $WORK"
    rm -rf --one-file-system -- "$WORK"
fi
mkdir -p "$WORK" "$OUTPUT"

on_exit() {
    local status=$?
    chroot_umount "$ROOTFS" 2> /dev/null || true
    if [[ $status -ne 0 ]]; then
        warn "The build failed (exit $status). The work tree is kept in $WORK for inspection."
    fi
}
trap on_exit EXIT

started=$(date +%s)
iso_name="newteller-$DISTRO-$(date +%Y%m%d)-x86_64.iso"
log "Building $iso_name ($DISTRO_NAME) in $WORK"

# 1. Root file system.
distro_bootstrap

# 2. System, NewTeller, installer.
chroot_mount "$ROOTFS"
distro_install_base
build_newteller
install_installer
configure_defaults

# 3. Live session.
configure_live "$ADMIN_GROUP"
distro_configure
distro_live_initramfs
distro_clean
clean_root
chroot_umount "$ROOTFS"

# 4. Root image, kernel, initramfs and boot menu.
distro_pack
write_grub_cfg "$WORK/iso/boot/grub/grub.cfg" "$LIVE_CMDLINE"
printf '%s\n' "$DISTRO_NAME with NewTeller, built $(date -u +%FT%TZ)" > "$WORK/iso/newteller-release"

# 5. The ISO, made by the image's own GRUB tools.
chroot_mount "$ROOTFS"
make_iso "$ISO_LABEL" "$OUTPUT/$iso_name"
chroot_umount "$ROOTFS"

if [[ $KEEP_WORK == no ]]; then
    rm -rf --one-file-system -- "$WORK"
fi
log "Done in $((($(date +%s) - started) / 60)) min: $OUTPUT/$iso_name ($(du -h "$OUTPUT/$iso_name" | cut -f1))"
