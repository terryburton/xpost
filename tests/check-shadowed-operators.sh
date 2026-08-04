#!/bin/sh
# Guard the names implemented twice, once in C and once in PostScript.
#
# Only one of the pair is in force. Which one is settled while the
# language is read, by the order the dictionaries are filled and
# searched, and neither the interpreter nor the program is told which
# way it went. The failure is quiet: an operator that answers, with the
# wrong implementation behind it.
#
# So each such name is declared in tests/shadowed_operators.golden, with
# the implementation that is meant to win. This checks that the set of
# doubly-implemented names is exactly the set declared -- a new one
# appearing is a choice nobody made on purpose, and has to be made here
# before it can be relied on anywhere else.
#
# Which implementation actually won is a run-time question, checked by
# startup_surface_test.ps against this same register.
#
# Usage: check-shadowed-operators.sh <srcdir> <datadir> <golden>

set -eu

srcdir=${1:?usage: check-shadowed-operators.sh <srcdir> <datadir> <golden>}
datadir=${2:?usage: check-shadowed-operators.sh <srcdir> <datadir> <golden>}
golden=${3:?usage: check-shadowed-operators.sh <srcdir> <datadir> <golden>}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# every name a C operator is installed under
grep -rho 'xpost_operator_cons(ctx, "[^"]*"' "$srcdir"/*.c 2>/dev/null \
    | sed 's/.*"\(.*\)"/\1/' | LC_ALL=C sort -u > "$work/c"

# every name a PostScript file defines at top level. A definition opens a
# procedure or an array on the line naming it; a `where` guard testing for
# a name does not, and is not a second implementation.
grep -rhoE '^/[A-Za-z=][A-Za-z0-9]* *[[{]' "$datadir"/*.ps 2>/dev/null \
    | sed 's|^/||; s| *[[{]$||' | LC_ALL=C sort -u > "$work/ps"

LC_ALL=C comm -12 "$work/c" "$work/ps" > "$work/both"

grep -v '^[[:space:]]*#' "$golden" | grep -v '^[[:space:]]*$' \
    | awk '{print $1}' | LC_ALL=C sort -u > "$work/declared"

if [ ! -s "$work/c" ] || [ ! -s "$work/ps" ]; then
    echo "FAILURES: found no operators to compare in $srcdir or $datadir"
    exit 1
fi

fail=0

LC_ALL=C comm -23 "$work/both" "$work/declared" > "$work/new"
if [ -s "$work/new" ]; then
    echo "FAIL: implemented in both C and PostScript, but not declared:"
    sed 's/^/      /' "$work/new"
    echo "      say which one wins in $(basename "$golden")"
    fail=1
fi

LC_ALL=C comm -13 "$work/both" "$work/declared" > "$work/gone"
if [ -s "$work/gone" ]; then
    echo "FAIL: declared as implemented twice, but no longer are:"
    sed 's/^/      /' "$work/gone"
    echo "      remove them from $(basename "$golden")"
    fail=1
fi

# every declaration names one of the two implementations
grep -v '^[[:space:]]*#' "$golden" | grep -v '^[[:space:]]*$' \
    | awk '$2 != "postscript" && $2 != "c" { print }' > "$work/bad"
if [ -s "$work/bad" ]; then
    echo "FAIL: a declaration names neither implementation:"
    sed 's/^/      /' "$work/bad"
    fail=1
fi

[ "$fail" = 0 ] || exit 1
echo "SUCCESS ($(wc -l < "$work/both" | tr -d ' ') names implemented twice, each declared)"
