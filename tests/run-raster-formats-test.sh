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
case $xpost in /*) ;; *) xpost=$PWD/$xpost ;; esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/paint.ps" <<'PSEOF'
1 0 0 setrgbcolor
newpath 10 10 moveto 60 10 lineto 60 40 lineto closepath fill
0 0 1 setrgbcolor
newpath 20 20 moveto 30 0 rlineto 0 30 rlineto closepath fill
showpage
quit
PSEOF

fail=0
for sub in rgb argb bgr bgra; do
    "$xpost" -q --no-sandbox -d "raster:$sub" -o /dev/null "$work/paint.ps" \
        </dev/null >/dev/null 2>&1
    status=$?
    [ "$status" -eq 0 ] || { echo "FAIL: raster:$sub exited with status $status"; fail=1; }
done

# a name that names no format, and no name at all
for sub in "nosuchformat" "" "rg" "argbx"; do
    "$xpost" -q --no-sandbox -d "raster:$sub" -o /dev/null "$work/paint.ps" \
        </dev/null >/dev/null 2>&1
    status=$?
    [ "$status" -eq 0 ] \
        || { echo "FAIL: raster:'$sub' exited with status $status"; fail=1; }
done

# and the device with no format named at all
"$xpost" -q --no-sandbox -d raster -o /dev/null "$work/paint.ps" \
    </dev/null >/dev/null 2>&1
status=$?
[ "$status" -eq 0 ] || { echo "FAIL: raster exited with status $status"; fail=1; }

[ "$fail" = 0 ] || { echo "FAILURES: the formats above"; exit 1; }
echo "SUCCESS"
