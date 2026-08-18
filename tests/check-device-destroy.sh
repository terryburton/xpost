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
#   A Destroy may hand the whole struct to a function to release instead of
#   releasing member by member, which is what a device does whose Destroy
#   and whose collector-side release are one body. That function is then
#   the one holding the handles, so it is read under the same rule -- the
#   last call other than the accessor pair that takes the struct by
#   address, the release being the last thing a Destroy does with its
#   struct before storing it back. Reading it is what keeps the two paths
#   from drifting: a member the release frees and does not clear fails
#   here whichever path calls it.
#
#   That function may be shared, and then it cannot be the one read: shared
#   code does not know this device's handles, and calls through the page
#   codec the device handed it. So the DEVICE's own release is derived --
#   which member of a descriptor the shared body reaches, where that member
#   sits in the descriptor type, and which function this device put at that
#   position. The one name in that chain is the slot's, and a slot of the
#   published descriptor is contract rather than layout: a device may rename
#   its release, the descriptor may be reordered, and a further shared
#   wrapper may be put between, without any of it being written here.
#
#   Reading only the release, and not every callback the shared body makes,
#   is deliberate. The rule being applied -- a member handed to a call and
#   not cleared is a handle the next Destroy follows -- holds in a body
#   whose calls are releases and nowhere else. A writer hands the same
#   members to calls that use them, and must not clear them.
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
#   rather than being passed over. The funnel's own definition is the one
#   file that names both and brings no table: it names the type because a
#   table is what it is handed, so it is read past rather than held to
#   registering a device it has none of.
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

# The descriptor slot that means "give up what this instance holds". It
# is named here because it is the contract -- src/lib/xpost_dev_driver.h
# declares it, every device fills it, and shared code calls through it --
# in the same way the Destroy method slot is named above. What is NOT
# named is which function any device puts there, or which file the shared
# release is written in: those are derived, so that moving code does not
# move this.
RELEASE_MEMBER=reclaim

# ---- the grounds for trusting a clearing macro ----
#
# A member handed to XPOST_DEV_BUFFER_RECLAIM counts as cleared below,
# which would be a name taken on trust if the macro were not read. It is
# read here, and held to assigning its raster parameter null and its
# ownership parameter zero. A macro that stopped doing either would
# otherwise let every device calling it pass by saying its name.
macro=$(sed -n '/^#define XPOST_DEV_BUFFER_RECLAIM/,/while (0)/p' \
        "$tree/src/lib/xpost_dev_driver.h" | tr -d ' \t\\')
case $macro in
    *"(raster)=NULL"*"(owned)=0"*) ;;
    *)
        echo "FAILURES: XPOST_DEV_BUFFER_RECLAIM is trusted by this check to"
        echo "      clear both of the members handed to it, and what it"
        echo "      expands to does not clear them. Either restore that, or"
        echo "      take it out of the clearing macros this check knows, so"
        echo "      that every device calling it is held to clearing its own."
        fail=1
        ;;
esac

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
        print FILENAME "\t" FNR "\t" s
    }' src/lib/*.c ) > "$work/strings"

