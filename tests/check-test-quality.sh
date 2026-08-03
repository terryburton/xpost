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
#      status passes a run that printed SUCCESS and then crashed.
#   3. A wrapper that accepts a golden file, register or list without
#      requiring it to be non-empty passes when given nothing to check.
# Usage: check-test-quality.sh <tests directory>

set -eu
dir=${1:?usage: check-test-quality.sh <tests directory>}
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

# 2. wrappers must consult the interpreter's exit status
for f in "$dir"/run-*.sh; do
    [ -e "$f" ] || continue
    grep -q '\$xpost' "$f" || continue
    if ! grep -qE 'status=\$\?|st=\$\?|\$\? -ne 0|\|\| (exit|fail)|rc=\$\?' "$f"; then
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
    if ! grep -q '! -s' "$f"; then
        echo "FAIL: $(basename "$f") accepts its golden input without requiring"
        echo "      it to be non-empty -- an empty file would pass vacuously"
        fail=1
    fi
done

# The verdict's position on the line is not checked here: the wrappers
# already require ^SUCCESS$ at run time, so a suite that prints without a
# trailing newline fails the moment it runs.

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: a test cannot reliably fail"
    exit 1
fi
echo SUCCESS
exit 0
