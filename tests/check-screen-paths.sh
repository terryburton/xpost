#!/bin/sh
#
# The same grey, screened through the same cell by two different pieces
# of code, must reach the page the same way.
#
# A bilevel device compares every grey it is given against the threshold
# under its pixel. That rule is written three times:
#
#   data/pbmimage.ps /.screenpix          the device's own method, used
#                                         for the ground row and for
#                                         reading back a pixel the
#                                         device holds no storage for
#   xpost_dev_generic.c _fillrect         a rectangle fill, from a real
#                                         in 0..1
#   xpost_dev_generic.c the blit writer   an image, from a byte in 0..255
#
# They are not the same expression. The fill path forms its level as
# floor(v * 256 + 0.5); the image path forms it as (256 * g + 127) / 255
# in integers. Those agree for every byte value, but they agree by
# arithmetic that is not obvious from either line, and nothing held them
# to each other until this. A fill and an image asking for the same grey
# under the same screen are the same page or one of the three has moved.
#
# WHAT IS COMPARED: the emitted page, byte for byte, from a fill and
# from an image of a single sample stretched over the page. The screen
# is a cell holding all 256 threshold values, so every level in it is a
# level some pixel is compared against -- a coarser cell cannot see a
# difference of one and would pass while the two disagreed. A cell that
# steps by seventeen was tried first and reported agreement it could not
# have detected.
#
# WHAT IS NOT COMPARED, and it is the third of the three: /.screenpix.
# It is reached for the ground row and for reading a pixel back, not for
# a mark, so putting it beside these two needs a page read back through
# GetPix rather than a page emitted. Until that is written, the device's
# own method is held to the other two by nothing.
#
# WHICH LEVELS: the two expressions can only part company where
# 256 * g / 255 lands within half a unit of an integer, which in 0..255
# is g = 127 and its neighbours. The levels below are those, the ends,
# and a spread between them. THE FULL 256-LEVEL SWEEP HAS BEEN RUN and
# reported no disagreement; it takes about ninety seconds, which is too
# long to spend on every build. Run it -- every g from 0 to 255 -- when
# either expression is touched.
#
#   $1  path to the source tree root
#   $2  path to the xpost binary
set -u
src=${1:?usage: check-screen-paths.sh <srcroot> <xpost>}
xpost=${2:?usage: check-screen-paths.sh <srcroot> <xpost>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_require_file "$src/data/pbmimage.ps" "the bilevel device"

guard_workdir
trap 'rm -rf "$work"' EXIT

# a cell holding every threshold value there is
thresholds=$(awk 'BEGIN { for (i = 0; i < 256; i++) printf "%02X", i }')
screen="<< /HalftoneType 3 /Width 16 /Height 16 /Thresholds <$thresholds> >>
        sethalftone"

levels="0 1 2 62 63 64 65 126 127 128 129 130 190 191 192 253 254 255"

fail=0
compared=0
varied=""

for g in $levels; do
    v=$(awk -v g="$g" 'BEGIN { printf "%.17g", g / 255 }')
    {
        printf '<< /PageSize [16 16] >> setpagedevice\n'
        printf '%s\n' "$screen"
        printf '%s setgray\n0 0 16 16 rectfill\nshowpage\n' "$v"
    } > "$work/fill.ps"
    {
        printf '<< /PageSize [16 16] >> setpagedevice\n'
        printf '%s\n' "$screen"
        printf '16 16 scale\n'
        printf '<< /ImageType 1 /Width 1 /Height 1 /BitsPerComponent 8\n'
        printf '   /Decode [0 1] /DataSource <%02X>\n' "$g"
        printf '   /ImageMatrix [1 0 0 -1 0 1] >> image\nshowpage\n'
    } > "$work/image.ps"

    XPOST_DATA_DIR="$src/data" "$xpost" -q --no-sandbox -d pbm \
        -o "$work/fill.pbm" "$work/fill.ps" </dev/null >/dev/null 2>&1
    XPOST_DATA_DIR="$src/data" "$xpost" -q --no-sandbox -d pbm \
        -o "$work/image.pbm" "$work/image.ps" </dev/null >/dev/null 2>&1

    if [ ! -s "$work/fill.pbm" ] || [ ! -s "$work/image.pbm" ]; then
        echo "FAIL: grey $g produced no page by one route or the other, so"
        echo "      there is nothing to compare and nothing is proved"
        fail=1
        continue
    fi
    compared=$((compared + 1))
    ink=$(od -An -v -tu1 "$work/fill.pbm" | awk '
        { for (i = 1; i <= NF; i++) v[n++] = $i }
        END {
            t = 0; i = 0
            while (t < 3 && i < n) {
                while (i < n && (v[i] == 32 || v[i] == 10 || v[i] == 9)) i++
                if (v[i] == 35) { while (i < n && v[i] != 10) i++; continue }
                while (i < n && !(v[i] == 32 || v[i] == 10 || v[i] == 9)) i++
                t++
            }
            i++
            ink = 0
            for (; i < n; i++)
                for (k = 0; k < 8; k++) if (int(v[i] / 2^k) % 2) ink++
            print ink
        }')
    varied="$varied $ink"

    if ! cmp -s "$work/fill.pbm" "$work/image.pbm"; then
        echo "FAIL: grey $g reaches the page differently as a fill and as an"
        echo "      image. Both go through the same threshold cell, so one of"
        echo "      the two ways of forming the level has moved -- they are"
        echo "      separate expressions in separate functions and neither"
        echo "      knows about the other."
        fail=1
    fi
done

# ---- the comparison has to be able to see a difference
#
# Two routes that both paint nothing, or both paint everything, agree
# for a reason that says nothing about the screen. The counts have to
# move with the grey, or this passed without testing anything.
distinct=$(printf '%s\n' $varied | LC_ALL=C sort -u | grep -c .)
if [ "$compared" -lt 10 ] || [ "$distinct" -lt 5 ]; then
    echo "FAILURES: $compared levels compared and only $distinct distinct ink"
    echo "      counts among them. A screen that answers the same for every"
    echo "      grey is one this comparison cannot report on"
    exit 1
fi

# ---- and there is still only one of it
#
# The pages agreeing is the behaviour; this is the structure. A grey
# meets a threshold in exactly one expression, xpost_dev_ht_ink, and
# every path calls it. A fourth writer that inlines the comparison again
# would paint the same page today and drift tomorrow, which is precisely
# how there came to be three, so the shapes the three used are searched
# for and must not be back.
inlined=$(grep -rn "] ? 255 : 0" "$src/src/lib" 2>/dev/null \
    | grep -v "xpost_dev_generic.h" || true)
if [ -n "$inlined" ]; then
    echo "FAIL: a threshold comparison is written out again rather than"
    echo "      calling xpost_dev_ht_ink:"
    printf '%s\n' "$inlined" | sed 's/^/      /'
    fail=1
fi
psinline=$(grep -rn "htcell exch get" "$src/data" 2>/dev/null || true)
if [ -n "$psinline" ]; then
    echo "FAIL: a device method reads a threshold out of the cell itself"
    echo "      rather than calling the screening operator:"
    printf '%s\n' "$psinline" | sed 's/^/      /'
    fail=1
fi

# the two searches above are worth nothing if they cannot match, so they
# are put to a body known to contain both shapes
probe=$(printf 'x[i] ? 255 : 0\n.htcell exch get\n' \
    | grep -c -e "] ? 255 : 0" -e "htcell exch get")
if [ "$probe" -ne 2 ]; then
    echo "FAILURES: the searches for an inlined comparison matched $probe of"
    echo "      2 lines written to contain exactly what they look for, so"
    echo "      they would report a tree with four copies as having one"
    exit 1
fi

callers=$(grep -rc "xpost_dev_ht_ink" "$src/src/lib/xpost_dev_generic.c" || echo 0)

[ "$fail" = 0 ] || exit 1
printf 'SUCCESS (%s greys, %s distinct inkings, fill and image byte-identical; one comparison, %s references)\n' \
    "$compared" "$distinct" "$callers"
