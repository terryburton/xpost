#!/bin/sh
# Which stored fields hold an address into a memory file's arena.
#
# An address is an offset into a region that can move. Today it moves only
# when the file grows, and every pointer is derived afresh through
# xpost_vm_ptr, so a stored offset stays valid. A pass that rearranged the
# arena -- compacting it, so that the live blocks sit together and the tail
# can be handed back -- would move the bytes under those offsets instead,
# and every field holding one would have to be rewritten as part of it.
#
# The set of such fields is small and almost everything references by
# entity number instead. But "almost everything" is the state a register
# exists for: the handful that do hold an address are a fact someone
# established once, and the next field to hold one arrives in a patch that
# nobody thinks to check against it.
#
# WHAT IS DERIVED, and why it is not a list of names. Every integer member
# of every structure the library declares is found, whatever it is called.
# Each must appear in the register below with a disposition, so a member
# added anywhere forces the question "is this an address?" before the build
# is green again. A register that named only the address-holders would be
# satisfied by a new address-holder nobody added to it.
#
#   $1  path to the source root
set -u
src=${1:?usage: check-vm-address-fields.sh <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"

guard_workdir
trap 'rm -rf "$work"' EXIT INT TERM

# Read with carriage returns taken out, for the reason given where
# guard_mirror is defined: a declaration is followed by brace depth and a
# member is recognised by what a line begins with, so a line ending the
# scan does not know takes members out of the population in silence.
guard_mirror lib "$src"/src/lib/*.h "$src"/src/lib/*.c
lib=$mirror
golden=$src/tests/vm_address_fields.golden
guard_require_file "$golden" "the register of stored integers"

# Every integer member of every structure, by brace depth rather than by
# looking for a closing line: a structure carrying a documentation comment
# with a brace in it would otherwise end early and take its members with
# it, which is how this check would come to pass by seeing less.
for f in "$lib"/*.h; do
    [ -e "$f" ] || continue
    awk '
    function emit(decl,   n, i, g) {
        sub(/;.*/, "", decl)
        sub(/^[[:blank:]]*((unsigned|signed|int|long|short|word|dword)[[:blank:]]+)+/, "", decl)
        n = split(decl, f, /[[:blank:]]*,[[:blank:]]*/)
        for (i = 1; i <= n; i++) {
            g = f[i]
            sub(/\[.*/, "", g); gsub(/[ \t*]/, "", g)
            if (g != "") printf "%s %s\n", tag, g
        }
    }
    {
        line = $0; code = ""
        while (length(line)) {                       # strip comments
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

        # A declaration ends the candidacy: a struct named in the parameter
        # list of a prototype is not a structure being opened, and taking it
        # for one latches on to the next brace anywhere below and collects
        # that other function parameters as though they were members.
        if (depth == 0 && index(code, ";")) opening = 0
        if (depth == 0 && code ~ /(^|[^A-Za-z0-9_])struct([[:blank:]]|$)/ && code !~ /\)/ && !index(code, ";")) {
            opening = 1
            # The name the structure is opened with, where it has one. Two
            # structures in one header can declare a member of the same
            # name meaning different things -- a size in one and the arena
            # cursor in the other -- and a register keyed by the header
            # alone gives them one row and one disposition, which is a
            # register that cannot say which of them a rearrangement has
            # to rewrite.
            sname = code
            sub(/^.*[^A-Za-z0-9_]struct/, "", sname)
            sub(/^struct/, "", sname)
            sub(/^[[:blank:]]+/, "", sname)
            sub(/[^A-Za-z0-9_].*/, "", sname)
        }
        if (opening && index(code, "{")) { opening = 0; depth = 1; base = FILENAME
             sub(/^.*\//, "", base)
             # A structure opened without a name -- named after its closing
             # brace, as a typedef -- keeps the header as its tag. Its
             # members are still every one of them derived; they are only
             # gathered under a coarser heading. A nested structure does
             # not reach here, so its members are attributed to the named
             # one that encloses them, which is where a reader looks.
             tag = (sname != "" ? base ":" sname : base)
             sname = ""; next }

        if (depth > 0) {
            # A declaration is gathered until its semicolon rather than
            # read a line at a time, because one naming several members
            # is commonly written with the type on the first line and a
            # member on each of the next. Matching per line sees a first
            # line that does not end in a semicolon and continuation
            # lines that name no type, so it takes none of them -- the
            # members go missing and the count still looks healthy, which
            # is a register passing by seeing less.
            # a preprocessor line is not part of any declaration, and
            # gathering one would put text in front of the type keyword
            # that the declaration is recognised by, losing the member
            # that follows it
            if (code !~ /^[[:blank:]]*#/) pend = pend " " code
            while ((semi = index(pend, ";")) > 0) {
                one = substr(pend, 1, semi)
                pend = substr(pend, semi + 1)
                # A member declaration names no parameter list; a line of
                # a prototype does, so neither is taken for a member.
                # char is excluded: a byte cannot hold an offset, and a
                # declaration of one would otherwise be read as an integer
                # whose name began with the word.
                if (one ~ /^[[:blank:]]*((unsigned|signed|int|long|short|word|dword)[[:blank:]]+)+[A-Za-z_]/ && one !~ /(^|[^A-Za-z0-9_])char([[:blank:]]|$)/ && !index(one, "(") && !index(one, ")"))
                    emit(one)
            }
            no = gsub(/\{/, "{", code); nc = gsub(/\}/, "}", code)
            # a brace either way ends whatever was being gathered: what
            # follows belongs to another structure, not to this one
            if (no || nc) pend = ""
            depth += no - nc
            if (depth <= 0) { depth = 0; pend = "" }
        }
    }' "$f"
done | sort -u > "$work/found"

nfound=$(grep -c . "$work/found")
if [ "$nfound" -lt 40 ]; then
    echo "FAILURES: only $nfound stored integers were found in the library's"
    echo "      headers, which is fewer than this tree has. The scan is not"
    echo "      reading the declarations, and a register held against a"
    echo "      population that has collapsed passes by seeing less"
    exit 1
fi

sed -n 's/^\(address\|count\)  *\([^ ]*\)  *\([^ ]*\).*/\2 \3/p' "$golden" \
    | sort -u > "$work/register"

fail=0

guard_held=0
guard_hold "$work/found" "$work/register" \
    "a stored integer with no disposition. Say in
      tests/vm_address_fields.golden whether it holds an offset into a
      memory file's arena -- which a pass that rearranged the arena
      would have to rewrite -- or something else:" \
    "the register names a member the headers no longer declare. A
      register carrying entries for members that have gone is one
      nobody has read lately:"
[ "$guard_held" -eq 0 ] || fail=1

# One member, one disposition. Both sides of the comparison above are
# deduplicated, so a member given a row as an address and another as a
# count satisfies it either way and the register passes while
# contradicting itself -- which is what happened when three members were
# reclassified and the old rows were not taken out.
sed -n 's/^\(address\|count\)  *\([^ ]*\)  *\([^ ]*\).*/\2 \3/p' "$golden" \
    | sort | uniq -d > "$work/twice"
if [ -s "$work/twice" ]; then
    echo "FAIL: registered more than once. A member has one disposition;"
    echo "      two rows for it means one was left behind when it was"
    echo "      reclassified, and which one is read is nobody's decision:"
    sed 's/^/      /' "$work/twice"
    fail=1
fi

# The dispositions are cross-checked against how the members are used, so
# that the register cannot be wrong in the direction that matters. An
# address is what reaches the offset argument of xpost_vm_ptr, the one call
# that turns an offset into a pointer; a member that gets there and is filed
# as a count is filed wrongly, and a compaction taking the register at its
# word would leave that one behind.
#
# The typed spellings -- xpost_stack_at, xpost_dict_head -- are not asked
# about, because what they take is an entity number and the table turns it
# into an offset first. A member reaching one of those is named by which
# entity it is, which is what a count is.
#
# This finds what it can rather than everything: a member reached through a
# subscript, as the table row is, does not match. It is a floor under the
# register, not a substitute for reading it.
grep -hoE "xpost_vm_ptr[[:blank:]]*\([^,()]*,[[:blank:]]*[^,()]*" \
    "$lib"/*.c "$lib"/*.h 2>/dev/null \
  | sed -E 's/^[^,]*,[[:blank:]]*//' \
  | grep -oE "[A-Za-z_][A-Za-z0-9_]*(->|\.)[A-Za-z_][A-Za-z0-9_]*$" \
  | sed -E 's/^.*(->|\.)//' | sort -u > "$work/used-as-address"

# A name filed as a count somewhere and as an address somewhere else is
# two members that share a spelling, and the call the cross-check found
# belongs to the one already filed as an address. Only a name filed as a
# count and nowhere as an address can be the miscounted one; matching on
# the spelling alone would report the pair every time, which is a check
# that has to be ignored and so is not a check.
awk '$1 == "count" { print $3 }' "$golden" | sort -u > "$work/filed-as-count"
awk '$1 == "address" { print $3 }' "$golden" | sort -u > "$work/filed-as-address"
comm -23 "$work/filed-as-count" "$work/filed-as-address" > "$work/only-a-count"
comm -12 "$work/used-as-address" "$work/only-a-count" > "$work/miscounted"
if [ -s "$work/miscounted" ]; then
    echo "FAIL: filed as a count, but reaches the offset argument of a call"
    echo "      that turns it into a pointer, so it names a place in the"
    echo "      arena and a pass that rearranged one would have to move it:"
    sed 's/^/      /' "$work/miscounted"
    fail=1
fi

# Every address-holder carries a reason, since the disposition is what a
# later compaction would act on and an unexplained one cannot be checked.
awk '$1 == "address" {
        if (NF < 4) { printf "      %s %s\n", $2, $3; bad = 1 }
     }
     END { exit bad ? 1 : 0 }' "$golden" > "$work/unreasoned" || {
    echo "FAIL: an address-holder with no reason recorded:"
    cat "$work/unreasoned"
    fail=1
}

[ "$fail" -eq 0 ] || exit 1

naddr=$(awk '$1 == "address"' "$golden" | grep -c .)
echo "SUCCESS ($nfound stored integers, $naddr of them offsets into an arena)"
