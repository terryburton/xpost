#!/bin/sh
# Meson test wrapper: the page device's imaging bounding box (PLRM 6.2).
#
# A program supplying /ImagingBBox asserts that it paints no marks
# outside the box, and PLRM 6.2 says of the marks that do fall outside
# that they "may or may not be rendered on the output medium". That is
# what lets a device hold less of the page than the page, which is the
# whole of what the parameter is for: PLRM asks applications to supply
# one wherever they can "since it can improve performance by freeing
# raster memory for other purposes".
#
# Two questions, put to every device the build has:
#
#   1. What a program observes. The rows a device holds and the marks it
#      keeps are the device's own business; the answers are in
#      tests/imaging_bbox_test.ps, which reads them off the running
#      device and reports for itself.
#
#   2. What comes out. A program that keeps to its own assertion must get
#      the page it would have got without the box -- the same page, of
#      the same size, to the byte. That is the strongest thing that can
#      be said about the change and the only one that covers a writer's
#      whole output rather than the raster behind it, so it is asked of
#      every device that writes a file, whether or not that device holds
#      any part of the page differently.
#
# Four pages carry the second question, each covering a way of getting
# the run of rows wrong:
#
#   marks    every route a mark takes to the raster -- a fill, a stroke,
#            text, an image and an image mask -- and a first mark that
#            covers the box exactly to the pixel, so a device taking the
#            box a row too far in either direction loses part of it
#   ground   an atypical transfer function, so that the colour the page
#            is cleared to is not white and a device showing plain white
#            over the rows it does not hold is caught
#   scaled   the same marks at a resolution where a point is not a pixel,
#            which is where a box measured in points meets rows
#   offset   the same marks with the page image shifted, which moves the
#            marks and must move the box with them
#
#   $1  path to the built xpost binary
#   $2  path to imaging_bbox_test.ps
set -u
xpost=$1
script=$2
. "$(dirname "$0")/verdict.sh"
. "$(dirname "$0")/device-fleet.sh"

if "$xpost" -h 2>/dev/null | grep -q -- '--no-sandbox'; then
    ns='--no-sandbox'
else
    ns=''
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/marks.body" <<'PSEOF'
% the box, to the pixel: 100 units square with its corners on the box's
% own corners, so a device holding one row too few drops part of it
0.75 setgray
newpath 50 50 moveto 150 50 lineto 150 150 lineto 50 150 lineto
closepath fill
1 0 0 setrgbcolor
newpath 60 60 moveto 140 60 lineto 140 100 lineto closepath fill
0 0 1 setrgbcolor
newpath 70 70 moveto 40 0 rlineto 0 40 rlineto closepath 3 setlinewidth stroke
0 setgray /Helvetica findfont 14 scalefont setfont
60 130 moveto (Ag) show
gsave 40 0 0 40 60 105 6 array astore concat
4 4 8 [4 0 0 -4 0 4] <00408000C0FF40208060A0E01030507090B0D0F0> image
grestore
gsave 30 0 0 30 105 105 6 array astore concat
4 4 true [4 0 0 -4 0 4] <A050A050> imagemask
grestore
showpage
quit
PSEOF

cat > "$work/ground.body" <<'PSEOF'
% erasepage paints the whole page with gray 1.0 through the transfer
% function (PLRM 8.2), which is ordinarily white and here is not
{ 0.35 mul 0.2 add } settransfer
erasepage
0 setgray
newpath 90 90 moveto 110 90 lineto 110 110 lineto 90 110 lineto
closepath fill
showpage
quit
PSEOF

cp "$work/marks.body" "$work/scaled.body"
cp "$work/marks.body" "$work/offset.body"

CASES='marks ground scaled offset'

