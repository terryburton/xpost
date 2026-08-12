#!/bin/sh
# Guard: virtual memory holds nothing that names this process, except what
# tests/vm_host_state.register admits.
#
# A composite object names its storage by entity number, an index into the
# memory table, and never by address; a handle is likewise a number rather
# than a block's address. So a memory file does not depend on where the
# process put anything, and could be written out and read back somewhere
# else with every reference still resolving. A host address stored in
# virtual memory is what breaks that, and it breaks it in silence: the
# file loads, the address is a number that meant something to the process
# that wrote it, and the interpreter follows it.
#
# tests/check-c-held-objects.sh guards the other direction -- an object
# held in C, which the collector cannot reach. This is its mirror: host
# state reached from virtual memory, which no reader of a memory file can
# resolve. The two read different things and share only the path helper,
# so they are siblings rather than one check.
#
# WHAT THIS CATCHES. It reads the sources, so it catches a host address
# put where virtual memory can hold one:
#
#   a pointer member added to a type virtual memory is read or written
#   as -- the operator table's rows and signatures, a dictionary's header
#   and records, a stack segment, a name-tree node, an object. The object
#   union is held twice over: a member of it given a pointer to carry is
#   wider than the tag word and the two fields beside it, and
#   XPOST_OBJECT_MEMBER_FILLS_UNION fails the build before this runs;
#
#   a type that has pointer members becoming one of those, by a VM
#   pointer being derived as it, by an entity being allocated the size of
#   it, or by one being copied in or out of an entity;
#
#   a kind of block held outside virtual memory and named from it by a
#   handle, which is a number and does carry, but which names nothing in
#   another process and so must be reissued rather than trusted;
#
#   the handle mechanism itself widening to a pointer's width, which is
#   how the entities that now carry a handle held a raw address before.
#
# WHAT IT CANNOT CATCH, plainly. It reads types, not bytes:
#
#   an address written through an integer-shaped field. A pointer cast to
#   an unsigned long long and stored in a field declared as one reads
#   here as an integer, because in C it is one. Nothing at the source
#   level can tell those apart, and neither can a sweep of the bytes: a
#   payload is a run of bytes whose meaning is the type of whoever wrote
#   it, so a scan that reported every aligned word falling in a mapped
#   region would report string contents and object pairs, and the
#   register would fill up with them. A guard that appears to prove more
#   than it does is worse than one with modest, stated reach, so what is
#   claimed here is what the declarations say.
#
#   an address written as raw bytes with no type -- a memcpy into an
#   entity from an untyped buffer -- since there is no declaration to
#   read.
#
#   anything outside src/lib, and anything a build of a different
#   platform compiles in that this tree does not hold.
#
# The bookkeeping outside virtual memory is not in scope and is not
# listed: Xpost_Memory_File holds the base of the mapped region and the
# allocator it was given, and the memory table is a host array. None of
# it is inside the region, and a reader builds all of it as it opens the
# file. Only what lies in the region is at issue here.
#
# Usage: check-vm-host-state.sh <source root>

