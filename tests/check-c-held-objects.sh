#!/bin/sh
# Guard: every object held in C is named to the collector.
#
# The collector reaches an object by walking objects. One held in a
# variable or a structure of C is reached by nothing, so the first
# collection that runs takes it, and what the holder does with it
# afterwards is whatever the storage has become. Such a failure does not
# announce itself: the object is used again long after the collection
# that took it, so what goes wrong has no visible connection to the
# collector at all.
#
# The rule is that a C variable or structure may hold host resources --
# a file handle, a face the font library opened, a path -- and may hold
# an object only where something names that object to the collector.
# Each of the entries below says which way it uses:
#
#   context   the context holds it, and the collector walks the context's
#             roots from the list that declares them
#   entity    an entity the collector does walk names it, and the
#             marking of that entity descends into the structure
#   reached   it is reachable from an object the walk already covers, and
#             the entry says from where
#
# A new holder is a failure here until it is entered below with the way
# it is named. That is the point: the collector cannot be taught to look
# in a variable it has never heard of, so the compiler cannot catch this
# and neither can a reviewer who does not already know to look.
set -u
src=${1:?usage: check-c-held-objects.sh <source root>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_workdir
guard_mirror_tree "$src"
src=$mirror
libdir="$src/src/lib"
guard_require_dir "$libdir" "the library source directory"

# An empty register admits nothing, which is indistinguishable from a
# tree that holds nothing: both leave the two comparisons below with
# nothing on either side, and the check then answers SUCCESS having
# compared no holder against no entry.
register="$src/tests/c_held_objects.register"
guard_require_file "$register" "the register of objects held in C"

fail=0

# What the register admits, as "file:name".
sed 's/#.*//' "$register" | awk 'NF >= 2 { print $1 }' | sort -u > "$work/allowed"
if [ ! -s "$work/allowed" ]; then
    echo "check-c-held-objects: the register at tests/c_held_objects.register" >&2
    echo "      admits nothing; every holder in the tree would be a finding" >&2
    echo "      and a tree with none would read as one in good order" >&2
    exit 1
fi

# A declaration at the start of a line is a variable of the file; one
# indented is a local or a parameter, and a local cannot outlive the
# call that made it.
: > "$work/found"
nsrc=0
for f in "$libdir"/*.c; do
    [ -f "$f" ] || continue
    nsrc=$((nsrc + 1))
    b=$(basename "$f")
    # a file-scope object variable
    sed -nE 's/^(static +)?Xpost_Object +([A-Za-z_][A-Za-z0-9_]*)[;[].*/\2/p' "$f" \
        | while read -r n; do echo "$b:$n"; done >> "$work/found"
    # an object inside a structure the file keeps for itself
    awk -v b="$b" '
        /^[ \t]*static[ \t]+struct[ \t]*\{/ { instruct = 1 }
        instruct && /Xpost_Object[ \t]+[A-Za-z_]/ {
            line = $0
            while (match(line, /Xpost_Object[ \t]+[A-Za-z_][A-Za-z0-9_]*/)) {
                m = substr(line, RSTART, RLENGTH)
                sub(/Xpost_Object[ \t]+/, "", m)
                print b ":" m
                line = substr(line, RSTART + RLENGTH)
            }
        }
        instruct && /\}/ { instruct = 0 }
    ' "$f" >> "$work/found"
done

# Counts in before counts out. The two comparisons below say only what
# the population they are given says, and an empty one agrees with the
# register whatever the register holds. So say how many sources were
# read and how many declarations the scan recognised in them: a library
# that was not there, and a declaration written in a shape this no
# longer matches, each leave the check answering about nothing.
ndecl=$(sort -u "$work/found" | grep -c . || true)
if [ "$nsrc" -lt 20 ] || [ "$ndecl" -lt 20 ]; then
    echo "check-c-held-objects: $nsrc library source(s) were read and $ndecl" >&2
    echo "      object declaration(s) recognised in them; the library holds" >&2
    echo "      far more, so this is reading a fraction of the tree and" >&2
    echo "      would find no holder anywhere" >&2
    exit 1
fi

# A name interned once and held is a name object: it carries an index
# into the name stack, which the collector walks whole, so holding one
# names nothing the sweep could take.
grep -vE ':name[A-Za-z_0-9]*$' "$work/found" | sort -u > "$work/held"

# The register may not outlive what it describes either: an entry for a
# holder that is gone reads as cover for one that is not.
guard_held=0
guard_hold "$work/held" "$work/allowed" \
    "held in C and not named to the collector. Say in
      tests/c_held_objects.register how each is named -- context,
      entity or reached -- or stop holding it in C:" \
    "named by the register as held in C, and not there:"
[ "$guard_held" -eq 0 ] || fail=1

[ "$fail" -eq 0 ] || exit 1
echo "SUCCESS ($nsrc sources read, $ndecl object declaration(s) seen," \
     "$(grep -c . "$work/held") held in C and each named to the collector)"
