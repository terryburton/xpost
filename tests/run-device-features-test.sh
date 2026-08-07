#!/bin/sh
# Meson test wrapper: every painting operator through the marking roster
# of tests/device-fleet.sh, one device per marking implementation.
#
# A device implements some methods and inherits the rest. Which it must
# implement itself depends on how it keeps its raster, and a device that
# keeps a buffer of its own while inheriting a method that reaches for
# the base class's raster fails only for the operators using that method.
# That is how both buffer devices came to be unable to paint a glyph
# while filling, stroking and measuring text perfectly well: nothing
# asked them to paint one.
#
#   $1  path to the built xpost binary
#   $2  path to the PostScript workload
set -u
xpost=$1
script=$2
. "$(dirname "$0")/verdict.sh"
. "$(dirname "$0")/device-fleet.sh"
# an absolute path may begin with a drive letter as well as a slash;
# prepending the working directory to one of those makes every
# invocation a path that does not exist
case $xpost in /* | ?:/* | ?:\\*) ;; *) xpost=$PWD/$xpost ;; esac
case $script in /* | ?:/* | ?:\\*) ;; *) script=$PWD/$script ;; esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The window devices need a display and the Windows ones another platform;
# everything else renders headless. The raster device is named once per
# pixel format, since each keeps its buffer differently.
# the marking roster, and the raster device once per pixel format,
# since each keeps its buffer differently
devices="$DEVICE_FLEET_MARKING raster:rgb raster:argb raster:bgr raster:bgra"

fail=0
ran=0
for dev in $devices; do
    out=$("$xpost" -q --no-sandbox -d "$dev" -o "$work/out.bin" "$script" \
          </dev/null 2>&1)
    status=$?
    ran=$((ran + 1))
    if [ "$status" -ne 0 ]; then
        echo "FAIL: $dev exited with status $status"
        fail=1
        continue
    fi
    verdict_ok "$out" "$dev" || fail=1
done

# The window device needs a display. Where one can be conjured, it is
# held to the same set: it keeps its own drawable and so carries the same
# risk of inheriting a method that reaches for a raster it does not have.
if command -v xvfb-run >/dev/null 2>&1; then
    out=$(xvfb-run -a "$xpost" -q --no-sandbox -d xcb "$script" </dev/null 2>&1)
    status=$?
    ran=$((ran + 1))
    if [ "$status" -ne 0 ]; then
        echo "FAIL: xcb exited with status $status"
        fail=1
    elif ! verdict_ok "$out" "xcb"; then
        fail=1
    fi
else
    echo "note: no virtual display, the window device was not tried"
fi

[ "$ran" -gt 0 ] || { echo "FAILURES: no device was tried"; exit 1; }
[ "$fail" = 0 ] || { echo "FAILURES: the devices above"; exit 1; }
echo "SUCCESS ($ran devices)"