set -u
src=${1:?usage: check-vm-host-state.sh <source root>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"

guard_workdir
trap 'rm -rf "$work"' EXIT
guard_mirror_tree "$src"
src=$mirror

lib="$src/src/lib"
guard_require_dir "$lib" "the library source directory"
register="$src/tests/vm_host_state.register"
guard_require_file "$register" "the register of what names this process"
handleh="$lib/xpost_handle.h"
guard_require_file "$handleh" "the header stating what a handle is"

fail=0

# Read the sources as C: comments and string literals gone, so prose
# about a pointer is not a pointer and a word inside a message is not a
# declaration. Take the files by name -- a build in the tree leaves
# object files beside them whose debug information answers to the same
# patterns.
# Headers first: what a source file uses without declaring is declared in
# one of them, and the reading below runs forward through the stream.
guard_c_source "$lib"/*.h "$lib"/*.c > "$work/code"
if [ ! -s "$work/code" ]; then
    echo "FAILURES: no C source was read from $lib; this check is reading"
    echo "      the wrong tree"
    exit 1
fi

# Statements, not lines. A derivation written across three lines is one
# expression, and reading it a line at a time leaves the middle line
# looking like a call with no home -- which is how a scan passes over the
# one site it was written for. A definition's return type is on a line of
# its own in this tree, so a statement runs until a parenthesis-free
# semicolon or brace rather than merely until the parentheses close.
# Preprocessor lines stand alone: a macro whose body ends in a closing
# parenthesis would otherwise swallow the file after it.
awk -F: '
{
    file = $1; ln = $2
    code = $0
    sub(/^[^:]*:[^:]*:/, "", code)
    if (file != prevfile) {
        if (depth != 0) { print "\t\t@UNBALANCED@"; exit 1 }
        started = 0; prevfile = file
    }
    hash = (code ~ /^[ \t]*#/)
    if (!started) { sfile = file; sln = ln; buf = ""; started = 1 }
    buf = buf " " code
    n = split(code, ch, "")
    for (i = 1; i <= n; i++) {
        if (ch[i] == "(") depth++
        else if (ch[i] == ")" && depth > 0) depth--
    }
    tail = buf
    sub(/[ \t]*$/, "", tail)
    if (depth == 0 && (hash || tail == "" || tail ~ /[;{}]$/)) {
        sub(/^[ \t]+/, "", buf)
        if (buf != "") print sfile "\t" sln "\t" buf
        started = 0
    }
}
END {
    if (depth != 0) print "\t\t@UNBALANCED@"
    else if (started) { sub(/^[ \t]+/, "", buf); if (buf != "") print sfile "\t" sln "\t" buf }
}
' "$work/code" > "$work/stmts"

if grep -q '@UNBALANCED@' "$work/stmts"; then
    echo "FAILURES: the sources do not read as balanced statements; this"
    echo "      check would scan a joined-up run of the file rather than"
    echo "      the expressions in it"
    exit 1
fi

# ---------------------------------------------------------------- types
#
# The type table: every struct and union the tree defines, with its
# members, whether each member is a pointer, and what type each member
# has. A member is a pointer where its declarator carries a star, or
# where its type is a typedef for one -- an operator's function pointer
# is spelled Xpost_Op_Func and carries no star of its own.
awk '
function emit(type, member, isptr, base) {
    if (member == "") return
    print type "\t" member "\t" isptr "\t" base "\t" deffile[type]
}
{
    line = $0
    match(line, /^[^:]*:[0-9]+:/)
    base = substr(line, 1, RLENGTH); sub(/:[0-9]+:$/, "", base); sub(/^.*\//, "", base)
    line = substr(line, RLENGTH + 1)
    lines[++nl] = base "\t" line
}
END {
    # pointer typedefs, so a member carrying one is seen as a pointer
    for (i = 1; i <= nl; i++) {
        split(lines[i], a, "\t"); code = a[2]
        if (code !~ /^[ \t]*typedef[ \t]/) continue
        if (match(code, /\([ \t]*\*+[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*\)[ \t]*\(/)) {
            s = substr(code, RSTART, RLENGTH); gsub(/[()* \t]/, "", s)
            ptrtypedef[s] = 1
        } else if (match(code, /\*+[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*;/)) {
            s = substr(code, RSTART, RLENGTH); gsub(/[*; \t]/, "", s)
            ptrtypedef[s] = 1
        }
    }
    # the blocks. A definition opens with typedef struct/union or with a
    # named struct, and closes with a brace at the depth it opened at.
    for (i = 1; i <= nl; i++) {
        split(lines[i], a, "\t"); file = a[1]; code = a[2]
        if (!open) {
            if (code ~ /^[ \t]*(typedef[ \t]+)?(struct|union)([ \t]+[A-Za-z_][A-Za-z0-9_]*)?[ \t]*\{?[ \t]*$/ ||
                code ~ /^[ \t]*(typedef[ \t]+)?(struct|union)([ \t]+[A-Za-z_][A-Za-z0-9_]*)?[ \t]*\{/) {
                open = 1; depth = 0; nm = 0; pend = ""; blockfile = file
                tagname = ""
                if (match(code, /(struct|union)[ \t]+[A-Za-z_][A-Za-z0-9_]*/)) {
                    tagname = substr(code, RSTART, RLENGTH); sub(/^[a-z]+[ \t]+/, "", tagname)
                }
            } else continue
        }
        d = 0
        n = split(code, ch, "")
        for (j = 1; j <= n; j++) {
            if (ch[j] == "{") d++
            else if (ch[j] == "}") d--
        }
        # A member runs to its semicolon, which need not be on the line it
        # started on: the name-tree node declares its four fields one to a
        # line, and reading a line at a time finds no member in it at all.
        if (depth == 1 && code !~ /^[ \t]*\}/) {
            pend = pend " " code
            if (index(pend, ";") > 0) { mem[++nm] = pend; pend = "" }
        }
        depth += d
        if (depth <= 0 && code ~ /\}/) {
            name = code
            sub(/^.*\}[ \t]*/, "", name)
            sub(/[ \t]*;.*$/, "", name)
            gsub(/[ \t]/, "", name)
            if (name == "") name = tagname
            if (name != "") {
                deffile[name] = blockfile
                for (k = 1; k <= nm; k++) {
                    m = mem[k]
                    sub(/^[ \t]+/, "", m); sub(/[ \t]+$/, "", m)
                    if (m == "" || m !~ /;/) continue
                    sub(/;.*$/, "", m)
                    # a function-pointer member: the name sits inside the
                    # parentheses, and the identifiers after it are the
                    # parameters rather than members
                    if (match(m, /\([ \t]*\*+[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*\)/)) {
                        s = substr(m, RSTART, RLENGTH); gsub(/[()* \t]/, "", s)
                        emit(name, s, 1, "")
                        continue
                    }
                    # The declared type, then one or more declarators.
                    # Taken a token at a time rather than by one pattern:
                    # a type built of keywords runs on until one that is
                    # not, and a type named by a typedef is the one token,
                    # which a pattern reading "Xpost_Object_Int int_" as a
                    # type ending in int gets wrong.
                    gsub(/[*]/, " * ", m)
                    gsub(/,/, " , ", m)
                    gsub(/\[/, " [", m)
                    ntok = split(m, tok, /[ \t]+/)
                    p = 1
                    while (p <= ntok && (tok[p] == "const" || tok[p] == "volatile")) p++
                    if (p > ntok) continue
                    if (tok[p] == "struct" || tok[p] == "union") {
                        p++
                        if (p > ntok) continue
                        t = tok[p]; p++
                    } else if (tok[p] ~ /^(unsigned|signed|long|short|int|char|double|float)$/) {
                        t = ""
                        while (p <= ntok && tok[p] ~ /^(unsigned|signed|long|short|int|char|double|float)$/) {
                            t = (t == "") ? tok[p] : t " " tok[p]
                            p++
                        }
                    } else { t = tok[p]; p++ }
                    rest = ""
                    while (p <= ntok) { rest = rest " " tok[p]; p++ }
                    cnt = split(rest, decls, ",")
                    for (q = 1; q <= cnt; q++) {
                        dcl = decls[q]
                        isptr = (dcl ~ /\*/) ? 1 : (ptrtypedef[t] ? 1 : 0)
                        sub(/\[.*$/, "", dcl)
                        gsub(/[* \t]/, "", dcl)
                        if (dcl == "") continue
                        emit(name, dcl, isptr, t)
                    }
                }
            }
            open = 0
        }
    }
}
' "$work/code" > "$work/members"

