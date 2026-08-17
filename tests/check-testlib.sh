#!/bin/sh
#
# The shared test framework, held to the tree.
#
# tests/testlib.ps is the one place a case that may raise is run and the one
# place that puts the world back afterwards. That is worth nothing if a
# suite can quietly keep its own copy, because a copy is what let a false
# pass sit unnoticed: a refused case left a singular transform in force, the
# five cases after it raised undefinedresult, and that suite's own harness
# read them as correctly refusing the thing they were actually testing.
#
# So two things are checked, and the second is the one that matters:
#
#   the register of suites that have not moved yet is held to the tree in
#   BOTH directions, so the list can only shrink, and only by conversion
#
#   a suite that ASKS for the framework and then defines one of its names
#   anyway is refused, because that shadows the shared runner with a local
#   one and nothing else would say so
#
#   $1  path to the source tree root
set -u
src=${1:?usage: check-testlib.sh <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_require_file "$src/tests/testlib.ps" "the shared test framework"
guard_require_file "$src/tests/testlib-facts" "the framework register"
guard_require_file "$src/tests/run-ps-test.sh" "the test harness"

guard_workdir
trap 'rm -rf "$work"' EXIT

fail=0

# ---- the names the framework defines, read from it rather than listed
sed -n 's|^/\([A-Za-z][A-Za-z0-9]*\) {.*|\1|p' "$src/tests/testlib.ps" \
    | LC_ALL=C sort -u > "$work/shared"
if [ "$(grep -c . "$work/shared")" -lt 4 ]; then
    echo "FAILURES: fewer than four names were read out of tests/testlib.ps;"
    echo "      the framework was rewritten in a way this cannot follow, and"
    echo "      a check that finds no shared names proves nothing"
    exit 1
fi

# ---- the register still carries its own header
#
# Everything below reads only the uncommented lines, so none of it can see the
# header. A rewrite that generates the register by finding the first `own` in
# the file truncates the header mid-sentence at the word in `own source through
# currentfile` and merges the first entry onto the comment, and no other check
# here can tell. The two legend lines are the end of the prose, so requiring
# them requires the prose that precedes them.
for legend in 'own    <suite>' 'cannot <suite>'; do
    if ! grep -q "^#   $legend" "$src/tests/testlib-facts"; then
        echo "FAIL: tests/testlib-facts has lost the '#   $legend' legend"
        echo "      line, so its header has been truncated -- most likely by"
        echo "      something that rewrote the register by searching the whole"
        echo "      file for a disposition word and finding one in the prose."
        echo "      Nothing else here reads the header, so this is the only"
        echo "      check that can say so."
        fail=1
    fi
done

# ---- the harness prepends it, and only on the marker
if ! grep -q 'testlib.ps' "$src/tests/run-ps-test.sh"; then
    echo "FAIL: tests/run-ps-test.sh does not mention testlib.ps, so nothing"
    echo "      prepends the framework and every suite asking for it is"
    echo "      running without one."
    fail=1
fi
if ! grep -q "'%!testlib'" "$src/tests/run-ps-test.sh"; then
    echo "FAIL: the harness does not look for the %!testlib marker. Prepending"
    echo "      unconditionally breaks the suites that assert their own"
    echo "      dictionary starts empty and the six that read their own source."
    fail=1
fi

# ---- who asks for the framework, and who runs a case their own way
#
# DERIVED BY `stopped`, NOT BY NAME, and that is the whole point of this
# section. Looking for procedures named after the framework's own runners --
# works, refuses, raises, outcome -- finds only the copies that happen to share
# a name with the shared one, and a runner is renamed as easily as it is
# written. `/erris` is a raises-clone under another name, and it is defined in
# twenty-four suites here. A register derived by name shrinks when a runner is
# renamed, which is not a conversion.
#
# `stopped` is the operator every such runner has to reach -- it is the only
# way a PostScript program catches an error -- so counting it cannot be
# dodged by choosing a different name. What is counted is OCCURRENCES rather
# than lines, because five lines here carry two, and an occurrence is only
# counted when no % precedes it on the line, so that prose about the
# operator does not register as a use of it.
#
# The count is part of the register entry. A suite converted case by case
# has its count fall, which is visible progress, and a suite that grows a
# new hand-rolled case fails even though it was already listed -- which a
# bare list of names could not catch.
: > "$work/asks"
: > "$work/own"
for f in "$src"/tests/*.ps; do
    b=$(basename "$f")
    [ "$b" = testlib.ps ] && continue
    if head -n 1 "$f" 2>/dev/null | grep -q '^%!testlib'; then
        echo "$b" >> "$work/asks"
        # a suite that asks and then defines one of the shared names has
        # shadowed it: the local copy wins and the framework is decoration
        while read -r nm; do
            if grep -q "^/$nm  *{" "$f"; then
                echo "$b $nm" >> "$work/shadow"
            fi
        done < "$work/shared"
    fi
    n=$(awk '
        {
            line = $0; p = index(line, "%"); base = 0
            while ((s = index(substr(line, base + 1), "stopped")) > 0) {
                abs = base + s
                if (p == 0 || abs < p) n++
                base = abs + 6
            }
        }
        END { print n + 0 }' "$f")
    [ "$n" -gt 0 ] && printf '%s %s\n' "$b" "$n" >> "$work/own"
done
LC_ALL=C sort -o "$work/own" "$work/own"

# One derivation, two callers: `--derive` prints exactly what the check
# compares against, so the register is regenerated by the same code that
# enforces it and the two cannot drift apart.
if [ "${2:-}" = --derive ]; then
    while read -r b n; do
        printf 'own    %-38s %s\n' "$b" "$n"
    done < "$work/own"
    exit 0
fi

if [ -s "$work/shadow" ]; then
    echo "FAIL: these suites ask for the shared framework and then define one"
    echo "      of its names, so the local copy is what runs and the shared"
    echo "      runner -- the unwind included -- is decoration:"
    sed 's/^/        /' "$work/shadow"
    fail=1
fi

# ---- the register, held to the tree both ways
#
# Two dispositions. `own` is a suite that has not moved yet; `cannot` is one
# that must not, and carries the reason. The distinction is the point: a
# suite that asserts its own dictionary starts empty cannot be handed a
# framework defined into that dictionary, and without somewhere to say so
# the only records of that would be a conversion that failed and whoever
# next tried it.
#
# Only the suite name and its count are compared. Everything after them on a
# `cannot` line is the reason, which is prose and is not matched against
# anything -- but it must be there, and the check below says so.
grep -v '^[[:space:]]*#' "$src/tests/testlib-facts" \
    | awk '($1 == "own" || $1 == "cannot") && NF >= 3 && $3 ~ /^[0-9]+$/ {
             print $2, $3
           }' \
    | LC_ALL=C sort > "$work/reg"
sed 's/[[:space:]][[:space:]]*/ /g' "$work/own" | LC_ALL=C sort > "$work/own.n"

# every `cannot` states a reason, because a suite recorded as unconvertible
# without one is a suite nobody will ever try again and nobody can check
grep -v '^[[:space:]]*#' "$src/tests/testlib-facts" \
    | awk '$1 == "cannot" {
             r = ""
             for (i = 4; i <= NF; i++) r = r " " $i
             if (r == "") print $2
           }' > "$work/noreason"
