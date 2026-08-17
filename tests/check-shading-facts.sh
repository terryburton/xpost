#!/bin/sh
#
# Every shading type is accounted for, and a difference between two of
# them carries a reason.
#
# PLRM 4.9.3 names seven shading types, and shfill sends each to a
# painter. Three of the seven describe a field of colour by a function of
# position; four hand over a packed stream of vertices or control points
# and share one reader. What a program can ask for and get is therefore
# not one question but seven, and the answers have drifted apart: a width
# one type takes and another does not, a check written twice, a
# subdivision derived in one painter and constant in the other.
#
# ---- what this holds
#
# tests/shading-facts has one line per type, and the two are held to each
# other in BOTH directions:
#
#   a type the dispatch table paints and the register does not name fails
#   -- so a type newly wired up cannot arrive quietly.
#
#   a type the register names and the table does not paint fails -- so
#   the register cannot outlive what it describes.
#
#   the painter named for each type is the painter the table sends it to,
#   so a type moved to a different painter fails here rather than being
#   discovered by a page that changed.
#
# Membership is derived from the table, and then checked from the other
# side against a running interpreter: every registered type is offered a
# shading of its own shape and must PAINT -- not merely be accepted, but
# put ink on a page -- and every type code the table does not name is
# offered one and must be refused.
#
# Painting is what is asked for because acceptance is cheap. A painter
# that silently drew nothing would satisfy a check for the absence of an
# error, and drawing nothing is exactly what a mis-set stream reader
# does.
#
# ---- the widths, and the divergences
#
# The four stream types share one reader, so which widths that reader can
# walk is a family fact and not a detail of any one type. Every width
# PLRM allows is offered and the verdict held to the register.
#
# Each divergence in the register has a probe here, and the probe decides
# whether the line must be present: a divergence the probe finds and the
# register does not name fails, and so does a line for a divergence the
# probe can no longer find. That is what stops a reason outliving its
# defect, which is the failure mode a register has that a test does not.
#
#   $1  path to the source tree root
#   $2  the built interpreter
set -u
src=${1:?usage: check-shading-facts.sh <srcroot> <xpost>}
xpost=${2:?usage: check-shading-facts.sh <srcroot> <xpost>}
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
case $src in
    /*) srcdata=$src/data ;;
    *) srcdata=$(cd "$src" && pwd)/data ;;
esac

guard_workdir
trap 'rm -rf "$work"' EXIT
guard_mirror_tree "$src"
src=$mirror

register="$src/tests/shading-facts"
guard_require_file "$register" "the register of shading types"
shade="$src/data/shade.ps"
guard_require_file "$shade" "the shading machinery"

fail=0

# ---------------------------------------------------------------------
# What the tree says: the dispatch table, type by painter
#
# The table is a dictionary of number-procedure pairs written over
# several lines, so it is taken between its opening and its closing and
# read as a stream of pairs rather than line by line.
awk '
    /^\/\.shadingtypes[ \t]*<</ { open = 1; next }
    open && /^>>/ { exit }
    open { gsub(/%.*/, ""); print }
' "$shade" \
  | tr -s ' \t' '\n' | grep -v '^$' \
  | awk '
        /^[0-9]+$/ { t = $0; next }
        t != "" && /^\/\// { p = $0; sub(/^\/\//, "", p); print t " " p; t = "" }
    ' | sort -n > "$work/table"

ntable=$(grep -c . "$work/table" || true)
if [ "$ntable" -lt 2 ]; then
    echo "FAILURES: the dispatch table in data/shade.ps reads as $ntable"
    echo "      type(s). It is what the population here is derived from, so"
    echo "      a reading this thin would hold the register to nothing"
    exit 1
fi

# ---------------------------------------------------------------------
# What the register says
sed 's/[[:blank:]]*#.*//' "$register" | grep -v '^[[:space:]]*$' > "$work/reg"

