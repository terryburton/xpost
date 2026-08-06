#!/bin/sh
# Guard: a cursor is held in a slot at least as wide as the bound it is
# checked against.
#
# The build has two object widths. In the default one `word` is sixteen
# bits, and every slot an object carries -- a composite's size, its
# offset, a dictionary header's entry count -- is that wide. A quantity
# derived from one of those, rather than being one of them, is not: a
# dictionary's record table is DICTABN(sz) = 2*sz+1 records long, so a
# dictionary of more than 32767 slots has a table a word cannot index.
# A cursor of that type walking that table reaches 65535, wraps to zero,
# and the walk never ends. The wide-word build is unaffected, which is
# why such a loop can sit in the tree looking correct.
#
# The rule is therefore about the pair, not about either half: a cursor
# may be compared against its composite's own size field, or against a
# constant, and against nothing else. Where the extent *is* the field,
# the field can hold every value the cursor takes and no wrap is
# possible -- that is what makes array and string forall safe while
# dictionary forall was not. Where the bound is derived, the cursor must
# be counted in a type wide enough for the derived value, and this check
# has no way to know which types those are, so it requires the bound to
# be one it can reason about instead.
#
# What is scanned is every slot a word wide that the code advances:
# a `word`-typed local, or a reference to one of the word-typed fields
# declared in the object and dictionary headers. A slot that is never
# advanced is not a cursor -- `if (str.comp_.sz < needed)` is a capacity
# test, and reporting it would bury the thing being looked for.
#
# tests/narrow_cursors.golden is the register. Every cursor found is
# named there with the class this check derives for it, so a new one
# arrives as a line to write rather than as silence, and one that goes
# away takes a line and the count with it. The class `derived` is the
# unsafe one and the register holds none: zero is the one size at which
# "may not grow" is a rule a check can hold, so a cursor that acquires a
# derived bound fails here whatever the register says.
#
# The second rule is the same statement made about the derived length
# itself: DICTABN yields a value more than twice the size it is computed
# from, so nothing word-wide may receive it or be compared against it.
#
# The third is the same statement made at the other end, where a size is
# stored rather than walked. A constructor handed a size and storing it
# back cannot store more than its caller could name; one that computes a
# different size can, and the field then describes a far smaller
# composite than the allocation behind it -- entries rehashing themselves
# into a table smaller than the one they came from. A constructor that
# changes the size before storing it must hold it to a declared maximum.
#
#   $1  path to the source tree root
set -u
src=${1:?usage: check-narrow-cursors.sh <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
golden="$src/tests/narrow_cursors.golden"
guard_require_file "$golden" "the register of cursors and their bounds"
guard_require_dir "$src/src/lib" "the library source directory"

guard_workdir
trap 'rm -rf "$work"' EXIT INT TERM
guard_mirror_tree "$src"
tree=$mirror
guard_mirror register "$golden"
golden="$mirror/$(basename "$golden")"

fail=0

set -- "$tree"/src/lib/*.c "$tree"/src/lib/*.h "$tree"/src/bin/*.c
for f do
    [ -r "$f" ] || continue
done
guard_c_source "$@" > "$work/code" 2>/dev/null
if [ ! -s "$work/code" ]; then
    echo "FAILURES: no C source could be read under $src/src"
    exit 1
fi

# The word-wide field names, read from the headers that declare them
# rather than listed here: a field added to an object subtype is a slot
# of that width the day it is written, and a list kept beside this check
# would not know about it.
fields=$(guard_c_source "$tree/src/lib/xpost_object.h" "$tree/src/lib/xpost_dict.h" \
    | sed 's/^[^:]*:[0-9]*://' \
    | awk '{ for (i = 1; i < NF; i++)
                 if ($i == "word") {
                     n = $(i + 1); gsub(/[;,*].*/, "", n)
                     if (n ~ /^[a-z_][a-z0-9_]*$/) print n
                 } }' \
    | sort -u | paste -sd, -)
case $fields in
    *sz*off*|*off*sz*) ;;
    *)  echo "FAILURES: the word-typed object fields could not be read from"
        echo "      src/lib/xpost_object.h; the scan below would have no"
        echo "      population and would agree with anything"
        exit 1 ;;
esac

