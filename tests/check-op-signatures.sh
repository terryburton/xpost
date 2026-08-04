#!/bin/sh
# Guard the operand-contract invariant: an operator installed by wrapping
# a procedure must state the operands it takes, so the dispatcher can
# enforce the statement before the procedure runs.
#
# An operator written in C declares its operand types and never accepted
# one it could not use. A wrapped operator registered with no signature
# has nothing for the dispatcher to check and must check for itself,
# which is what every operator found accepting a wrong operand had in
# common. Wrapping a new operator without stating its operands fails
# here.
#
# An operator states its operands at its definition, through .defop.
# tests/op_signatures.allowed lists the wrapped operators that do not yet
# state theirs. The list only shrinks: an entry that has since gained a
# statement is a failure too, so it cannot go stale.
#
# Usage: check-op-signatures.sh <data/init.ps> <op_signatures.allowed>

set -eu

initps=${1:?usage: check-op-signatures.sh <init.ps> <allowed>}
allowed=${2:?usage: check-op-signatures.sh <init.ps> <allowed>}
datadir=$(dirname "$initps")

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The operators wrapped at lockdown: the bracketed list of names whose
# closing bracket begins the loop that wraps each one.
awk '
    /^    \] \{$/ { inlist = 0 }
    inlist { for (i = 1; i <= NF; i++) if ($i ~ /^\//) print substr($i, 2) }
    /wrapped procedures/ { inlist = 1 }
' "$initps" > "$work/wrapped"

# When the banner above is not found, fall back to the shape: a run of
# name-only lines ending at the wrapping loop.
if [ ! -s "$work/wrapped" ]; then
    awk '
        /^    \] \{$/ { inlist = 0 }
        inlist == 1 { for (i = 1; i <= NF; i++) if ($i ~ /^\//) print substr($i, 2) }
        /^    \[$/ { inlist = 1 }
    ' "$initps" > "$work/wrapped"
fi

# The operators that state their operands: each states it where it is
# defined, so the statements are gathered from the definitions rather
# than from a table someone maintains beside them. A definition opens
# its statement on the line naming the operator; an operator taking
# more than one shape of operand list runs the statement over several
# lines, so what is recognised is the opening, not the whole of it.
{
    grep -h '^/[A-Za-z][A-Za-z0-9]* \[' "$datadir"/*.ps 2>/dev/null \
        | grep -v '^/[A-Za-z][A-Za-z0-9]* \[[^][]*\][[:space:]]*def[[:space:]]*$' \
        | sed 's|^/||; s| .*||'
    grep -h -o '\.opsigs get /[A-Za-z][A-Za-z0-9]* \[' "$datadir"/*.ps 2>/dev/null \
        | sed 's|.*/||; s| .*||'
} > "$work/stated"

LC_ALL=C sort -u "$work/wrapped" > "$work/w"
LC_ALL=C sort -u "$work/stated" > "$work/s"
grep -v '^[[:space:]]*#' "$allowed" | grep -v '^[[:space:]]*$' \
    | LC_ALL=C sort -u > "$work/a"

if [ ! -s "$work/w" ]; then
    echo "FAILURES: no wrapped operators found in $initps"
    exit 1
fi

fail=0

# every wrapped operator either states its operands or is listed
LC_ALL=C comm -23 "$work/w" "$work/s" > "$work/unstated"
LC_ALL=C comm -23 "$work/unstated" "$work/a" > "$work/new"
if [ -s "$work/new" ]; then
    echo "FAIL: wrapped operators that do not state their operands:"
    sed 's/^/      /' "$work/new"
    echo "      add a signature to .opsigs, or the name to $(basename "$allowed")"
    fail=1
fi

# the list only shrinks: an entry that now states its operands is stale
LC_ALL=C comm -12 "$work/a" "$work/s" > "$work/stale"
if [ -s "$work/stale" ]; then
    echo "FAIL: listed as unstated, but they state their operands now:"
    sed 's/^/      /' "$work/stale"
    echo "      remove them from $(basename "$allowed")"
    fail=1
fi

# and an entry naming something that is not wrapped at all is stale too
LC_ALL=C comm -23 "$work/a" "$work/w" > "$work/unknown"
if [ -s "$work/unknown" ]; then
    echo "FAIL: listed as unstated, but not wrapped at all:"
    sed 's/^/      /' "$work/unknown"
    fail=1
fi

[ "$fail" = 0 ] || exit 1
LC_ALL=C comm -12 "$work/w" "$work/s" > "$work/both"
echo "SUCCESS ($(wc -l < "$work/both" | tr -d ' ') of $(wc -l < "$work/w" | tr -d ' ') wrapped operators state their operands)"
