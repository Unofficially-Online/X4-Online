#!/bin/sh
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
binary=$(mktemp "${TMPDIR:-/tmp}/x4-recurring-boundary.XXXXXX")
trap 'rm -f "$binary"' EXIT HUP INT TERM
${CC:-cc} -UNDEBUG -std=gnu11 -O2 -Wall -Wextra -Werror -pthread "$script_dir/test_gate_player_control_recurring.c" -o "$binary"
timeout 30s "$binary"
