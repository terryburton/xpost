#!/bin/sh
# Guard: the arena is closed up only where nothing holds a pointer into it.
#
# Rearranging the arena moves the bytes under every pointer derived from an
# entity's recorded address. Those pointers are derived where they are used
# and not held across a call that can allocate, which is enough for the
# allocator and for the collector -- but an operator holds them across its
# own body, and the machinery that ran the operator holds them across the
# call to it. So a rearrangement reached from inside an operator leaves the
# caller reading bytes that have moved, and the failure is not a crash: it
# is an operator dispatched with whatever now lies where its arguments were.
#
# That was found the hard way. Calling it from vmreclaim broke every real
# render with an "unregistered" error, from a stack whose segments had
# moved under the argument marshalling. Moving the call to the
# interpreter's safe point between operator executions fixed it entirely,
# so the rule is not a preference: it is what makes the pass usable.
#
# WHAT IS DERIVED. Every call to xpost_free_compact outside its own file,
# with the function it sits in. The safe point is the one place allowed to
# make it, and it is named below rather than pattern-matched, so a second
# caller -- however reasonable it looks -- fails this until someone says
# why it is safe.
#
#   $1  path to the source root
set -u
src=${1:?usage: check-compaction-safe-point.sh <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"

guard_workdir
trap 'rm -rf "$work"' EXIT INT TERM

# The one function permitted to close the arena up. Held as a pair, so
# that moving the safe point to another file is as visible as adding a
# caller.
allowed_file=xpost_interpreter.c
allowed_fn=_compaction_wanted

# Every call, named by the function it is written in. The name is taken
# from the last line at column zero that opens a body, which is how a C
# file says whose text this is.
for f in "$src"/src/lib/*.c; do
    base=${f##*/}
    [ "$base" = xpost_free.c ] && continue   # the definition, and its own file
    awk -v base="$base" '
        /^[A-Za-z_].*\(/ { fn = $0
                           sub(/\(.*/, "", fn)
                           # the space a definition may put before its
                           # parenthesis is taken off first: leaving it
                           # makes the strip below eat the name itself and
                           # report every caller as one it cannot name
                           sub(/[ \t]+$/, "", fn)
                           sub(/^.*[ \t*]/, "", fn)
                           if (fn != "") cur = fn }
        /xpost_free_compact[ \t]*\(/ {
            if ($0 ~ /^[ \t]*[*#]/) next        # a comment or a directive
            printf "%s %s\n", base, (cur == "" ? "@NOFN@" : cur)
        }' "$f"
done | sort -u > "$work/callers"

# A call the derivation could not attribute is not passed over: it would
# be a caller this guard reports nothing about, which is the shape of a
# guard that passes by seeing less.
if grep -q '@NOFN@' "$work/callers"; then
    echo "FAIL: a call to xpost_free_compact whose function could not be"
    echo "      named. The guard cannot say whether it is at the safe"
    echo "      point, and passing over it would leave it unchecked:"
    sed 's/^/      /' "$work/callers" | grep '@NOFN@'
    exit 1
fi

printf '%s %s\n' "$allowed_file" "$allowed_fn" > "$work/allowed"
comm -23 "$work/callers" "$work/allowed" > "$work/extra"
if [ -s "$work/extra" ]; then
    echo "FAIL: the arena is closed up somewhere other than the interpreter's"
    echo "      safe point. Every pointer derived from an entity's address is"
    echo "      invalidated by it, and an operator's caller holds such"
    echo "      pointers across the call, so what follows reads bytes that"
    echo "      have moved:"
    sed 's/^/      /' "$work/extra"
    echo "      Ask for it instead -- set compact_pending -- and let the"
    echo "      safe point act on the request."
    exit 1
fi

# The safe point must still be making the call. A guard that only forbids
# is satisfied by the feature having been deleted.
if ! grep -q . "$work/callers"; then
    echo "FAIL: nothing calls xpost_free_compact at all, so the arena is"
    echo "      never closed up. This guard forbids the wrong callers and"
    echo "      would be satisfied by there being none."
    exit 1
fi

echo "SUCCESS (the arena is closed up only from $allowed_fn in $allowed_file)"
