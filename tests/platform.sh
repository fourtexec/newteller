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
}
for spec in 'debian 13 debian13-x86_64' 'ubuntu 26.04 ubuntu26.04-x86_64'; do
    read -r test_id test_version expected <<< "$spec"
    newteller_detect_platform
    [[ $NEWTELLER_TARGET == "$expected" ]]
done
for spec in 'debian 12' 'ubuntu 24.04' 'ubuntu 26.10' 'fedora 44'; do
    read -r test_id test_version <<< "$spec"
    if newteller_detect_platform; then
        echo "Unexpectedly accepted $spec" >&2
        exit 1
    fi
done
echo 'Platform detection tests passed.'
