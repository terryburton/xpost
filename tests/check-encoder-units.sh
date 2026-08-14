#!/bin/sh
# Guard: an encoder gives up a part-finished unit before it offers it to
# the target, not after the target has taken it.
#
# Every coding that gathers input into a unit wider than a byte carries
# the part of one it has not completed from write to write and counts it:
# the ASCII85 group of four, the RunLength literal block, the bit buffer
# the LZW and CCITTFax coders share, the CCITTFax scanline. The code
# around each counter is written for a width -- the padding shift
# subtracts the bit count from eight, the ASCII85 tail shifts by eight
# times four less the byte count, the scanline byte count indexes a
# buffer holding exactly one row.
#
# A target may refuse a byte. If the counter returns to the start of the
# unit only once the target has taken the whole of it, the refusal
# leaves the unit open and the counter free to run on for as long as the
# program keeps writing -- which it may, and then close the stream. Past
# the end of a unit the padding shift count goes negative, the tail
# shift's exponent runs to nonsense, and the scanline byte lands past
# the end of the row: undefined behaviour, a read off the end of a
# stack array, and a write off the end of a heap one.
#
# What is checked, for each counter and each function that returns it to
# the start of a unit: between the point the counter was last advanced
# in that function -- or the start of the function, when it arrives
# already advanced -- and the point it is returned to the start, no
# `return` may follow a write. A write there is a refusal the counter's
# reset does not survive. A call that itself returns the counter to the
# start is not such a write; what it left behind is already accounted.
#
# The counters this applies to are the ones the code around them reads
# as a width: a counter used as a shift count, as a subscript, or
# subtracted from a constant. `col`, which only counts to a line break,
# and `reccnt`, which only marks a record boundary, are read for neither
# and are not here -- a check reporting them would bury the ones that
# matter. Which counters those are is derived from the source, not
# listed: an existing counter that acquires such a use joins the
# population that day.
#
# tests/encoder_units.golden is the register, so that a counter which
# leaves the population -- because the arithmetic that read it as a
# width moved off the field -- says so with a line and a count rather
# than by this check quietly holding one thing fewer.
#
#   $1  path to the source tree root
set -u
src=${1:?usage: check-encoder-units.sh <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
golden="$src/tests/encoder_units.golden"
guard_require_file "$golden" "the register of part-unit counters"
guard_require_file "$src/src/lib/xpost_file.c" "the file and filter module"

guard_workdir
trap 'rm -rf "$work"' EXIT INT TERM
guard_mirror_tree "$src"
tree=$mirror
guard_mirror register "$golden"
golden="$mirror/$(basename "$golden")"

fail=0
guard_c_source "$tree/src/lib/xpost_file.c" > "$work/code" 2>/dev/null
if [ ! -s "$work/code" ]; then
    echo "FAILURES: src/lib/xpost_file.c could not be read as C"
    exit 1
fi

# ---- the encoder structs, and the scalar counters they carry ----
awk '
{
    code = substr($0, length($1) + length($2) + 3)
    if (inb) {
        if (code ~ /^\}[ \t]*Xpost_[A-Za-z0-9_]*Enc[A-Za-z0-9_]*[ \t]*;/) {
            name = code
            sub(/^\}[ \t]*/, "", name); sub(/[ \t]*;.*/, "", name)
            for (i = 1; i <= nb; i++) print name "\t" buf[i]
            inb = 0
        } else if (code ~ /^\}/) inb = 0
        else {
            f = code
            sub(/^[ \t]+/, "", f); sub(/[ \t]*$/, "", f)
            # scalar integer members only: no pointers, no arrays, no
            # aggregates. What is counted is counted in one of these.
            if (f ~ /^(unsigned[ \t]+|signed[ \t]+)?(int|short|long|char)[ \t]+[A-Za-z_][A-Za-z0-9_, \t]*;$/ \
                && f !~ /[*\[]/) {
                d = f
                sub(/^(unsigned[ \t]+|signed[ \t]+)?(int|short|long|char)[ \t]+/, "", d)
                sub(/;$/, "", d)
                nn = split(d, parts, ",")
                for (j = 1; j <= nn; j++) {
                    p = parts[j]
                    gsub(/[ \t]/, "", p)
                    if (p ~ /^[A-Za-z_][A-Za-z0-9_]*$/) buf[++nb] = p
                }
            }
        }
        next
    }
    if (code ~ /^typedef[ \t]+struct[ \t]*$/) { inb = 1; nb = 0 }
}' FS="$guard_tab" "$work/code" | sort -u > "$work/fields"