awk '$1 ~ /^[0-9]+$/ { print $1 " " $3 }' "$work/reg" | sort -n > "$work/reg.type"
awk '$1 ~ /^[0-9]+$/ { print $1 " " $2 " " $4 " " $5 }' "$work/reg" | sort -n \
    > "$work/reg.detail"
awk '$1 == "width" { print $2 " " $3 " " $4 }' "$work/reg" | sort > "$work/reg.width"
awk '$1 == "flagmax" { print $2 " " $3 }' "$work/reg" | sort > "$work/reg.flagmax"
awk 'NF >= 3 && $2 ~ /^(settled|thorn|heading)$/ { print $1 }' "$work/reg" \
    | sort -u > "$work/reg.diverge"

if [ ! -s "$work/reg.type" ]; then
    echo "FAILURES: tests/shading-facts names no shading type; a tree in good"
    echo "      order would read the same as one in disorder"
    exit 1
fi

# ---- the two directions, and the painter with them
guard_held=0
guard_hold "$work/reg.type" "$work/table" \
    "in the register and no longer painted by any painter the dispatch
      table names. Retire the line and the count with it, so that a
      family that lost a member says so:" \
    "painted by the dispatch table and not in the register. A type that
      can be asked for is a type whose differences from the rest have to
      be written down; add it to tests/shading-facts:"
[ "$guard_held" -eq 0 ] || fail=1

# ---- the counts the register states
count() {           # <keyword> <have>
    n=$(awk -v K="$1" '$1 == K && NF == 2 && $2 ~ /^[0-9]+$/ && !f { print $2; f = 1 }' \
        "$work/reg")
    case ${n:-} in
        ''|*[!0-9]*)
            echo "FAILURES: the register has no '$1 <n>' line"
            fail=1 ;;
        *)  if [ "$n" -ne "$2" ]; then
                echo "FAILURES: the register records $1 $n and holds $2"
                fail=1
            fi ;;
    esac
}
count types "$(grep -c . "$work/reg.type")"
count widths "$(grep -c . "$work/reg.width")"
count divergences "$(grep -c . "$work/reg.diverge")"
count flagmaxes "$(grep -c . "$work/reg.flagmax")"

# ---------------------------------------------------------------------
# What the interpreter does
#
# A shading of each type's own shape. The coordinates and colours are
# chosen so that every one of them covers a good part of the page: a
# painter that lost its geometry would paint a sliver or nothing, and
# the ink is counted below so that it would.
CS='/ColorSpace /DeviceGray'
FN='<< /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [1] /N 1 >>'
FN2='<< /FunctionType 0 /Domain [0 8 0 8] /Range [0 1] /Size [2 2]
       /BitsPerSample 8 /DataSource <0040C0FF> >>'
BITS='/BitsPerCoordinate 16 /BitsPerComponent 8 /Decode [0 8 0 8 0 1]'
D4=00000000000000FFFF000080008000FFFFFF
D5=0000000000FFFF0000400000FFFF80FFFFFFFFFF
D6=0000000000111122222222444433336666444488885555AAAA6666CCCC7777EEEE8888111099993332AAAA5554BBBB7776004080FF
D7=0000000000111122222222444433336666444488885555AAAA6666CCCC7777EEEE8888111099993332AAAA5554BBBB7776CCCC9998DDDDBBBAEEEEDDDCFFFFFFFE004080FF

# Three free-form vertices at a stated field width: the same triangle
# every time, written to fit. A width probe cannot reuse one string --
# data sized for sixteen-bit coordinates runs out under thirty-two-bit
# ones, and running out raises the same error as being refused, which
# would report every wider width as unsupported.
meshdata() {        # <coordinate bits> <component bits> -> hex
    awk -v CB="$1" -v MB="$2" '
        function field(frac, bytes,    v, i, s, p) {
            v = int(frac * (256 ^ bytes - 1) + 0.5)
            s = ""
            for (i = bytes; i >= 1; i--) {
                p = 256 ^ (i - 1)
                s = s sprintf("%02X", int(v / p) % 256)
            }
            return s
        }
        BEGIN {
            cb = int((CB + 7) / 8); if (cb < 1) cb = 1
            mb = int((MB + 7) / 8); if (mb < 1) mb = 1
            split("0 1 0.5", x, " "); split("0 0 1", y, " ")
            split("0 0.5 1", c, " ")
            out = ""
            for (i = 1; i <= 3; i++)
                out = out "00" field(x[i], cb) field(y[i], cb) field(c[i], mb)
            print out
        }'
}

