#!/usr/bin/env bash
# Fedora: dnf --installroot, live boot with dracut's dmsquash-live module.
#
# Host: Fedora (dnf5 or dnf) with squashfs-tools and e2fsprogs.
# Live layout (the classic Fedora live layout): /LiveOS/squashfs.img holding
# LiveOS/rootfs.img (ext4), kernel command line
# root=live:CDLABEL=<label> rd.live.image; with rd.live.overlay.overlayfs=1
# dracut mounts the ext4 image read-only at /run/rootfsbase, which is what the
# installer copies.

ADMIN_GROUP=wheel
FEDORA_RELEASE=${FEDORA_RELEASE:-$(rpm -E %fedora 2> /dev/null || echo 42)}
DISTRO_NAME="Fedora $FEDORA_RELEASE"
ISO_LABEL="NEWTELLER_FEDORA$FEDORA_RELEASE"
LIVE_CMDLINE="root=live:CDLABEL=$ISO_LABEL rd.live.image rd.live.overlay.overlayfs=1 selinux=0"

distro_check_host() {
    need mksquashfs mkfs.ext4 rpm
    if command -v dnf5 > /dev/null 2>&1; then
        DNF=(dnf5 --use-host-config --skip-unavailable)
    elif command -v dnf > /dev/null 2>&1; then
        DNF=(dnf --setopt=strict=False)
    else
        die 'Building the Fedora image needs a Fedora host (dnf).'
    fi
}

fedora_packages=(
    @core kernel kernel-modules-extra linux-firmware
    dracut dracut-live dracut-config-generic
    glibc-langpack-en glibc-langpack-ru glibc-langpack-uk glibc-langpack-be glibc-langpack-kk
    glibc-langpack-de glibc-langpack-fr glibc-langpack-es glibc-langpack-it glibc-langpack-pt
    glibc-langpack-pl glibc-langpack-tr glibc-langpack-zh glibc-langpack-ja
    sudo NetworkManager NetworkManager-wifi wpa_supplicant bluez
    grub2-tools grub2-tools-extra grub2-pc grub2-pc-modules grub2-efi-x64 grub2-efi-x64-modules
    shim-x64 efibootmgr os-prober
    dosfstools e2fsprogs rsync parted gdisk xorriso mtools
    sddm sddm-x11 xorg-x11-server-Xorg xorg-x11-drv-libinput
    pipewire pipewire-pulseaudio wireplumber
    python3 python3-pyside6 foot dejavu-sans-fonts google-noto-sans-fonts xdg-user-dirs
    dnf5 fedora-repos
)

distro_bootstrap() {
    log "Installing $DISTRO_NAME into the build root"
    mkdir -p "$ROOTFS"
    # shellcheck disable=SC2086
    "${DNF[@]}" -y --installroot="$ROOTFS" --releasever="$FEDORA_RELEASE" \
        --setopt=install_weak_deps=False \
        install "${fedora_packages[@]}" $EXTRA_PACKAGES
}

distro_install_base() {
    in_root 'python3 -c "import PySide6.QtQuick"' \
        || die 'PySide6 (QtQuick) is missing in the image: the installer cannot run.'
    echo 'LANG=en_US.UTF-8' > "$ROOTFS/etc/locale.conf"
    # SELinux labels are applied on the installed system's first boot.
    if [[ -f $ROOTFS/etc/selinux/config ]]; then
        sed -i 's/^SELINUX=.*/SELINUX=permissive/' "$ROOTFS/etc/selinux/config"
    fi
}

distro_snapshot_packages() {
    in_root "rpm -qa --qf '%{NAME}\n' | sort -u > /var/tmp/newteller-rpms-$1"
}

distro_remove_build_deps() {
    log 'Removing the build-only packages'
    in_root '
        rpm -qa --qf "%{NAME}\n" | sort -u > /var/tmp/newteller-rpms-after
        comm -13 /var/tmp/newteller-rpms-before /var/tmp/newteller-rpms-after > /var/tmp/newteller-build-only
        if [[ -s /var/tmp/newteller-build-only ]]; then
            xargs dnf -y remove < /var/tmp/newteller-build-only
        fi
        rm -f /var/tmp/newteller-*
    '
}

distro_configure() {
    # The installed system goes back to enforcing once relabelled.
    if [[ -f $ROOTFS/etc/selinux/config ]]; then
        sed -i 's/^SELINUX=.*/SELINUX=enforcing/' "$ROOTFS/etc/selinux/config"
    fi
}

kernel_version() {
    find "$ROOTFS/lib/modules" -mindepth 1 -maxdepth 1 -printf '%f\n' | sort -V | tail -n 1
}

distro_live_initramfs() {
    log 'Building the live initramfs (dracut dmsquash-live)'
    local kver
    kver=$(kernel_version)
    [[ -n $kver ]] || die 'No kernel in the image.'
    [[ -e $ROOTFS/boot/vmlinuz-$kver ]] || cp "$ROOTFS/lib/modules/$kver/vmlinuz" "$ROOTFS/boot/vmlinuz-$kver"
    in_root "dracut --force --no-hostonly --add 'dmsquash-live' --omit 'plymouth' \
        --kver '$kver' /boot/initramfs-live.img"
}

distro_clean() {
    in_root 'dnf clean all > /dev/null 2>&1 || true'
    rm -rf "$ROOTFS"/var/cache/dnf/* "$ROOTFS"/var/cache/libdnf5/*
}

distro_pack() {
    local kver size
    kver=$(kernel_version)
    install -d "$WORK/iso/LiveOS" "$WORK/iso/boot/grub" "$WORK/squash/LiveOS"
    log 'Writing the ext4 root image'
    # Room for the files plus a quarter, at least 1 GiB free.
    size=$(du -sx --block-size=1M "$ROOTFS" | cut -f1)
    size=$((size + size / 4 + 1024))
    rm -f "$WORK/squash/LiveOS/rootfs.img"
    truncate -s "${size}M" "$WORK/squash/LiveOS/rootfs.img"
    mkfs.ext4 -q -F -L Anaconda -d "$ROOTFS" "$WORK/squash/LiveOS/rootfs.img"
    log 'Packing it (squashfs)'
    squash "$WORK/squash" "$WORK/iso/LiveOS/squashfs.img"
    rm -rf "$WORK/squash"
    cp "$ROOTFS/boot/vmlinuz-$kver" "$WORK/iso/boot/vmlinuz"
    cp "$ROOTFS/boot/initramfs-live.img" "$WORK/iso/boot/initrd.img"
}
