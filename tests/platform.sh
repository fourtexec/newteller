#!/usr/bin/env bash
# Exercise distribution detection without changing the host or installing packages.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$root/scripts/platform.sh"
# Intercept only the OS metadata read inside the detector.
source() {
    [[ $1 == /etc/os-release ]]
    ID=$test_id
    VERSION_ID=$test_version
    ID_LIKE=$test_like
}
for spec in 'debian 13 - debian13-x86_64 debian' 'ubuntu 26.04 - ubuntu26.04-x86_64 debian' \
            'arch - - arch-x86_64 arch' 'endeavouros - arch arch-x86_64 arch' \
            'fedora 42 - fedora42-x86_64 fedora' 'fedora 45 - fedora45-x86_64 fedora'; do
    read -r test_id test_version test_like expected family <<< "$spec"
    [[ $test_version == - ]] && test_version=''
    [[ $test_like == - ]] && test_like=''
    newteller_detect_platform
    [[ $NEWTELLER_TARGET == "$expected" && $NEWTELLER_FAMILY == "$family" ]]
done
for spec in 'debian 12' 'ubuntu 24.04' 'ubuntu 26.10' 'fedora 41' 'opensuse-tumbleweed 20261103'; do
    read -r test_id test_version <<< "$spec"
    test_like=''
    if newteller_detect_platform 2>/dev/null; then
        echo "Unexpectedly accepted $spec" >&2
        exit 1
    fi
done
echo 'Platform detection tests passed.'