# <type> [omit-function] -> the shading dictionary body
shading() {
    _t=$1
    _nofn=${2:-}
    case $_t in
        1) _b="/ShadingType 1 $CS /Domain [0 8 0 8]"
           [ -n "$_nofn" ] || _b="$_b /Function $FN2" ;;
        2) _b="/ShadingType 2 $CS /Coords [0 0 8 8]"
           [ -n "$_nofn" ] || _b="$_b /Function $FN" ;;
        3) _b="/ShadingType 3 $CS /Coords [4 4 0 4 4 4]"
           [ -n "$_nofn" ] || _b="$_b /Function $FN" ;;
        4) _b="/ShadingType 4 $CS /DataSource <$D4> $BITS /BitsPerFlag 8" ;;
        5) _b="/ShadingType 5 $CS /DataSource <$D5> $BITS /VerticesPerRow 2" ;;
        6) _b="/ShadingType 6 $CS /DataSource <$D6> $BITS /BitsPerFlag 8" ;;
        7) _b="/ShadingType 7 $CS /DataSource <$D7> $BITS /BitsPerFlag 8" ;;
        *) _b="" ;;
    esac
    printf '%s' "$_b"
}

# Run one shading and answer with the error name, or with the ink it put
# on the page. The page is small and the scale takes the shading's own
# eight units across it, so a type that paints is a type most of whose
# page is not the ground.
paint() {           # <dictionary body> -> "<errorname>" or "ink <n> of <m>"
    {
        printf '<< /PageSize [32 32] >> setpagedevice 4 4 scale\n'
        printf '/S 64 string def\n'
        printf '{ << %s >> shfill } stopped\n' "$1"
        printf '{ (E ) print $error /errorname get S cvs print (\\n) print\n'
        printf '  clear }{ (E none\\n) print } ifelse\n'
        printf 'showpage\n'
    } > "$work/case.ps"
    rm -f "$work/case.pgm"
    _e=$( cd "$work" && XPOST_DATA_DIR="$srcdata" \
          "$xpost" -q --no-sandbox -d pgm -o case.pgm case.ps </dev/null 2>&1 \
          | awk '$1 == "E" { print $2; exit }' )
    if [ "${_e:-}" != none ]; then
        printf '%s' "${_e:-noanswer}"
        return
    fi
    od -An -v -tu1 "$work/case.pgm" 2>/dev/null | awk '
        { for (i = 1; i <= NF; i++) v[n++] = $i }
        END {
            # four whitespace-separated header tokens, a comment allowed
            t = 0; i = 0
            while (t < 4 && i < n) {
                while (i < n && (v[i]==32||v[i]==10||v[i]==9||v[i]==13)) i++
                if (v[i] == 35) { while (i < n && v[i] != 10) i++; continue }
                while (i < n && !(v[i]==32||v[i]==10||v[i]==9||v[i]==13)) i++
                t++
            }
            i++
            ink = 0; tot = 0
            for (; i < n; i++) { tot++; if (v[i] != 255) ink++ }
            print "ink " ink " of " tot
        }'
}

