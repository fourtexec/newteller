#!/usr/bin/env bash
# Debian 13 (trixie) and Ubuntu 26.04 (resolute): debootstrap + live-boot.
#
# Host: Debian or Ubuntu with debootstrap and squashfs-tools.
# Live layout: /live/filesystem.squashfs, kernel command line boot=live;
# live-boot mounts the image at /run/live/rootfs/filesystem.squashfs, which is
# what the installer copies.

ADMIN_GROUP=sudo

if [[ $DISTRO == debian ]]; then
    SUITE=${SUITE:-trixie}
    MIRROR=${MIRROR:-http://deb.debian.org/debian}
    SECURITY_MIRROR=${SECURITY_MIRROR:-http://security.debian.org/debian-security}
    COMPONENTS='main contrib non-free non-free-firmware'
    DISTRO_NAME='Debian 13'
    ISO_LABEL=NEWTELLER_DEBIAN13
    FALLBACK_SCRIPT=sid
else
    SUITE=${SUITE:-resolute}
    MIRROR=${MIRROR:-http://archive.ubuntu.com/ubuntu}
    SECURITY_MIRROR=${SECURITY_MIRROR:-http://security.ubuntu.com/ubuntu}
    COMPONENTS='main restricted universe multiverse'
    DISTRO_NAME='Ubuntu 26.04'
    ISO_LABEL=NEWTELLER_UBUNTU2604
    FALLBACK_SCRIPT=gutsy
fi
LIVE_CMDLINE='boot=live'

distro_check_host() {
    need debootstrap mksquashfs
}

distro_bootstrap() {
    log "Bootstrapping $DISTRO_NAME ($SUITE) from $MIRROR"
    local script=()
    # An older debootstrap does not know the newest suite yet; its rules are
    # the same as the development suite's.
    if [[ ! -e /usr/share/debootstrap/scripts/$SUITE ]]; then
        script=("/usr/share/debootstrap/scripts/$FALLBACK_SCRIPT")
    fi
    debootstrap --arch=amd64 --variant=minbase --components="${COMPONENTS// /,}" \
        --include=ca-certificates,apt-utils "$SUITE" "$ROOTFS" "$MIRROR" "${script[@]}"
}

apt_sources() {
    rm -f "$ROOTFS"/etc/apt/sources.list.d/*.sources "$ROOTFS"/etc/apt/sources.list.d/*.list
    {
        echo "deb $MIRROR $SUITE $COMPONENTS"
        echo "deb $MIRROR $SUITE-updates $COMPONENTS"
        if [[ $DISTRO == debian ]]; then
            echo "deb $SECURITY_MIRROR $SUITE-security $COMPONENTS"
        else
            echo "deb $SECURITY_MIRROR $SUITE-security $COMPONENTS"
            echo "deb $MIRROR $SUITE-backports $COMPONENTS"
        fi
    } > "$ROOTFS/etc/apt/sources.list"
}

# Installs what exists among the given packages (names differ between
# Debian and Ubuntu releases; firmware packages come and go).
apt_install_available() {
    in_root "
        wanted=()
        for package in $*; do
            candidate=\$(apt-cache policy \"\$package\" 2> /dev/null | awk '/Candidate:/ { print \$2; exit }')
            if [[ -n \$candidate && \$candidate != '(none)' ]]; then
                wanted+=(\"\$package\")
            else
                echo \"note: \$package is not available, skipped\" >&2
            fi
        done
        apt-get install -y --no-install-recommends \"\${wanted[@]}\"
    "
}

distro_install_base() {
    apt_sources
    # No daemons may start inside the build root.
    printf '#!/bin/sh\nexit 101\n' > "$ROOTFS/usr/sbin/policy-rc.d"
    chmod 755 "$ROOTFS/usr/sbin/policy-rc.d"
    echo "$DISTRO_NAME" > "$ROOTFS/etc/debian_chroot"

    log 'Installing the base system'
    in_root '
        apt-get update
        echo "locales locales/default_environment_locale select en_US.UTF-8" | debconf-set-selections
        echo "locales locales/locales_to_be_generated multiselect en_US.UTF-8 UTF-8, ru_RU.UTF-8 UTF-8" | debconf-set-selections
        echo "keyboard-configuration keyboard-configuration/layoutcode string us" | debconf-set-selections
        apt-get install -y --no-install-recommends \
            systemd-sysv dbus udev kmod sudo locales tzdata keyboard-configuration console-setup \
            less nano bash-completion iproute2 iputils-ping pciutils usbutils \
            network-manager wpasupplicant bluez \
            initramfs-tools live-boot \
            grub-common grub2-common grub-pc-bin grub-efi-amd64-bin efibootmgr os-prober \
            dosfstools e2fsprogs rsync parted fdisk gdisk util-linux xorriso mtools \
            sddm xserver-xorg-core xserver-xorg-input-libinput \
            pipewire pipewire-pulse wireplumber \
            python3 fonts-dejavu-core
    '
    if [[ $DISTRO == debian ]]; then
        apt_install_available linux-image-amd64 firmware-linux-free firmware-linux-nonfree \
            firmware-misc-nonfree firmware-iwlwifi firmware-realtek firmware-atheros \
            firmware-brcm80211 firmware-sof-signed intel-microcode amd64-microcode
    else
        apt_install_available linux-generic linux-firmware intel-microcode amd64-microcode
    fi
    # Secure Boot for the installed system (signed shim and GRUB), the
    # installer's toolkit, a terminal and fonts.
    apt_install_available shim-signed grub-efi-amd64-signed \
        python3-pyside6.qtcore python3-pyside6.qtgui python3-pyside6.qtqml \
        python3-pyside6.qtquick python3-pyside6.qtnetwork \
        qml6-module-qtquick qml6-module-qtquick-window \
        foot fonts-noto-core xdg-user-dirs $EXTRA_PACKAGES
    in_root 'python3 -c "import PySide6.QtQuick" ' \
        || die 'PySide6 (QtQuick) is missing in the image: the installer cannot run.'
    in_root 'sed -i "s/^# *\(en_US.UTF-8 UTF-8\)/\1/" /etc/locale.gen 2> /dev/null || true
             locale-gen en_US.UTF-8 || true
             echo LANG=en_US.UTF-8 > /etc/default/locale'
}

distro_snapshot_packages() {
    in_root "apt-mark showmanual | sort > /var/tmp/newteller-manual-$1"
}

# Whatever the build added as a manual package becomes automatic and is
# removed with what only it needed; the runtime packages were manual before.
distro_remove_build_deps() {
    log 'Removing the build-only packages'
    in_root '
        apt-mark showmanual | sort > /var/tmp/newteller-manual-after
        comm -13 /var/tmp/newteller-manual-before /var/tmp/newteller-manual-after > /var/tmp/newteller-build-only
        if [[ -s /var/tmp/newteller-build-only ]]; then
            xargs apt-mark auto < /var/tmp/newteller-build-only > /dev/null
        fi
        apt-get autoremove --purge -y
        rm -f /var/tmp/newteller-*
    '
}

distro_configure() {
    :
}

kernel_version() {
    find "$ROOTFS/boot" -maxdepth 1 -name 'vmlinuz-*' -printf '%f\n' | sed 's/^vmlinuz-//' | sort -V | tail -n 1
}

distro_live_initramfs() {
    log 'Building the live initramfs (live-boot)'
    in_root 'update-initramfs -u -k all'
}

distro_clean() {
    in_root 'apt-get clean'
    rm -rf "$ROOTFS"/var/lib/apt/lists/*
    rm -f "$ROOTFS/usr/sbin/policy-rc.d" "$ROOTFS/etc/debian_chroot"
}

distro_pack() {
    local kver
    kver=$(kernel_version)
    [[ -n $kver ]] || die 'No kernel in the image.'
    install -d "$WORK/iso/live" "$WORK/iso/boot/grub"
    log 'Packing the live root filesystem (squashfs)'
    squash "$ROOTFS" "$WORK/iso/live/filesystem.squashfs"
    cp "$ROOTFS/boot/vmlinuz-$kver" "$WORK/iso/boot/vmlinuz"
    cp "$ROOTFS/boot/initrd.img-$kver" "$WORK/iso/boot/initrd.img"
}