# each page twice over, once declaring the box and once declaring none:
# everything below the first line is the same text, so the only
# difference between a pair of runs is the request itself
for case in $CASES; do
    rest=
    case $case in
        scaled) rest='/HWResolution [50 91]' ;;
        offset) rest='/PageOffset [0 20]' ;;
    esac
    { printf '<< /PageSize [200 200] %s /ImagingBBox [50 50 150 150] >> setpagedevice\n' \
        "$rest"
      cat "$work/$case.body"; } > "$work/hinted-$case.ps"
    { printf '<< /PageSize [200 200] %s /ImagingBBox null >> setpagedevice\n' \
        "$rest"
      cat "$work/$case.body"; } > "$work/plain-$case.ps"
done

fail=0

# The roster less what a build may not have the library for: a roster
# that skipped from end to end asks nothing and would report the same
# success as one that answered everywhere.
floor=0
for dev in $DEVICE_FLEET_ALL; do
    case " $DEVICE_FLEET_OPTIONAL " in *" $dev "*) continue ;; esac
    floor=$((floor + 1))
done

one_device() {
    dev=$1
    d_fail=0

    # 1. what the program observes
    out=$("$xpost" -q $ns -d "$dev" -o "$work/probe.$dev" "$script" \
          </dev/null 2>&1)
    st=$?
    case "$out" in
        *"wrong device"*) echo "$dev: not built in; not asked"; return 2 ;;
    esac
    printf '%s\n' "$out" | grep -E '^FAIL' | sed "s/^/$dev: /"
    verdict_run "$st" "$out" "the imaging-bbox job on $dev" || d_fail=1
    verdict_ok "$out" "the imaging-bbox check on $dev" || d_fail=1

    # 2. what comes out
    wrote=no
    for case in $CASES; do
        rm -f "$work/h-$case.$dev" "$work/p-$case.$dev"
        out=$("$xpost" -q $ns -d "$dev" -o "$work/h-$case.$dev" \
              "$work/hinted-$case.ps" </dev/null 2>&1)
        verdict_run "$?" "$out" "the hinted $case page on $dev" || d_fail=1
        out=$("$xpost" -q $ns -d "$dev" -o "$work/p-$case.$dev" \
              "$work/plain-$case.ps" </dev/null 2>&1)
        verdict_run "$?" "$out" "the unhinted $case page on $dev" || d_fail=1

        # Whether there is a page to compare is read off the run rather
        # than assumed: a device that keeps its raster for whoever
        # embedded the interpreter, or that paints nothing at all,
        # leaves nothing at the path it was given and is held to the
        # first question alone.
        [ -s "$work/h-$case.$dev" ] || [ -s "$work/p-$case.$dev" ] || continue
        wrote=yes
        if cmp -s "$work/h-$case.$dev" "$work/p-$case.$dev"; then
            echo "$dev: the $case page is the same with the box and without it"
        else
            echo "FAILURES: $dev: declaring the box changed the $case page"
            d_fail=1
        fi
    done
    if [ "$wrote" = no ]; then
        echo "$dev: writes no page file; held to what the program observes"
        echo "$dev" >> "$work/nofile"
    fi

    [ "$d_fail" -eq 0 ] || return 1
    return 0
}

fleet_each one_device $DEVICE_FLEET_ALL || fail=1

# The devices with no page file to compare, and why: raster and bgr keep
# their raster for whoever embedded the interpreter rather than writing
# it, null paints nothing, and bbox records the extent of a page instead
# of its pixels. The reading is taken from the runs above and held
# against this list, so a device that has quietly stopped writing its
# page fails here rather than leaving one fewer comparison made.
NO_FILE='raster bgr null bbox'
want=$(printf '%s\n' $NO_FILE | sort | tr '\n' ' ')
got=$([ -f "$work/nofile" ] && sort "$work/nofile" | tr '\n' ' ')
if [ "$want" != "$got" ]; then
    echo "FAILURES: the devices with no page to compare are [$got],"
    echo "      and the ones named here as writing none are [$want]"
    fail=1
fi

if [ "$fleet_asked" -lt "$floor" ]; then
    echo "FAILURES: $fleet_asked of the roster answered and $floor of it is"
    echo "      made without an optional library"
    exit 1
fi
[ "$fail" -eq 0 ] || exit 1
echo "imaging-bbox: held on $fleet_asked device(s)"
echo SUCCESS