ntypes=$(cut -f1 "$work/members" | sort -u | grep -c .)
if [ "$ntypes" -lt 20 ]; then
    echo "FAILURES: only $ntypes composite types parsed out of the library"
    echo "      sources; the type scan is no longer reading the tree and"
    echo "      would report no pointer anywhere"
    exit 1
fi
# The library is full of structures that do hold pointers -- a file's
# methods, a device's raster, a document's sections -- and none of them
# belongs to virtual memory. That they are seen is what says the reading
# below distinguishes, rather than finding nothing because it can find
# nothing.
nallptr=$(awk -F'\t' '$3 == 1' "$work/members" | grep -c .)
if [ "$nallptr" -lt 20 ]; then
    echo "FAILURES: only $nallptr pointer members were seen in the whole"
    echo "      library; a scan that cannot see a pointer where there are"
    echo "      many would report none in virtual memory and pass"
    exit 1
fi

# ------------------------------------------------- what lives in memory
#
# Three independent readings of what a memory file holds, because each
# misses a different thing on its own:
#
#   derived    a VM pointer is taken as a pointer to it
#   allocated  an entity or a run of the file is allocated the size of it
#   copied     it is handed to xpost_memory_get or xpost_memory_put
#
# A derivation this cannot read is a failure rather than something to
# pass over: a scan that shrugs at the one line it does not understand
# reports about everything else and calls it everything.
#
# The singleton objects are declared by expanding a list through a macro,
# so their declarations are nowhere in the text and are taken from the
# list itself. Nothing else in the library declares a variable that way.
sed -n '/^#define XPOST_OBJECT_SINGLETONS(_)/,/XPOST_OBJECT_SINGLETONS \*\//p' \
    "$lib/xpost_object.h" \
  | sed -n 's/^[ \t]*_(\([A-Za-z_][A-Za-z0-9_]*\)).*/\1\tXpost_Object/p' \
  > "$work/singletons"