awk -v WF="$fields" '
function trim(s) { gsub(/^[ \t]+/, "", s); gsub(/[ \t]+$/, "", s); return s }

# strip parentheses that wrap the whole expression
function strip_parens(s,   n, pi, pc) {
    s = trim(s)
    while (substr(s, 1, 1) == "(" && substr(s, length(s), 1) == ")") {
        n = 0
        for (pi = 1; pi <= length(s); pi++) {
            pc = substr(s, pi, 1)
            if (pc == "(") n++
            else if (pc == ")") { n--; if (n == 0 && pi < length(s)) return s }
        }
        s = trim(substr(s, 2, length(s) - 2))
    }
    return s
}

# the word-typed field a reference ends in, or ""
function fieldof(e,   m) {
    e = strip_parens(e)
    if (e !~ /^[A-Za-z_][A-Za-z0-9_]*([.]|->)/) return ""
    if (match(e, /([.]|->)[A-Za-z_][A-Za-z0-9_]*$/) == 0) return ""
    m = substr(e, RSTART, RLENGTH)
    sub(/^([.]|->)/, "", m)
    if (m in WORDFIELD) return m
    return ""
}

function is_wordslot(e, key) {
    e = strip_parens(e)
    if (fieldof(e) != "") return 1
    if (e ~ /^[A-Za-z_][A-Za-z0-9_]*$/ && ((key SUBSEP e) in WORDLOCAL)) return 1
    return 0
}

BEGIN {
    FS = ":"
    n = split(WF, a, ",")
    for (i = 1; i <= n; i++) if (a[i] != "") WORDFIELD[a[i]] = 1
}

