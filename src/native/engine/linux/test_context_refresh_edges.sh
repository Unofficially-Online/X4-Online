#!/bin/sh
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_binary=$(mktemp "${TMPDIR:-/tmp}/x4-context-edges.XXXXXX")
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
${CC:-cc} -UNDEBUG -std=c11 -Wall -Wextra -Werror -O2 -pthread "$script_dir/test_context_refresh_edges.c" -o "$test_binary"
"$test_binary"