if [ ! -s "$work/singletons" ]; then
    echo "FAILURES: the singleton object list was not found in"
    echo "      src/lib/xpost_object.h; the names it declares would read"
    echo "      here as undeclared and this check would refuse the tree"
    exit 1
fi

awk -F'\t' -v singletons="$work/singletons" '
FILENAME == singletons { shared[$1] = $2; next }
# The storage and type qualifiers a declaration may carry, taken off
# whatever else it says. Word boundaries are not written here because
# not every awk has them: the name is bounded by the characters either
# side of it instead, and the pass is repeated because two qualifiers
# in a row share the character between them and one pass takes only the
# first of the pair.
function strip_qualifiers(t,   was) {
    t = " " t " "
    do {
        was = t
        gsub(/[^A-Za-z0-9_](const|static|volatile|register|extern)[^A-Za-z0-9_]/, " ", t)
    } while (t != was)
    return t
}

function trim(s) { gsub(/^[ \t]+|[ \t]+$/, "", s); return s }
function declared_type(id,   k) {
    k = base "|" id
    if (k in decl) return decl[k]
    return (id in shared) ? shared[id] : ""
}
# A declaration or a signature, as against a statement that happens to
# start with a word. A control keyword is what tells the two apart:
# `return f(x);` has the shape of a declaration and is a call.
function keyword(w) {
    return w == "return" || w == "if" || w == "else" || w == "while" ||
           w == "for" || w == "do" || w == "switch" || w == "case" ||
           w == "goto" || w == "sizeof" || w == "typedef"
}
function isdecl(code,   w) {
    w = code; sub(/^[ \t]*/, "", w); sub(/[^A-Za-z0-9_].*$/, "", w)
    if (keyword(w)) return 0
    return code ~ /^((static|const|extern|volatile|register|inline|XPOST_[A-Z_]+)[ \t]+)*(struct[ \t]+|union[ \t]+|unsigned[ \t]+|signed[ \t]+)?[A-Za-z_][A-Za-z0-9_]*([ \t]+(int|char|long|short|double))?([ \t]+\**|[ \t]*\*+)[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*[(;,=[]/
}
# The identifier the last argument of a call names, or "" where the
# argument is not one. `s` starts at the callee, and only the commas at
# the depth its own parenthesis opens separate its arguments.
function lastarg(s,   i, n, d, c, last, arg) {
    n = length(s); d = 0; last = 0
    for (i = 1; i <= n; i++) {
        c = substr(s, i, 1)
        if (c == "(") { d++; if (d == 1) last = i }
        else if (c == ")") { d--; if (d == 0) { arg = substr(s, last + 1, i - last - 1); break } }
        else if (c == "," && d == 1) last = i
    }
    if (arg == "") return ""
    gsub(/^[ \t&*]+|[ \t]+$/, "", arg)
    if (arg !~ /^[A-Za-z_][A-Za-z0-9_]*$/) return ""
    return arg
}
# Record every declarator a declaration or a function signature carries.
# A parameter is a declaration too: the one statement that assigns a VM
# pointer through a parameter is read from the signature it appeared in.
function harvest(code,   c, d, t, id) {
    if (!isdecl(code)) return
    if (code !~ /^((static|const|extern|volatile|register|inline)[ \t]+)*(struct[ \t]+|union[ \t]+|unsigned[ \t]+|signed[ \t]+)?[A-Za-z_][A-Za-z0-9_]*([ \t]+(int|char|long|short|double))?([ \t]+\**|[ \t]*\*+)[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*[(;,=[]/)
        return
    c = "," code
    while (match(c, /[(,][ \t]*((const|static|volatile|register|extern)[ \t]+)*(struct[ \t]+|union[ \t]+)?(unsigned[ \t]+|signed[ \t]+)?[A-Za-z_][A-Za-z0-9_]*([ \t]+(int|char|long|short|double))?([ \t]+\**|[ \t]*\*+)[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*(\[[^]]*\])?[ \t]*[;,=)]/)) {
        d = substr(c, RSTART, RLENGTH)
        c = substr(c, RSTART + RLENGTH - 1)
        sub(/^[(,][ \t]*/, "", d)
        id = d
        sub(/[ \t]*(\[[^]]*\])?[ \t]*[;,=)].*$/, "", id)
        sub(/^.*[ \t*]/, "", id)
        t = d
        sub(/[ \t*]*[A-Za-z_][A-Za-z0-9_]*[ \t]*(\[[^]]*\])?[ \t]*[;,=)].*$/, "", t)
        t = strip_qualifiers(t)
        t = trim(t)
        if (id != "" && t != "" && !keyword(t)) {
            decl[base "|" id] = t
            # a header declares what the whole library shares -- the
            # singleton objects among them -- so a use in a source file
            # that declares no such name of its own falls back to it
            if (base ~ /\.h$/) shared[id] = t
        }
    }
}
{
    base = $1; sub(/^.*\//, "", base); ln = $2; code = $3

    # --- allocated: an entity or a run of the file taken the size of a type
    if (code ~ /(xpost_memory_(file|table)_alloc|xpost_free_(re)?alloc)[ \t]*\(/) {
        c = code
        while (match(c, /sizeof[ \t]*\([ \t]*(struct[ \t]+|union[ \t]+)?[A-Za-z_][A-Za-z0-9_ \t]*\)/)) {
            s = substr(c, RSTART, RLENGTH)
            c = substr(c, RSTART + RLENGTH)
            sub(/^sizeof[ \t]*\([ \t]*/, "", s); sub(/[ \t]*\)$/, "", s)
            print "allocated\t" base ":" ln "\t" trim(s)
        }
    }

    # --- copied: the buffer handed to a payload read or write. It is the
    # call'"'"'s last argument, so the argument list is split at the commas
    # that belong to this call rather than to a call inside it.
    if (match(code, /xpost_memory_(get|put)[ \t]*\(/) && !isdecl(code)) {
        id = lastarg(substr(code, RSTART))
        t = (id == "") ? "" : declared_type(id)
        print "copied\t" base ":" ln "\t" (t == "" ? "@UNREAD@ " code : t)
    }

    # --- derived: a pointer into the memory file taken as a pointer to a type
    if (code ~ /xpost_(vm|ent)_ptr(_checked)?[ \t]*\(/ && base != "xpost_memory.h")
        derive(code)
    harvest(code)
    next
}
function derive(code,   t, id) {
    if (match(code, /\([ \t]*(const[ \t]+)?(struct[ \t]+|union[ \t]+)?(unsigned[ \t]+|signed[ \t]+)?[A-Za-z_][A-Za-z0-9_]*([ \t]+(int|char|long|short|double))?[ \t]*\*+[ \t]*\)[ \t]*xpost_(vm|ent)_ptr/)) {
        t = substr(code, RSTART, RLENGTH)
        sub(/^\([ \t]*/, "", t); sub(/[ \t]*\*+[ \t]*\).*$/, "", t)
        t = strip_qualifiers(t)
        print "derived\t" base ":" ln "\t" trim(t); return
    }
    if (code ~ /memset[ \t]*\([ \t]*xpost_(vm|ent)_ptr/) {
        print "derived\t" base ":" ln "\tBYTES"; return
    }
    if (match(code, /memcpy[ \t]*\([ \t]*&[A-Za-z_][A-Za-z0-9_]*/)) {
        id = substr(code, RSTART, RLENGTH); sub(/^.*&/, "", id)
        t = declared_type(id)
        print "derived\t" base ":" ln "\t" (t == "" ? "@UNREAD@ " code : t); return
    }
    if (code ~ /memcpy[ \t]*\([ \t]*xpost_(vm|ent)_ptr/) {
        if (match(code, /,[ \t]*&[A-Za-z_][A-Za-z0-9_]*/)) {
            id = substr(code, RSTART, RLENGTH); sub(/^.*&/, "", id)
            t = declared_type(id)
            print "derived\t" base ":" ln "\t" (t == "" ? "@UNREAD@ " code : t); return
        }
        print "derived\t" base ":" ln "\tBYTES"; return
    }
    if (match(code, /^(const[ \t]+)?(struct[ \t]+|union[ \t]+)?(unsigned[ \t]+|signed[ \t]+)?[A-Za-z_][A-Za-z0-9_]*([ \t]+(int|char|long|short|double))?[ \t]*\*+[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*=[ \t]*xpost_(vm|ent)_ptr/)) {
        t = substr(code, RSTART, RLENGTH); sub(/\*.*$/, "", t)
        t = strip_qualifiers(t)
        print "derived\t" base ":" ln "\t" trim(t); return
    }
    if (match(code, /(^|[^A-Za-z0-9_])\**[A-Za-z_][A-Za-z0-9_]*[ \t]*=[ \t]*xpost_(vm|ent)_ptr/)) {
        id = substr(code, RSTART, RLENGTH); sub(/[ \t]*=.*$/, "", id)
        gsub(/[^A-Za-z0-9_]/, "", id)
        t = declared_type(id)
        print "derived\t" base ":" ln "\t" (t == "" ? "@UNREAD@ " code : t); return
    }
    if (code ~ /return[ \t]+xpost_(vm|ent)_ptr/) {
        print "derived\t" base ":" ln "\tBYTES"; return
    }
    print "derived\t" base ":" ln "\t@UNREAD@ " code
}
' "$work/singletons" "$work/stmts" > "$work/reach"

if grep -q '@UNREAD@' "$work/reach"; then
    echo "FAIL: virtual memory is reached in a way this check cannot read,"
    echo "      so it cannot say what type is being stored there:"
    sed -n 's/^[a-z]*\t\([^\t]*\)\t@UNREAD@ */      \1: /p' "$work/reach" >&2
    echo "      Passing over it would leave this check reporting about"
    echo "      every other site and calling that all of them."
    exit 1
fi

# What is left is the set of types a memory file holds. Scalars and raw
# bytes have no members and nothing to name a process with.
cut -f3 "$work/reach" | sed 's/[ \t]*$//' | sort -u > "$work/reached"
if [ ! -s "$work/reached" ]; then
    echo "FAILURES: nothing was found to reach virtual memory at all; the"
    echo "      scan above is not reading the sources"
    exit 1
fi

# Close over the members: a stack segment holds objects, an object is a
# union of structures, and a pointer put in any of them is a pointer in
# virtual memory just the same.
: > "$work/vmtypes"
cp "$work/reached" "$work/frontier"
rounds=0
while [ -s "$work/frontier" ]; do
    rounds=$((rounds + 1))
    if [ "$rounds" -gt 20 ]; then
        echo "FAILURES: the type walk did not settle; it is following a"
        echo "      cycle rather than a containment"
        exit 1
    fi
    cat "$work/frontier" >> "$work/vmtypes"
    sort -u "$work/vmtypes" -o "$work/vmtypes"
    awk -F'\t' 'NR == FNR { want[$1] = 1; next }
                ($1 in want) && $4 != "" { t = $4; gsub(/^[ \t]+|[ \t]+$/, "", t); print t }' \
        "$work/frontier" "$work/members" | sort -u > "$work/next"
    comm -23 "$work/next" "$work/vmtypes" > "$work/frontier"
done

# ---------------------------------------------------- the pointers found
awk -F'\t' 'NR == FNR { vm[$1] = 1; next }
            ($1 in vm) && $3 == 1 { print $5 ":" $1 "." $2 }' \
    "$work/vmtypes" "$work/members" | sort -u > "$work/pointers"

# ------------------------------------------------------ the handle kinds
#
# A handle is a number, so it carries; what it names is a block this
# process allocated, so it names nothing in another. Every kind of block
# a handle can stand for is an entry, and the kinds are enumerated in one
# place.
sed -n '/^typedef enum/,/} Xpost_Handle_Kind;/p' "$handleh" \
    | sed 's|/\*.*\*/||' \
    | sed -n 's/^[ \t]*\(XPOST_HANDLE_[A-Z_]*\).*/xpost_handle.h:\1/p' \
    | sort -u > "$work/kinds"
nkinds=$(grep -c . "$work/kinds")
if [ "$nkinds" -lt 1 ]; then
    echo "FAILURES: no handle kinds were read from src/lib/xpost_handle.h;"
    echo "      the enumeration moved and this check no longer sees any"
    echo "      of the state virtual memory names by a handle"
    exit 1
fi

cat "$work/pointers" "$work/kinds" | sort -u > "$work/found"

# ----------------------------------------------------------- the register
sed 's/#.*//' "$register" | awk 'NF >= 2 { print $1 }' | sort -u > "$work/allowed"
if [ ! -s "$work/allowed" ]; then
    echo "FAILURES: the register admits nothing; it is empty or unreadable"
    exit 1
fi
# and the kind each entry gives must be one this check knows what to do
# with, or the register says nothing about how to read the memory file
sed 's/#.*//' "$register" | awk 'NF >= 2 && $2 != "rebuilt" && $2 != "handle" { print $1 " " $2 }' \
    > "$work/badkind"
if [ -s "$work/badkind" ]; then
    echo "FAIL: the register gives a kind that is not rebuilt or handle:" >&2
    sed 's/^/      /' "$work/badkind" >&2
    fail=1
fi

comm -23 "$work/found" "$work/allowed" > "$work/unlisted"
if [ -s "$work/unlisted" ]; then
    echo "FAIL: virtual memory names this process where the register does not" >&2
    echo "      admit it:" >&2
    sed 's/^/      /' "$work/unlisted" >&2
    echo "Say in tests/vm_host_state.register what each names and what a" >&2
    echo "reader of a memory file written elsewhere must do with it --" >&2
    echo "rebuilt or handle -- or store a number virtual memory can carry." >&2
    fail=1
fi

# The register may not outlive what it describes: an entry for something
# that has gone reads as cover for something that has not.
comm -13 "$work/found" "$work/allowed" > "$work/stale"
if [ -s "$work/stale" ]; then
    echo "FAIL: the register admits what is not there:" >&2
    sed 's/^/      /' "$work/stale" >&2
    fail=1
fi

# ------------------------------------------------- the handle's own width
#
# The entities that carry a handle held a raw address before, and what
# separates the two is the width of what the entity holds. A handle is
# the number of an issued block, so it is the width of that number; the
# day it is the width of a pointer, it is a pointer again and every
# reading above still passes, because an entity's payload has no declared
# type for this check to read.
if ! grep -q '^#define XPOST_HANDLE_ENTITY_SIZE (sizeof(unsigned int))$' "$handleh"; then
    echo "FAIL: XPOST_HANDLE_ENTITY_SIZE is not the width of the number a" >&2
    echo "      handle is; an entity carrying a handle is the one place" >&2
    echo "      virtual memory held a host address, and its width is what" >&2
    echo "      says it no longer does" >&2
    fail=1
fi

# and that width must still be what an entity carrying a handle is
# allocated by, or the check above is guarding a macro nothing uses
if ! grep -q 'xpost_memory_table_alloc.*XPOST_HANDLE_ENTITY_SIZE' \
        "$lib"/*.c 2>/dev/null; then
    echo "FAILURES: no entity is allocated at XPOST_HANDLE_ENTITY_SIZE;"
    echo "      the width checked above belongs to nothing and says"
    echo "      nothing about what an entity carrying a handle holds"
    exit 1
fi

[ "$fail" -eq 0 ] || exit 1

nvm=$(grep -c . "$work/vmtypes")
nptr=$(grep -c . "$work/pointers")
echo "SUCCESS ($ntypes composite types read, $nvm of them reached from virtual memory; $nptr host address(es) and $nkinds handle kind(s), each in the register)"
exit 0
