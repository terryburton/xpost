#!/bin/sh
# Meson test wrapper: run the device-method-contract safety check
# (device_contract_test.ps) against every headless-capable built device.
# The test feeds each device method its boundary inputs (degenerate,
# inverted, fractional, out-of-range) and requires no errors and an
# emitted page. Window devices (xcb, gdi) need a display and are not run.
#
#   $1  path to the built xpost binary
#   $2  path to device_contract_test.ps
set -u
xpost=$1
script=$2

if "$xpost" -h 2>/dev/null | grep -q -- '--no-sandbox'; then
    ns='--no-sandbox'
else
    ns=''
fi

work=$(mktemp -d)
devices='pgm ppm pbm tiff null bbox raster bgr png pdfwrite svgwrite dscwrite jpeg'
fail=0

for dev in $devices; do
    out=$("$xpost" -q $ns -d "$dev" -o "$work/out.$dev" "$script" </dev/null 2>&1)
    case "$out" in
        *"wrong device"*) echo "SKIP $dev (not built in)"; continue ;;
    esac
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
    echo "FAILURES: a device rejected a boundary input"
    exit 1
fi
echo "SUCCESS"
exit 0
