#!/bin/sh
# Which storage in the arena the entity table does not describe.
#
# xpost_memory_file_alloc takes bytes off the high-water mark and hands back
# an address. It records nothing: no table row, no size, no tag. The entity
# table is a host allocation rather than arena storage, so the arena holds
# exactly entity storage, blocks taken this way, and the padding skipped to
# reach an 8-aligned address.
#
# A block with no row is a block nothing can find. A pass that rearranged
# the arena walks the table, so it cannot see such a block, cannot learn its
# size, and cannot reach whatever holds its address -- and would relocate a
# live entity on top of it. That is why the population is held here rather
# than left to be rediscovered: every call is a decision about whether the
# arena stays describable.
#
# WHAT IS DERIVED. Every call to xpost_memory_file_alloc in the library,
# wherever it is and whatever it allocates, less the one inside the table
# allocator itself -- which is the call that gives an entity its storage and
# records a row for it. Each remaining call must appear in the register with
# a disposition, so a new one forces the question before the build is green.
#
#   $1  path to the source root
set -u
src=${1:?usage: check-raw-allocations.sh <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"

guard_workdir
trap 'rm -rf "$work"' EXIT INT TERM

# Read with carriage returns taken out, for the reason given where
# guard_mirror is defined: a call site is found by what starts a line, and
# a line ending the scan does not recognise takes the site out of the
# population without saying so.
guard_mirror lib "$src"/src/lib/*.c
lib=$mirror
golden=$src/tests/raw_allocations.golden
guard_require_file "$golden" "the register of undescribed arena storage"

# The call sites, as file and function. The function is tracked by the last
# definition seen at column one, which is this tree's style throughout, so a
# site is named by something a reader can go to rather than by a line number
# that every edit above it invalidates.
for f in "$lib"/*.c; do
    [ -e "$f" ] || continue
    base=${f##*/}
    [ "$base" = "xpost_memory.c" ] && continue   # the allocator's own file
    awk -v base="$base" '
    /^[A-Za-z_][A-Za-z0-9_ *]*\(/ { if (index($0, ";") == 0) { fn = $0
        sub(/\(.*/, "", fn)
        # the blanks between the name and the parenthesis go first: the
        # match that takes the last word is greedy, so a space left there
        # is the last separator and the name is what gets removed
        sub(/[[:blank:]]+$/, "", fn)
        sub(/^.*[ *]/, "", fn) } }
    /^[A-Za-z_][A-Za-z0-9_]*$/    { fn = $0 }
    /xpost_memory_file_alloc[[:blank:]]*\(/ {
        # a call whose function could not be named is reported as one, not
        # passed over: dropping it would take the site out of the
        # population and leave the register looking satisfied
        printf "%s %s\n", base, (fn == "" ? "UNNAMED-FUNCTION" : fn)
    }' "$f"
done | sort -u > "$work/found"

# The allocator's own file is skipped wholesale above, so the count must not
# be allowed to reach zero by a scan that stopped working: this tree has
# sites, and finding none means the derivation is broken rather than that
# the arena became describable.
if ! grep -q . "$work/found" && ! grep -q "^none" "$golden"; then
    echo "FAILURES: no call to xpost_memory_file_alloc was found outside the"
    echo "      allocator's own file. Either every one has been converted --"
    echo "      in which case say so with a 'none' line in the register --"
    echo "      or this scan has stopped reading the calls"
    exit 1
fi

sed -n 's/^\(settled\|blocks\)  *\([^ ]*\)  *\([^ ]*\).*/\2 \3/p' "$golden" \
    | sort -u > "$work/register"

fail=0

guard_held=0
guard_hold "$work/found" "$work/register" \
    "storage is taken from the arena here with no row to describe it.
      Say in tests/raw_allocations.golden why this block needs no entity
      of its own -- which means saying why a pass that rearranged the
      arena may step over it:" \
    "the register names a call the library no longer makes. A converted
      site has its row removed in the same commit, not left standing:"
[ "$guard_held" -eq 0 ] || fail=1

# Either disposition carries a reason: a settled site has to say why a
# rearrangement may step over it, and a blocking one has to say what the
# block is, since that is the work the reason describes.
awk '$1 == "settled" || $1 == "blocks" {
        if (NF < 4) { printf "      %s %s\n", $2, $3; bad = 1 }
     }
     END { exit bad ? 1 : 0 }' "$golden" > "$work/unreasoned" || {
    echo "FAIL: a block with no row and no reason recorded:"
    cat "$work/unreasoned"
    fail=1
}

[ "$fail" -eq 0 ] || exit 1

nset=$(awk '$1 == "settled"' "$golden" | grep -c .)
nblk=$(awk '$1 == "blocks"' "$golden" | grep -c .)
if [ "$nblk" -gt 0 ]; then
    echo "SUCCESS ($nset settled, $nblk still undescribed and blocking a"
    echo "         rearrangement of the arena)"
else
    echo "SUCCESS ($nset settled, nothing left undescribed: every block in"
    echo "         the arena now carries a row saying where and how big)"
fi
