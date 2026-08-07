#!/bin/sh
# Meson test wrapper: run the embedding suite with the collector set to
# collect constantly.
#
# The collector runs rarely under ordinary settings, so anything the
# interpreter holds outside the root set survives by luck rather than by
# being reachable, and the gap only shows when a collection lands between
# the moment the last reference goes and the moment the holder uses it.
# Turning the threshold down makes that landing happen every time: it is
# how the run-input file was found to be held where the collector could
# not see it.
#
#   $1  path to the run-status test executable
#   $2  path to the context-reuse test executable
set -u
. "$(dirname "$0")/verdict.sh"

fail=0
ran=0
for exe in "$@"; do
    name=$(basename "$exe")
    out=$(XPOST_GC_THRESHOLD=4000 "$exe" 2>&1)
    status=$?
    verdict_run "$status" "$out" "$name under constant collection" || fail=1
    ran=$((ran + 1))
done

# A loop over nothing takes no verdict and leaves the tally at zero,
# which is what a run in which everything passed leaves too. The two
# executables named above are what this wrapper is for, so it is held to
# having been given them.
if [ "$ran" -lt 2 ]; then
    echo "FAILURES: $ran executable(s) were run under the collector; this"
    echo "      wrapper is given the two named in its own usage"
    exit 1
fi

[ "$fail" = 0 ] || { echo "FAILURES: the above did not survive the collector"; exit 1; }
echo "SUCCESS ($ran executables under constant collection)"