if [ ! -s "$work/fields" ]; then
    echo "FAILURES: no encoder struct was found in src/lib/xpost_file.c;"
    echo "      the typedefs changed shape and this check has no population"
    exit 1
fi

# ---- which of them the code reads as a width ----
#
# A counter matters here when something around it takes it for a
# quantity of a known size: a shift count, a subscript, or a number
# taken away from a constant. Those are the reads that go wrong when
# the counter has run past the unit it counts.
cut -f2 "$work/fields" | sort -u > "$work/fieldnames"
: > "$work/bearing"
while read -r fld; do
    [ -n "$fld" ] || continue
    if awk -v F="$fld" '
        function rightop(s, b,   i, ch, d, n) {
            n = length(s)
            while (b <= n && substr(s, b, 1) ~ /[ \t]/) b++
            i = b; d = 0
            while (i <= n) {
                ch = substr(s, i, 1)
                if (ch == "(" || ch == "[") { d++; i++; continue }
                if (ch == ")" || ch == "]") { if (d == 0) break; d--; i++; continue }
                if (d == 0 && (ch == "," || ch == ";" || ch == "?")) break
                i++
            }
            return substr(s, b, i - b)
        }
        {
            code = substr($0, length($1) + length($2) + 3)
            if (code ~ /^[ \t]*#/) next
            ref = "->" F
            if (index(code, ref) == 0) next
            # a shift count
            n = length(code)
            for (i = 1; i < n; i++) {
                two = substr(code, i, 2)
                if (two == "<<" || two == ">>") {
                    r = rightop(code, i + 2)
                    if (r ~ ("->" F "([^A-Za-z0-9_]|$)")) { print "yes"; exit }
                }
            }
            # a subscript
            d = 0; start = 0
            for (i = 1; i <= n; i++) {
                ch = substr(code, i, 1)
                if (ch == "[") { if (d == 0) start = i; d++ }
                else if (ch == "]") {
                    d--
                    if (d == 0 && start) {
                        inner = substr(code, start + 1, i - start - 1)
                        if (inner ~ ("->" F "([^A-Za-z0-9_]|$)")) { print "yes"; exit }
                    }
                }
            }
            # taken away from a constant
            if (code ~ ("[0-9][ \t]*-[ \t]*[A-Za-z_][A-Za-z0-9_]*->" F "([^A-Za-z0-9_]|$)")) {
                print "yes"; exit
            }
        }' FS="$guard_tab" "$work/code" | grep -q yes; then
        echo "$fld" >> "$work/bearing"
    fi
done < "$work/fieldnames"

if [ ! -s "$work/bearing" ]; then
    echo "FAILURES: not one encoder counter is read as a shift count, a"
    echo "      subscript or a number taken from a constant; the codings"
    echo "      no longer look as this check expects and it holds nothing"
    exit 1
fi

# ---- which functions write to the target ----
#
# Reached by closure rather than by a list: a helper that writes through
# another helper is a write at the site that calls it.
awk '
{
    code = substr($0, length($1) + length($2) + 3)
    if (code ~ /^[ \t]*#/) code = ""
    text[++n] = code
}
END {
    depth = 0; pend = ""; cur = ""
    for (k = 1; k <= n; k++) {
        c = text[k]
        m = length(c)
        for (i = 1; i <= m; i++) {
            ch = substr(c, i, 1)
            if (ch == "{") {
                if (depth == 0) {
                    hdr = pend
                    gsub(/^[ \t]+/, "", hdr)
                    cur = ""
                    if (hdr !~ /;/ && hdr !~ /(^|[^A-Za-z0-9_])(if|for|while|switch|else|do|return)[ \t]*\(/ \
                        && match(hdr, /[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/)) {
                        cur = substr(hdr, RSTART, RLENGTH)
                        sub(/[ \t]*\($/, "", cur)
                    }
                }
                depth++; pend = ""; continue
            }
            if (ch == "}") {
                depth--
                if (depth <= 0) { depth = 0; cur = "" }
                pend = ""; continue
            }
            if (depth >= 1 && cur != "") body[cur] = body[cur] substr(c, i, 1)
            if (ch == ";") pend = ""; else pend = pend ch
        }
        pend = pend " "
        if (depth >= 1 && cur != "") body[cur] = body[cur] " "
    }
    for (f in body) print f "\t" body[f]
}' FS="$guard_tab" "$work/code" > "$work/bodies"

if [ ! -s "$work/bodies" ]; then
    echo "FAILURES: no function body could be read from src/lib/xpost_file.c"
    exit 1
fi

cut -f1 "$work/bodies" | sort -u > "$work/allfn"
echo xpost_file_putc > "$work/emitters"
while : ; do
    before=$(grep -c . "$work/emitters")
    awk -F'\t' '
        NR == FNR { em[$1] = 1; next }
        {
            for (e in em) {
                if (index($2, e "(") > 0 && $1 != e) { print $1; break }
            }
        }' "$work/emitters" "$work/bodies" >> "$work/emitters"
    sort -u "$work/emitters" -o "$work/emitters"
    after=$(grep -c . "$work/emitters")
    [ "$after" -gt "$before" ] || break
done

nem=$(grep -c . "$work/emitters")
nenc=$(grep -c 'enc' "$work/emitters" || true)
if [ "$nem" -lt 5 ] || [ "$nenc" -lt 1 ]; then
    echo "FAILURES: the closure of functions that write to the target reached"
    echo "      $nem functions, $nenc of them an encoder's; it was not computed"
    echo "      and every window below would look free of writes"
    exit 1
fi

# ---- the windows ----
awk -v BEARING="$(paste -sd, - < "$work/bearing")" -v EMIT="$(paste -sd, - < "$work/emitters")" '
BEGIN {
    FS = "\t"
    n = split(BEARING, b, ","); for (i = 1; i <= n; i++) if (b[i] != "") BEAR[b[i]] = 1
    n = split(EMIT, e, ","); for (i = 1; i <= n; i++) if (e[i] != "") EM[e[i]] = 1
}
{
    code = substr($0, length($1) + length($2) + 3)
    if (code ~ /^[ \t]*#/) code = ""
    file[++n2] = $1; lno[n2] = $2; text[n2] = code
}
function ev(kind, what, pos) {
    NE++
    EK[NE] = kind; EW[NE] = what; EP[NE] = pos; EFN[NE] = cur; EL[NE] = curl
}
END {
    depth = 0; pend = ""; cur = ""
    for (k = 1; k <= n2; k++) {
        c = text[k]
        m = length(c)
        # events on this line, in the order they are written
        if (cur != "") {
            curl = lno[k]
            for (i = 1; i <= m; i++) {
                # a return
                if (substr(c, i, 6) == "return" \
                    && (i == 1 || substr(c, i - 1, 1) !~ /[A-Za-z0-9_]/) \
                    && substr(c, i + 6, 1) !~ /[A-Za-z0-9_]/)
                    ev("RET", "", k * 100000 + i)
                # a call to something that writes
                if (substr(c, i, 1) == "(" && i > 1) {
                    j = i - 1
                    while (j >= 1 && substr(c, j, 1) ~ /[ \t]/) j--
                    e2 = j
                    while (j >= 1 && substr(c, j, 1) ~ /[A-Za-z0-9_]/) j--
                    nm = substr(c, j + 1, e2 - j)
                    if (nm != "" && (nm in EM)) ev("EMIT", nm, k * 100000 + i)
                }
                # the counter advanced, or returned to the start
                if (substr(c, i, 2) == "->") {
                    j = i + 2
                    while (j <= m && substr(c, j, 1) ~ /[A-Za-z0-9_]/) j++
                    f = substr(c, i + 2, j - i - 2)
                    if (f in BEAR) {
                        rest = substr(c, j)
                        sub(/^[ \t]*/, "", rest)
                        if (rest ~ /^\+\+/ || rest ~ /^\+=/)
                            ev("ADV", f, k * 100000 + i)
                        else if (rest ~ /^=[ \t]*-?[0-9]+/ || rest ~ /^-=/)
                            ev("CLR", f, k * 100000 + i)
                        # a pre-increment written in front of the name
                        pre = substr(c, 1, i - 1)
                        if (pre ~ /\+\+[ \t]*[A-Za-z_][A-Za-z0-9_]*$/)
                            ev("ADV", f, k * 100000 + i)
                    }
                }
            }
        }
        for (i = 1; i <= m; i++) {
            ch = substr(c, i, 1)
            if (ch == "{") {
                if (depth == 0) {
                    hdr = pend
                    gsub(/^[ \t]+/, "", hdr)
                    cur = ""
                    if (hdr !~ /;/ && hdr !~ /(^|[^A-Za-z0-9_])(if|for|while|switch|else|do|return)[ \t]*\(/ \
                        && match(hdr, /[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/)) {
                        cur = substr(hdr, RSTART, RLENGTH)
                        sub(/[ \t]*\($/, "", cur)
                        curstart = NE + 1
                    }
                }
                depth++; pend = ""; continue
            }
            if (ch == "}") {
                depth--
                if (depth <= 0) { depth = 0; cur = "" }
                pend = ""; continue
            }
            if (ch == ";") pend = ""; else pend = pend ch
        }
        pend = pend " "
    }

    # for every reset, the window back to the last advance in the same
    # function, or to the start of it
    for (i = 1; i <= NE; i++) {
        if (EK[i] != "CLR") continue
        f = EW[i]; fn = EFN[i]
        start = 0
        for (j = i - 1; j >= 1; j--) {
            if (EFN[j] != fn) break
            if (EK[j] == "ADV" && EW[j] == f) { start = j; break }
        }
        if (start == 0) {
            for (j = i - 1; j >= 1 && EFN[j] == fn; j--) start = j
            start = (start ? start - 1 : i - 1)
        }
        wrote = 0; wrotewhat = ""
        for (j = start + 1; j < i; j++) {
            if (EFN[j] != fn) continue
            if (EK[j] == "EMIT") {
                # a call that itself returns this counter to the start
                # leaves nothing this one has to survive
                if (CLEARS[EW[j] SUBSEP f]) wrote = 0
                else { wrote = 1; wrotewhat = EW[j] }
            } else if (EK[j] == "RET" && wrote) {
                print "FAIL\t" fn "\t" f "\t" EL[j] "\t" wrotewhat
                wrote = 0
            }
        }
        print "CLR\t" fn "\t" f
    }
    for (i = 1; i <= NE; i++)
        if (EK[i] == "ADV") print "ADV\t" EFN[i] "\t" EW[i]
}' "$work/code" > "$work/events0"

# A reset inside a function is a fact the windows above need, and it is
# only known once every function has been read: run the scan twice, the
# second time knowing which function resets which counter.
awk -F'\t' '$1 == "CLR" { print $2 "\t" $3 }' "$work/events0" | sort -u > "$work/clearers"
clr=$(awk -F'\t' '{ printf "%s%s:%s", (NR > 1 ? "," : ""), $1, $2 }' "$work/clearers")

awk -v BEARING="$(paste -sd, - < "$work/bearing")" \
    -v EMIT="$(paste -sd, - < "$work/emitters")" \
    -v CLEARERS="$clr" '
BEGIN {
    FS = "\t"
    n = split(BEARING, b, ","); for (i = 1; i <= n; i++) if (b[i] != "") BEAR[b[i]] = 1
    n = split(EMIT, e, ","); for (i = 1; i <= n; i++) if (e[i] != "") EM[e[i]] = 1
    n = split(CLEARERS, cc, ",")
    for (i = 1; i <= n; i++) if (cc[i] != "") { split(cc[i], q, ":"); CLEARS[q[1] SUBSEP q[2]] = 1 }
}
{
    code = substr($0, length($1) + length($2) + 3)
    if (code ~ /^[ \t]*#/) code = ""
    lno[++n2] = $2; text[n2] = code
}
function ev(kind, what, pos) {
    NE++
    EK[NE] = kind; EW[NE] = what; EP[NE] = pos; EFN[NE] = cur; EL[NE] = curl
}
END {
    depth = 0; pend = ""; cur = ""
    for (k = 1; k <= n2; k++) {
        c = text[k]
        m = length(c)
        if (cur != "") {
            curl = lno[k]
            for (i = 1; i <= m; i++) {
                if (substr(c, i, 6) == "return" \
                    && (i == 1 || substr(c, i - 1, 1) !~ /[A-Za-z0-9_]/) \
                    && substr(c, i + 6, 1) !~ /[A-Za-z0-9_]/)
                    ev("RET", "", i)
                if (substr(c, i, 1) == "(" && i > 1) {
                    j = i - 1
                    while (j >= 1 && substr(c, j, 1) ~ /[ \t]/) j--
                    e2 = j
                    while (j >= 1 && substr(c, j, 1) ~ /[A-Za-z0-9_]/) j--
                    nm = substr(c, j + 1, e2 - j)
                    if (nm != "" && (nm in EM)) ev("EMIT", nm, i)
                }
                if (substr(c, i, 2) == "->") {
                    j = i + 2
                    while (j <= m && substr(c, j, 1) ~ /[A-Za-z0-9_]/) j++
                    f = substr(c, i + 2, j - i - 2)
                    if (f in BEAR) {
                        rest = substr(c, j)
                        sub(/^[ \t]*/, "", rest)
                        pre = substr(c, 1, i - 1)
                        if (rest ~ /^\+\+/ || rest ~ /^\+=/ \
                            || pre ~ /\+\+[ \t]*[A-Za-z_][A-Za-z0-9_]*$/)
                            ev("ADV", f, i)
                        else if (rest ~ /^=[ \t]*-?[0-9]+/ || rest ~ /^-=/)
                            ev("CLR", f, i)
                    }
                }
            }
        }
        for (i = 1; i <= m; i++) {
            ch = substr(c, i, 1)
            if (ch == "{") {
                if (depth == 0) {
                    hdr = pend
                    gsub(/^[ \t]+/, "", hdr)
                    cur = ""
                    if (hdr !~ /;/ && hdr !~ /(^|[^A-Za-z0-9_])(if|for|while|switch|else|do|return)[ \t]*\(/ \
                        && match(hdr, /[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/)) {
                        cur = substr(hdr, RSTART, RLENGTH)
                        sub(/[ \t]*\($/, "", cur)
                    }
                }
                depth++; pend = ""; continue
            }
            if (ch == "}") {
                depth--
                if (depth <= 0) { depth = 0; cur = "" }
                pend = ""; continue
            }
            if (ch == ";") pend = ""; else pend = pend ch
        }
        pend = pend " "
    }

    for (i = 1; i <= NE; i++) {
        if (EK[i] != "CLR") continue
        f = EW[i]; fn = EFN[i]
        start = 0
        for (j = i - 1; j >= 1; j--) {
            if (EFN[j] != fn) break
            if (EK[j] == "ADV" && EW[j] == f) { start = j; break }
        }
        if (start == 0) {
            start = i - 1
            while (start >= 1 && EFN[start] == fn) start--
        }
        wrote = 0; wrotewhat = ""
        for (j = start + 1; j < i; j++) {
            if (EFN[j] != fn) continue
            if (EK[j] == "EMIT") {
                if ((EW[j] SUBSEP f) in CLEARS) wrote = 0
                else { wrote = 1; wrotewhat = EW[j] }
            } else if (EK[j] == "RET" && wrote) {
                print "FAIL\t" fn "\t" f "\t" EL[j] "\t" wrotewhat
                wrote = 0
            }
        }
        print "SEEN\t" fn "\t" f
    }
}' "$work/code" > "$work/events"

if ! grep -q '^SEEN' "$work/events"; then
    echo "FAILURES: no counter is returned to the start of a unit anywhere"
    echo "      in the encoders; the scan found nothing to hold"
    exit 1
fi

if grep -q '^FAIL' "$work/events"; then
    echo "FAILURES: a part-finished unit is given up only once the target"
    echo "      has taken it. A refusal there leaves the unit open and the"
    echo "      counter running on past the width the code around it reads"
    echo "      it as -- a negative shift count, or a byte past the end of"
    echo "      the buffer the counter indexes. Give up the unit before"
    echo "      offering it:"
    awk -F'\t' '$1 == "FAIL" {
            print "      src/lib/xpost_file.c:" $4 "  " $2 "()  " $3
            print "        returns on a refusal from " $5 "() before returning "$3" to zero"
        }' "$work/events" | sort -u
    fail=1
fi

# ---- the register ----
sort -u "$work/bearing" > "$work/bearing_s"
grep -vE '^[[:space:]]*(#|$)' "$golden" | tr -d '\r' \
  | grep -vE '^entries ' | awk '{ print $1 }' | sort -u > "$work/recorded"

if [ ! -s "$work/recorded" ]; then
    echo "FAILURES: the register at $golden names no counters"
    exit 1
fi

comm -13 "$work/recorded" "$work/bearing_s" > "$work/newly"
comm -23 "$work/recorded" "$work/bearing_s" > "$work/lost"

if [ -s "$work/newly" ]; then
    echo "FAILURES: these counters are now read as a width and are not in"
    echo "      the register:"
    sed 's/^/      /' "$work/newly"
    echo "      Add them to tests/encoder_units.golden in the same commit."
    fail=1
fi
if [ -s "$work/lost" ]; then
    echo "FAILURES: these are in the register and nothing reads them as a"
    echo "      width any more:"
    sed 's/^/      /' "$work/lost"
    echo "      Retire the line and the count above it together, so that a"
    echo "      population which shrank says so."
    fail=1
fi

entries=$(awk '/^entries /{ print $2; found = 1 } END { if (!found) print "" }' "$golden")
have=$(grep -c . "$work/recorded")
case $entries in
    ''|*[!0-9]*)
        echo "FAILURES: the register has no 'entries <n>' line"
        fail=1 ;;
    *)  if [ "$entries" -ne "$have" ]; then
            echo "FAILURES: the register records $entries counters and holds $have"
            fail=1
        fi ;;
esac

[ "$fail" = 0 ] || exit 1

nseen=$(awk -F'\t' '$1 == "SEEN"' "$work/events" | sort -u | grep -c . || true)
echo "SUCCESS ($have part-unit counters, $nseen reset sites, none behind a refusal)"
exit 0
