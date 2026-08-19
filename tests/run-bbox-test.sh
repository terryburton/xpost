#!/bin/sh
# Meson/make-check wrapper: render a known fill through the bbox device and
# require the exact bounding box it reports. Exercises the -d bbox path end to
# end (device selection, fill accumulation, device->user y-flip), then the
# WhiteIsOpaque device key: by default painted white does not contribute to
# the box (crop semantics); with /WhiteIsOpaque true it does. A final job at
# 144dpi requires the same user-space box, proving Emit unscales the device
# resolution and that a same-device setpagedevice merges with overrides.
#   $1  path to the built xpost binary
#   $2  path to bbox_test.ps
set -u
xpost=$1
script=$2
. "$(dirname "$0")/verdict.sh"
expect='%%BoundingBox: 10 10 50 60'
out=$("$xpost" -q -d bbox -o /dev/null "$script" </dev/null 2>&1)
status=$?
printf '%s\n' "$out"
verdict_run "$status" "$out" "the bbox job" || exit 1
printf '%s\n' "$out" | grep -qx "$expect" || exit 1

tmp=${TMPDIR:-/tmp}/bbox-wio-$$.ps
trap 'rm -f "$tmp"' EXIT INT TERM
cat > "$tmp" <<'PSEOF'
<< /OutputDevice /bbox /PageSize [200 200] /HWResolution [72 72] >> setpagedevice
0 setgray newpath 10 10 moveto 40 0 rlineto 0 40 rlineto -40 0 rlineto closepath fill
1 setgray newpath 100 100 moveto 50 0 rlineto 0 50 rlineto -50 0 rlineto closepath fill
showpage
<< /WhiteIsOpaque true >> setpagedevice
0 setgray newpath 10 10 moveto 40 0 rlineto 0 40 rlineto -40 0 rlineto closepath fill
1 setgray newpath 100 100 moveto 50 0 rlineto 0 50 rlineto -50 0 rlineto closepath fill
showpage
<< /HWResolution [144 144] /WhiteIsOpaque false >> setpagedevice
0 setgray newpath 10 10 moveto 40 0 rlineto 0 40 rlineto -40 0 rlineto closepath fill
1 setgray newpath 100 100 moveto 50 0 rlineto 0 50 rlineto -50 0 rlineto closepath fill
showpage
quit
PSEOF
out=$("$xpost" -q -d null -o /dev/null "$tmp" </dev/null 2>&1)
status=$?
printf '%s\n' "$out"
verdict_run "$status" "$out" "the WhiteIsOpaque job" || exit 1
printf '%s\n' "$out" | grep -q '%%BoundingBox: 10 10 50 50' || exit 1
printf '%s\n' "$out" | grep -q '%%BoundingBox: 10 10 150 150' || exit 1
test "$(printf '%s\n' "$out" | grep -c '%%BoundingBox: 10 10 50 50')" = 2 || exit 1
# Text reaches the box by a path of its own: an extent-tracking device
# needs no glyph rasterization, so each glyph contributes its ink box
# rather than its pixels. The numbers depend on whichever font resolves,
# so what is required here holds for any of them: the box is not empty,
# it starts no further left than the pen, the ascender of the capital
# rises above the baseline and the descender of the g falls below it.
# An even-odd frame at fractional coordinates: the box must be the
# path's own vertices, identical at 72 and 144 dpi. The frame's interior
# resolves to pixel-band rectangles on the polygon route, whose rows sit
# on the device grid, so a box read off the bands grows with the grid's
# coarseness; the bbox device takes the whole path instead (FillPath)
# and this holds it to that. Quarter-point coordinates are exact in
# binary at both resolutions, so the required strings are exact.
eo=${TMPDIR:-/tmp}/bbox-eo-$$.ps
trap 'rm -f "$tmp" "$eo"' EXIT INT TERM
cat > "$eo" <<'PSEOF'
<< /OutputDevice /bbox /PageSize [200 200] /HWResolution [72 72] >> setpagedevice
0 setgray
newpath 10.25 10.75 moveto 89.25 10.75 lineto 89.25 60.75 lineto 10.25 60.75 lineto closepath
        20.25 20.75 moveto 20.25 50.75 lineto 79.25 50.75 lineto 79.25 20.75 lineto closepath
eofill
showpage
<< /HWResolution [144 144] >> setpagedevice
0 setgray
newpath 10.25 10.75 moveto 89.25 10.75 lineto 89.25 60.75 lineto 10.25 60.75 lineto closepath
        20.25 20.75 moveto 20.25 50.75 lineto 79.25 50.75 lineto 79.25 20.75 lineto closepath
eofill
showpage
quit
PSEOF
out=$("$xpost" -q -d null -o /dev/null "$eo" </dev/null 2>&1)
status=$?
printf '%s\n' "$out"
verdict_run "$status" "$out" "the even-odd exactness job" || exit 1
test "$(printf '%s\n' "$out" | grep -c '%%HiResBoundingBox: 10.25 10.75 89.25 60.75')" = 2 \
    || { echo "FAIL: the even-odd box is not the path's own, at both resolutions"; exit 1; }
echo "even-odd exact bounding box OK"

txt=${TMPDIR:-/tmp}/bbox-text-$$.ps
trap 'rm -f "$tmp" "$eo" "$txt"' EXIT INT TERM
cat > "$txt" <<'PSEOF'
/Helvetica findfont 24 scalefont setfont
20 40 moveto (Ag) show
showpage
quit
PSEOF
out=$("$xpost" -q -d bbox -o /dev/null "$txt" </dev/null 2>&1)
status=$?
printf '%s\n' "$out"
verdict_run "$status" "$out" "the text job" || exit 1
box=$(printf '%s\n' "$out" | grep -m1 '^%%BoundingBox:')
[ -n "$box" ] || { echo "FAIL: shown text produced no bounding box"; exit 1; }
set -- $box
llx=$2; lly=$3; urx=$4; ury=$5
[ "$urx" -gt "$llx" ] && [ "$ury" -gt "$lly" ] \
    || { echo "FAIL: the text box is empty: $box"; exit 1; }
[ "$llx" -ge 19 ] || { echo "FAIL: the text box starts left of the pen: $box"; exit 1; }
[ "$ury" -gt 40 ] || { echo "FAIL: nothing rises above the baseline: $box"; exit 1; }
[ "$lly" -lt 40 ] || { echo "FAIL: nothing falls below the baseline: $box"; exit 1; }
echo "text bounding box OK ($box)"

exit 0
