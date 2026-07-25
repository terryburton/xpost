#!/bin/sh
# Meson test wrapper: run a script through the interpreter with graphics
# loading skipped (--no-graphics), exercising the no-graphics start procedure
# and lockdown path. Passes iff the script reports SUCCESS.
#   $1  path to the built xpost binary
#   $2  path to the test .ps
set -u
xpost=$1
script=$2
# --no-graphics selects the no-graphics start procedure; lets the
# script exercise the interpreter's own file operations
out=$("$xpost" --no-graphics -q -d null "$script" </dev/null 2>&1)
printf '%s\n' "$out"
printf '%s\n' "$out" | grep -q '^SUCCESS$'
