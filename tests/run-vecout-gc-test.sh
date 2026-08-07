#!/bin/sh
# Meson test wrapper: a garbage collection between two pieces of vector
# output must leave the device's open output file alone.
#
# The interpreter must survive the run and the file must be complete: a
# reclaimed file object is read back as whatever now occupies its slot,
# which either faults or writes the output somewhere else entirely.
#
#   $1  path to the built xpost binary
#   $2  path to the PostScript workload
set -u
xpost=$1
script=$2
. "$(dirname "$0")/verdict.sh"

pdf=$(mktemp)
trap 'rm -f "$pdf"' EXIT

out=$("$xpost" -q --no-sandbox -d pdfwrite -o "$pdf" "$script" </dev/null 2>&1)
status=$?

verdict_run "$status" "$out" "the interpreter" || exit 1

head -c 8 "$pdf" | grep -q '%PDF-1' || {
    echo "FAIL: no PDF header -- the output file was not written through"
    exit 1
}
grep -q '/Type[ ]*/Page' "$pdf" || { echo "FAIL: no page object"; exit 1; }
tail -c 16 "$pdf" | grep -q '%%EOF' || { echo "FAIL: no EOF trailer"; exit 1; }

echo "SUCCESS"