# ---- every registered type paints
: > "$work/painted"
while read -r t painter; do
    body=$(shading "$t")
    if [ -z "$body" ]; then
        echo "FAILURES: type $t is painted by $painter and this check has no"
        echo "      shading of that shape to offer it, so it would pass by"
        echo "      never being asked. Write one in shading() above."
        exit 1
    fi
    ans=$(paint "$body")
    case $ans in
        ink\ *)
            n=$(printf '%s' "$ans" | awk '{ print $2 }')
            tot=$(printf '%s' "$ans" | awk '{ print $4 }')
            if [ "$n" -lt 32 ]; then
                echo "FAIL: type $t was accepted and painted $n of $tot pixels."
                echo "      A painter that puts almost nothing down has lost its"
                echo "      geometry, and an error is not what that looks like."
                fail=1
            else
                echo "$t $n" >> "$work/painted"
            fi ;;
        *)  echo "FAIL: type $t is in the register as painting and shfill"
            echo "      answered $ans"
            fail=1 ;;
    esac
done < "$work/reg.type"

# ---- and a type no painter is named for is refused
#
# Offered the shape of a type 4, which is the richest dictionary here: a
# type refused for want of an entry would be refused for the wrong
# reason, and this way the only thing wrong with it is its number.
outside=0
t=0
while [ "$t" -le 20 ]; do
    if ! awk -v T="$t" '$1 == T { found = 1 } END { exit !found }' "$work/table"
    then
        body="/ShadingType $t $CS /DataSource <$D4> $BITS /BitsPerFlag 8
              /Coords [0 0 8 8] /Domain [0 1 0 1] /Function $FN"
        ans=$(paint "$body")
        if [ "$ans" != rangecheck ]; then
            echo "FAIL: shading type $t is named by no painter and shfill"
            echo "      answered '$ans' rather than refusing it"
            fail=1
        else
            outside=$((outside + 1))
        fi
    fi
    t=$((t + 1))
done
if [ "$outside" -lt 10 ]; then
    echo "FAILURES: only $outside type codes outside the table were refused;"
    echo "      the sweep is not reaching them"
    exit 1
fi

# ---- the Function column, probed
while read -r t verdict source fn; do
    body=$(shading "$t" nofunction)
    ans=$(paint "$body")
    case $fn in
        required)
            case $ans in
                ink\ *)
                    echo "FAIL: type $t is recorded as requiring a Function and"
                    echo "      painted without one ($ans)"
                    fail=1 ;;
            esac ;;
        optional)
            case $ans in
                ink\ *) ;;
                *)  echo "FAIL: type $t is recorded as taking a Function"
                    echo "      optionally and answered $ans without one"
                    fail=1 ;;
            esac ;;
        *)  echo "FAIL: type $t has no readable function column"
            fail=1 ;;
    esac
done < "$work/reg.detail"

# ---------------------------------------------------------------------
# The widths the shared reader takes
#
# One shading per width, differing only in the width. A width that is
# taken must paint; the data is sized for sixteen-bit coordinates, so a
# width taken but read differently would run out of data and raise --
# which is a refusal by another name and is reported as a disagreement.
: > "$work/got.width"
while read -r entry bits verdict; do
    case $entry in
        BitsPerCoordinate) cb=$bits; mb=8 ;;
        BitsPerComponent)  cb=16; mb=$bits ;;
        *)  echo "FAIL: the register has a width line for $entry, which is not"
            echo "      an entry of the packed reader"
            fail=1
            continue ;;
    esac
    ds=$(meshdata "$cb" "$mb")
    ans=$(paint "/ShadingType 4 $CS /DataSource <$ds>
                 /BitsPerCoordinate $cb /BitsPerComponent $mb
                 /BitsPerFlag 8 /Decode [0 8 0 8 0 1]")
    case $ans in
        ink\ *) echo "$entry $bits takes" >> "$work/got.width" ;;
        rangecheck) echo "$entry $bits refuses" >> "$work/got.width" ;;
        *)  echo "$entry $bits $ans" >> "$work/got.width" ;;
    esac
done < "$work/reg.width"
sort "$work/got.width" -o "$work/got.width"

if ! cmp -s "$work/reg.width" "$work/got.width"; then
    echo "FAIL: the widths the packed reader takes are not the widths the"
    echo "      register records:"
    diff "$work/reg.width" "$work/got.width" 2>/dev/null | sed 's/^/      /'
    echo "      A width is a promise to every program with data in it, so one"
    echo "      arriving or leaving is a line to write either way."
    fail=1
