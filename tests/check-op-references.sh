#!/bin/sh
# Guard the way C reaches a PostScript operator.
#
# The rule: C that schedules a standard operator holds the operator object.
# A name pushed on the execution stack is resolved against the dictionary
# stack at the moment it runs, so a program that defines that name -- which
# PLRM 3.3 entitles it to do -- takes over the inside of a standard
# operator, with the operator's own operands, in the middle of its work.
# That was fixed in the glyph painter, and then found live again in the arc
# machinery and in initmatrix: the same rule broken at one more site each
# time, which is what a rule kept by imitation does.
#
# XPOST_OP_REFS in xpost_context.h names every operator the interpreter
# reaches for, and generates from that one list the storage, the uncaptured
# marker, the capture and the end-of-registration check. This holds the
# tree to going through it, three ways:
#
#   1. No name is pushed for execution. The safe form is now the shorter
#      one, so there is nothing to weigh up at a call site.
#   2. The reference table is reached only through its accessors, so the
#      one statement of the set stays the one statement of it.
#   3. Every surviving lookup of an operator by string at run time is
#      registered. Each is a global name intern plus a walk of the whole
#      operator table, and answers a null object for a name that is not
#      there -- which the interpreter then schedules. The register only
#      shrinks.
#
# Usage: check-op-references.sh <source root>

set -u
src=${1:?usage: check-op-references.sh <source root>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"

lib="$src/src/lib"
guard_require_dir "$lib" "the library source directory"
table="$lib/xpost_context.h"
guard_require_file "$table" "the header holding XPOST_OP_REFS"
register="$src/tests/op_lookups.golden"
guard_require_file "$register" "the run-time operator lookup register"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

# ---------------------------------------------------------------- rule 1
#
# Two files legitimately make a name: the scanner, which answers with the
# name a token spells, and the type operator, which answers with the name
# of an object's type. Both produce a name as a value; neither schedules
# one. Every other name reaching the execution stack is the defect.
producers='xpost_op_token.c xpost_op_type.c'

for f in "$lib"/*.c "$lib"/*.h; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    case " $producers " in
        *" $base "*) continue ;;
    esac
    # a commented-out call is not a call site, and says nothing about what
    # the interpreter does
    hits=$(grep -n 'xpost_object_cvx(xpost_name_cons' "$f" \
           | grep -vE '^[0-9]+: *(//|/\*|\*)' || true)
    if [ -n "$hits" ]; then
        echo "FAIL: $base pushes an operator's name where it means the operator:"
        printf '%s\n' "$hits" | sed 's/^/      /'
        echo "      hold it instead -- XPOST_OP(ctx, <entry>), adding the entry"
        echo "      to XPOST_OP_REFS in xpost_context.h if it is not there yet"
        fail=1
    fi
done

# ---------------------------------------------------------------- rule 2
#
# The members are generated from XPOST_OP_REFS and reached through XPOST_OP
# and XPOST_OP_CODE. A file that names the structure directly has stepped
# around the one statement of the set.
# only the sources: a build in the tree leaves object files beside them,
# and a match in one of those is debug information, not a call site
strays=$(grep -l 'opcode_shortcuts' "$lib"/*.c "$lib"/*.h 2>/dev/null \
         | grep -v '/xpost_context\.h$' || true)
if [ -n "$strays" ]; then
    echo "FAIL: the reference table is reached without its accessor by:"
    printf '%s\n' "$strays" | sed 's/^/      /'
    echo "      use XPOST_OP(ctx, <entry>) or XPOST_OP_CODE(ctx, <entry>)"
    fail=1
fi

# the table itself: each entry named once, and enough of them that this
# check is still reading the table it thinks it is
grep -oE '^ *_\([a-z]+,' "$table" | sed -E 's/^ *_\(//; s/,$//' | sort > "$work/refs"
nrefs=$(grep -c . "$work/refs" || true)
dupes=$(uniq -d "$work/refs")
if [ -n "$dupes" ]; then
    echo "FAIL: XPOST_OP_REFS names an entry twice:"
    printf '%s\n' "$dupes" | sed 's/^/      /'
    fail=1
fi
if [ "$nrefs" -lt 20 ]; then
    echo "FAILURES: XPOST_OP_REFS parsed as only $nrefs entries; the table"
    echo "      moved or changed shape and this check no longer reads it"
    exit 1
fi

# ---------------------------------------------------------------- rule 3
#
# The register lists each surviving by-string lookup as "file operator",
# with the reason it is still there in a comment above it.
grep -n 'xpost_operator_cons *(ctx, *"[^"]*", *NULL' "$lib"/*.c "$lib"/*.h \
  | grep -vE ':[0-9]+: *(//|/\*|\*)' \
  | sed -E 's|^.*/([a-z0-9_]+\.c):[0-9]+:.*xpost_operator_cons *\(ctx, *"([^"]*)".*|\1 \2|' \
  | sort > "$work/current"
grep -v '^#' "$register" | grep -v '^[[:space:]]*$' | sort > "$work/recorded"

added=$(comm -23 "$work/current" "$work/recorded")
removed=$(comm -13 "$work/current" "$work/recorded")

if [ -n "$added" ]; then
    echo "FAIL: an operator is looked up by string at run time, unregistered:"
    printf '%s\n' "$added" | sed 's/^/      /'
    echo "      reach it through XPOST_OP(ctx, <entry>) instead"
    fail=1
fi
if [ -n "$removed" ]; then
    echo "FAIL: the register lists a lookup that is no longer there:"
    printf '%s\n' "$removed" | sed 's/^/      /'
    echo "      delete the line from tests/op_lookups.golden -- the register"
    echo "      only shrinks, and a stale entry makes room for a new lookup"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: C reaches an operator by name where it should hold it"
    exit 1
fi

nleft=$(grep -c . "$work/recorded" || true)
echo "SUCCESS ($nrefs operators referenced by value; $nleft lookup(s) left to retire)"
exit 0
