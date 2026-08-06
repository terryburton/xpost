#!/bin/sh
# Meson test wrapper: run the relocation-stress program with every memory
# grow forced to relocate.
#
# The memory file grows by realloc, which may extend in place, so a
# pointer derived before an allocating call usually still works and a
# stale one is found only by chance. XPOST_GROW_MOVES makes every grow
# copy to a fresh block and release the old one, so any pointer held
# across an allocation reads freed memory: the defect becomes a crash
# here, and an immediate report under a sanitizer build.
#
#   $1  path to the built xpost binary
#   $2  path to reloc_stress_test.ps
#   $3  device to render through
set -u
xpost=$1
script=$2
dev=$3
. "$(dirname "$0")/verdict.sh"

if "$xpost" -h 2>/dev/null | grep -q -- '--no-sandbox'; then
    ns='--no-sandbox'
else
    ns=''
fi

work=$(mktemp -d)
out=$(XPOST_GROW_MOVES=1 "$xpost" -q $ns -d "$dev" -o "$work/out" "$script" </dev/null 2>&1)
status=$?
rm -rf "$work"
printf '%s\n' "$out"

if [ "$status" -ne 0 ]; then
    echo "FAILURES: the interpreter exited with status $status under forced relocation"
    exit 1
fi
verdict_ok "$out" "the suite"
