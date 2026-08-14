#!/bin/sh
#
# Every fact a device states is accounted for, and a difference between
# two devices carries a reason.
#
# A device says what it is by the entries on its class: which colour
# models it offers, how many bits of glyph coverage it can take, whether
# a grey reaches it as a pattern of pixels. The machinery above the
# device reads those entries and sends it different work accordingly. So
# an entry is a question the family has been asked, and every device is
# answering it -- including the ones whose author never heard the
# question, which answer by inheriting whatever the class they were
# copied from said.
#
# That is how the differences got there. A mechanism arrives, its author
# wires it to the device in front of them, and the rest of the family is
# never asked: the entry spreads by dict copy to devices it does not fit
# and stops short of devices it does. Nothing about a spelling says
# which of those happened, so nothing catches either.
#
# ---- what this holds
#
# The register beside this file, tests/device-facts, has one line per
# entry, and the two are held to each other in BOTH directions:
#
#   an entry no line classifies fails -- so a new mechanism cannot be
#   added quietly. Its author is stopped here until they say whether
#   every device must answer it, or why it is not a family question.
#
#   a line naming an entry no device carries fails -- so a register
#   cannot outlive what it describes. A reason that has stopped being
#   about anything reads exactly like one that still holds.
#
#   the devices carrying an entry are named, and are held to the ones
#   that do. An entry spreading to another device is the drift this
#   exists to catch, and it fails here whichever direction it spread.
#
# Some entries are questions with an answer per device. Those carry a
# family answer, and a device answering otherwise must name the file it
# states its own answer in and say why -- prose sitting where the
# difference is, saying what would make it false. An answer that has
# come back to the family answer and kept its reason fails too.
#
# ---- why it asks a running interpreter
#
# A class is built, not spelled. The compiled drivers copy a class
# written in PostScript and then say their own thing about the copy, and
# one driver body makes two classes and says different things for each.
# What a file spells and what a class ends up holding are two questions,
# and the one that matters is the second -- so every device is installed
# by name and its dictionary read, exactly as tests/check-device-roster.sh
# reads what a device says about taking its page in bands.
#
# ---- and the recorder
#
# The record paints nothing. It stands in front of a device that does,
# writes down what it was asked to paint, and plays it back. So its
# answers are not its own: they are its target's, and a record answering
# for itself would answer about a device it is standing in front of.
# That is held here directly -- the record is made over each target in
# turn and asked -- which is the one way to see it, since what the
# record carries is copied at the moment it is specialised and no
# reading of any file shows it.
#
#   $1  path to the source tree root
#   $2  the built interpreter
set -u
src=${1:?usage: check-device-facts.sh <srcroot> <xpost>}
xpost=${2:?usage: check-device-facts.sh <srcroot> <xpost>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
if [ ! -x "$xpost" ]; then
    echo "FAILURES: the interpreter is not an executable: $xpost"
    exit 1
fi
case $xpost in
    /*) ;;
    *) xpost=$(cd "$(dirname "$xpost")" && pwd)/$(basename "$xpost") ;;
esac

guard_workdir
trap 'rm -rf "$work"' EXIT

srcdir=$src
guard_mirror_tree "$src"
src=$mirror

register="$src/tests/device-facts"
guard_require_file "$register" "the register of device facts"
fleet="$src/tests/device-fleet.sh"
guard_require_file "$fleet" "the device roster"

fail=0

# ---------------------------------------------------------------------
# What the devices say
#
# Each device is installed by a page-device request and its dictionary
# read. The run starts on the device that paints nothing, so what a
# device says is read after the request that installed it and never off
# whatever device this build was configured with.
#
# A value is written down as what can be compared: a procedure as the
# word, a dictionary as its keys, a string and an array as what they
# are. What matters about a procedure is that the device has one, and
# two devices whose procedures differ differ in every line of them.
cat > "$work/enc.ps" <<'EOF'
/S 64 string def
/.enc {
    dup xcheck { pop (proc) print }{
    dup type /dicttype eq { (set:) print { pop S cvs print (,) print } forall }{
    dup type /arraytype eq { pop (array) print }{
    dup type /packedarraytype eq { pop (array) print }{
    dup type /stringtype eq { pop (str) print }{
    dup type /nulltype eq { pop (null) print }{
    S cvs print } ifelse } ifelse } ifelse } ifelse } ifelse } ifelse
} bind def
/.dump {
    /DN exch def
    DEVICE { exch (K ) print DN S cvs print ( ) print S cvs print ( ) print
             .enc (\n) print } forall
} bind def
EOF

( . "$fleet"
  for v in $DEVICE_FLEET_ALL; do echo "$v"; done ) 2>/dev/null \
    | sort -u > "$work/roster"
( . "$fleet"
  for v in $DEVICE_FLEET_BANDS; do echo "$v"; done ) 2>/dev/null \
    | sort -u > "$work/targets"
if [ ! -s "$work/roster" ] || [ ! -s "$work/targets" ]; then
    echo "FAILURES: tests/device-fleet.sh names no roster or no band targets"
    exit 1
fi

{
    cat "$work/enc.ps"
    echo "["
    grep -vx record "$work/roster" | sed 's|^|/|'
    cat <<'EOF'
]
{ /D exch def
  { << /OutputDevice D /PageSize [ 8 8 ] >> setpagedevice } stopped
  { (UNMADE ) print D 60 string cvs print (\n) print }
  { D .dump } ifelse
} forall
EOF
} > "$work/ask.ps"
{ cat "$work/enc.ps"; echo "/record .dump"; } > "$work/askrec.ps"

: > "$work/said"
out=$( cd "$work" && "$xpost" -q -d null -o facts.scratch ask.ps </dev/null 2>&1 )
rc=$?
printf '%s\n' "$out" >> "$work/said"
if [ $rc -ne 0 ] || ! grep -q '^K ' "$work/said"; then
    echo "FAILURES: the interpreter could not be asked what its devices state:"
    printf '%s\n' "$out" | sed 's/^/      /' | head -8
    exit 1
fi

# The record over each target in turn. A target this build cannot make
# takes its record with it and is left out of both sides below.
: > "$work/rec"
recasked=0
while read -r t; do
    rout=$( cd "$work" && "$xpost" -q -d "$t:band" -o facts.scratch askrec.ps \
            </dev/null 2>&1 )
    printf '%s\n' "$rout" | grep '^K record ' \
        | sed "s/^K record /R $t /" >> "$work/rec"
    if grep -q "^R $t " "$work/rec"; then
        recasked=$((recasked + 1))
        echo "$t" >> "$work/tasked"
    fi
done < "$work/targets"
[ -f "$work/tasked" ] || : > "$work/tasked"

grep '^UNMADE ' "$work/said" | awk '{print $2}' | sort -u > "$work/unmade"
grep -vxF -f "$work/unmade" "$work/roster" 2>/dev/null | grep . \
    > "$work/made" || cp "$work/roster" "$work/made"
[ -s "$work/unmade" ] || cp "$work/roster" "$work/made"

# counts in before counts out: a run that installed nothing agrees with
# any register at all
nasked=$(grep -c . "$work/made" || true)
nkey=$(awk '$1 == "K" { print $3 }' "$work/said" | sort -u | grep -c . || true)
nrkey=$(awk '$1 == "R" { print $3 }' "$work/rec" | sort -u | grep -c . || true)
if [ "$nasked" -lt 8 ] || [ "$nkey" -lt 40 ] || [ "$recasked" -lt 1 ]; then
    echo "FAILURES: $nasked device(s) answered, stating $nkey distinct entries,"
    echo "      with the record made over $recasked target(s). The family is"
    echo "      larger than that; the question is being asked wrong."
    exit 1
fi

# The value a device states, canonicalised: a dictionary is its keys, and
# its keys are a set rather than an order.
canon() {
    awk '{
        v = $4
        if (v ~ /^set:/) {
            sub(/^set:/, "", v); n = split(v, a, ","); m = 0
            for (i = 1; i <= n; i++) if (a[i] != "") b[++m] = a[i]
            for (i = 1; i < m; i++) for (j = i + 1; j <= m; j++)
                if (b[j] < b[i]) { t = b[i]; b[i] = b[j]; b[j] = t }
            v = "set:"; for (i = 1; i <= m; i++) v = v b[i] (i < m ? "," : "")
            delete b
        }
        print $2, $3, v
    }'
}
awk '$1 == "K"' "$work/said" | canon | sort > "$work/state"
awk '$1 == "R"' "$work/rec"  | canon | sort > "$work/rstate"

# Who carries an entry. The record carries one when it carries it over
# every target it could be made over; where it carries one over some of
# them, that is the target's entry showing through, which the mirror
# rule below is what holds.
awk '{ print $2, $1 }' "$work/state" | sort -u > "$work/carry"
awk -v n="$recasked" '{ c[$2]++ } END { for (k in c) if (c[k] == n) print k, "record" }' \
    "$work/rstate" | sort >> "$work/carry"
sort -u "$work/carry" -o "$work/carry"
awk '{ print $1 }' "$work/carry" | sort -u > "$work/keys.seen"

# ---------------------------------------------------------------------
# What the register says
sed 's/[[:blank:]]*#.*//' "$register" > "$work/reg"

awk '
    /^[ \t]/ { if (last != "") prose[last] = prose[last] + 1; next }
    NF == 0 { next }
    $1 == "question" || $1 == "method" || $1 == "state" || $1 == "part" ||
    $1 == "elsewhere" || $1 == "open" {
        print "KIND", $2, $1 > out "/reg.kind"
        line = ""
        for (i = 3; i <= NF; i++) line = line " " $i
        print $2 line > out "/reg.carry"
        key = $2; last = "kind:" $2; next
    }
    $1 == "family" { print key, $2 > out "/reg.family"; last = "family:" key; next }
    $1 == "answer" {
        print key, $2, $3, ($4 == "" ? "-" : $4) > out "/reg.answer"
        last = "answer:" key ":" $2; next
    }
    { print "check-device-facts: unreadable register line: " $0 > "/dev/stderr"
      bad = 1 }
    END {
        for (k in prose) print k, prose[k] > out "/reg.prose"
        exit bad ? 1 : 0
    }
' out="$work" "$work/reg" || fail=1
for f in reg.kind reg.carry reg.family reg.answer reg.prose; do
    [ -f "$work/$f" ] || : > "$work/$f"
done
if [ ! -s "$work/reg.kind" ]; then
    echo "FAILURES: tests/device-facts classifies nothing; every entry the"
    echo "      devices state would be a finding and a tree in good order"
    echo "      would read the same as one in disorder"
    exit 1
fi

awk '{ print $2 }' "$work/reg.kind" | sort > "$work/keys.reg"
if [ "$(sort "$work/keys.reg" | uniq -d | grep -c . || true)" -ne 0 ]; then
    echo "FAIL: tests/device-facts classifies an entry twice:"
    sort "$work/keys.reg" | uniq -d | sed 's/^/      /'
    fail=1
fi
sort -u "$work/keys.reg" -o "$work/keys.reg"

# ---- the two directions
unlisted=$(comm -23 "$work/keys.seen" "$work/keys.reg")
if [ -n "$unlisted" ]; then
    echo "FAIL: the devices state entries tests/device-facts does not classify:"
    printf '%s\n' "$unlisted" | while read -r k; do
        printf '      %-18s carried by%s\n' "$k" \
            "$(awk -v k="$k" '$1 == k { printf " %s", $2 }' "$work/carry")"
    done
    echo "      Say in tests/device-facts what each is: a question every"
    echo "      device must answer, with its answers, or why it is not one."
    echo "      An entry nobody classified is a mechanism the family was"
    echo "      never asked about."
    fail=1
fi
stale=$(comm -13 "$work/keys.seen" "$work/keys.reg")
if [ -n "$stale" ]; then
    echo "FAIL: tests/device-facts classifies entries no device states:"
    printf '%s\n' "$stale" | sed 's/^/      /'
    echo "      A line that has outlived its entry reads exactly like one"
    echo "      that still holds."
    fail=1
fi

# ---- and who carries each
while read -r k; do
    want=$(awk -v k="$k" '$1 == k { for (i = 2; i <= NF; i++) print $i }' \
           "$work/reg.carry" | sort -u)
    case " $want " in
        *" every "*) want=$(cat "$work/made") ;;
    esac
    want=$(printf '%s\n' "$want" | grep . | sort -u)
    # a device this build could not make states nothing, so it is held to
    # nothing and the register's side is narrowed to match
    if [ -s "$work/unmade" ]; then
        want=$(printf '%s\n' "$want" | grep -vxF -f "$work/unmade" || true)
    fi
    got=$(awk -v k="$k" '$1 == k { print $2 }' "$work/carry" | sort -u)
    if [ "$want" != "$got" ]; then
        echo "FAIL: $k is carried by devices tests/device-facts does not name:"
        echo "      register: $(printf '%s ' $want)"
        echo "      devices:  $(printf '%s ' $got)"
        fail=1
    fi
