#!/bin/sh
# Meson test wrapper: run a script on the ppm raster device, which is needed to
# exercise the raster-device paths (the form cache builds an image device and
# clears it with the rectangle-fill operators). Passes iff the script reports
# SUCCESS.
#   $1  path to the built xpost binary
#   $2  path to the test .ps
set -u
xpost=$1
script=$2
out=$("$xpost" -q --no-sandbox -d ppm -o /dev/null "$script" </dev/null 2>&1)
printf '%s\n' "$out"
printf '%s\n' "$out" | grep -q '^SUCCESS$'
