#!/bin/sh
# Meson test wrapper: the raster device's pixel formats.
#
# The device takes its format from the name after the colon in the device
# string -- raster:argb and the rest -- and falls back to rgb when no
# name is given. Only the fallback was ever exercised, so neither the
# selection of the others nor the buffer each one sizes had run.
#
# A name that matches none of them is asked for too. It has to be
# harmless: the format is the one taken when no name is given, rather
# than whatever the memory happened to hold.
#
#   $1  path to the built xpost binary
set -u
xpost=$1
# an absolute path may begin with a drive letter as well as a slash;
# prepending the working directory to one of those makes every
# invocation a path that does not exist
case $xpost in /* | ?:/* | ?:\\*) ;; *) xpost=$PWD/$xpost ;; esac
. "$(dirname "$0")/verdict.sh"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/paint.ps" <<'PSEOF'
1 0 0 setrgbcolor
newpath 10 10 moveto 60 10 lineto 60 40 lineto closepath fill
0 0 1 setrgbcolor
newpath 20 20 moveto 30 0 rlineto 0 30 rlineto closepath fill
% Text as well as fills: a glyph's edge pixels are blended into the
% device's own buffer, which is a different method from the one that
% fills reach and one these devices have to carry themselves.
0 setgray /Helvetica findfont 14 scalefont setfont
10 60 moveto (Ag) show
showpage
quit
PSEOF

fail=0

paint() {   # $1 the device string
    p_out=$("$xpost" -q --no-sandbox -d "$1" -o /dev/null "$work/paint.ps" \
            </dev/null 2>&1)
    verdict_run "$?" "$p_out" "$1" || fail=1
}

for sub in rgb argb bgr bgra; do
    paint "raster:$sub"
done

# a name that names no format, and no name at all
for sub in "nosuchformat" "" "rg" "argbx"; do
    paint "raster:$sub"
done

# and the device with no format named at all
paint raster

# the other device that keeps its pixels in a buffer of its own
paint bgr

[ "$fail" = 0 ] || { echo "FAILURES: the formats above"; exit 1; }
echo "SUCCESS"