fi

# ---------------------------------------------------------------------
# The values each painter's edge flag may take
#
# Which painters need a line is derived: every painter of a type whose
# source is a stream, and no other. Each is offered its largest legal
# value, which must paint, and one above it, which must be refused --
# so a line stating the wrong maximum fails on one side or the other
# rather than being believed.
awk '$1 ~ /^[0-9]+$/ && $4 == "stream" { print $3 }' "$work/reg" | sort -u \
    > "$work/streamers"
awk '{ print $1 }' "$work/reg.flagmax" | sort -u > "$work/flagmax.named"

guard_held=0
guard_hold "$work/flagmax.named" "$work/streamers" \
    "given an edge flag maximum and painting no type that reads a
      stream. A flag belongs to a stream; retire the line:" \
    "painting a type that reads a stream and given no edge flag
      maximum. Say in tests/shading-facts which values its flag may
      take:"
[ "$guard_held" -eq 0 ] || fail=1

# a triangle stream: one whole triangle, then a unit carrying the flag
# under test, then two more so that running out of data cannot be the
# reason for a refusal
tridata() {         # <flag> -> hex
    printf '000000000000' ; printf '00FFFF000080' ; printf '008000FFFFFF'
    printf '%02XFFFFFFFF40' "$1" ; printf '010000FFFF20' ; printf '014000800060'
}
# a patch stream: one whole patch, then a unit carrying the flag under
# test with the eight further points a shared edge needs
P12=00000000111122222222444433336666444488885555AAAA6666CCCC7777EEEE8888111099993332AAAA5554BBBB7776
P8=00000000111122222222444433336666444488885555AAAA6666CCCC7777EEEE
patchdata() {       # <flag> -> hex
    printf '00%s004080FF' "$P12"
    printf '%02X%s8040' "$1" "$P8"
}
: > "$work/flagmax.got"
while read -r painter maxv; do
    [ -n "$maxv" ] || continue
    for v in "$maxv" $((maxv + 1)); do
        case $painter in
            .meshsh)
                ds=$(tridata "$v" | tr -d ' ')
                body="/ShadingType 4 $CS /DataSource <$ds> $BITS /BitsPerFlag 8" ;;
            .patchsh)
                ds=$(patchdata "$v")
                body="/ShadingType 6 $CS /DataSource <$ds> $BITS /BitsPerFlag 8" ;;
            *)  echo "FAIL: $painter has an edge flag maximum and this check has"
                echo "      no stream of its shape to offer it"
                fail=1
                continue ;;
        esac
        ans=$(paint "$body")
        case $ans in
            ink\ *) echo "$painter $v paints" >> "$work/flagmax.got" ;;
            *)      echo "$painter $v $ans" >> "$work/flagmax.got" ;;
        esac
    done
done < "$work/reg.flagmax"

while read -r painter maxv; do
    [ -n "$maxv" ] || continue
    at=$(awk -v p="$painter" -v v="$maxv" '$1 == p && $2 == v { print $3 }' \
         "$work/flagmax.got")
    over=$(awk -v p="$painter" -v v="$((maxv + 1))" '$1 == p && $2 == v { print $3 }' \
           "$work/flagmax.got")
    if [ "$at" != paints ]; then
        echo "FAIL: $painter is recorded as taking an edge flag of $maxv and"
        echo "      answered '$at' to one. A value the specification allows"
        echo "      names an edge of the shape before it, and there is one."
        fail=1
    fi
    if [ "$over" != rangecheck ]; then
        echo "FAIL: $painter is recorded as taking an edge flag no larger than"
        echo "      $maxv and answered '$over' to $((maxv + 1)). A value outside"
        echo "      the set names an edge of a shape the data does not describe,"
        echo "      so there is nothing to paint and nothing to guess."
        fail=1
    fi
