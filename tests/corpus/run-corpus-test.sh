#!/bin/sh
# Meson wrapper for the differential corpus. It runs evaluate.sh over whatever
# corpora have been fetched into place and reports:
#   - SKIP (exit 77) when there is nothing to run: no corpus is present, the
#     comparison tools it needs (Ghostscript, ImageMagick compare) are absent,
#     or a corpus is present without the prelude its programs need and that
#     prelude is one fetch.sh populates rather than one the tree carries. The
#     corpus is thus never a build-time dependency -- populate it with fetch.sh
#     to make this test do its work.
#   - FAIL (exit 1) when xpost crashes or hangs on a corpus program, when the
#     evaluation did not reach every program it named, or when what drew no
#     page is not what the corpus declares draws none. A rendering
#     difference is a lead, not a verdict (see README.md), so it is reported
#     but does not fail the test; a signal death or a timeout is an
#     unambiguous regression and does, and so is a corpus only part of which
#     was evaluated -- a gate that reports success over work it did not do
#     says nothing about the work it did not do. Reaching a program is not
#     the same as rendering one, and the count of programs evaluated cannot
#     tell the two apart: a run in which every program failed reaches all of
#     them. So the pages that were never drawn are held to a declared set as
#     well, and a run that compared nothing has nothing declaring it may.
#   - PASS (exit 0) otherwise, with the per-page differences left in the log for
#     inspection (meson test corpus -v, or meson-logs/testlog.txt).
#   $1  path to the built xpost binary (optional; evaluate.sh finds one itself)
#   $2  one corpus to evaluate (optional; all of them by default). Naming one
#       per test lets them run concurrently rather than end to end.
set -u
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
[ "${1:-}" ] && XPOST=$1 && export XPOST
corpus=${2:-}

command -v gs >/dev/null 2>&1 || {
    echo "corpus: Ghostscript not found -- skipping"; exit 77; }
command -v compare >/dev/null 2>&1 || {
    echo "corpus: ImageMagick 'compare' not found -- skipping"; exit 77; }

have=0
if [ -n "$corpus" ]; then set -- "$here/$corpus/"; else set -- "$here"/*/; fi
for d in "$@"; do
    for p in "$d"*.ps "$d"*.eps; do
        [ -f "$p" ] && { have=1; break 2; }
    done
done
[ "$have" = 1 ] || {
    echo "corpus: ${corpus:-no corpus} not present -- run tests/corpus/fetch.sh, then re-run. Skipping."
    exit 77; }

out=$("$here/evaluate.sh" $corpus 2>&1)
printf '%s\n' "$out"
# The evaluator skips the lot when a tool it needs is not there, saying so
# and exiting zero. Taken as a pass, that has a run which did nothing tell
# the same story as a run which did everything.
printf '%s\n' "$out" | grep -q 'skipping all' && {
    echo "corpus: the evaluator found nothing to run with -- skipping"; exit 77; }
printf '%s\n' "$out" | grep -Eq 'XPOST (CRASHED|TIMED OUT)' && {
    echo "corpus: xpost crashed or hung on a program -- see above"; exit 1; }
printf '%s\n' "$out" | grep -q 'NOT EVALUATED' && {
    echo "corpus: part of the corpus was never evaluated -- see above"; exit 1; }
printf '%s\n' "$out" | grep -q 'NO-PAGE SET DIFFERS' && {
    echo "corpus: what drew no page is not what the corpus declares -- see above"; exit 1; }
# A corpus whose prelude is populated rather than committed, and has not
# been populated, is passed over by the evaluator: with nothing to
# prepend there is nothing to compare. That is a corpus not fetched, so
# it reports as a skip here. Taken as a pass it would be the very thing
# the page count guards against one level down -- a run that compared
# nothing telling the same story as a run that compared everything.
printf '%s\n' "$out" | grep -q 'programs evaluated' || {
    printf '%s\n' "$out" | grep -q 'prelude absent -- skipped' && {
        echo "corpus: ${corpus:-a corpus} has no prelude to run with -- skipping"; exit 77; }; }
exit 0
