#!/bin/sh
#
# Guard the device-driver skeleton: the compiled-buffer device fleet must
# reach the shared mechanics through xpost_dev_driver.h rather than
# hand-writing them, so the contract stated there stays the only
# statement of how a device folds operands, reaches its private struct,
# and which rectangle FillRect paints.
#
# Exemptions: xpost_dev_win32.c cannot be compiled or exercised on the
# platforms this suite runs on, so its migration cannot be gated here.
# xpost_dev_generic.c is held to the private-struct rule only: its two
# compiled base-class FillRects deliberately mirror the PostScript
# classes' floor-space arithmetic (pinned by the golden render), not the
# integer contract path.
#
# Usage: check-device-skeleton.sh <src/lib directory>

set -eu

libdir=${1:?usage: check-device-skeleton.sh <src/lib directory>}

fleet="xpost_dev_bgr.c xpost_dev_jpeg.c xpost_dev_png.c xpost_dev_raster.c xpost_dev_xcb.c"

fail=0

# 1. Private-struct access goes through xpost_dev_private_get/put: no raw
#    memory accessor in any device source (the helpers in the driver
#    header hold the only calls).
for f in $fleet xpost_dev_generic.c; do
    hits=$(grep -nE '\bxpost_memory_(get|put)\(' "$libdir/$f" || true)
    if [ -n "$hits" ]; then
        echo "check-device-skeleton: raw memory accessor in $f:" >&2
        printf '%s\n' "$hits" >&2
        echo "Reach the private struct through xpost_dev_private_get/put." >&2
        fail=1
    fi
done

# 2. Operand folding goes through xpost_dev_num_to_*: hand-folding is
#    recognisable by its realtype dispatch, which a migrated device no
#    longer needs.
for f in $fleet; do
    hits=$(grep -nE '\brealtype\b' "$libdir/$f" || true)
    if [ -n "$hits" ]; then
        echo "check-device-skeleton: hand-folded numeric operand in $f:" >&2
        printf '%s\n' "$hits" >&2
        echo "Fold operands with the xpost_dev_num_to_* helpers." >&2
        fail=1
    fi
done

# 3. A device registering a FillRect operator paints the contract
#    rectangle: its extent arithmetic must be xpost_dev_rect_normalize,
#    not a private restatement.
for f in $fleet; do
    if grep -q '"FillRect"' "$libdir/$f" &&
       ! grep -q 'xpost_dev_rect_normalize' "$libdir/$f"; then
        echo "check-device-skeleton: $f registers FillRect without xpost_dev_rect_normalize()." >&2
        echo "The painted rectangle is defined once, in xpost_dev_driver.h." >&2
        fail=1
    fi
done

# 4. The fleet includes the contract header it is being held to.
for f in $fleet; do
    if ! grep -q 'xpost_dev_driver\.h' "$libdir/$f"; then
        echo "check-device-skeleton: $f does not include xpost_dev_driver.h." >&2
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    exit 1
fi

echo "check-device-skeleton: ok (fleet behind the driver contract)"
