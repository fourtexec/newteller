#!/usr/bin/env bash
# Shared pieces of the NewTeller ISO builders (sourced by iso/build-iso.sh).
#
# The pipeline is the same for every distribution:
#   1. bootstrap a root filesystem          (distro_bootstrap, per distro)
#   2. install the base system and NewTeller (distro_install_base, build_newteller)
#   3. turn it into a live system            (configure_live, distro_live_initramfs)
#   4. pack it                               (distro_pack: squashfs + kernel + initrd)
#   5. make a hybrid BIOS/UEFI ISO           (make_iso: grub-mkrescue inside the root)
#
# Everything heavy runs inside the new root (chroot), so the host only needs
# the bootstrap tool, mount/chroot and squashfs-tools.

log() { printf '\033[1m==> %s\033[0m\n' "$*" >&2; }
warn() { printf '\033[33mwarning:\033[0m %s\n' "$*" >&2; }
die() { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }

need() {
    local tool
    for tool in "$@"; do
        command -v "$tool" > /dev/null 2>&1 || die "$tool is required on the build host"
    done
}

# --- chroot -------------------------------------------------------------------

CHROOT_MOUNTS=()

chroot_mount() {
    local root=$1 target
    for target in proc sys dev dev/pts run; do
        mkdir -p "$root/$target"
    done
    mount -t proc proc "$root/proc" && CHROOT_MOUNTS+=("$root/proc")
    mount --rbind /sys "$root/sys" && mount --make-rslave "$root/sys" && CHROOT_MOUNTS+=("$root/sys")
    mount --rbind /dev "$root/dev" && mount --make-rslave "$root/dev" && CHROOT_MOUNTS+=("$root/dev")
    mount -t tmpfs tmpfs "$root/run" && CHROOT_MOUNTS+=("$root/run")
    # Name resolution for the package manager; the original is put back.
    if [[ -e $root/etc/resolv.conf || -L $root/etc/resolv.conf ]]; then
        mv -f "$root/etc/resolv.conf" "$root/etc/resolv.conf.newteller-orig"
    else
        : > "$root/etc/resolv.conf.newteller-none"
    fi
    cp -L /etc/resolv.conf "$root/etc/resolv.conf"
}

