#!/bin/sh
# Guard: a device's Destroy leaves nothing behind that a second Destroy
# would release again.
#
# The rule is stated in src/lib/xpost_dev_driver.h: Destroy is called at
# setpagedevice, at job end, and possibly by the program itself, so it
# clears each resource handle in the private struct and stores the struct
# back, making a repeated Destroy a no-op rather than a double free. A
# device is retired more than once as a matter of course -- setpagedevice
# retires the outgoing one, the interpreter's shutdown retires the current
# one, and PLRM 6.1 makes the device an element of the graphics state, so a
# restore past the change makes a retired device current again -- and every
# one of those paths runs the same method on the same instance.
#
# What is checked, for each Destroy in the population:
#
#   Every member of the private struct it hands to a call by value is
#   cleared before it returns. Cleared means assigned NULL or 0, or handed
#   to a call by address, which is how a callee that clears through the
#   pointer it was given counts. A member handed over and not cleared is a
#   handle the next Destroy follows into freed memory.
#
#   The struct is stored back through the accessor it was loaded with. A
#   Destroy that clears its local copy and does not store it releases the
#   memory and leaves the instance holding the pointers it released.
#
#   The store comes after the clearing, not before it. A store written
#   above the assignments writes back the handles as they arrived.
#
# The population is derived, not listed. Two routes reach a Destroy:
#
#   A compiled device registers one in its method table, so the population
#   is every function the Destroy slot of an Xpost_Dev_Method table names.
#   A file is held to yielding one when it mentions that table type or
#   calls xpost_dev_class_install, which is the one funnel a device's suite
#   reaches a class dictionary through and which takes the table as its
#   argument -- so a device whose Destroy this scan cannot read fails here
#   rather than being passed over.
#
#   A device written in PostScript carries a procedure, and what those hold
#   outside virtual memory they release through an operator. So every
#   class in data/ that defines a /Destroy slot is read, and the C function
#   behind each operator its body names joins the population. A class whose
#   Destroy is the bare `pop` releases nothing and is an exemption, listed
#   in the register with its reason and counted there, so that a new one
#   cannot be added in silence. A class whose Destroy does something else
#   and reaches no operator this can follow fails: it is releasing
#   something and this check cannot say what.
#
# A device whose class dictionary is a copy of another's is not a separate
# entry, because the copy carries the original's Destroy; tests/check-vecbase.sh
# holds the DSC writer to being such a copy of the PDF writer.
#
# The C is read through guard_c_source, so a mention in a comment answers
# nothing. The two places a slot name and an operator name are needed are
# read from the raw line at the same position, because that reading takes
# string literals out -- and a line only counts when its stripped form is
# code, so a comment cannot supply either.
#
# tests/device_destroy.golden is the register. It counts the population and
# the exemptions, and holds at zero the number of Destroys that release
# without clearing: the count is compared, so a regression cannot be
# absorbed by editing the number.
#
#   $1  path to the source tree root
set -u
src=${1:?usage: check-device-destroy.sh <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
golden="$src/tests/device_destroy.golden"
guard_require_file "$golden" "the register of device Destroy methods"
guard_require_file "$src/src/lib/xpost_dev_driver.h" "the device driver contract"

guard_workdir
trap 'rm -rf "$work"' EXIT INT TERM
guard_mirror_tree "$src"
tree=$mirror
guard_mirror register "$golden"
golden="$mirror/$(basename "$golden")"

fail=0

# ---- the C, as code, and the raw line beside it ----
#
# Paths are relative to the mirrored root so that what this reports and
# what the register holds are the tree's own names.
( cd "$tree" && guard_c_source src/lib/*.c ) > "$work/code" 2>/dev/null
if [ ! -s "$work/code" ]; then
    echo "FAILURES: the library C sources could not be read as C"
    exit 1
fi
# the first string literal of every line, taken from the source as written
( cd "$tree" && awk '{
        sub(/\r$/, "")
        s = ""
        if (match($0, /"[^"\\]*"/)) s = substr($0, RSTART + 1, RLENGTH - 2)
        print FILENAME ":" FNR ":" s
    }' src/lib/*.c ) > "$work/strings"

# ---- route one: the Destroy slot of every device method table ----
#
# The entry is recognised by its shape in the stripped code -- a brace, the
# two literals the stripping emptied, and the cast the table takes its
# function through -- and the slot it fills is the first literal of the raw
# line. Every file carrying a method table must yield exactly one, so a
# table this cannot read is a failure rather than a device passed over.
awk -F: '
    NR == FNR { str[$1 ":" $2] = $3; next }
    {
        code = $0
        sub(/^[^:]*:[0-9]*:/, "", code)
        if (code ~ /Xpost_Dev_Method/ || code ~ /xpost_dev_class_install/) tab[$1] = 1
        if (code !~ /\{[ \t]*,[ \t]*,[ \t]*\(Xpost_Op_Func\)[ \t]*[A-Za-z_]/) next
        if (str[$1 ":" $2] != "Destroy") next
        match(code, /\(Xpost_Op_Func\)[ \t]*[A-Za-z_][A-Za-z0-9_]*/)
        fn = substr(code, RSTART, RLENGTH)
        sub(/\(Xpost_Op_Func\)[ \t]*/, "", fn)
        got[$1] = got[$1] " " fn
        print "table " $1 " " fn
    }
    END {
        for (f in tab) {
            n = split(got[f], parts, " ")
            k = 0
            for (i = 1; i <= n; i++) if (parts[i] != "") k++
            if (k != 1) print "BADTABLE " f " " k
        }
    }' "$work/strings" "$work/code" > "$work/route-table"

