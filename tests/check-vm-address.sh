#!/bin/sh
# Guard how the address of a special entity is obtained.
#
# The first few table slots hold structures the interpreter always has: the
# free lists, the save stack, the context list, the two halves of the name
# table, the operator table. Each is built once, by one constructor, and
# nextent only ever increments -- so once built, a special entity's row
# exists for the life of the memory file and its address cannot fail to be
# found.
#
# It was nevertheless reached through a fallible lookup that returned the
# address by out-parameter, and two callers in three dropped the answer:
# they passed an uninitialised local, ignored the refusal, and then used the
# local as an offset from mem->base. Nothing went wrong, because the refusal
# could not happen -- but nothing said so, the same lookup was spelled with
# four different messages for a branch none of them could take, and every
# one of those call sites read as though the entity might be missing and it
# were fine to carry on regardless.
#
# So the enumerators are named in the header that defines them and in the
# five constructors that build the entities. Everywhere else calls the
# accessor named for the entity, which returns the address directly.
#
# Usage: check-vm-address.sh <source root>

set -u
src=${1:?usage: check-vm-address.sh <source root>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"

lib="$src/src/lib"
guard_require_dir "$lib" "the library source directory"
header="$lib/xpost_memory.h"
guard_require_file "$header" "the header holding the accessors"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

# The five constructors, each of which allocates its entity and asserts it
# landed in the slot the enumerator names. Nothing else may name one.
cat > "$work/permitted" <<'EOF'
xpost_context.c xpost_context_init_ctxlist
xpost_free.c xpost_free_init
xpost_name.c xpost_name_init
xpost_operator.c xpost_operator_init_optab
xpost_save.c xpost_save_init
EOF

# Scan the sources BY NAME. A build in the tree leaves object files beside
# them whose debug information matches this pattern, so a directory walk is
# green where nothing was built and red where something was.
#
# Comments are stripped before matching -- prose about the mechanism, of
# which there is a good deal, is not a use of it -- and the enclosing
# function is tracked so a permitted site can be named by the constructor it
# belongs to. Function bodies in this tree open and close with a brace at
# column 0.
for f in "$lib"/*.c "$lib"/*.h; do
    [ -e "$f" ] || continue
    [ "$f" = "$header" ] && continue
    awk '
    {
        line = $0; code = ""
        while (length(line)) {
            if (incomment) {
                i = index(line, "*/")
                if (i == 0) { line = ""; break }
                line = substr(line, i + 2); incomment = 0; continue
            }
            i = index(line, "/*"); j = index(line, "//")
            if (j > 0 && (i == 0 || j < i)) { code = code substr(line, 1, j - 1); break }
            if (i == 0) { code = code line; break }
            code = code substr(line, 1, i - 1)
            line = substr(line, i + 2); incomment = 1
        }
        if (code ~ /^[A-Za-z_][A-Za-z0-9_ \t*]*\(/) {
            sig = code; sub(/\(.*/, "", sig); sub(/[ \t]*$/, "", sig)
            n = split(sig, parts, /[ \t*]+/); pending = parts[n]
        }
        if (code ~ /^\{/) curfn = pending
        if (code ~ /^\}/) curfn = ""
        if (code ~ /XPOST_MEMORY_TABLE_SPECIAL_[A-Z]/)
            printf "%s %s %d\n", FILENAME, (curfn == "" ? "<file-scope>" : curfn), FNR
    }' "$f"
done | sed "s|^$lib/||" > "$work/found"

while read -r file fn ln; do
    [ -n "${file:-}" ] || continue
    if ! grep -qx "$file $fn" "$work/permitted"; then
        echo "      $file:$ln (in $fn)"
        fail=1
    fi
done < "$work/found" > "$work/strays"

if [ -s "$work/strays" ]; then
    echo "FAIL: a special entity is named outside its constructor:"
    cat "$work/strays"
    echo "      call the accessor for it in xpost_memory.h --"
    echo "      xpost_memory_save_stack_adr(mem) and its siblings, which"
    echo "      return the address directly because it cannot be missing"
    fail=1
fi

# Each constructor must still be there, and the header must still hold an
# accessor for every enumerator: otherwise this check passes because the
# thing it guards has moved, not because the rule holds.
while read -r file fn; do
    if ! grep -q "^[A-Za-z_].*[ *]$fn(" "$lib/$file"; then
        echo "FAILURES: $fn is not defined in $file; this check's list of"
        echo "      constructors is stale and it is no longer reading the"
        echo "      code it thinks it is"
        exit 1
    fi
done < "$work/permitted"

nspecial=$(sed -n '/^} Xpost_Memory_Table_Special;/q;p' "$header" \
           | grep -c '^ *XPOST_MEMORY_TABLE_SPECIAL_[A-Z_]*,\{0,1\}$')
naccessor=$(grep -c '^xpost_memory_[a-z_]*_adr(Xpost_Memory_File \*mem)$' "$header")
if [ "$nspecial" -lt 7 ]; then
    echo "FAILURES: the special-entity enum parsed as only $nspecial members;"
    echo "      it moved or changed shape and this check no longer reads it"
    exit 1
fi
# FREE, SAVE_STACK, CONTEXT_LIST, NAME_STACK, NAME_TREE, OPERATOR_TABLE each
# have an address accessor. BOGUS_NAME is an entity number, not a structure,
# and is named only where the name stack is built.
if [ "$naccessor" -ne 6 ]; then
    echo "FAILURES: $naccessor address accessors for $nspecial special"
    echo "      entities; an entity without one has nothing to reach it by"
    echo "      and its callers will go back to the fallible lookup"
    exit 1
fi

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: a special entity's address is obtained off the one path"
    exit 1
fi

echo "SUCCESS ($naccessor accessors; the enumerators named only in xpost_memory.h and $(grep -c . "$work/permitted") constructors)"
exit 0