done < "$work/keys.reg"

# ---------------------------------------------------------------------
# The questions
#
# A question carries a family answer and one answer per carrier. An
# answer other than the family's is a difference, and a difference must
# be owned -- stated in a named file rather than inherited -- and must
# carry a reason. An answer that has come back to the family's may not
# keep one.
nq=0
nreason=0
# An entry held by another guard names which, and one this does not yet
# ask names why not: both are judgements that can go stale, so both are
# held to saying enough to be argued with.
awk '$3 == "elsewhere" || $3 == "open" { print $2, $3 }' "$work/reg.kind" \
    | while read -r k kind; do
    if ! awk -v k="kind:$k" '$1 == k && $2 >= 3 { f = 1 } END { exit f ? 0 : 1 }' \
         "$work/reg.prose"; then
        echo "FAIL: $k is classified $kind in fewer than three lines. Say which"
        echo "      guard holds it, or why it is not a family question and what"
        echo "      asking it would cost."
        fail=1
    fi
done

awk '$3 == "question" { print $2 }' "$work/reg.kind" | sort > "$work/questions"
while read -r k; do
    [ -n "$k" ] || continue
    nq=$((nq + 1))
    fam=$(awk -v k="$k" '$1 == k { print $2 }' "$work/reg.family")
    if [ -z "$fam" ]; then
        echo "FAIL: $k is a question and states no family answer"
        fail=1
        continue
    fi
    if ! awk -v k="kind:$k" '$1 == k && $2 >= 3 { f = 1 } END { exit f ? 0 : 1 }' \
         "$work/reg.prose"; then
        echo "FAIL: $k is a question and says in fewer than three lines what it"
        echo "      asks. A question needs what the entry is for, what the"
        echo "      family answer means and what answering otherwise buys."
        fail=1
    fi
    carriers=$(awk -v k="$k" '$1 == k { print $2 }' "$work/carry" | sort -u)
    atfamily=0
    for d in $carriers; do
        [ "$d" = record ] && continue
        got=$(awk -v k="$k" -v d="$d" '$1 == d && $2 == k { print $3 }' "$work/state")
        rec=$(awk -v k="$k" -v d="$d" '$1 == k && $2 == d { print $3 }' "$work/reg.answer")
        file=$(awk -v k="$k" -v d="$d" '$1 == k && $2 == d { print $4 }' "$work/reg.answer")
        if [ -z "$rec" ]; then
            echo "FAIL: $d carries $k and tests/device-facts records no answer"
            echo "      for it. Adding a question costs the whole family an"
            echo "      answer each; that is what makes it a family question."
            fail=1
            continue
        fi
        if [ "$rec" != "$got" ]; then
            echo "FAIL: $d answers $k with '$got' and tests/device-facts"
            echo "      records '$rec'"
            fail=1
            continue
        fi
        hasprose=$(awk -v k="answer:$k:$d" '$1 == k { print $2 }' "$work/reg.prose")
        [ -n "$hasprose" ] || hasprose=0
        if [ "$fam" != "per-device" ] && [ "$got" = "$fam" ]; then
            atfamily=$((atfamily + 1))
            if [ "$hasprose" -gt 0 ] || [ "$file" != "-" ]; then
                echo "FAIL: $d answers $k as the family does and keeps a reason"
                echo "      for differing. The reason has stopped being about"
                echo "      anything; take it out with the difference."
                fail=1
            fi
            continue
        fi
        # a difference, or a question with no family answer to inherit
        if [ "$hasprose" -lt 3 ]; then
            echo "FAIL: $d answers $k with '$got' and says why in fewer than"
            echo "      three lines. Say what the difference is, why this"
            echo "      device makes it, and what would make it false."
            fail=1
        else
            nreason=$((nreason + 1))
        fi
        if [ "$file" = "-" ]; then
            echo "FAIL: $d answers $k with '$got' and names no file it states"
            echo "      that in. An answer nobody stated is one the device was"
            echo "      copied into, which is how a claim made about one device"
            echo "      comes to be made on behalf of another."
            fail=1
        elif [ ! -f "$srcdir/$file" ]; then
            echo "FAIL: $d answers $k in $file, which is not there"
            fail=1
        elif ! grep -q "/$(printf '%s' "$k" | sed 's/\./\\./g')\([^A-Za-z0-9_.]\|\$\)" \
                  "$srcdir/$file"; then
            echo "FAIL: $d answers $k with '$got' and $file does not state $k"
            fail=1
        fi
    done
    if [ "$fam" != "per-device" ] && [ "$atfamily" -eq 0 ]; then
        echo "FAIL: no device answers $k as tests/device-facts says the family"
        echo "      does ($fam). A family answer nobody gives is not one."
        fail=1
    fi
    # the record answers its target's answer, over every target
    if awk -v k="$k" '$1 == k && $2 == "record" { f = 1 } END { exit f ? 0 : 1 }' \
       "$work/carry"; then
        while read -r t; do
            rv=$(awk -v k="$k" -v t="$t" '$1 == t && $2 == k { print $3 }' "$work/rstate")
            tv=$(awk -v k="$k" -v t="$t" '$1 == t && $2 == k { print $3 }' "$work/state")
            if [ "$rv" != "$tv" ]; then
                echo "FAIL: a record made for $t answers $k with '$rv' and $t"
                echo "      itself answers '$tv'. A record answers for the"
                echo "      device it stands in front of, or it is played into"
                echo "      a device the marks were not made for."
                fail=1
            fi
        done < "$work/tasked"
    fi
