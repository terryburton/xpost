#!/bin/sh
# Meson test wrapper: a job's work must leave local memory where it found it.
#
# Local memory is what save and restore reach. A job that wraps save around
# its work has said that none of what it allocated is to survive, so the
# memory it took must be available again to the job after it. Where it is
# not, a context serving one job per request grows by that amount for every
# request -- and it grows in the bank a restore was supposed to have emptied,
# which is the last place anyone looks.
#
# The workload cannot read this as a difference: the used figure vmstatus
# reports never falls, so a before-and-after comparison counts what was
# allocated and not what came back. It reads reuse instead -- a collection
# each time round, and the question of whether the next iteration had to be
# given anything new.
#
# The verdict is reached here rather than in the workload, because an
# exercise wrapping save around its work reverts local memory and would take
# a total kept in the interpreter with it.
#
#   $1  path to the built xpost binary
#   $2  path to the PostScript workload
set -u
xpost=$1
script=$2
. "$(dirname "$0")/verdict.sh"

out=$("$xpost" -q --no-sandbox -d null "$script" </dev/null 2>&1)
status=$?

verdict_run "$status" "$out" "the interpreter" || exit 1

measured=$(echo "$out" | grep -c 'B/iteration')
[ "$measured" -ge 15 ] || {
    echo "FAIL: only $measured exercises reported -- too few to be a measurement"
    exit 1
}

# The control keeps one array of a thousand elements per iteration, so the
# instrument is held not merely to reporting something but to reporting the
# size of what was kept. An object is eight bytes on the primary build and
# sixteen on the wide one, so the floor is half the narrower figure; the
# measured number is printed either way, so a change in it is visible rather
# than absorbed.
control=$(echo "$out" | sed -n 's/^  [A-Za-z]* CONTROL: \([0-9]*\) B\/iteration$/\1/p')
[ -n "$control" ] || {
    echo "$out" | grep 'CONTROL' | sed -n 's/^/  /p'
    echo "FAIL: the control did not report a figure -- the measurement is blind"
    exit 1
}
if [ "$control" -lt 4000 ]; then
    echo "  control reported $control B/iteration"
    echo "FAIL: the control keeps a thousand-element array every iteration and"
    echo "      the measurement did not see it, so nothing else it reports"
    echo "      means anything"
    exit 1
fi

bad=$(echo "$out" | grep -E '^  (OVER|DIDNOTRUN) ' | grep -v 'CONTROL')
[ -z "$bad" ] || {
    echo "$bad" | sed -n 's/^/  /p'
    echo "FAIL: a job's work did not give local memory back when it restored"
    exit 1
}

echo "  operations measured: $((measured - 1)), control saw $control B/iteration"
echo "SUCCESS"
