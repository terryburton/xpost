#!/bin/sh
# Meson test wrapper: no repeatable operation may spend global memory each
# time it runs.
#
# Global memory outlives a job's save and restore, so an allocation a
# repeated operation keeps naming is held for as long as the thing naming
# it, which for the machinery below is the life of the context. It is spent
# for the life of the context. That is correct for machinery built once at
# load and for what must outlive a job's save/restore; it is a leak for
# anything a job allocates per operation, and it shows up only in a context
# that serves many jobs -- an embedder's worker -- never in a single run.
#
# The verdict is reached here rather than in the workload: an exercise that
# wraps save around its work reverts local memory, so a total accumulated in
# the interpreter could be rolled back before it could be reported.
#
#   $1  path to the built xpost binary
#   $2  path to the PostScript workload
set -u
xpost=$1
script=$2
. "$(dirname "$0")/verdict.sh"

pdf=$(mktemp)
trap 'rm -f "$pdf"' EXIT INT TERM

# -o gives the vector writers somewhere to put their documents, so stdout
# carries only the report
out=$("$xpost" -q --no-sandbox -d pdfwrite -o "$pdf" "$script" </dev/null 2>&1)
status=$?

verdict_run "$status" "$out" "the interpreter" || exit 1

measured=$(echo "$out" | grep -c 'B/iteration')
[ "$measured" -ge 15 ] || {
    echo "FAIL: only $measured exercises reported -- too few to be a measurement"
    exit 1
}

# the control allocates in global memory and has to be seen, or the
# measurement is blind and every other line means nothing. It allocates far
# less than a collection's threshold over the run, so nothing reclaims it
# before it is measured
echo "$out" | grep -q '^  OVER CONTROL:' || {
    echo "$out" | grep 'CONTROL' | sed -n 's/^/  /p'
    echo "FAIL: the control's global allocation was not seen -- the measurement is blind"
    exit 1
}

bad=$(echo "$out" | grep -E '^  (OVER|DIDNOTRUN) ' | grep -v 'CONTROL')
[ -z "$bad" ] || {
    echo "$bad" | sed -n 's/^/  /p'
    echo "FAIL: an operation spends global memory it goes on naming"
    exit 1
}

echo "  operations measured: $((measured - 1)), control seen"
echo "SUCCESS"
