#!/bin/sh
# Meson test wrapper: run the device-method contract check
# (device_contract_test.ps) against the marking roster of
# tests/device-fleet.sh, one device per marking implementation.
# The test feeds each device method its boundary inputs (degenerate,
# inverted, fractional, out-of-range) and requires no errors and an
# emitted page; on a device that reports its own pixels back it also
# asserts what the marking methods painted. Window devices need a
# display: xcb runs under a virtual one (xvfb-run) when the host
# provides it, gdi is not run.
#
# The behaviour tier skips itself where it cannot see the raster, so a
# run in which it skipped everywhere would still pass. The count below
# holds it to running on the devices that can witness it: a device that
# stops reporting its pixels is a regression, not a reason to assert
# less.
#
#   $1  path to the built xpost binary
#   $2  path to device_contract_test.ps
set -u
xpost=$1
script=$2
. "$(dirname "$0")/verdict.sh"
. "$(dirname "$0")/device-fleet.sh"

# devices whose GetPix reports back what a marking method wrote
readback_min=8
readback=0

if "$xpost" -h 2>/dev/null | grep -q -- '--no-sandbox'; then
    ns='--no-sandbox'
else
    ns=''
fi

work=$(mktemp -d)
devices=$DEVICE_FLEET_MARKING
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
    if printf '%s\n' "$out" | grep -q '^READBACK$'; then
        readback=$((readback + 1))
    fi
    if verdict_ok "$out" "$dev"; then
        echo "OK   $dev"
    else
        fail=1
    fi
done

# the xcb window device, on a private virtual display; its FillRect,
# PutPix and DrawLine methods see the same boundary inputs as the
# headless devices above
if command -v xvfb-run >/dev/null 2>&1; then
    out=$(xvfb-run -a "$xpost" -q $ns -d xcb "$script" </dev/null 2>&1)
    case "$out" in
        *"wrong device"*) echo "SKIP xcb (not built in)" ;;
        *)
            if verdict_ok "$out" "xcb"; then
                echo "OK   xcb"
            else
                # the whole run, not just the failure lines: this device
                # talks to a display server, and what it says on the way
                # to the failure is the diagnosis
                printf '%s\n' "$out" | sed 's/^/      /'
                fail=1
            fi
            ;;
    esac
else
    echo "SKIP xcb (no xvfb-run)"
fi

rm -rf "$work"
if [ "$fail" -ne 0 ]; then
    echo "FAILURES: a device rejected a boundary input"
    exit 1
fi
if [ "$readback" -lt "$readback_min" ]; then
    echo "FAILURES: the behaviour tier ran on $readback devices, fewer than $readback_min"
    echo "      a device that no longer reports its pixels back silently"
    echo "      stops being asserted about; restore its GetPix"
    exit 1
fi
echo "SUCCESS ($readback devices witnessed the behaviour tier)"
exit 0
