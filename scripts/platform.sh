#!/usr/bin/env bash
# Shared by source-tree scripts and copied into each release archive.
#
# Sets NEWTELLER_TARGET (the archive name suffix, also written to BUILDINFO)
# and NEWTELLER_FAMILY (which package manager the scripts use):
#   debian13-x86_64, ubuntu26.04-x86_64   family debian (apt)
#   arch-x86_64                           family arch   (pacman; rolling, so
#                                          Arch-based systems share it)
#   fedora<N>-x86_64 (N >= 42)            family fedora (dnf)
newteller_detect_platform() {
    local ID VERSION_ID ID_LIKE
    source /etc/os-release
    NEWTELLER_FAMILY=''
    case "${ID:-}:${VERSION_ID:-}" in
        debian:13) NEWTELLER_TARGET=debian13-x86_64; NEWTELLER_FAMILY=debian ;;
        ubuntu:26.04) NEWTELLER_TARGET=ubuntu26.04-x86_64; NEWTELLER_FAMILY=debian ;;
        arch:*) NEWTELLER_TARGET=arch-x86_64; NEWTELLER_FAMILY=arch ;;
        fedora:*)
            if [[ ${VERSION_ID:-0} =~ ^[0-9]+$ ]] && (( VERSION_ID >= 42 )); then
                NEWTELLER_TARGET="fedora${VERSION_ID}-x86_64"
                NEWTELLER_FAMILY=fedora
            fi
            ;;
    esac
    # Arch derivatives (EndeavourOS, CachyOS, ...) use Arch's packages.
    if [[ -z $NEWTELLER_FAMILY && " ${ID_LIKE:-} " == *" arch "* ]]; then
        NEWTELLER_TARGET=arch-x86_64
        NEWTELLER_FAMILY=arch
    fi
    if [[ -z $NEWTELLER_FAMILY ]]; then
        echo 'Supported systems: Debian 13 (trixie), Ubuntu 26.04 LTS, Arch Linux and Fedora 42 or newer.' >&2
        return 1
    fi
}
