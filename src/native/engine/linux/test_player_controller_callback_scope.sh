#!/bin/sh
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
binary=$(mktemp "${TMPDIR:-/tmp}/x4-controller-callback.XXXXXX")
trap 'rm -f "$binary"' EXIT HUP INT TERM
${CC:-cc} -UNDEBUG -std=c11 -O2 -Wall -Wextra -Werror -pthread "$script_dir/test_player_controller_callback_scope.c" -o "$binary"
"$binary"
