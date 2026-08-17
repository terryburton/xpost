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

# ---- who asks for the framework, and who keeps their own
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
    else
        d=""
        while read -r nm; do
            # only the names that RUN a case count. `note` and `assert`
            # report one; a suite with its own printer is not a suite the
            # unwind fails to reach, and counting those overstated this
            # register by a factor of four when it was first written.
            case $nm in
                works|works2|refuses|refuses2|raises|raises2|outcome) ;;
                *) continue ;;
            esac
            # the name must be bound to a PROCEDURE: three suites here
            # write `/refuses false def`, a boolean recording whether the
            # platform has a target that refuses data, and counting those
            # as runners put files in this register that have none
            grep -q "^/$nm  *{" "$f" && d="$d $nm"
        done < "$work/shared"
        [ -n "$d" ] && printf '%s%s\n' "$b" "$d" >> "$work/own"
    fi
done
LC_ALL=C sort -o "$work/own" "$work/own"

if [ -s "$work/shadow" ]; then
    echo "FAIL: these suites ask for the shared framework and then define one"
    echo "      of its names, so the local copy is what runs and the shared"
    echo "      runner -- the unwind included -- is decoration:"
    sed 's/^/        /' "$work/shadow"
    fail=1
fi

# ---- the register, held to the tree both ways
grep -v '^[[:space:]]*#' "$src/tests/testlib-facts" \
    | awk '$1 == "own" && NF >= 3 { $1 = ""; sub(/^ /, ""); print }' \
    | sed 's/[[:space:]][[:space:]]*/ /g' | LC_ALL=C sort > "$work/reg"
sed 's/[[:space:]][[:space:]]*/ /g' "$work/own" | LC_ALL=C sort > "$work/own.n"

guard_held=0
guard_hold "$work/reg" "$work/own.n" \
    "named in tests/testlib-facts as carrying its own case runner and no
      longer carrying one. It has been converted -- take the line out, and
      the count with it:" \
    "carrying its own case runner and not named in tests/testlib-facts. A
      suite that grows one is a suite the shared unwind does not reach, so
      say so there, with the shapes its runner takes:"
[ "$guard_held" -eq 0 ] || fail=1

nown=$(grep -c . "$work/own.n" || true)
nsaid=$(awk '/^owns /{ print $2; found = 1 } END { if (!found) print "" }' \
    "$src/tests/testlib-facts")
case $nsaid in
    ''|*[!0-9]*)
        echo "FAILURES: tests/testlib-facts has no 'owns <n>' line. Nothing"
        echo "      else would stop the register being emptied, and two empty"
        echo "      sets agree about everything"
        fail=1 ;;
    *)  if [ "$nsaid" -ne "$nown" ]; then
            echo "FAILURES: tests/testlib-facts records $nsaid suites carrying"
            echo "      their own runner and the tree has $nown"
            fail=1
        fi ;;
esac

[ "$fail" = 0 ] || exit 1
printf 'check-testlib: ok (%s suite(s) on the shared framework, %s still carrying their own)\n' \
    "$(grep -c . "$work/asks" || true)" "$nown"
exit 0