if grep -q '^BADTABLE' "$work/route-table"; then
    echo "FAILURES: a device method table names no Destroy this check can read:"
    awk '$1 == "BADTABLE" { print "      " $2 " (" $3 " found, expected 1)" }' \
        "$work/route-table"
    echo "      Every compiled device registers a Destroy through its table."
    exit 1
fi
ntable=$(grep -c '^table ' "$work/route-table" || true)
if [ "$ntable" -lt 5 ]; then
    echo "FAILURES: only $ntable compiled device Destroys were found; the"
    echo "      method tables no longer look as this check expects and the"
    echo "      population it would hold is a fraction of the fleet"
    exit 1
fi

# ---- route two: the /Destroy slot of every PostScript device class ----
#
# The body is taken with the braces counted, so a brace inside a string or
# behind a comment does not end it early.
( cd "$tree" && awk '
    function flush(   b) {
        b = body
        gsub(/[ \t]+/, " ", b)
        sub(/^ /, "", b); sub(/ $/, "", b)
        print FILENAME "\t" b
    }
    FNR == 1 { state = 0 }
    {
        sub(/\r$/, "")
        line = $0
        if (state == 0) {
            if (line !~ /^[ \t]*\/Destroy[ \t]*\{/) next
            i = index(line, "{")
            state = 1; depth = 0; instr = 0; sdepth = 0; body = ""
        } else i = 1
        n = length(line)
        while (i <= n) {
            c = substr(line, i, 1)
            if (instr) {
                if (c == "\\") { i += 2; continue }
                if (c == "(") sdepth++
                else if (c == ")") { sdepth--; if (sdepth == 0) instr = 0 }
                body = body c; i++; continue
            }
            if (c == "%") break
            if (c == "(") { instr = 1; sdepth = 1; body = body c; i++; continue }
            if (c == "{") {
                depth++
                if (depth > 1) body = body c
                i++; continue
            }
            if (c == "}") {
                depth--
                if (depth == 0) { flush(); state = 0; break }
                body = body c; i++; continue
            }
            body = body c
            i++
        }
        if (state == 1) body = body " "
    }' data/*.ps ) > "$work/psdestroy"

npsclass=$(grep -c . "$work/psdestroy" || true)
if [ "$npsclass" -lt 4 ]; then
    echo "FAILURES: only $npsclass PostScript device classes define a /Destroy;"
    echo "      the classes no longer look as this check expects"
    exit 1
fi
# A slot whose brace is on the next line is one the extraction above would
# pass over in silence. Counted over the concatenation rather than per file,
# because grep -c prefixes each count with the path and a Windows path
# carries a drive-letter colon.
nslot=$(cat "$tree"/data/*.ps | grep -c '^[ 	]*/Destroy' || true)
if [ "$nslot" -ne "$npsclass" ]; then
    echo "FAILURES: $nslot PostScript /Destroy slots are written and $npsclass"
    echo "      bodies could be read; one is shaped so that this check does"
    echo "      not see it"
    exit 1
fi

# what each of those bodies reaches: the operators it fetches by name
: > "$work/psops"
: > "$work/psexempt"
: > "$work/psopaque"
while IFS="$(printf '\t')" read -r psfile psbody; do
    [ -n "$psfile" ] || continue
    if [ "$psbody" = "pop" ]; then
        echo "$psfile" >> "$work/psexempt"
        continue
    fi
    printf '%s\n' "$psbody" | tr ' ' '\n' > "$work/psbodytok"
    ops=$(awk '
        { tok[++n] = $0 }
        END {
            for (i = 1; i <= n; i++)
                if (tok[i] ~ /internaldict$/ && tok[i + 1] ~ /^\/[.A-Za-z]/ \
                    && tok[i + 2] == "get")
                    print substr(tok[i + 1], 2)
        }' "$work/psbodytok" | sort -u)
    if [ -z "$ops" ]; then
        echo "$psfile" >> "$work/psopaque"
        continue
    fi
    for o in $ops; do
        echo "$psfile $o" >> "$work/psops"
    done
done < "$work/psdestroy"

if [ -s "$work/psopaque" ]; then
    echo "FAILURES: a PostScript device class releases something in its"
    echo "      /Destroy and reaches no operator this check can follow:"
    sed 's/^/      /' "$work/psopaque"
    echo "      Release through an operator, so that what it frees is held to"
    echo "      being cleared and stored back, or make the method the bare"
    echo "      pop of a class with nothing outside virtual memory and record"
    echo "      it as an exemption in tests/device_destroy.golden."
    exit 1
fi

# the C function behind each of those operators
awk -F: '
    NR == FNR { str[$1 ":" $2] = $3; next }
    {
        code = $0
        sub(/^[^:]*:[0-9]*:/, "", code)
        if (code !~ /xpost_operator_cons[ \t]*\(/) next
        if (code !~ /\(Xpost_Op_Func\)[ \t]*[A-Za-z_]/) next
        name = str[$1 ":" $2]
        if (name == "") next
        match(code, /\(Xpost_Op_Func\)[ \t]*[A-Za-z_][A-Za-z0-9_]*/)
        fn = substr(code, RSTART, RLENGTH)
        sub(/\(Xpost_Op_Func\)[ \t]*/, "", fn)
        print name "\t" $1 "\t" fn
    }' "$work/strings" "$work/code" | sort -u > "$work/opmap"

if [ ! -s "$work/opmap" ]; then
    echo "FAILURES: no operator registration could be read from the library"
    echo "      sources; the class route has no population"
    exit 1
fi

: > "$work/route-class"
while read -r psfile op; do
    [ -n "$op" ] || continue
    hit=$(awk -F'\t' -v O="$op" '$1 == O { print $2 " " $3 }' "$work/opmap")
    if [ -z "$hit" ]; then
        echo "FAILURES: $psfile releases through $op, which is registered by no"
        echo "      operator this check can find; the release it performs is"
        echo "      held to nothing"
        exit 1
    fi
    printf '%s\n' "$hit" | while read -r cfile cfn; do
        echo "class $cfile $cfn" >> "$work/route-class"
    done
done < "$work/psops"

sort -u "$work/route-table" "$work/route-class" > "$work/population"
npop=$(grep -c . "$work/population" || true)

# ---- the rule, function by function ----
#
# Each body is read from the definition to its matching brace, flattened so
# that a call spanning lines reads as one, and walked once for the calls it
# makes, the members of the private struct it names, and where each is
# handed over or cleared.
analyse() {         # <file> <function>
    awk -F: -v F="$1" -v FN="$2" '
    BEGIN {
        KW["if"] = 1; KW["for"] = 1; KW["while"] = 1; KW["switch"] = 1
        KW["return"] = 1; KW["sizeof"] = 1; KW["do"] = 1; KW["else"] = 1
    }
    $1 == F {
        code = $0
        sub(/^[^:]*:[0-9]*:/, "", code)
        nl++; L[nl] = $2; T[nl] = code
    }
    END {
        # the definition, and the body between its braces
        for (i = 1; i <= nl && bs == 0; i++) {
            if (T[i] !~ /^[A-Za-z_]/) continue
            # padded, so that the character before the name is always there
            # to be matched: an alternation with a start anchor inside it
            # has been inert in this tree before now
            if ((" " T[i]) !~ ("[^A-Za-z0-9_]" FN "[ \t]*\\(")) continue
            S = ""; bs = 0; be = 0
            depth = 0; seen = 0; decl = 0
            for (j = i; j <= nl && !decl; j++) {
                m = length(T[j])
                for (k = 1; k <= m; k++) {
                    c = substr(T[j], k, 1)
                    S = S c; LN[length(S)] = L[j]
                    if (c == "{") { if (depth == 0 && !seen) { seen = 1; bs = length(S) + 1 } depth++ }
                    else if (c == "}") { depth--; if (seen && depth == 0) { be = length(S) - 1; break } }
                    else if (c == ";" && !seen) { decl = 1; break }
                }
                if (be) break
                S = S " "; LN[length(S)] = L[j]
            }
            if (decl || !be) { bs = 0; be = 0; continue }
        }
        if (!bs || be < bs) { print "NOBODY"; exit }

        # the calls, and the region the arguments of each one occupy
        for (p = bs; p <= be; p++) {
            if (substr(S, p, 1) != "(") continue
            q = p - 1
            while (q >= bs && substr(S, q, 1) ~ /[ \t]/) q--
            e = q
            while (q >= bs && substr(S, q, 1) ~ /[A-Za-z0-9_]/) q--
            nm = substr(S, q + 1, e - q)
            if (nm == "" || nm ~ /^[0-9]/ || (nm in KW)) continue
            d = 0
            for (r = p; r <= be; r++) {
                c = substr(S, r, 1)
                if (c == "(") d++
                else if (c == ")") { d--; if (d == 0) break }
            }
            nc++; CN[nc] = nm; CS[nc] = p + 1; CE[nc] = r - 1; CP[nc] = q + 1
            for (k = p + 1; k <= r - 1; k++) inarg[k] = 1
        }

        # every identifier used as the base of a member reference
        for (p = bs; p <= be; p++) {
            if (substr(S, p, 1) !~ /[A-Za-z_]/) continue
            if (p > bs && substr(S, p - 1, 1) ~ /[A-Za-z0-9_.]/) continue
            if (p > bs + 1 && substr(S, p - 2, 2) == "->") continue
            q = p
            while (q <= be && substr(S, q, 1) ~ /[A-Za-z0-9_]/) q++
            if (substr(S, q, 1) == ".") base[substr(S, p, q - p)] = 1
            p = q - 1
        }

        # the private struct: the last address-of argument of the accessor
        # the body loads it with, chosen as the one that names a struct the
        # body then reaches members of
        for (c = 1; c <= nc; c++) {
            if (CN[c] !~ /_get$/) continue
            args = substr(S, CS[c], CE[c] - CS[c] + 1) " "
            cand = ""
            for (p = 1; p <= length(args) - 1; p++) {
                if (substr(args, p, 1) != "&") continue
                q = p + 1
                while (substr(args, q, 1) ~ /[ \t]/) q++
                r = q
                while (substr(args, r, 1) ~ /[A-Za-z0-9_]/) r++
                if (r > q) cand = substr(args, q, r - q)
            }
            if (cand != "" && (cand in base)) { V = cand; break }
        }
        if (V == "") { print "NOSTRUCT"; exit }

        # every member of it: handed to a call by value, handed by address,
        # or cleared
        for (p = bs; p <= be; p++) {
            if (substr(S, p, 1) !~ /[A-Za-z_]/) continue
            if (p > bs && substr(S, p - 1, 1) ~ /[A-Za-z0-9_.]/) continue
            if (p > bs + 1 && substr(S, p - 2, 2) == "->") continue
            q = p
            while (q <= be && substr(S, q, 1) ~ /[A-Za-z0-9_]/) q++
            id = substr(S, p, q - p)
            if (id != V || substr(S, q, 1) != ".") { p = q - 1; continue }
            r = q + 1
            while (r <= be && substr(S, r, 1) ~ /[A-Za-z0-9_]/) r++
            mem = substr(S, q + 1, r - q - 1)
            if (mem == "") { p = q - 1; continue }

            a = p - 1
            while (a >= bs && substr(S, a, 1) ~ /[ \t]/) a--
            if (substr(S, a, 1) == "&" && substr(S, a - 1, 1) != "&")
                byaddr[mem] = 1
            else if (inarg[p] && !(mem in handed))
                handed[mem] = LN[p]

            b = r
            while (b <= be && substr(S, b, 1) ~ /[ \t]/) b++
            if (substr(S, b, 1) == "=" && substr(S, b + 1, 1) != "=" \
                && substr(S, b - 1, 1) !~ /[!<>=+*\/%&|^-]/) {
                z = b + 1
                while (substr(S, z, 1) ~ /[ \t]/) z++
                rhs = substr(S, z, 5)
                if (rhs ~ /^NULL[^A-Za-z0-9_]/ || rhs ~ /^0[^A-Za-z0-9_.]/) {
                    cleared[mem] = 1
                    if (b > lastclear) lastclear = b
                }
            }
            p = r - 1
        }

        # the store back
        for (c = 1; c <= nc; c++) {
            if (CN[c] !~ /_put$/) continue
            args = substr(S, CS[c], CE[c] - CS[c] + 1) " "
            if (args ~ ("&[ \t]*" V "[^A-Za-z0-9_]")) { putpos = CP[c]; putline = LN[CP[c]]; break }
        }

        nh = 0
        for (m in handed) {
            nh++
            if ((m in cleared) || (m in byaddr)) continue
            print "UNCLEARED\t" m "\t" handed[m]
        }
        ncl = 0
        for (m in cleared) ncl++
        if (nh == 0 && ncl == 0) { print "INERT"; exit }
        if (putpos == 0) { print "NOSTORE\t" V }
        else if (lastclear > putpos) print "LATESTORE\t" V "\t" putline
        print "SEEN\t" V "\t" nh "\t" ncl
    }' "$work/code"
}

: > "$work/verdicts"
while read -r route cfile cfn; do
    [ -n "$cfn" ] || continue
    out=$(analyse "$cfile" "$cfn")
    if [ -z "$out" ]; then
        echo "FAILURES: $cfn() in $cfile produced no reading at all; this check"
        echo "      cannot see the function it is holding"
        exit 1
    fi
    printf '%s\n' "$out" \
      | awk -v f="$cfile" -v n="$cfn" '{ print f "\t" n "\t" $0 }' >> "$work/verdicts"
done < "$work/population"

for kind in NOBODY NOSTRUCT INERT; do
    if grep -q "$(printf '\t')$kind" "$work/verdicts"; then
        case $kind in
            NOBODY)   why="its body could not be read" ;;
            NOSTRUCT) why="it loads no private struct through an accessor" ;;
            INERT)    why="it neither hands a member of its private struct to a call nor clears one" ;;
        esac
        echo "FAILURES: a Destroy this check must hold cannot be read: $why."
        awk -F'\t' -v K="$kind" '$3 == K { print "      " $1 "  " $2 "()" }' \
            "$work/verdicts"
        exit 1
    fi
done

if grep -q "$(printf '\t')UNCLEARED" "$work/verdicts"; then
    echo "FAILURES: a Destroy releases a resource handle and leaves it in the"
    echo "      private struct. The next Destroy -- setpagedevice's, the"
    echo "      shutdown's, or the program's own -- reads it back and releases"
    echo "      it again:"
    awk -F'\t' '$3 == "UNCLEARED" { print "      " $1 ":" $5 "  " $2 "()  " $4 }' \
        "$work/verdicts"
    echo "      Clear the member, or hand it over by address so the callee can."
    fail=1
fi
if grep -q "$(printf '\t')NOSTORE" "$work/verdicts"; then
    echo "FAILURES: a Destroy clears its copy of the private struct and does"
    echo "      not store it back, so the instance keeps the handles it just"
    echo "      released:"
    awk -F'\t' '$3 == "NOSTORE" { print "      " $1 "  " $2 "()  " $4 }' \
        "$work/verdicts"
    fail=1
fi
if grep -q "$(printf '\t')LATESTORE" "$work/verdicts"; then
    echo "FAILURES: a Destroy stores the private struct back above the"
    echo "      assignments that clear it, so what it writes are the handles"
    echo "      as they arrived:"
    awk -F'\t' '$3 == "LATESTORE" { print "      " $1 ":" $5 "  " $2 "()  " $4 }' \
        "$work/verdicts"
    fail=1
fi

nseen=$(awk -F'\t' '$3 == "SEEN"' "$work/verdicts" | grep -c . || true)
if [ "$nseen" -ne "$npop" ]; then
    echo "FAILURES: $npop Destroys are in the population and $nseen were read"
    exit 1
fi

# ---- the register ----
grep -vE '^[[:space:]]*(#|$)' "$golden" | tr -d '\r' \
  | awk '$1 == "table" || $1 == "class" { print $1 " " $2 " " $3 }' \
  | sort -u > "$work/recorded"
grep -vE '^[[:space:]]*(#|$)' "$golden" | tr -d '\r' \
  | awk '$1 == "exempt" { print $2 }' | sort -u > "$work/recorded-exempt"
sort -u "$work/psexempt" > "$work/exempt"

if [ ! -s "$work/recorded" ]; then
    echo "FAILURES: the register at tests/device_destroy.golden names no Destroy"
    exit 1
fi

report() {          # <label> <file of offenders> <advice>
    if [ -s "$2" ]; then
        echo "FAILURES: $1"
        sed 's/^/      /' "$2"
        echo "      $3"
        fail=1
    fi
}
comm -13 "$work/recorded" "$work/population" > "$work/newpop"
comm -23 "$work/recorded" "$work/population" > "$work/lostpop"
report "these Destroys are in the tree and not in the register:" "$work/newpop" \
    "Add them to tests/device_destroy.golden in the same commit."
report "these are in the register and no longer reachable as a Destroy:" \
    "$work/lostpop" \
    "Retire the line and the count above it together, so a population that shrank says so."
comm -13 "$work/recorded-exempt" "$work/exempt" > "$work/newex"
comm -23 "$work/recorded-exempt" "$work/exempt" > "$work/lostex"
report "these classes take the no-op Destroy and are not recorded as exempt:" \
    "$work/newex" \
    "A class releases nothing only because its raster is objects the collector owns; say so in tests/device_destroy.golden."
report "these are recorded as exempt and no longer take the no-op Destroy:" \
    "$work/lostex" \
    "Retire the line and the count above it together."

count() {           # <keyword> <have>
    n=$(awk -v K="$1" '$1 == K && !found { print $2; found = 1 } END { if (!found) print "" }' "$golden")
    case $n in
        ''|*[!0-9]*)
            echo "FAILURES: the register has no '$1 <n>' line"
            fail=1 ;;
        *)  if [ "$n" -ne "$2" ]; then
                echo "FAILURES: the register records $1 $n and holds $2"
                fail=1
            fi ;;
    esac
}
count entries "$(grep -c . "$work/recorded")"
count exemptions "$(grep -c . "$work/recorded-exempt")"

# How many Destroys release without clearing, or clear without storing
# back. The register states it and the reading must equal it -- but the
# number is not a dial: it is zero, and a register that says otherwise is a
# regression written down rather than removed.
unsafe=$(awk '$1 == "unsafe" && !found { print $2; found = 1 } END { if (!found) print "" }' "$golden")
nbad=$(awk -F'\t' '$3 == "UNCLEARED" || $3 == "NOSTORE" || $3 == "LATESTORE"' \
       "$work/verdicts" | grep -c . || true)
if [ "$unsafe" != 0 ]; then
    echo "FAILURES: the register's unsafe count is '$unsafe'. It is zero, and"
    echo "      raising it records a double free rather than removing one."
    fail=1
elif [ "$nbad" -ne "$unsafe" ]; then
    # said above, in detail, one Destroy at a time
    fail=1
fi

[ "$fail" = 0 ] || exit 1

nex=$(grep -c . "$work/exempt")
echo "SUCCESS ($npop device Destroys clear and store back, $nex classes exempt, $ntable through method tables)"
exit 0
