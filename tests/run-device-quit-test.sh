#!/bin/sh
# Meson test wrapper: run the quit check (quit_run_test.ps) against every
# built device.
#
# quit ends the run, and what a run does after it has ended is the
# device's teardown. A device whose Destroy is a PostScript procedure is
# torn down by re-entering the interpreter, so it is the one that can
# carry the re-entry on into the program the quit abandoned; a device
# whose Destroy is an operator is called directly and never could. Both
# kinds are run, because which kind a device is is not a property the
# test can see and not one a device is held to.
#
#   $1  path to the built xpost binary
#   $2  path to quit_run_test.ps
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
ran=0

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
    ran=$((ran + 1))
    if verdict_ok "$out" "$dev"; then
        echo "OK   $dev"
    else
        fail=1
    fi
done

if command -v xvfb-run >/dev/null 2>&1; then
    out=$(xvfb-run -a "$xpost" -q $ns -d xcb "$script" </dev/null 2>&1)
    case "$out" in
        *"wrong device"*) echo "SKIP xcb (not built in)" ;;
        *)
            ran=$((ran + 1))
            if verdict_ok "$out" "xcb"; then
                echo "OK   xcb"
            else
                fail=1
            fi
            ;;
    esac
else
    echo "SKIP xcb (no xvfb-run)"
fi

rm -rf "$work"
# a run that skipped every device would have asked nothing
if [ "$ran" -lt 8 ]; then
    echo "FAILURES: only $ran devices were tried; that is not this build"
    exit 1
fi
if [ "$fail" -ne 0 ]; then
    echo "FAILURES: a run did not stop at its quit"
    exit 1
fi
echo "SUCCESS ($ran devices)"
exit 0
