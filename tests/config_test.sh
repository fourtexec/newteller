#!/usr/bin/env bash
# Builds and runs the unit test of the compositor's settings parser.
# No wlroots or Qt needed: src/session/config.c is plain C.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
binary=$(mktemp "${TMPDIR:-/tmp}/newteller-config-test.XXXXXX")
trap 'rm -f "$binary"' EXIT

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g ${CONFIG_TEST_CFLAGS:-} \
    -I"$root/src/session" "$root/tests/config_test.c" \
    "$root/src/session/config.c" -lm -o "$binary"
"$binary"
