#!/bin/sh
# Meson test wrapper for the differential renderer (render-differ.sh),
# put to the one comparison whose answer is known before it runs: this
# tree against itself, which has to render alike.
#
# Two things are being held. The first is the interpreter's: rendering
# one page through every deterministic device twice has to give the same
# bytes twice, which is what the golden manifest assumes and never
# checks -- it compares one run against a record, so a device that
# rendered differently on alternate runs would pass half the time and
# be read as a flapping test rather than as what it is.
#
# The second is the instrument's. A harness nothing runs is a harness
# that stops working quietly, and this one exists because three separate
# pieces of work rebuilt it from nothing, each losing what the last had
# learned. Held to a case with a known answer, on every quick run, it
# either works on the day it is next needed or says so long before.
#
# What it cannot decide it does not pass: the harness skips (77) where
# it has no interpreter or no setsid to give a render a process group,
# and that skip is passed through rather than turned into a success.
#
#   $1   path to the built xpost binary
#   $2   path to the source tree (the side, twice)
#   $3.. the programs to render
set -u
xpost=${1:?usage: run-render-differ-test.sh <xpost> <srcroot> <program>...}
root=${2:?usage: run-render-differ-test.sh <xpost> <srcroot> <program>...}
shift 2
[ $# -ge 1 ] || { echo "FAILURES: no program named to render"; exit 1; }
. "$(dirname "$0")/verdict.sh"

out=$(XPOST="$xpost" RENDER_JOBS="${RENDER_JOBS:-4}" \
      "$(dirname "$0")/render-differ.sh" --gate "$root" "$root" "$@" 2>&1)
status=$?
printf '%s\n' "$out"

# A skip is not a pass and is not a failure: it is the run saying it
# never started.
[ "$status" -eq 77 ] && exit 77

verdict_run "$status" "$out" "the differential render" || exit 1
echo "SUCCESS"
exit 0
