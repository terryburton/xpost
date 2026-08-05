#!/bin/sh
#
# Guard the device-driver skeleton: the compiled-buffer device fleet must
# reach the shared mechanics through xpost_dev_driver.h rather than
# hand-writing them, so the contract stated there stays the only
# statement of how a device folds operands, reaches its private struct,
# and which rectangle FillRect paints. The PostScript device classes are
# held to the same idea from their side: what every class does the same
# way is written once and referred to.
#
# Exemptions: xpost_dev_win32.c cannot be compiled or exercised on the
# platforms this suite runs on, so its migration cannot be gated here.
# xpost_dev_generic.c is held to the private-struct rule only: its two
# compiled base-class FillRects deliberately mirror the PostScript
# classes' floor-space arithmetic (pinned by the golden render), not the
# integer contract path.
#
# Sources are named rather than globbed out of a directory: a built tree
# leaves object files beside them whose debug information matches every
# pattern here, so a scan would read green where nothing was built and
# red where something was.
#
# Usage: check-device-skeleton.sh <source root>

set -eu

src=${1:?usage: check-device-skeleton.sh <source root>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"

libdir="$src/src/lib"
guard_require_dir "$libdir" "the library source directory"

fleet="xpost_dev_bgr.c xpost_dev_jpeg.c xpost_dev_png.c xpost_dev_raster.c xpost_dev_xcb.c"

# every file that defines a device class
classes="image.ps pgmimage.ps pbmimage.ps ppmimage.ps tiffimage.ps
         nulldev.ps bboxdev.ps pdfwrite.ps svgwrite.ps dscwrite.ps"

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

# 5. A class dictionary that would not take a method leaves the device
#    incomplete, so the refusal reaches the caller: the value of every
#    xpost_dict_put is either returned or tested, and a test never
#    answers success. Textual, so it holds for the sources this platform
#    cannot compile as well as the ones it can.
for f in $fleet xpost_dev_generic.c xpost_dev_win32.c; do
    hits=$(awk '
        /xpost_dict_put[ \t]*\(/ {
            if ($0 !~ /=/ && $0 !~ /return/)
                printf "%s:%d: the refusal is discarded\n", FILENAME, FNR
            win = 8; sawif = 0; next
        }
        win > 0 {
            win--
            if ($0 ~ /^[ \t]*if \(ret\)/) { sawif = 1; next }
            if (sawif && $0 ~ /^[ \t]*return 0;/)
                printf "%s:%d: the refusal is answered with success\n", FILENAME, FNR
            sawif = 0
        }
    ' "$libdir/$f")
    if [ -n "$hits" ]; then
        echo "check-device-skeleton: registration refusal ignored in $f:" >&2
        printf '%s\n' "$hits" >&2
        echo "A device that could not register a method must not load." >&2
        fail=1
    fi
done

# 6. The class-to-instance copy is one procedure. A class dictionary
#    stores /.copydict, and Create (and every C driver, which fetches it
#    from the class before specialising the copy) calls it. Each class
#    used to carry its own body, and two of them carried a shorter one
#    that left the output file name off the instance, so a device made
#    from those classes wrote wherever its Emit defaulted to. A class
#    may name .classcopydict; it may not restate it.
copies=0
for f in $classes; do
    p="$src/data/$f"
    [ -f "$p" ] || continue
    if grep -qE '^[ \t]*/\.copydict[ \t]*\{' "$p"; then
        echo "check-device-skeleton: $f writes a class copy of its own:" >&2
        grep -nE '^[ \t]*/\.copydict[ \t]*\{' "$p" >&2
        echo "The copy is .xpostsys /.classcopydict; store that, do not restate it." >&2
        fail=1
    fi
    grep -qE '/\.copydict[ \t]+//\.xpostsys[ \t]+/\.classcopydict[ \t]+get' "$p" &&
        copies=$((copies + 1))
done
if [ "$copies" -lt 5 ]; then
    echo "check-device-skeleton: only $copies classes store the shared class copy;" >&2
    echo "expected every class that defines /.copydict to name .classcopydict." >&2
    fail=1
fi
if [ "$(grep -c '\.xpostsys /\.classcopydict {' "$src/data/device.ps")" != 1 ]; then
    echo "check-device-skeleton: the shared class copy is not defined once in device.ps." >&2
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi

echo "check-device-skeleton: ok (fleet behind the driver contract, $copies classes behind one copy)"
