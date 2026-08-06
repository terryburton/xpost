#!/bin/sh
# Guard the properties a test must have to be able to fail.
#
# Every rule here encodes a defect found in this suite, each of which let
# tests report success over broken code:
#
#   1. A failure counter incremented with `def` inside a procedure that
#      opens a scratch dictionary writes the scratch dictionary; `end`
#      discards it. Such a test prints its FAIL lines and then reports
#      SUCCESS. `store` writes the binding where the name is defined.
#   2. A wrapper that captures the interpreter's output without its exit
#      status passes a run that printed SUCCESS and then crashed. The
#      rule used to apply only to wrappers that mentioned $xpost, so a
#      wrapper that ran nothing at all was outside it: one whose whole
#      body was `exit 0` passed.
#   3. A wrapper that accepts a golden file, register or list without
#      requiring it to be non-empty passes when given nothing to check.
#   4. A test file with nothing in it, or nothing but comments, runs
#      clean. So does one whose content was commented out.
#   5. A guard that is not executable runs only because meson falls back
#      to the shebang, and not at all through any other route.
#   6. A wrapper that looks for SUCCESS anywhere in a run's output passes
#      a run that printed a failure and then printed SUCCESS. Seventeen
#      wrappers did, and one of them was passing over a real failure.
#      The rule lives in tests/verdict.sh so there is one of it.
#
# This check reads a directory, so it says which directory it will
# accept. It used to accept any: pointed at a directory that does not
# exist, at an empty one, at the source root, at data/ or at a build
# tree, it found no test to complain about and reported SUCCESS -- the
# same answer it gives for a suite in good order.
#
# Usage: check-test-quality.sh <tests directory>

set -eu
dir=${1:?usage: check-test-quality.sh <tests directory>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_dir "$dir" "the tests directory"
guard_require_file "$dir/guard-paths.sh" "the guard path helper"
guard_require_file "$dir/verdict.sh" "the verdict helper"

count_of() {
    n=0
    for f in "$@"; do
        [ -e "$f" ] && n=$((n + 1))
    done
    echo "$n"
}
nps=$(count_of "$dir"/*.ps)
nrun=$(count_of "$dir"/run-*.sh)
ncheck=$(count_of "$dir"/check-*.sh)
if [ "$nps" -lt 50 ] || [ "$nrun" -lt 15 ] || [ "$ncheck" -lt 15 ]; then
    echo "FAILURES: $dir holds $nps .ps tests, $nrun wrappers and $ncheck guards;"
    echo "      that is not the tests directory, or the suite has collapsed"
    exit 1
fi

fail=0

# 1. scoped counter increments
for f in "$dir"/*.ps; do
    [ -e "$f" ] || continue
    grep -q 'dict begin' "$f" || continue
    if grep -qE '/(failcount|nbad|nunbound|failures)[a-z]* +[a-z]+ +1 add def' "$f"; then
        echo "FAIL: $(basename "$f") increments a failure counter with def;"
        echo "      a scratch dictionary would swallow it -- use store"
        fail=1
    fi
done

# 2. wrappers must run what they were handed, and consult its exit status
for f in "$dir"/run-*.sh; do
    [ -e "$f" ] || continue
    if ! grep -qE '\$\{?[1-9@]|\$\{?[A-Za-z_]+:\?' "$f"; then
        echo "FAIL: $(basename "$f") never uses what it was handed -- it cannot"
        echo "      be running the thing under test"
        fail=1
    fi
    if ! grep -qE 'status=\$\?|st=\$\?|\$\? -ne 0|\|\| (exit|fail)|rc=\$\?|ret=\$\?' "$f"; then
        echo "FAIL: $(basename "$f") runs the interpreter without checking its"
        echo "      exit status -- a crash after SUCCESS would pass"
        fail=1
    fi
done

# 3. guards fed a golden/register file must require content
for f in "$dir"/check-*.sh "$dir"/run-golden-render.sh; do
    [ -e "$f" ] || continue
    # only scripts that read such a file, not ones that mention one
    grep -qE '(<|\bread .*<) *"\$(golden|manifest)"|grep [^|]*"\$(golden|manifest)"' "$f" || continue
    if ! grep -qE '! -s|guard_require_file' "$f"; then
        echo "FAIL: $(basename "$f") accepts its golden input without requiring"
        echo "      it to be non-empty -- an empty file would pass vacuously"
        fail=1
    fi
done

# 4. a test file has to have something in it
for f in "$dir"/*.ps; do
    [ -e "$f" ] || continue
    if ! sed 's/%.*//' "$f" | grep -q '[^[:space:]]'; then
        echo "FAIL: $(basename "$f") is empty or is nothing but comments;"
        echo "      it runs clean because it does not run"
        fail=1
    fi
done

# 5. a guard has to be runnable as itself
for f in "$dir"/check-*.sh "$dir"/run-*.sh; do
    [ -e "$f" ] || continue
    if [ ! -x "$f" ]; then
        echo "FAIL: $(basename "$f") is not executable; it runs only where"
        echo "      something falls back to its shebang for it"
        fail=1
    fi
done

# 6. the verdict a run printed is judged by one rule, in one place
#
# A wrapper that matches SUCCESS against a run's output itself is
# deciding, on its own, what makes a verdict count -- and what every one
# of them left out was that a run which printed a failure first has
# already failed, whatever it went on to conclude. tests/verdict.sh
# carries that; a wrapper reaches it through verdict_ok.
#
# This catches the spelling the suite uses. A wrapper that reads a
# verdict of some other spelling is outside it, which is why the helper
# is where the rule lives rather than where it is checked: the way to
# stay outside this is to write a wrapper that judges a run some other
# way entirely, and that wrapper would be a new thing to review.
for f in "$dir"/run-*.sh; do
    [ -e "$f" ] || continue
    body=$(tr -d '\r' < "$f" | sed 's/#.*//')
    printf '%s\n' "$body" | grep -qE 'grep[^|]*SUCCESS|=[[:space:]]*"?SUCCESS' \
        || continue
    if ! printf '%s\n' "$body" | grep -q 'verdict_ok'; then
        echo "FAIL: $(basename "$f") matches SUCCESS against a run's output"
        echo "      itself; a run that printed a failure first has already"
        echo "      failed -- judge it with verdict_ok from verdict.sh"
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: a test cannot reliably fail"
    exit 1
fi
echo "SUCCESS ($nps test files, $nrun wrappers, $ncheck guards)"
exit 0
