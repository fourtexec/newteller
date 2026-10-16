#!/usr/bin/env bash
# Shared by source-tree scripts and copied into each release archive.
newteller_detect_platform() {
    local ID VERSION_ID
    source /etc/os-release
    case "${ID:-}:${VERSION_ID:-}" in
        debian:13) NEWTELLER_TARGET=debian13-x86_64 ;;
        ubuntu:26.04) NEWTELLER_TARGET=ubuntu26.04-x86_64 ;;
        *)
            echo 'Supported systems: Debian 13 (trixie) and Ubuntu 26.04 LTS.' >&2
            return 1
            ;;
    esac
}
