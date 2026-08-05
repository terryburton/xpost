#!/bin/sh
# Meson test wrapper: a setpagedevice whose maker fails must leave the
# incumbent device painting (setpagedevice_failure_test.ps), run against
# every built device.
#
# The devices that keep their raster in a malloc'd buffer are the ones with
# something to lose: retiring them frees that buffer, so a graphics state
# still naming a retired device reads freed memory on the next mark. The
# devices that hold their raster as PostScript objects are run too -- they
# cost nothing to check and the rule is the same for all of them.
#
#   $1  path to the built xpost binary
#   $2  path to setpagedevice_failure_test.ps
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
    echo "FAILURES: a caught setpagedevice failure did not leave the device painting"
    exit 1
fi
echo "SUCCESS"
exit 0