{
    code = $0
    sub(/^[^:]*:[0-9]*:/, "", code)
    if (code ~ /^[ \t]*#/) next
    lines[++nl] = $1 "\t" $2 "\t" code
}

# the name a function definition introduces: the identifier before the
# parameter list
function fname(decl,   m) {
    decl = trim(decl)
    if (decl ~ /(^|[^A-Za-z0-9_])(if|for|while|switch|return|sizeof)[ \t]*\(/) return ""
    if (match(decl, /[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/) == 0) return ""
    m = substr(decl, RSTART, RLENGTH)
    sub(/[ \t]*\($/, "", m)
    return m
}

function declare(d, key,   parts, i, nm, n2) {
    d = trim(d)
    if (d !~ /(^|[^A-Za-z0-9_])word[ \t]/) return
    sub(/.*(^|[^A-Za-z0-9_])word[ \t]/, "", d)
    n2 = split(d, parts, ",")
    for (i = 1; i <= n2; i++) {
        nm = trim(parts[i])
        sub(/^\**/, "", nm)
        if (nm ~ /^[A-Za-z_][A-Za-z0-9_]*$/) WORDLOCAL[key SUBSEP nm] = 1
    }
}

# the operand ending at position e
function leftop(s, e,   i, ch, d) {
    while (e >= 1 && substr(s, e, 1) ~ /[ \t]/) e--
    if (e < 1) return ""
    i = e; d = 0
    while (i >= 1) {
        ch = substr(s, i, 1)
        if (ch == ")" || ch == "]") { d++; i--; continue }
        if (ch == "(" || ch == "[") { if (d == 0) break; d--; i--; continue }
        if (d > 0) { i--; continue }
        if (ch ~ /[A-Za-z0-9_.]/) { i--; continue }
        if (ch == ">" && i > 1 && substr(s, i - 1, 1) == "-") { i -= 2; continue }
        break
    }
    return trim(substr(s, i + 1, e - i))
}

# the operand starting at position b, up to the end of the comparison
function rightop(s, b,   i, ch, d, n) {
    n = length(s)
    while (b <= n && substr(s, b, 1) ~ /[ \t]/) b++
    i = b; d = 0
    while (i <= n) {
        ch = substr(s, i, 1)
        if (ch == "(" || ch == "[") { d++; i++; continue }
        if (ch == ")" || ch == "]") { if (d == 0) break; d--; i++; continue }
        if (d == 0) {
            if (ch == "," || ch == "?" || ch == ":") break
            if ((ch == "&" || ch == "|") && substr(s, i + 1, 1) == ch) break
            if (ch == ">" && i > 1 && substr(s, i - 1, 1) == "-") { i++; continue }
            if (ch == "<" || ch == ">") break
        }
        i++
    }
    return trim(substr(s, b, i - b))
}

function flip(o) {
    if (o == "<") return ">"
    if (o == "<=") return ">="
    if (o == ">") return "<"
    return "<="
}

# record that fn advances slot
function advance(key, slot) {
    if (slot == "") return
    if (!((key SUBSEP slot) in ADV)) {
        ADV[key SUBSEP slot] = 1
        ADVN++
        ADVFN[ADVN] = key
        ADVSLOT[ADVN] = slot
    }
}

# ++x, x++, x +=, x = x + ...
function scan_advances(key, s,   i, n, ch, lft, rgt) {
    n = length(s)
    for (i = 1; i <= n; i++) {
        ch = substr(s, i, 1)
        if (ch == "+" && substr(s, i + 1, 1) == "+") {
            lft = leftop(s, i - 1)
            if (lft != "" && is_wordslot(lft, key)) { advance(key, lft); continue }
            rgt = rightop(s, i + 2)
            if (rgt != "" && is_wordslot(rgt, key)) advance(key, rgt)
            i++
            continue
        }
        if (ch == "+" && substr(s, i + 1, 1) == "=") {
            lft = leftop(s, i - 1)
            if (lft != "" && is_wordslot(lft, key)) advance(key, lft)
            i++
        }
    }
}

function scan_compares(f, l, key, s,   i, n, ch, nx, pv, op, oplen, lft, rgt, slot, bound) {
    n = length(s)
    for (i = 1; i <= n; i++) {
        ch = substr(s, i, 1)
        if (ch != "<" && ch != ">") continue
        pv = (i > 1) ? substr(s, i - 1, 1) : ""
        nx = (i < n) ? substr(s, i + 1, 1) : ""
        if (ch == "<" && (nx == "<" || pv == "<")) continue
        if (ch == ">" && (nx == ">" || pv == ">" || pv == "-")) continue
        if (pv == "=" || pv == "!") continue
        op = ch; oplen = 1
        if (nx == "=") { op = ch "="; oplen = 2 }
        lft = leftop(s, i - 1)
        rgt = rightop(s, i + oplen)
        if (lft == "" || rgt == "") continue
        if (is_wordslot(lft, key)) { slot = lft; bound = rgt }
        else if (is_wordslot(rgt, key)) { slot = rgt; bound = lft; op = flip(op) }
        else continue
        CMPN++
        CF[CMPN] = f; CL[CMPN] = l; CFN[CMPN] = key
        CSLOT[CMPN] = slot; CBOUND[CMPN] = bound
    }
}

# a bound this check can reason about: a size field, an integer
# literal, or a difference of those. Addition is not allowed -- a sum of
# two sizes is a value a size field need not hold.
function bound_class(b,   parts, i, n, t) {
    b = strip_parens(b)
    if (b == "") return "derived"
    n = 0
    t = b
    gsub(/->/, "@", t)
    n = split(t, parts, "-")
    for (i = 1; i <= n; i++) {
        gsub(/@/, "->", parts[i])
        parts[i] = strip_parens(parts[i])
        if (parts[i] ~ /^[0-9]+$/) continue
        if (parts[i] ~ /^0[xX][0-9a-fA-F]+$/) continue
        if (fieldof(parts[i]) == "sz") continue
        return "derived"
    }
    if (b ~ /^[0-9]+$/ || b ~ /^0[xX][0-9a-fA-F]+$/) return "const"
    return "size"
}

END {
    # pass one: the word-typed locals of every function
    depth = 0; curfn = ""; pend = ""; prevf = ""
    for (k = 1; k <= nl; k++) {
        split(lines[k], p, "\t")
        f = p[1]; c = p[3]
        if (f != prevf) { depth = 0; curfn = ""; pend = ""; prevf = f }
        n = length(c)
        for (i = 1; i <= n; i++) {
            ch = substr(c, i, 1)
            if (ch == "{") {
                if (depth == 0) curfn = fname(pend)
                depth++; pend = ""
            } else if (ch == "}") {
                depth--
                if (depth <= 0) { depth = 0; curfn = "" }
                pend = ""
            } else if (ch == ";") {
                if (depth >= 1 && curfn != "") declare(pend, f SUBSEP curfn)
                pend = ""
            } else pend = pend ch
        }
        pend = pend " "
    }

    # pass two: what each function advances, and what it compares
    depth = 0; curfn = ""; pend = ""; pline = 0; prevf = ""
    for (k = 1; k <= nl; k++) {
        split(lines[k], p, "\t")
        f = p[1]; l = p[2]; c = p[3]
        if (f != prevf) { depth = 0; curfn = ""; pend = ""; prevf = f }
        n = length(c)
        for (i = 1; i <= n; i++) {
            ch = substr(c, i, 1)
            if (ch == "{" || ch == "}" || ch == ";") {
                if (pend != "" && curfn != "") {
                    scan_advances(f SUBSEP curfn, pend)
                    scan_compares(f, pline, f SUBSEP curfn, pend)
                }
                if (ch == "{") { if (depth == 0) curfn = fname(pend); depth++ }
                else if (ch == "}") { depth--; if (depth <= 0) { depth = 0; curfn = "" } }
                pend = ""
            } else {
                if (pend == "") pline = l
                pend = pend ch
            }
        }
        if (pend != "") pend = pend " "
    }

    # every cursor, with the worst class of the bounds it is held to
    for (ai = 1; ai <= ADVN; ai++) {
        key = ADVFN[ai]; slot = ADVSLOT[ai]
        split(key, kp, SUBSEP)
        where = ""; worst = ""; whatb = "-"
        for (m = 1; m <= CMPN; m++) {
            if (CFN[m] != key || CSLOT[m] != slot) continue
            c1 = bound_class(CBOUND[m])
            if (where == "") { where = CF[m] ":" CL[m]; whatb = CBOUND[m] }
            if (c1 == "derived") { worst = "derived"; where = CF[m] ":" CL[m]; whatb = CBOUND[m] }
            else if (worst == "") { worst = c1; whatb = CBOUND[m] }
            else if (worst == "const" && c1 == "size") { worst = "size"; whatb = CBOUND[m] }
        }
        if (worst == "") { worst = "elsewhere"; where = "-"; whatb = "-" }
        rel = kp[1]
        sub(/^.*\/src\//, "src/", rel)
        print worst "\t" rel "\t" kp[2] "\t" slot "\t" where "\t" whatb
    }
}
' "$work/code" | sort -u > "$work/cursors"

if [ ! -s "$work/cursors" ]; then
    echo "FAILURES: the scan found no advanced word-wide slot anywhere in"
    echo "      src/; the object headers or the scan itself have changed"
    echo "      shape and this check is answering about nothing"
    exit 1
fi

# ---- the unsafe class, which must stay empty ----
if grep -q '^derived	' "$work/cursors"; then
    echo "FAILURES: a cursor a word wide is held to a bound that is not the"
    echo "      composite's own size field. In the narrow-word build the"
    echo "      cursor wraps before it reaches such a bound and the loop"
    echo "      does not end. Count it in a type as wide as the bound, or"
    echo "      carry it beside the object rather than in it:"
    awk -F'\t' '$1 == "derived" {
            sub(/^.*\/src\//, "src/", $5)
            print "      " $5 "  " $3 "()  " $4 " against " $6
        }' "$work/cursors"
    fail=1
fi

# ---- the register: every cursor named, with the class derived here ----
grep -vE '^[[:space:]]*(#|$)' "$golden" | tr -d '\r' \
  | grep -vE '^(entries|bound-elsewhere) ' \
  | awk '{ print $1 "\t" $2 "\t" $3 "\t" $4 }' | sort -u > "$work/recorded"

if [ ! -s "$work/recorded" ]; then
    echo "FAILURES: the register at $golden names no cursors"
    exit 1
fi

awk -F'\t' '{ print $1 "\t" $2 "\t" $3 "\t" $4 }' "$work/cursors" | sort -u > "$work/found"

comm -13 "$work/recorded" "$work/found" > "$work/added"
comm -23 "$work/recorded" "$work/found" > "$work/gone"

if [ -s "$work/added" ]; then
    echo "FAILURES: these advance a word-wide slot and are not in the register:"
    awk -F'\t' '{ print "      " $2 "  " $3 "()  " $4 "  (" $1 ")" }' "$work/added"
    echo "      Add them to tests/narrow_cursors.golden in the same commit,"
    echo "      with the class this check derives and the reason the bound"
    echo "      is one the slot can reach."
    fail=1
fi

if [ -s "$work/gone" ]; then
    echo "FAILURES: these are in the register and no longer in the source,"
    echo "      or their class changed:"
    awk -F'\t' '{ print "      " $2 "  " $3 "()  " $4 "  (" $1 ")" }' "$work/gone"
    echo "      Retire the line and the count above it together."
    fail=1
fi

# The size of the set, so that retiring a line is two edits and the
# count going down is what says so.
entries=$(awk '/^entries /{ print $2; found = 1 } END { if (!found) print "" }' "$golden")
have=$(grep -c . "$work/recorded")
case $entries in
    ''|*[!0-9]*)
        echo "FAILURES: the register has no 'entries <n>' line"
        fail=1 ;;
    *)  if [ "$entries" -ne "$have" ]; then
            echo "FAILURES: the register records $entries cursors and holds $have"
            fail=1
        fi ;;
esac

# The cursors bounded by something this check cannot see. Each is a
# claim about a site, counted so that adding one is deliberate.
elsewhere=$(awk '/^bound-elsewhere /{ print $2; found = 1 } END { if (!found) print "" }' "$golden")
haveelse=$(awk -F'\t' '$1 == "elsewhere"' "$work/recorded" | grep -c . || true)
case $elsewhere in
    ''|*[!0-9]*)
        echo "FAILURES: the register has no 'bound-elsewhere <n>' line; the cursors"
        echo "      whose bound this check cannot read would then be uncounted"
        fail=1 ;;
    *)  if [ "$elsewhere" -ne "$haveelse" ]; then
            echo "FAILURES: $haveelse cursors are recorded bounded elsewhere, $elsewhere claimed"
            fail=1
        fi ;;
esac

# ---- the derived table length never reaches a word ----
#
# DICTABN(n) is 2*n+1: more than twice the size it is computed from, and
# the only bound in the tree derived from a size rather than being one.
# Whatever receives it or is compared against it must be wider than the
# field the size came out of.
guard_c_source "$tree"/src/lib/*.c "$tree"/src/lib/*.h \
  | grep -vE ':[[:space:]]*#' | grep 'DICTABN' > "$work/dictabn" || true
if [ ! -s "$work/dictabn" ]; then
    echo "FAILURES: DICTABN is named nowhere in src/lib; the table length is"
    echo "      computed some other way now and this rule holds nothing"
    exit 1
fi

# the identifier on the other side of the assignment or comparison
awk -F: '
{
    code = $0
    sub(/^[^:]*:[0-9]*:/, "", code)
    if (code !~ /DICTABN[ \t]*\(/) next
    # what receives it, or what is compared against it
    lhs = code
    sub(/DICTABN.*/, "", lhs)
    if (match(lhs, /[A-Za-z_][A-Za-z0-9_]*([.]|->|[A-Za-z0-9_])*[ \t]*(=|<|<=|>|>=)[ \t]*$/)) {
        who = substr(lhs, RSTART, RLENGTH)
        sub(/[ \t]*(=|<=|<|>=|>)[ \t]*$/, "", who)
        print $1 "\t" $2 "\t" who
    }
}' "$work/dictabn" > "$work/dictabn_who"