done < "$work/reg.flagmax"

# ---------------------------------------------------------------------
# The divergences, each with the probe that finds it
#
# Held both ways: a probe that finds its difference requires the line,
# and a probe that no longer finds it requires the line to go.
: > "$work/got.diverge"

# a required entry absent raises undefined rather than one of the two
# errors PLRM gives shfill
ans=$(paint "/ShadingType 2 $CS /Coords [0 0 8 8]")
case $ans in
    rangecheck|undefinedresult) ;;
    *) echo missing-required >> "$work/got.diverge" ;;
esac

# a width PLRM allows that the reader refuses
awk '$3 == "refuses" { found = 1 } END { exit !found }' "$work/reg.width" &&
    echo byte-aligned-reader >> "$work/got.diverge"

# a flag width PLRM allows, taken without complaint and read as nothing.
# The data is five vertices with the flag in the top half of its byte,
# which is what PLRM's per-vertex padding makes of a four-bit flag.
F4=00000000000000FFFF000080008000FFFFFF10FFFFFFFF40100000FFFF20
ans=$(paint "/ShadingType 4 $CS /DataSource <$F4> $BITS /BitsPerFlag 4")
case $ans in
    ink\ *) echo flag-width >> "$work/got.diverge" ;;
esac

# an edge flag outside the three values PLRM allows, taken as if it were
# one of them. The flag byte here is 16, which is 0 in its low two bits
# and nothing at all in the other six.
FBAD=00000000000000FFFF000080008000FFFFFF10FFFFFFFF40
ans=$(paint "/ShadingType 4 $CS /DataSource <$FBAD> $BITS /BitsPerFlag 8")
case $ans in
    ink\ *) echo flag-value >> "$work/got.diverge" ;;
esac

# one painter takes its subdivision from device space and the other does
# not. Read as: the patch painter transforms its points, the triangle
# painter sets its depth to a constant.
body() {            # <procedure name> -- its body, braces counted
    awk -v P="$1" '
        index($0, "/" P " {") == 1 { d = 0; on = 1 }
        on {
            line = $0; sub(/%.*/, "", line)
            print line
            n = split(line, ch, "")
            for (i = 1; i <= n; i++) {
                if (ch[i] == "{") d++
                else if (ch[i] == "}") { d--; if (d == 0) { on = 0; exit } }
            }
        }' "$shade"
}
meshconst=$(body .meshsh | grep -c '/dpt[ \t][ \t]*[0-9][0-9]*[ \t][ \t]*def' || true)
patchderives=$(body .patchpaint | grep -c 'transform' || true)
if [ "$meshconst" -gt 0 ] && [ "$patchderives" -gt 0 ]; then
    echo mesh-depth >> "$work/got.diverge"
fi

# the packed reader's setup written twice: the width refusal is the part
# of it that is one rule, so it is what is counted
setups=$(grep -c 'bpc 16 eq bpc 32 eq or' "$shade" || true)
[ "$setups" -gt 1 ] && echo reader-setup-twice >> "$work/got.diverge"

sort -u "$work/got.diverge" -o "$work/got.diverge"

guard_held=0
guard_hold "$work/reg.diverge" "$work/got.diverge" \
    "named in the register and no longer found by the probe that finds
      it. A reason that has outlived its difference reads exactly like
      one that still holds; retire the line and the count with it:" \
    "found by a probe here and named by no line in the register. Say
      what the difference is and whether it is settled, a thorn or being
      changed, in tests/shading-facts:"
[ "$guard_held" -eq 0 ] || fail=1

[ "$fail" = 0 ] || exit 1

npaint=$(grep -c . "$work/painted")
nw=$(grep -c . "$work/reg.width")
nd=$(grep -c . "$work/reg.diverge")
echo "SUCCESS ($npaint shading type(s) painted and held to the dispatch table,\
 $outside type code(s) outside it refused, $nw width(s) probed,\
 $nd divergence(s) each found by its own probe)"
exit 0
