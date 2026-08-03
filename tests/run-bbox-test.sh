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
expect='%%BoundingBox: 10 10 50 60'
out=$("$xpost" -q -d bbox -o /dev/null "$script" </dev/null 2>&1)
printf '%s\n' "$out"
printf '%s\n' "$out" | grep -qx "$expect" || exit 1

tmp=${TMPDIR:-/tmp}/bbox-wio-$$.ps
trap 'rm -f "$tmp"' 0
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
printf '%s\n' "$out"
printf '%s\n' "$out" | grep -q '%%BoundingBox: 10 10 50 50' || exit 1
printf '%s\n' "$out" | grep -q '%%BoundingBox: 10 10 150 150' || exit 1
test "$(printf '%s\n' "$out" | grep -c '%%BoundingBox: 10 10 50 50')" = 2 || exit 1
# Text reaches the box by a path of its own: an extent-tracking device
# needs no glyph rasterization, so each glyph contributes its ink box
# rather than its pixels. The numbers depend on whichever font resolves,
# so what is required here holds for any of them: the box is not empty,
# it starts no further left than the pen, the ascender of the capital
# rises above the baseline and the descender of the g falls below it.
txt=${TMPDIR:-/tmp}/bbox-text-$$.ps
trap 'rm -f "$tmp" "$txt"' 0
cat > "$txt" <<'PSEOF'
/Helvetica findfont 24 scalefont setfont
20 40 moveto (Ag) show
showpage
quit
PSEOF
out=$("$xpost" -q -d bbox -o /dev/null "$txt" </dev/null 2>&1)
status=$?
printf '%s\n' "$out"
if [ "$status" -ne 0 ]; then
    echo "FAIL: the interpreter exited with status $status on the text job"
    exit 1
fi
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