while IFS="$(printf '\t')" read -r df dl who; do
    [ -n "${who:-}" ] || continue
    # a word-typed declaration of that name anywhere in the same file
    if guard_c_source "$df" | grep -qE ":[[:space:]]*(register[[:space:]]+)?word[[:space:]]+[^;]*\<$who\>[ ,;]"; then
        echo "FAILURES: ${df#$tree/}:$dl holds the record table's length in '$who',"
        echo "      which is declared a word. The table is 2*sz+1 records and"
        echo "      the size it is derived from fills the field on its own."
        fail=1
    fi
done < "$work/dictabn_who"

# ---- a composite is born no larger than its size field describes ----
#
# A constructor stores the size it made into a field a word wide. One
# that stores back the size it was handed cannot store more than the
# caller could name; one that computes a different size can, and the
# field then reads back a far smaller composite sitting in a far larger
# allocation. XPOST_OBJECT_COMP_MAX_SZ is what that field holds, so a
# constructor that changes the size before storing it must hold it to
# that. Which constructors those are is read from the tree.
awk '
function trim(s) { gsub(/^[ \t]+/, "", s); gsub(/[ \t]+$/, "", s); return s }
BEGIN { FS = ":" }
{
    code = $0
    sub(/^[^:]*:[0-9]*:/, "", code)
    if (code ~ /^[ \t]*#/) code = ""
    file[NR] = $1
    line[NR] = $2
    text[NR] = code
    n = NR
}
END {
    depth = 0; pend = ""; prevf = ""; cur = ""
    for (k = 1; k <= n; k++) {
        if (file[k] != prevf) { depth = 0; pend = ""; cur = ""; prevf = file[k] }
        c = text[k]
        m = length(c)
        for (i = 1; i <= m; i++) {
            ch = substr(c, i, 1)
            if (ch == "{") {
                if (depth == 0) {
                    hdr = trim(pend)
                    cur = ""
                    if (hdr ~ /[A-Za-z_][A-Za-z0-9_]*_cons_memory[ \t]*\(/ \
                        && hdr !~ /;/) {
                        match(hdr, /[A-Za-z_][A-Za-z0-9_]*_cons_memory/)
                        cur = substr(hdr, RSTART, RLENGTH)
                        curfile = file[k]
                        curline = line[k]
                        # the last parameter of the definition names the size
                        args = hdr
                        sub(/^[^(]*\(/, "", args)
                        sub(/\)[^)]*$/, "", args)
                        na = split(args, ap, ",")
                        p = trim(ap[na])
                        sub(/.*[^A-Za-z0-9_]/, "", p)
                        curparam = p
                        body = ""
                    }
                }
                depth++; pend = ""
                continue
            }
            if (ch == "}") {
                depth--
                if (depth <= 0) {
                    depth = 0
                    if (cur != "") {
                        # does it store back something other than the size
                        # it was handed? A member of that name is not the
                        # parameter, so a store to one does not count.
                        re = "(^|[^A-Za-z0-9_.>])" curparam "[ \t]*(=[^=]|[-+*/]=|\\+\\+)"
                        rea = (curparam != "" && body ~ re) ? "yes" : "no"
                        clamp = (body ~ /_MAX_SZ([^A-Za-z0-9_]|$)/) ? "yes" : "no"
                        print cur "\t" curfile "\t" curline "\t" curparam "\t" rea "\t" clamp
                    }
                    cur = ""
                }
                pend = ""
                continue
            }
            if (depth >= 1 && cur != "") body = body ch
            if (ch == ";") { pend = "" } else pend = pend ch
        }
        pend = pend " "
        if (depth >= 1 && cur != "") body = body " "
    }
}' "$work/code" > "$work/cons"

if [ ! -s "$work/cons" ]; then
    echo "FAILURES: no composite constructor was found in src/; the naming"
    echo "      changed and this rule holds nothing"
    exit 1
fi
ncons=$(grep -c . "$work/cons")
if [ "$ncons" -lt 3 ]; then
    echo "FAILURES: only $ncons composite constructors were found; the string,"
    echo "      array and dictionary constructors are all named *_cons_memory"
    echo "      and this rule is reading a fraction of them"
    exit 1
fi

while IFS="$(printf '\t')" read -r cfn cfile cline cparam crea cclamp; do
    [ -n "${cparam:-}" ] || continue
    [ "$crea" = yes ] || continue
    if [ "$cclamp" != yes ]; then
        echo "FAILURES: ${cfile#$tree/}:$cline $cfn() gives the composite a size"
        echo "      other than the one it was asked for, and holds it to no"
        echo "      declared maximum. The size is stored in a field of its own"
        echo "      width, and a size wider than that field reads back as a far"
        echo "      smaller composite in a far larger allocation."
        fail=1
    fi
done < "$work/cons"

[ "$fail" = 0 ] || exit 1

nfound=$(grep -c . "$work/found")
nelse=$(awk -F'\t' '$1 == "elsewhere"' "$work/found" | grep -c . || true)
echo "SUCCESS ($nfound word-wide cursors, none held to a derived bound; $nelse bounded elsewhere)"
exit 0
