#!/bin/sh
#
# Guard: a test that reads virtual memory says what it assumes about
# collection.
#
# A test measures memory in one of two ways, and they want opposite
# things of the collector.
#
# The first reads a figure before some work and after it and takes the
# difference to be what the work cost. That subtraction means something
# only while nothing hands memory back in the middle of it. A collection
# falling between the two readings returns what an earlier page held,
# and the difference then describes the collection rather than the work
# -- it can come out smaller than the work, or negative.
#
# The second reads collection itself: whether a count changes what a run
# retains, whether an operator that asks for a collection gets one. Such
# a test must NOT disable collection, because disabling it is disabling
# the subject.
#
# So there is no single mechanism to require. What can be required is
# that the file SAYS which of the two it is, because the failure mode of
# getting it wrong is silence rather than noise: three files in this
# tree read memory by difference while automatic collection could not
# happen, each stating in its own words that memory "only grows". The
# one that asserted growth failed loudly when a collection began landing
# inside its measurements. The one that asserted NO growth went on
# passing, because a collection satisfies "did not grow" -- it measured
# nothing and said everything was well.
#
# A file satisfies this rule in either of two ways:
#
#   it turns automatic collection off, with -1 or -2 vmreclaim, which is
#   what PLRM C.3.5 provides for; or
#
#   it carries a line of the form
#
#       % COLLECTION: <why this reading is sound with collection running>
#
#   which is a claim its author has to make on purpose. The rule does
#   not read the reason -- it cannot -- but an author who writes one has
#   had to decide which of the two kinds of test they are writing, and
#   that decision is the whole of what was missing.
#
#   $1  path to the source tree root
set -u
src=${1:?usage: check-memory-declarations.sh <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_require_dir "$src/tests" "the test directory"

guard_workdir
fail=0

# A file reads virtual memory if it names one of the operators that
# report it. Found rather than listed: a test added tomorrow is held by
# this without anybody remembering to name it here.
reads='vmstatus|globalvmstatus'

# The declaration, either way round.
offs='(^|[^A-Za-z0-9])-[12][[:space:]]+vmreclaim'
says='^%[[:space:]]*COLLECTION:'

: > "$work/undeclared"
: > "$work/all"
for f in "$src"/tests/*.ps; do
    [ -f "$f" ] || continue
    grep -qE "$reads" "$f" || continue
    echo "${f##*/}" >> "$work/all"
    if grep -qE "$offs" "$f" || grep -qE "$says" "$f"; then
        continue
    fi
    echo "${f##*/}" >> "$work/undeclared"
done

nall=$(grep -c . "$work/all" || true)
if [ "${nall:-0}" -lt 5 ]; then
    echo "FAILURES: only $nall test files were found to read virtual memory,"
    echo "      which is fewer than this tree has; the rule is reading the"
    echo "      wrong place and a tree in disorder would look like this one"
    exit 1
fi

if [ -s "$work/undeclared" ]; then
    echo "FAILURES: these tests read virtual memory and say nothing about"
    echo "      what they assume of the collector:"
    sed 's/^/      /' "$work/undeclared"
    echo "      Either turn automatic collection off (-2 vmreclaim, PLRM"
    echo "      C.3.5), or write a line saying why the reading is sound"
    echo "      with it running:"
    echo "          % COLLECTION: <reason>"
    fail=1
fi

# Put the rule to a file of each kind, so that a rule which finds nothing
# in a sound tree is told apart from one which cannot find anything.
printf '%s\n' '/used { vmstatus pop exch pop } bind def' > "$work/bare.ps"
if ! grep -qE "$reads" "$work/bare.ps" ||
   grep -qE "$offs" "$work/bare.ps" || grep -qE "$says" "$work/bare.ps"; then
    echo "FAILURES: the rule does not recognise a file that reads memory and"
    echo "      declares nothing, so a tree carrying one reads the same as a"
    echo "      tree that does not"
    fail=1
fi
printf '%s\n' '-2 vmreclaim' '/used { vmstatus pop exch pop } bind def' \
    > "$work/off.ps"
printf '%s\n' '% COLLECTION: this one measures the collector itself' \
    '/used { vmstatus pop exch pop } bind def' > "$work/said.ps"
for w in off said; do
    if ! { grep -qE "$offs" "$work/$w.ps" || grep -qE "$says" "$work/$w.ps"; }; then
        echo "FAILURES: the rule does not accept a file that declares itself"
        echo "      the '$w' way, so it refuses what it asks for"
        fail=1
    fi
done

[ "$fail" -eq 0 ] && echo "SUCCESS ($nall tests read virtual memory, every one of them declared)"
exit $fail
