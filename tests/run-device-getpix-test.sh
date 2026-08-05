#!/bin/sh
# Meson test wrapper: GetPix on a destroyed device must answer rather than
# follow the released buffer handle (device_getpix_destroyed_test.ps), run
# against every built device.
#
# The devices that keep their raster in a malloc'd buffer are the ones that
# can follow a cleared handle; the devices that hold theirs as PostScript
# objects are run too, because the rule is the same for all of them and a
# device that grows its own buffer later is then already covered.
#
#   $1  path to the built xpost binary
#   $2  path to device_getpix_destroyed_test.ps
set -u
xpost=$1
script=$2

if "$xpost" -h 2>/dev/null | grep -q -- '--no-sandbox'; then
    ns='--no-sandbox'
else
    ns=''
fi

work=$(mktemp -d)
devices='pgm ppm pbm tiff null bbox raster bgr png pngalpha pdfwrite svgwrite dscwrite jpeg'
fail=0

for dev in $devices; do
    out=$("$xpost" -q $ns -d "$dev" -o "$work/out.$dev" "$script" </dev/null 2>&1)
    st=$?
    case "$out" in
        *"wrong device"*) echo "SKIP $dev (not built in)"; continue ;;
    esac
    if [ "$st" -ne 0 ]; then
        echo "FAIL $dev: the interpreter exited with status $st"
        printf '%s\n' "$out" | tail -3
        fail=1
        continue
    fi
    if printf '%s\n' "$out" | grep -q 'SUCCESS$'; then
        echo "OK   $dev"
    else
        echo "FAIL $dev:"
        printf '%s\n' "$out" | tail -3
        fail=1
    fi
done

rm -rf "$work"
if [ "$fail" -ne 0 ]; then
    echo "FAILURES: GetPix did not survive a destroyed device"
    exit 1
fi
echo "SUCCESS"
exit 0
