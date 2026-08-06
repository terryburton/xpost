#!/bin/sh
# Meson test wrapper: run a script through the interpreter with graphics
# loading skipped (--no-graphics), exercising the no-graphics start procedure
# and lockdown path. Passes iff the script reports SUCCESS.
#   $1  path to the built xpost binary
#   $2  path to the test .ps
set -u
xpost=$1
script=$2
. "$(dirname "$0")/verdict.sh"
# --no-graphics selects the no-graphics start procedure; lets the
# script exercise the interpreter's own file operations
out=$("$xpost" --no-graphics --no-sandbox -q -d null "$script" </dev/null 2>&1)
status=$?
if [ "$status" -ne 0 ]; then
    echo "FAILURES: the interpreter exited with status $status"
    exit 1
fi
printf '%s\n' "$out"
verdict_ok "$out" "the script"