if [ -s "$work/noreason" ]; then
    echo "FAIL: these suites are recorded as unable to use the shared"
    echo "      framework and give no reason:"
    sed 's/^/        /' "$work/noreason"
    fail=1
fi

guard_held=0
guard_hold "$work/reg" "$work/own.n" \
    "named in tests/testlib-facts with a count of cases run its own way that
      the suite no longer matches. If cases were converted, lower the count to
      what the tree now has -- or drop the line entirely if it reached zero:" \
    "running a case its own way with no matching line in tests/testlib-facts.
      Every case a suite runs itself is a case the shared unwind does not
      reach, so say so there, with how many:"
[ "$guard_held" -eq 0 ] || fail=1

nown=$(grep -c . "$work/own.n" || true)
nsaid=$(awk '/^owns /{ print $2; found = 1 } END { if (!found) print "" }' \
    "$src/tests/testlib-facts")
ncsaid=$(awk '/^cannots /{ print $2; found = 1 } END { if (!found) print "" }' \
    "$src/tests/testlib-facts")
case $nsaid$ncsaid in
    *[!0-9]*|'')
        echo "FAILURES: tests/testlib-facts needs both an 'owns <n>' and a"
        echo "      'cannots <n>' line. Nothing else would stop the register"
        echo "      being emptied, and two empty sets agree about everything"
        fail=1 ;;
    *)  if [ "$((nsaid + ncsaid))" -ne "$nown" ]; then
            echo "FAILURES: tests/testlib-facts records $nsaid suites yet to move"
            echo "      and $ncsaid that must not, which is $((nsaid + ncsaid))"
            echo "      between them, and the tree has $nown carrying their own"
            fail=1
        fi ;;
esac

[ "$fail" = 0 ] || exit 1
printf 'check-testlib: ok (%s suite(s) on the shared framework, %s yet to move, %s that must not)\n' \
    "$(grep -c . "$work/asks" || true)" "$nsaid" "$ncsaid"
exit 0