done < "$work/questions"
if [ "$nq" -eq 0 ]; then
    echo "FAILURES: tests/device-facts asks no questions; fix the register"
    exit 1
fi

# ---------------------------------------------------------------------
# The recorder carries its target's entries and its own, and nothing else
#
# For every entry: the record either carries it over every target, which
# makes it the record's own, or over exactly the targets that carry it,
# which makes it the target's showing through. Over some other set it is
# neither, and a page played into that target is played into a device
# holding an entry the marks were not recorded under.
nmirror=0
while read -r k; do
    over=$(awk -v k="$k" '$2 == k { print $1 }' "$work/rstate" | sort -u)
    n=$(printf '%s\n' "$over" | grep -c . || true)
    [ "$n" -eq 0 ] && continue
    [ "$n" -eq "$recasked" ] && continue
    theirs=$(awk -v k="$k" '$2 == k { print $1 }' "$work/state" | sort -u \
             | grep -xF -f "$work/tasked" 2>/dev/null | sort -u)
    if [ "$over" != "$theirs" ]; then
        echo "FAIL: a record carries $k for [$(printf '%s ' $over)] and the"
        echo "      targets stating it are [$(printf '%s ' $theirs)]"
        fail=1
    else
        nmirror=$((nmirror + 1))
    fi
done < "$work/keys.seen"

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: the device facts and their register disagree"
    exit 1
fi

skipped=''
[ -s "$work/unmade" ] &&
    skipped=", $(grep -c . "$work/unmade") not built into this interpreter"
nall=$(grep -c . "$work/keys.seen" || true)
echo "SUCCESS ($nall entries stated by $nasked device(s) and every one classified;\
 $nq question(s) answered device by device with $nreason reason(s) for differing;\
 the recorder held to its target over $recasked target(s), $nmirror entry(ies)\
 of theirs showing through$skipped)"
exit 0
