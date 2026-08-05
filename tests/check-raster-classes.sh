#!/bin/sh
# Guard the raster-class derivation: the grayscale and rgb classes are
# parameter files over the .makerasterclass generator, the bilevel and
# TIFF classes are dict-copies of those overriding emission, and the
# shared method suite is defined exactly once, in the prototype
# (data/image.ps). A standalone method definition reappearing in a
# class file means the classes are diverging into twins again.
#
#   $1  path to the source tree root
set -u
src=${1:?usage: check-raster-classes.sh <source root>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
data=$src/data
fail=0

methods='Create Destroy PutPix GetPix DrawLine DrawRect FillRect FillPoly Emit .copydict .intersect .maxmin'

# the generated classes are parameter files: one generator call each,
# and no method definition of their own
for f in pgmimage.ps ppmimage.ps; do
    n=$(grep -c '/\.makerasterclass get exec' "$data/$f" || true)
    if [ "$n" != 1 ]; then
        echo "check-raster-classes: $f calls .makerasterclass $n times, expected exactly 1"
        fail=1
    fi
    for m in $methods; do
        if grep -qE "/$m[[:space:]]*\{" "$data/$f"; then
            echo "check-raster-classes: $f defines /$m; it belongs in the prototype (data/image.ps)"
            fail=1
        fi
    done
done

# the derived classes copy a generated class...
if ! grep -q '\.xpost_PGMIMAGE dup length .* dict copy' "$data/pbmimage.ps"; then
    echo "check-raster-classes: pbmimage.ps no longer derives by dict copy from .xpost_PGMIMAGE"
    fail=1
fi
if ! grep -q '\.xpost_PPMIMAGE dup length .* dict copy' "$data/tiffimage.ps"; then
    echo "check-raster-classes: tiffimage.ps no longer derives by dict copy from .xpost_PPMIMAGE"
    fail=1
fi
# ...and override emission (and, for the bilevel class, the screening
# pixel store), never the shared geometry methods or the Emit dispatch
for f in pbmimage.ps tiffimage.ps; do
    for m in Create Destroy GetPix DrawLine DrawRect FillRect FillPoly Emit .copydict .intersect .maxmin; do
        if grep -qE "/$m[[:space:]]*\{" "$data/$f"; then
            echo "check-raster-classes: $f overrides /$m; derived classes override emission only"
            fail=1
        fi
    done
done

# the suite is defined once, in the prototype. The class copy is not in
# it: every device class shares one copy (.classcopydict, data/device.ps),
# so the prototype names it like the rest of them do and
# check-device-skeleton.sh holds that.
for m in $(echo "$methods" | sed 's/\.copydict//'); do
    n=$(grep -cE "/$m[[:space:]]*\{" "$data/image.ps" || true)
    if [ "$n" != 1 ]; then
        echo "check-raster-classes: /$m defined $n times in data/image.ps, expected exactly 1"
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "check-raster-classes: the raster classes are no longer a single generated suite."
    exit 1
fi
echo "check-raster-classes: ok (one suite, parameter files, dict-copy derivation)"
exit 0