# ---- route one: the Destroy slot of every device method table ----
#
# The entry is recognised by its shape in the stripped code -- a brace, the
# two literals the stripping emptied, and the cast the table takes its
# function through -- and the slot it fills is the first literal of the raw
# line. What is read out is which function a file's Destroy is, so every
# file carrying a method table must name exactly one of them and a table
# this cannot read is a failure rather than a device passed over. A file
# may carry more than one table -- a device whose marking methods differ
# with the colour space it was installed for has a table per space -- and
# those tables name one Destroy between them, a device going away being
# the one call that does not depend on the space it painted in.
awk -F'\t' '
    NR == FNR { str[$1 SUBSEP $2] = substr($0, length($1) + length($2) + 3); next }
    {
        code = substr($0, length($1) + length($2) + 3)
        if ($1 != "src/lib/xpost_dev_driver.c" &&
            (code ~ /Xpost_Dev_Method/ || code ~ /xpost_dev_class_install/))
            tab[$1] = 1
        if (code !~ /\{[ \t]*,[ \t]*,[ \t]*\(Xpost_Op_Func\)[ \t]*[A-Za-z_]/) next
        if (str[$1 SUBSEP $2] != "Destroy") next
        match(code, /\(Xpost_Op_Func\)[ \t]*[A-Za-z_][A-Za-z0-9_]*/)
        fn = substr(code, RSTART, RLENGTH)
        sub(/\(Xpost_Op_Func\)[ \t]*/, "", fn)
        if (index(" " got[$1] " ", " " fn " ") == 0)
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
    echo "FAILURES: a device method table names no one Destroy this check can"
    echo "      read:"
    awk '$1 == "BADTABLE" { print "      " $2 " (" $3 " named, expected 1)" }' \
        "$work/route-table"
    echo "      Every compiled device registers a Destroy through its table,"
    echo "      and a device with a table per colour space registers one"
    echo "      Destroy across them."
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
awk -F'\t' '
    NR == FNR { str[$1 SUBSEP $2] = substr($0, length($1) + length($2) + 3); next }
    {
        code = substr($0, length($1) + length($2) + 3)
        if (code !~ /xpost_operator_cons[ \t]*\(/) next
        if (code !~ /\(Xpost_Op_Func\)[ \t]*[A-Za-z_]/) next
        name = str[$1 SUBSEP $2]
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
analyse() {         # <file> <function> [ptr]
    # ptr: the function reaches the struct through a pointer parameter, as
    # a release called from the collector does, rather than holding a copy
    awk -F'\t' -v F="$1" -v FN="$2" -v PTR="${3:-}" '
    BEGIN {
        KW["if"] = 1; KW["for"] = 1; KW["while"] = 1; KW["switch"] = 1
        KW["return"] = 1; KW["sizeof"] = 1; KW["do"] = 1; KW["else"] = 1
        # A macro that clears what it is handed. A member passed to one is
        # cleared as surely as one assigned null here, but only because the
        # macro says so, which is why what each of these expands to is read
        # and held to clearing its arguments before this trusts the name.
        CLEARERS["XPOST_DEV_BUFFER_RECLAIM"] = 1
    }
    $1 == F {
        code = substr($0, length($1) + length($2) + 3)
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
            if (nm in CLEARERS)
                for (k = p + 1; k <= r - 1; k++) clearedat[k] = r
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

        # every identifier a call other than the accessor pair is handed
        # the address of: a struct handed over whole is reached through
        # the callee, so it names the private struct as surely as a member
        # reference does
        for (c = 1; c <= nc; c++) {
            if (CN[c] ~ /_get$/ || CN[c] ~ /_put$/) continue
            args = substr(S, CS[c], CE[c] - CS[c] + 1) " "
            for (p = 1; p <= length(args) - 1; p++) {
                if (substr(args, p, 1) != "&") continue
                q = p + 1
                while (substr(args, q, 1) ~ /[ \t]/) q++
                r = q
                while (substr(args, r, 1) ~ /[A-Za-z0-9_]/) r++
                # the whole struct, not a member of it: the address of a
                # member says nothing about who holds the rest
                if (r > q && substr(args, r, 1) !~ /[.>-]/)
                    addrof[substr(args, q, r - q)] = 1
            }
        }

        # the private struct: the last address-of argument of the accessor
        # the body loads it with, chosen as the one that names a struct the
        # body then reaches members of, or hands over whole
        for (c = 1; c <= nc && !PTR; c++) {
            if (CN[c] !~ /_get$/) continue
            gotget = 1
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
            if (cand != "" && ((cand in base) || (cand in addrof))) { V = cand; break }
        }

        # a Destroy that hands the whole of itself over does not load the
        # struct either: the accessor pair is in the body it hands to. Its
        # struct is then the local whose SIZE goes with its address, which
        # is what taking a private struct somewhere looks like -- and it
        # tells that local apart from a descriptor handed over beside it
        if (V == "" && !PTR) {
            k = 0
            for (v in addrof)
                if (S ~ ("sizeof[ \t]*\\([ \t]*" v "[ \t]*\\)")) { V = v; k++ }
            if (k != 1) V = ""
        }

        # under ptr the struct is not loaded at all: it is the local the
        # pointer parameter is assigned to, which is the one declaration
        # of its kind in a release body
        for (p = bs; p <= be - 1 && PTR && V == ""; p++) {
            if (substr(S, p, 1) != "=") continue
            if (substr(S, p + 1, 1) == "=") continue
            if (substr(S, p - 1, 1) ~ /[!<>=+*\/%&|^-]/) continue
            q = p - 1
            while (q >= bs && substr(S, q, 1) ~ /[ \t]/) q--
            e = q
            while (q >= bs && substr(S, q, 1) ~ /[A-Za-z0-9_]/) q--
            if (e == q) continue
            r = q
            while (r >= bs && substr(S, r, 1) ~ /[ \t]/) r--
            if (substr(S, r, 1) != "*") continue
            V = substr(S, q + 1, e - q)
        }
        if (V == "") { print "NOSTRUCT"; exit }

        # the release the struct is handed to whole, if there is one
        for (c = 1; c <= nc && !PTR; c++) {
            if (CN[c] ~ /_get$/ || CN[c] ~ /_put$/) continue
            args = substr(S, CS[c], CE[c] - CS[c] + 1) " "
            if (args ~ ("&[ \t]*" V "[^A-Za-z0-9_.>-]")) {
                delegate = CN[c]; delegpos = CP[c]
            }
        }

        # every member of it: handed to a call by value, handed by address,
        # or cleared
        for (p = bs; p <= be; p++) {
            if (substr(S, p, 1) !~ /[A-Za-z_]/) continue
            if (p > bs && substr(S, p - 1, 1) ~ /[A-Za-z0-9_.]/) continue
            if (p > bs + 1 && substr(S, p - 2, 2) == "->") continue
            q = p
            while (q <= be && substr(S, q, 1) ~ /[A-Za-z0-9_]/) q++
            id = substr(S, p, q - p)
            sepl = PTR ? 2 : 1
            if (id != V || substr(S, q, sepl) != (PTR ? "->" : ".")) {
                p = q - 1; continue
            }
            r = q + sepl
            while (r <= be && substr(S, r, 1) ~ /[A-Za-z0-9_]/) r++
            mem = substr(S, q + sepl, r - q - sepl)
            if (mem == "") { p = q - 1; continue }

            a = p - 1
            while (a >= bs && substr(S, a, 1) ~ /[ \t]/) a--
            if (substr(S, a, 1) == "&" && substr(S, a - 1, 1) != "&")
                byaddr[mem] = 1
            else if (clearedat[p]) {
                cleared[mem] = 1
                if (clearedat[p] > lastclear) lastclear = clearedat[p]
            }
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

        # a release handed the struct clears through it, so the store must
        # come after that call just as it must come after an assignment
        if (delegpos > lastclear) lastclear = delegpos

        # the store back
        for (c = 1; c <= nc && !PTR; c++) {
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
        if (nh == 0 && ncl == 0 && delegate == "") { print "INERT"; exit }
        if (delegate != "") print "DELEGATE\t" V "\t" delegate
        if (!PTR) {
            # where the body loads nothing, it stores nothing either, and
            # the loading, the release and the storing are all in the body
            # it handed itself to; they are read there instead
            if (!gotget && delegate != "") print "DELEGATED\t" V "\t" delegate
            else if (putpos == 0) { print "NOSTORE\t" V }
            else if (lastclear > putpos) print "LATESTORE\t" V "\t" putline
        }
        print (PTR ? "RSEEN\t" : "SEEN\t") V "\t" nh "\t" ncl
    }' "$work/code"
}

# ---- following a Destroy that hands its struct to shared code ----
#
# Which file a function is written in. A definition begins a line and a
# call is indented, which is what separates them here; a declaration ends
# its line with a semicolon.
definer() {         # <function>
    awk -F'\t' -v FN="$1" '
        {
            code = substr($0, length($1) + length($2) + 3)
            if (code !~ /^[A-Za-z_]/) next
            if (code ~ /;[ \t]*$/) next
            if ((" " code) ~ ("[^A-Za-z0-9_]" FN "[ \t]*\\(")) print $1
        }' "$work/code" | sort -u
}

# What a function's body reaches: the descriptor members it calls
# through, and the functions it hands its own parameters to. Printed as
#   SLOT <type> <member>     a call through a member of a pointer parameter
#   PASSES <function>        a call handed one of this function's parameters
reach() {           # <file> <function>
    awk -F'\t' -v F="$1" -v FN="$2" '
    BEGIN { KW["if"]=1; KW["for"]=1; KW["while"]=1; KW["switch"]=1
            KW["return"]=1; KW["sizeof"]=1; KW["do"]=1; KW["else"]=1 }
    $1 == F {
        code = substr($0, length($1) + length($2) + 3)
        nl++; T[nl] = code
    }
    END {
        for (i = 1; i <= nl && bs == 0; i++) {
            if (T[i] !~ /^[A-Za-z_]/) continue
            if ((" " T[i]) !~ ("[^A-Za-z0-9_]" FN "[ \t]*\\(")) continue
            S = ""; depth = 0; seen = 0; decl = 0; be = 0
            for (j = i; j <= nl && !decl; j++) {
                m = length(T[j])
                for (k = 1; k <= m; k++) {
                    c = substr(T[j], k, 1)
                    S = S c
                    if (c == "{") {
                        if (depth == 0 && !seen) { seen = 1; bs = length(S) + 1 }
                        depth++
                    }
                    else if (c == "}") {
                        depth--
                        if (seen && depth == 0) { be = length(S) - 1; break }
                    }
                    else if (c == ";" && !seen) { decl = 1; break }
                }
                if (be) break
                S = S " "
            }
            if (decl || !be) { bs = 0; be = 0; continue }
        }
        if (!bs || be < bs) exit
        body = substr(S, bs, be - bs + 1)

        # the parameters, and the struct type each pointer one names
        head = substr(S, 1, bs - 2)
        p = index(head, "(")
        d = 0
        for (q = p; q <= length(head); q++) {
            c = substr(head, q, 1)
            if (c == "(") d++
            else if (c == ")") { d--; if (d == 0) break }
        }
        n = split(substr(head, p + 1, q - p - 1), par, ",")
        for (i = 1; i <= n; i++) {
            s = par[i]
            gsub(/^[ \t]+|[ \t]+$/, "", s)
            if (s !~ /\*/) continue
            nm = s; sub(/^.*[*][ \t]*/, "", nm)
            ty = s; sub(/[ \t]*[*].*$/, "", ty)
            sub(/^const[ \t]+/, "", ty)
            gsub(/^[ \t]+|[ \t]+$/, "", ty)
            if (nm ~ /^[A-Za-z_][A-Za-z0-9_]*$/ && ty ~ /^[A-Za-z_]/)
                type[nm] = ty
        }

        # every call through a member of one of them
        for (p = 1; p <= length(body); p++) {
            if (substr(body, p, 2) != "->") continue
            q = p - 1
            while (q >= 1 && substr(body, q, 1) ~ /[A-Za-z0-9_]/) q--
            id = substr(body, q + 1, p - q - 1)
            r = p + 2
            while (r <= length(body) && substr(body, r, 1) ~ /[A-Za-z0-9_]/) r++
            mem = substr(body, p + 2, r - p - 2)
            z = r
            while (substr(body, z, 1) ~ /[ \t]/) z++
            if (substr(body, z, 1) != "(") continue
            if (!(id in type) || mem == "") continue
            if (!seenslot[type[id] SUBSEP mem]++)
                print "SLOT " type[id] " " mem
        }

        # every call handed one of this function`s pointer parameters,
        # which is how the descriptor travels further in
        for (p = 1; p <= length(body); p++) {
            if (substr(body, p, 1) != "(") continue
            q = p - 1
            while (q >= 1 && substr(body, q, 1) ~ /[ \t]/) q--
            e = q
            while (q >= 1 && substr(body, q, 1) ~ /[A-Za-z0-9_]/) q--
            nm = substr(body, q + 1, e - q)
            if (nm == "" || nm ~ /^[0-9]/ || (nm in KW)) continue
            d = 0
            for (r = p; r <= length(body); r++) {
                c = substr(body, r, 1)
                if (c == "(") d++
                else if (c == ")") { d--; if (d == 0) break }
            }
            print "SEQ " ++ord " " nm
            args = " " substr(body, p + 1, r - p - 1) " "
            for (v in type)
                if (args ~ ("[^A-Za-z0-9_]" v "[^A-Za-z0-9_]")) {
                    if (!seenpass[nm]++) print "PASSES " nm
                    break
                }
            p = r
        }
    }' "$work/code"
}

# The members of a descriptor type, in the order they are declared. An
# anonymous typedef, so the run is taken between a struct opening and the
# line that gives it the name.
slot_index() {      # <type> <member>
    ( cd "$tree" && awk -v TY="$1" -v MEM="$2" '
        /^typedef[ \t]+struct/ { n = 0; next }
        /^}[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*;/ {
            name = $0
            sub(/^}[ \t]*/, "", name); sub(/[ \t]*;.*$/, "", name)
            if (name == TY) { for (i = 1; i <= n; i++) if (m[i] == MEM) print i }
            n = 0; next
        }
        /\([ \t]*\*[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*\)/ {
            s = $0
            match(s, /\([ \t]*\*[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*\)/)
            t = substr(s, RSTART, RLENGTH)
            gsub(/[()* \t]/, "", t)
            m[++n] = t
        }' src/lib/*.h )
}

# The function a device puts at a position in such a descriptor.
slot_function() {   # <file> <type> <index>
    awk -F'\t' -v F="$1" -v TY="$2" -v IX="$3" '
        $1 != F { next }
        {
            code = substr($0, length($1) + length($2) + 3)
            if (!open) {
                if (code !~ ("[^A-Za-z0-9_]" TY "[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*=")) next
                if (code !~ /\{/) next
                open = 1
                text = substr(code, index(code, "{") + 1)
                next
            }
            if (code ~ /\}/) { text = text " " substr(code, 1, index(code, "}") - 1); done = 1; exit }
            text = text " " code
        }
        END {
            if (!open) exit
            n = split(text, part, ",")
            if (IX > n) exit
            s = part[IX]
            gsub(/[ \t&]/, "", s)
            if (s ~ /^[A-Za-z_][A-Za-z0-9_]*$/) print s
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

    # The release it hands its struct to, held to the same rule. A Destroy
    # that hands its struct to the shared page retirement is releasing
    # through the codec that retirement was given, and the codec's release
    # is the one the collector runs -- which this device defines, and which
    # is what the members have to be cleared by. So the shared name is
    # followed to that, and everything else is read where it is written.
    deleg=$(printf '%s\n' "$out" | awk -F'\t' '$1 == "DELEGATE" { print $3 }')
    [ -n "$deleg" ] || continue

    relfile=$cfile
    relfn=$deleg
    # A name defined in this device's own file is that one: every device
    # calls its release _reclaim and each is static to its file, so the
    # search only leaves the file when the file has no such function.
    home=$(definer "$deleg")
    if printf '%s\n' "$home" | grep -qxF "$cfile"; then
        home=$cfile
    fi
    nhome=$(printf '%s\n' "$home" | grep -c . || true)
    if [ "$nhome" -ne 1 ]; then
        echo "FAILURES: $cfn() in $cfile hands its private struct to $deleg(),"
        echo "      which $nhome library sources define; this check cannot say"
        echo "      which body releases the handles"
        exit 1
    fi
    if [ "$home" != "$cfile" ]; then
        # The release is shared, so it does not know this device: it calls
        # through the descriptor it was handed, and the DEVICE says which
        # of its own functions fills each slot. Both halves are derived --
        # which member the shared body reaches, where that member sits in
        # the descriptor type, and what this device put there -- so the
        # only name here is the slot's, which is the contract in
        # src/lib/xpost_dev_driver.h rather than anybody's layout.
        shared=$(reach "$home" "$deleg")
        slot=$(printf '%s\n' "$shared" | awk -v M="$RELEASE_MEMBER" \
                    '$1 == "SLOT" && $3 == M { print $2; exit }')
        relcall=$RELEASE_MEMBER
        if [ -z "$slot" ]; then
            # it may hand the descriptor on to something beside it
            for onward in $(printf '%s\n' "$shared" | awk '$1 == "PASSES" { print $2 }'); do
                [ "$(definer "$onward")" = "$home" ] || continue
                slot=$(reach "$home" "$onward" | awk -v M="$RELEASE_MEMBER" \
                            '$1 == "SLOT" && $3 == M { print $2; exit }')
                if [ -n "$slot" ]; then relcall=$onward; break; fi
            done
        fi
        if [ -z "$slot" ]; then
            echo "FAILURES: $cfn() in $cfile hands its private struct to"
            echo "      $deleg(), which reaches no /$RELEASE_MEMBER slot of any"
            echo "      descriptor. A Destroy that hands its struct to shared"
            echo "      code is released by the slot the device fills, and this"
            echo "      check can no longer see which one that is."
            exit 1
        fi
        ix=$(slot_index "$slot" "$RELEASE_MEMBER")
        if [ -z "$ix" ]; then
            echo "FAILURES: $slot has no $RELEASE_MEMBER member, so the slot this"
            echo "      check follows a shared release through is not in the"
            echo "      descriptor it is declared in"
            exit 1
        fi
        relfn=$(slot_function "$cfile" "$slot" "$ix")
        if [ -z "$relfn" ]; then
            echo "FAILURES: $cfile installs no readable $slot, so the function it"
            echo "      releases through cannot be named. A device reaching a"
            echo "      shared release supplies one."
            exit 1
        fi

        # Where the Destroy handed over the whole of itself, the loading,
        # the release and the storing back are all in the shared body, and
        # the order they happen in is the rule this check exists for. It is
        # read there rather than assumed: a store above the release writes
        # back the handles the release is about to give up.
        if printf '%s\n' "$out" | grep -q "^DELEGATED"; then
            gp=$(printf '%s\n' "$shared" | awk '$1 == "SEQ" && $3 ~ /_get$/ { print $2; exit }')
            rp=$(printf '%s\n' "$shared" | awk -v R="$relcall" '$1 == "SEQ" && $3 == R { print $2; exit }')
            pp=$(printf '%s\n' "$shared" | awk '$1 == "SEQ" && $3 ~ /_put$/ { print $2; exit }')
            if [ -z "$gp" ] || [ -z "$rp" ] || [ -z "$pp" ]; then
                echo "FAILURES: $cfn() in $cfile hands the whole of itself to"
                echo "      $deleg(), which does not load its private struct,"
                echo "      release through it and store it back -- one of the"
                echo "      three is not there, so no body performs it"
                fail=1
            elif [ "$gp" -ge "$rp" ] || [ "$rp" -ge "$pp" ]; then
                echo "FAILURES: $deleg() releases and stores a private struct out"
                echo "      of order (load $gp, release $rp, store $pp). What is"
                echo "      stored back must be the struct the release cleared,"
                echo "      or the instance keeps the handles it gave up"
                fail=1
            fi
        fi
    fi

    rout=$(analyse "$relfile" "$relfn" 1)
    if [ -z "$rout" ]; then
        echo "FAILURES: $cfn() in $cfile releases through $relfn(), which this"
        echo "      check cannot read; the handles it clears cannot be held"
        exit 1
    fi
    printf '%s\n' "$rout" \
      | awk -v f="$relfile" -v n="$relfn" '{ print f "\t" n "\t" $0 }' >> "$work/verdicts"
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

guard_held=0
guard_hold "$work/recorded" "$work/population" \
    "in the register and no longer reachable as a Destroy. Retire the
      line and the count above it together, so a population that shrank
      says so:" \
    "Destroys in the tree and not in the register. Add them to
      tests/device_destroy.golden in the same commit:"
guard_hold "$work/recorded-exempt" "$work/exempt" \
    "recorded as exempt and no longer taking the no-op Destroy. Retire
      the line and the count above it together:" \
    "classes taking the no-op Destroy and not recorded as exempt. A
      class releases nothing only because its raster is objects the
      collector owns; say so in tests/device_destroy.golden:"
[ "$guard_held" -eq 0 ] || fail=1

count() {           # <keyword> <how many were derived>
    guard_hold_count "$golden" "$1" "$2" || fail=1
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
