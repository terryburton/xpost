#!/bin/sh
# Meson test wrapper: run the device-teardown discipline check
# (device_destroy_test.ps) against every built device. The test Destroys
# the live device twice and job-end teardown makes a third call: each
# must be a no-op after the first, per the device contract. The window
# device holds a display connection, a window, a pixmap and a graphics
# context, so it has the most to release twice; it runs under a virtual
# display where the host provides one. The Windows window devices need
# another platform and are not run.
#
#   $1  path to the built xpost binary
#   $2  path to device_destroy_test.ps
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

if command -v xvfb-run >/dev/null 2>&1; then
    out=$(xvfb-run -a "$xpost" -q $ns -d xcb "$script" </dev/null 2>&1)
    case "$out" in
        *"wrong device"*) echo "SKIP xcb (not built in)" ;;
        *)
            if printf '%s\n' "$out" | grep -q 'SUCCESS$'; then
                echo "OK   xcb"
            else
                echo "FAIL xcb:"
                printf '%s\n' "$out" | tail -3
                fail=1
            fi
            ;;
    esac
else
    echo "SKIP xcb (no xvfb-run)"
fi

rm -rf "$work"
if [ "$fail" -ne 0 ]; then
    echo "FAILURES: a device did not survive repeated Destroy"
    exit 1
fi
echo "SUCCESS"
exit 0
