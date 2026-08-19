#!/bin/sh
# Meson test wrapper: hold the xpost_view program to its usage answer.
#
# The viewer needs a window system to view anything, and the harness has
# none to give it, so what is held here is the one answer it owes
# without a display: asked for help it prints its usage and succeeds.
# Registered only in the builds that carry the viewer at all.
#
#   $1  path to the built xpost_view binary
set -u
view=$1
. "$(dirname "$0")/verdict.sh"

out=$("$view" --help 2>&1)
if ! verdict_run "$?" "$out" "xpost_view --help"; then
    printf '%s\n' "$out"
    exit 1
fi
if ! printf '%s\n' "$out" | grep -q '^Usage:'; then
    printf '%s\n' "$out"
    echo "FAILURES: xpost_view --help did not print its usage"
    exit 1
fi
echo "SUCCESS"
exit 0
