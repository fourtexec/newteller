#!/usr/bin/env bash
# Arch Linux: the official bootstrap tarball + pacman, live boot with the
# archiso initramfs hooks (mkinitcpio-archiso).
#
# Host: any Linux with curl, tar with zstd support and squashfs-tools; the
# package manager runs inside the downloaded root.
# Live layout: /arch/x86_64/airootfs.sfs, kernel command line
# archisobasedir=arch archisolabel=<label>; archiso mounts the image at
# /run/archiso/airootfs, which is what the installer copies.

ADMIN_GROUP=wheel
DISTRO_NAME='Arch Linux'
ISO_LABEL=NEWTELLER_ARCH
ARCH_MIRROR=${ARCH_MIRROR:-https://geo.mirror.pkgbuild.com}
LIVE_CMDLINE="archisobasedir=arch archisolabel=$ISO_LABEL"

distro_check_host() {
    need curl tar zstd mksquashfs
}

distro_bootstrap() {
    log "Downloading the Arch Linux bootstrap image from $ARCH_MIRROR"
    local tarball="$WORK/archlinux-bootstrap.tar.zst"
    curl -fL --retry 3 -o "$tarball" "$ARCH_MIRROR/iso/latest/archlinux-bootstrap-x86_64.tar.zst"
    tar --zstd -xpf "$tarball" -C "$WORK" --numeric-owner
    rm -f "$tarball"
    mv "$WORK/root.x86_64" "$ROOTFS"

    printf 'Server = %s/$repo/os/$arch\n' "$ARCH_MIRROR" > "$ROOTFS/etc/pacman.d/mirrorlist"
    # The build root is not a mount point (no free-space check) and runs
    # pacman's downloads without the sandbox inside chroot.
    sed -i 's/^CheckSpace/#CheckSpace/' "$ROOTFS/etc/pacman.conf"
    grep -q '^DisableSandbox' "$ROOTFS/etc/pacman.conf" \
        || sed -i '/^\[options\]/a DisableSandbox' "$ROOTFS/etc/pacman.conf"
}

pacman_install_available() {
    in_root "
        wanted=()
        for package in $*; do
            if pacman -Si \"\$package\" > /dev/null 2>&1 || pacman -Sg \"\$package\" > /dev/null 2>&1; then
                wanted+=(\"\$package\")
            else
                echo \"note: \$package is not available, skipped\" >&2
            fi
        done
        pacman -S --noconfirm --needed \"\${wanted[@]}\"
    "
}

distro_install_base() {
    log 'Installing the base system'
    in_root '
        pacman-key --init
        pacman-key --populate archlinux
        pacman -Syu --noconfirm --needed archlinux-keyring
        pacman -S --noconfirm --needed \
            base linux linux-firmware mkinitcpio mkinitcpio-archiso \
            sudo nano bash-completion less pciutils usbutils \
            networkmanager wpa_supplicant bluez bluez-utils \
            grub efibootmgr os-prober dosfstools e2fsprogs rsync parted gptfdisk \
            libisoburn mtools squashfs-tools \
            sddm xorg-server xf86-input-libinput \
            pipewire pipewire-pulse wireplumber \
            python pyside6 ttf-dejavu
    '
    pacman_install_available intel-ucode amd-ucode foot noto-fonts xdg-user-dirs $EXTRA_PACKAGES
    in_root 'python3 -c "import PySide6.QtQuick"' \
        || die 'PySide6 (QtQuick) is missing in the image: the installer cannot run.'
    in_root '
        sed -i "s/^#\(en_US.UTF-8 UTF-8\)/\1/" /etc/locale.gen
        locale-gen
        echo LANG=en_US.UTF-8 > /etc/locale.conf
    '
}

distro_snapshot_packages() {
    in_root "pacman -Qqe | sort > /var/tmp/newteller-explicit-$1"
}

# The packages the build installed explicitly become dependencies, then
# every orphan goes (repeatedly, orphans can free further orphans).
distro_remove_build_deps() {
    log 'Removing the build-only packages'
    in_root '
        pacman -Qqe | sort > /var/tmp/newteller-explicit-after
        comm -13 /var/tmp/newteller-explicit-before /var/tmp/newteller-explicit-after > /var/tmp/newteller-build-only
        if [[ -s /var/tmp/newteller-build-only ]]; then
            xargs pacman -D --asdeps < /var/tmp/newteller-build-only > /dev/null
        fi
        for _ in 1 2 3 4 5; do
            orphans=$(pacman -Qtdq || true)
            [[ -n $orphans ]] || break
            pacman -Rns --noconfirm $orphans
        done
        rm -f /var/tmp/newteller-*
    '
}

distro_configure() {
    # Members of wheel administrate the installed system.
    install -d -m 750 "$ROOTFS/etc/sudoers.d"
    printf '%%wheel ALL=(ALL:ALL) ALL\n' > "$ROOTFS/etc/sudoers.d/10-newteller-wheel"
    chmod 440 "$ROOTFS/etc/sudoers.d/10-newteller-wheel"
}

distro_live_initramfs() {
    log 'Building the live initramfs (archiso hooks)'
    # A separate configuration file: the installed system keeps the normal
    # /etc/mkinitcpio.conf and its preset.
    install -d "$ROOTFS/etc/newteller"
    cat > "$ROOTFS/etc/newteller/live-mkinitcpio.conf" <<'EOF'
MODULES=(loop overlay squashfs)
BINARIES=()
FILES=()
HOOKS=(base udev microcode modconf kms memdisk archiso archiso_loop_mnt block filesystems keyboard)
COMPRESSION="xz"
EOF
    in_root 'mkinitcpio -c /etc/newteller/live-mkinitcpio.conf -k /boot/vmlinuz-linux -g /boot/initramfs-live.img'
}

distro_clean() {
    in_root 'pacman -Scc --noconfirm > /dev/null 2>&1 || true'
    rm -rf "$ROOTFS"/var/cache/pacman/pkg/*
    # The installed system must not inherit the build's mirror tweaks.
    sed -i 's/^#CheckSpace/CheckSpace/; /^DisableSandbox$/d' "$ROOTFS/etc/pacman.conf"
    printf 'Server = https://geo.mirror.pkgbuild.com/$repo/os/$arch\n' > "$ROOTFS/etc/pacman.d/mirrorlist"
}

distro_pack() {
    [[ -e $ROOTFS/boot/vmlinuz-linux ]] || die 'No kernel in the image.'
    install -d "$WORK/iso/arch/x86_64" "$WORK/iso/boot/grub"
    log 'Packing the live root filesystem (squashfs)'
    squash "$ROOTFS" "$WORK/iso/arch/x86_64/airootfs.sfs"
    cp "$ROOTFS/boot/vmlinuz-linux" "$WORK/iso/boot/vmlinuz"
    cp "$ROOTFS/boot/initramfs-live.img" "$WORK/iso/boot/initrd.img"
}