chroot_umount() {
    local root=$1 index
    for ((index = ${#CHROOT_MOUNTS[@]} - 1; index >= 0; index--)); do
        umount -R "${CHROOT_MOUNTS[index]}" 2> /dev/null || umount -lR "${CHROOT_MOUNTS[index]}" 2> /dev/null || true
    done
    CHROOT_MOUNTS=()
    if [[ -e $root/etc/resolv.conf.newteller-orig || -L $root/etc/resolv.conf.newteller-orig ]]; then
        rm -f "$root/etc/resolv.conf"
        mv -f "$root/etc/resolv.conf.newteller-orig" "$root/etc/resolv.conf"
    elif [[ -e $root/etc/resolv.conf.newteller-none ]]; then
        rm -f "$root/etc/resolv.conf" "$root/etc/resolv.conf.newteller-none"
    fi
}

# Runs a command (a bash script string) inside the root.
in_root() {
    chroot "$ROOTFS" /usr/bin/env -i \
        HOME=/root TERM="${TERM:-xterm}" LANG=C.UTF-8 \
        PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
        DEBIAN_FRONTEND=noninteractive \
        /bin/bash -euo pipefail -c "$1"
}

# --- NewTeller ----------------------------------------------------------------

# Copies the source tree into the root, installs the build dependencies with
# the project's own script, builds and installs to /usr, then removes what
# was only needed for building (distro_mark_build_deps / distro_remove_build_deps).
build_newteller() {
    log "Building NewTeller from $REPO inside the image"
    rm -rf "$ROOTFS/usr/src/newteller"
    mkdir -p "$ROOTFS/usr/src/newteller"
    tar -C "$REPO" --exclude=./build --exclude=./dist --exclude=./.git \
        --exclude=./iso/work --exclude=./iso/out -cf - . | tar -C "$ROOTFS/usr/src/newteller" -xf -

    # The runtime packages stay; whatever the build adds on top goes again.
    in_root 'bash /usr/src/newteller/scripts/install-runtime-deps.sh'
    distro_snapshot_packages before
    in_root 'bash /usr/src/newteller/scripts/install-build-deps.sh'
    in_root '
        cd /usr/src/newteller
        cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
            -DNEWTELLER_WITH_RUST=AUTO
        cmake --build build --parallel "$(nproc)"
        cmake --install build --strip
        command -v newteller > /dev/null
        command -v newteller-compositor > /dev/null
    '
    distro_remove_build_deps
    rm -rf "$ROOTFS/usr/src/newteller" "$ROOTFS/root/.cargo" "$ROOTFS/root/.rustup"
}

# --- installer and live session -----------------------------------------------

install_installer() {
    log 'Installing the NewTeller installer'
    install -d "$ROOTFS/usr/lib/newteller-installer"
    install -m 755 "$REPO/iso/installer/newteller-installer" "$ROOTFS/usr/lib/newteller-installer/newteller-installer"
    install -m 644 "$REPO/iso/installer/Installer.qml" "$ROOTFS/usr/lib/newteller-installer/Installer.qml"
    install -m 755 "$REPO/iso/installer/newteller-install-backend" \
        "$ROOTFS/usr/lib/newteller-installer/newteller-install-backend"
    ln -sf /usr/lib/newteller-installer/newteller-installer "$ROOTFS/usr/bin/newteller-installer"
    install -m 644 "$REPO/iso/installer/newteller-installer.desktop" \
        "$ROOTFS/usr/share/applications/newteller-installer.desktop"
    # Which distribution this image is, for the installer's texts and steps.
    printf 'NEWTELLER_DISTRO=%s\nNEWTELLER_DISTRO_NAME=%q\n' "$DISTRO" "$DISTRO_NAME" \
        > "$ROOTFS/usr/lib/newteller-installer/image.conf"
}

# The live session: user "live" without a password, logged in automatically
# into NewTeller, allowed to administrate without a prompt, with the
# installer started on login. The installer's backend undoes all of this on
# the installed system.
configure_live() {
    log 'Configuring the live session'
    local admin_group=$1
    in_root "
        id live > /dev/null 2>&1 || useradd -m -s /bin/bash -c 'Live session' live
        for group in $admin_group video input audio render netdev plugdev lpadmin bluetooth; do
            getent group \"\$group\" > /dev/null && usermod -aG \"\$group\" live
        done
        passwd -d live
    "
    install -d -m 750 "$ROOTFS/etc/sudoers.d"
    printf 'live ALL=(ALL:ALL) NOPASSWD: ALL\n' > "$ROOTFS/etc/sudoers.d/90-newteller-live"
    chmod 440 "$ROOTFS/etc/sudoers.d/90-newteller-live"

    install -d "$ROOTFS/etc/polkit-1/rules.d"
    cat > "$ROOTFS/etc/polkit-1/rules.d/49-newteller-live.rules" <<'EOF'
// Live session only (removed by the installer): the live user may do
// anything without a password, like on every live medium.
polkit.addRule(function (action, subject) {
    if (subject.user == "live")
        return polkit.Result.YES;
});
EOF

    install -d "$ROOTFS/etc/sddm.conf.d"
    cat > "$ROOTFS/etc/sddm.conf.d/90-newteller-live.conf" <<'EOF'
# Live session only (removed by the installer).
[Autologin]
User=live
Session=newteller.desktop
Relogin=false
EOF
    cat > "$ROOTFS/etc/sddm.conf.d/10-newteller.conf" <<'EOF'
# NewTeller is the only session on this system.
[General]
DisplayServer=x11
[Theme]
Current=
EOF

    local home="$ROOTFS/home/live"
    install -d "$home/.config/autostart" "$home/Desktop" "$home/.config/newteller"
    cp "$ROOTFS/usr/share/applications/newteller-installer.desktop" "$home/.config/autostart/"
    cp "$ROOTFS/usr/share/applications/newteller-installer.desktop" "$home/Desktop/"
    chmod +x "$home/Desktop/newteller-installer.desktop"
    in_root 'chown -R live: /home/live'

    # Debian's sddm unit only starts when it is the default display manager.
    if [[ -d $ROOTFS/etc/X11 ]]; then
        echo /usr/bin/sddm > "$ROOTFS/etc/X11/default-display-manager"
    fi

    echo newteller-live > "$ROOTFS/etc/hostname"
    printf '127.0.0.1 localhost\n127.0.1.1 newteller-live\n::1 localhost ip6-localhost ip6-loopback\n' \
        > "$ROOTFS/etc/hosts"

    in_root '
        systemctl enable NetworkManager.service
        systemctl enable sddm.service
        systemctl enable bluetooth.service 2> /dev/null || true
        systemctl set-default graphical.target
    '
}

# Keyboard and GRUB defaults every installed system starts from.
configure_defaults() {
    [[ -e $ROOTFS/etc/default/keyboard ]] || cat > "$ROOTFS/etc/default/keyboard" <<'EOF'
XKBMODEL="pc105"
XKBLAYOUT="us"
XKBVARIANT=""
XKBOPTIONS=""
BACKSPACE="guess"
EOF
    install -d "$ROOTFS/etc/default"
    cat > "$ROOTFS/etc/default/grub" <<EOF
GRUB_DEFAULT=0
GRUB_TIMEOUT=3
GRUB_TIMEOUT_STYLE=menu
GRUB_DISTRIBUTOR="$DISTRO_NAME (NewTeller)"
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash"
GRUB_CMDLINE_LINUX=""
GRUB_DISABLE_OS_PROBER=false
GRUB_ENABLE_BLSCFG=false
EOF
}

# Empties caches, logs and identity files before the image is packed.
clean_root() {
    log 'Cleaning the image'
    rm -rf "$ROOTFS"/tmp/* "$ROOTFS"/var/tmp/* "$ROOTFS"/var/log/journal/* \
        "$ROOTFS"/var/cache/man/* "$ROOTFS"/root/.cache "$ROOTFS"/root/.bash_history
    find "$ROOTFS/var/log" -type f -exec truncate -s 0 {} + 2> /dev/null || true
    # A fresh machine id on every boot of the live system and on install.
    : > "$ROOTFS/etc/machine-id"
    rm -f "$ROOTFS/var/lib/dbus/machine-id"
    rm -f "$ROOTFS"/etc/ssh/ssh_host_*
}

# --- ISO ----------------------------------------------------------------------

# Writes the boot menu shared by every image: start (and install), the same
# with safe graphics, and the way back to the disk or the firmware.
write_grub_cfg() {
    local file=$1 cmdline=$2
    cat > "$file" <<EOF
set default=0
set timeout=8
insmod all_video
insmod gfxterm
insmod part_gpt
insmod part_msdos
if loadfont unicode; then
    terminal_output gfxterm
fi
set menu_color_normal=light-gray/black
set menu_color_highlight=white/dark-gray

menuentry "Try or install NewTeller ($DISTRO_NAME)" {
    linux /boot/vmlinuz $cmdline quiet splash
    initrd /boot/initrd.img
}
menuentry "NewTeller ($DISTRO_NAME) - safe graphics" {
    linux /boot/vmlinuz $cmdline nomodeset
    initrd /boot/initrd.img
}
if [ "\$grub_platform" = "efi" ]; then
    menuentry "UEFI firmware settings" {
        fwsetup
    }
fi
if [ "\$grub_platform" = "pc" ]; then
    menuentry "Boot from the first disk" {
        set root=(hd1)
        chainloader +1
    }
fi
EOF
}

# Packs ISO_TREE into a hybrid ISO (BIOS + UEFI, bootable from DVD and USB)
# with the root's own grub-mkrescue, so the host needs no GRUB at all.
make_iso() {
    local label=$1 output=$2 mkrescue=grub-mkrescue
    [[ -x $ROOTFS/usr/bin/grub2-mkrescue ]] && mkrescue=grub2-mkrescue
    log "Writing $output"
    install -d "$ROOTFS/mnt/newteller-iso"
    mount --bind "$WORK" "$ROOTFS/mnt/newteller-iso"
    CHROOT_MOUNTS+=("$ROOTFS/mnt/newteller-iso")
    in_root "$mkrescue --compress=xz -o /mnt/newteller-iso/image.iso /mnt/newteller-iso/iso -- -volid '$label'"
    umount "$ROOTFS/mnt/newteller-iso"
    mkdir -p "$(dirname -- "$output")"
    mv -f "$WORK/image.iso" "$output"
    (cd "$(dirname -- "$output")" && sha256sum "$(basename -- "$output")" > "$(basename -- "$output").sha256")
}

# The root must not have anything mounted inside (chroot_umount first).
squash() {
    local source=$1 target=$2
    shift 2
    mksquashfs "$source" "$target" -noappend -comp xz -Xbcj x86 -b 1M "$@"
}
