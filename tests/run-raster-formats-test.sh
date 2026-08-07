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

% These devices keep their pixels in a buffer of their own and write no
% file, so nothing outside the interpreter can see what the marks above
% did. The buffer is therefore read here, before the page goes: a
% painted pixel and one nothing reached, which must not answer alike. An
% interpreter that made no device, or one whose buffer was sized and
% never written, prints nothing and exits cleanly, and that is what this
% line is against.
/device? { .privatedict /.graphicsdict get /currgstate get /device get } def
/dev device? def
/probe {        % x y  ->  the components summed
    % the marks above are placed in user space and the buffer is indexed
    % in the device's, so the point is carried across by the same matrix
    % that carried the marks
    transform round cvi exch round cvi exch
    /d0 count 2 sub def
    dev dup /GetPix get exec
    /n count d0 sub def
    0 n { add } repeat
} bind def
50 15 probe  5 55 probe
ne { (READBACK: the buffer holds a mark\n) }
   { (READBACK: the painted pixel reads as the unpainted one\n) } ifelse
print
showpage
quit
PSEOF

fail=0

paint() {   # $1 the device string
    p_out=$("$xpost" -q --no-sandbox -d "$1" -o /dev/null "$work/paint.ps" \
            </dev/null 2>&1)
    verdict_run "$?" "$p_out" "$1" || fail=1
    case "$p_out" in
        *"READBACK: the buffer holds a mark"*) ;;
        *) echo "FAIL $1: nothing was painted into the device's buffer"
           fail=1 ;;
    esac
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
