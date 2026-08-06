#!/bin/sh
# Meson test wrapper: a restore back past a setpagedevice makes the retired
# device current again, and painting on it must not follow its released
# buffer (device_restore_retired_test.ps), run against every built device.
#
# The devices that keep their raster in a malloc'd buffer are the ones that
# can follow a cleared handle here; the rest are run because the rule is the
# same for all of them and the sequence is an ordinary page-size change.
#
#   $1  path to the built xpost binary
#   $2  path to device_restore_retired_test.ps
set -u
xpost=$1
script=$2
. "$(dirname "$0")/verdict.sh"

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
    if verdict_ok "$out" "$dev"; then
        echo "OK   $dev"
    else
        fail=1
    fi
done

rm -rf "$work"
if [ "$fail" -ne 0 ]; then
    echo "FAILURES: a device retired by setpagedevice did not survive a restore"
    exit 1
fi
echo "SUCCESS"
exit 0
