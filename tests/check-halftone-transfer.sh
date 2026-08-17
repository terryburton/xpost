#!/bin/sh
#
# A halftone dictionary's own transfer function, held to what it is for.
#
# PLRM Table 7.4 gives a halftone dictionary an optional TransferFunction
# that "overrides the one specified by settransfer or setcolortransfer".
# Three things in that sentence are separately checkable, and each of
# them is a way the entry could be got wrong while looking implemented:
#
#   it must APPLY -- a screen built with one must not paint the same
#     page as the same screen without one
#   it must OVERRIDE and not compose -- a graphics state function must
#     not also be applied while one is in force
#   it must belong to the SCREEN -- a device that does not screen must
#     take no effect from it, and replacing the halftone must put the
#     graphics state's own function back
#
# The numbers below are not this interpreter's own output recorded as a
# baseline. They were measured on two other implementations first, which
# agreed with each other on every case, and this interpreter was changed
# until it agreed with them; the counts are theirs.
#
# Every case paints the same quarter grey through the same sixteen-place
# threshold cell onto the same sixteen by sixteen page, so a count is
# comparable with any other count here. A quarter grey through that cell
# inks 192 of the 256 places; a transfer that turns it into three
# quarters inks 64.
#
#   $1  path to the source tree root
#   $2  path to the xpost binary
set -u
src=${1:?usage: check-halftone-transfer.sh <srcroot> <xpost>}
xpost=${2:?usage: check-halftone-transfer.sh <srcroot> <xpost>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_require_file "$src/data/gstate.ps" "the halftone machinery"
guard_require_file "$src/data/paint.ps" "the painting machinery"

guard_workdir
trap 'rm -rf "$work"' EXIT
cr=$(printf '\r')

ht='<< /HalftoneType 3 /Width 4 /Height 4
     /Thresholds <00112233445566778899AABBCCDDEEFF>'

# a case is a name, the device to paint it on, the count expected, and
# the setup that goes before the fill
run_case() {
    _name=$1; _dev=$2; _want=$3; _setup=$4
    {
        printf '<< /PageSize [16 16] >> setpagedevice\n'
        printf '%s\n' "$_setup"
        printf '0.25 setgray\n0 0 16 16 rectfill\n'
        printf 'showpage\n'
    } > "$work/case.ps"
    XPOST_DATA_DIR="$src/data" "$xpost" -q --no-sandbox -d "$_dev" \
        -o "$work/case.out" "$work/case.ps" </dev/null >/dev/null 2>&1
    if [ ! -s "$work/case.out" ]; then
        echo "FAIL: $_name produced no page at all, so nothing about the"
        echo "      transfer function can be read from it"
        fail=1
        return
    fi
    _got=$(od -An -v -tu1 "$work/case.out" | awk -v dev="$_dev" '
        BEGIN { started = 0; ink = 0; seen = 0 }
        { for (i = 1; i <= NF; i++) v[seen++] = $i }
        END {
            # step over the header: three whitespace-separated tokens for
            # a bilevel page, four for a grey one, with a comment line
            # allowed among them
            need = (dev == "pbm") ? 3 : 4
            n = 0; i = 0
            while (n < need && i < seen) {
                while (i < seen && (v[i] == 32 || v[i] == 10 || v[i] == 9 || v[i] == 13)) i++
                if (v[i] == 35) { while (i < seen && v[i] != 10) i++; continue }
                while (i < seen && !(v[i] == 32 || v[i] == 10 || v[i] == 9 || v[i] == 13)) i++
                n++
            }
            i++
            if (dev == "pbm") {
                for (; i < seen; i++) {
                    b = v[i]
                    for (k = 0; k < 8; k++) { if (int(b / 2^k) % 2) ink++ }
                }
            } else {
                for (; i < seen; i++) if (v[i] < 128) ink++
            }
            print ink
        }')
    if [ "$_got" != "$_want" ]; then
        echo "FAIL: $_name inked $_got places and the two implementations"
        echo "      this was measured against ink $_want."
        fail=1
    fi
}

fail=0

# ---- the screen alone, and the same screen with a function on it
#
# The first of these is the control for every other: if a quarter grey
# through this cell stops inking 192, the cell or the grey has moved and
# no other number here means what it says.
run_case "the screen alone" pbm 192 \
    "$ht >> sethalftone"
run_case "the screen carrying a transfer function" pbm 64 \
    "$ht /TransferFunction { 1 exch sub } >> sethalftone"

# ---- it must be the screen's, not the graphics state's
#
# settransfer reaches the same page by the other route, which is what
# makes the pair above meaningful rather than merely different.
run_case "the same function through settransfer" pbm 64 \
    "$ht >> sethalftone { 1 exch sub } settransfer"

# ---- overriding, not composing
#
# A graphics state function set as well must not be applied on top. If
# it were, a quarter grey would be halved before being inverted and the
# count would not be 64.
run_case "a graphics state function set as well" pbm 64 \
    "{ 0.5 mul } settransfer
     $ht /TransferFunction { 1 exch sub } >> sethalftone"
run_case "a graphics state function set afterwards" pbm 64 \
    "$ht /TransferFunction { 1 exch sub } >> sethalftone
     { } settransfer"

# ---- it lasts as long as its halftone and no longer
run_case "a screen installed over one that carried a function" pbm 192 \
    "$ht /TransferFunction { 1 exch sub } >> sethalftone
     $ht >> sethalftone"
run_case "a function surviving a grestore of settransfer" pbm 64 \
    "$ht /TransferFunction { 1 exch sub } >> sethalftone
     gsave { } settransfer grestore"

# ---- a composite carries it on the component that reaches the device
run_case "a composite whose component carries one" pbm 64 \
    "<< /HalftoneType 5 /Default $ht /TransferFunction { 1 exch sub } >> >>
     sethalftone"
run_case "a composite whose component carries none" pbm 192 \
    "<< /HalftoneType 5 /Default $ht >> >> sethalftone"

# ---- and a device that does not screen takes no effect from it
#
# The grey device stores the colour rather than comparing it against a
# cell, so a quarter grey stays a quarter grey -- 63 of 255 -- whatever
# the halftone says. The settransfer case is the control: that route
# does reach a continuous-tone page, so a device reporting no difference
# for both is a device this cannot tell apart.
run_case "a grey page under a screen carrying a function" pgm 256 \
    "$ht /TransferFunction { 1 exch sub } >> sethalftone"
run_case "a grey page under settransfer" pgm 0 \
    "$ht >> sethalftone { 1 exch sub } settransfer"

[ "$fail" = 0 ] || exit 1
echo "SUCCESS (11 cases: the function applies, overrides, and belongs to the screen)"
