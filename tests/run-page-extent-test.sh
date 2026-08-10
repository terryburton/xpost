#!/bin/sh
# Meson test wrapper: run the page-extent check (page_extent_test.ps)
# against the devices that mark a page.
#
# The question is what a device does with a page whose pixels outnumber
# what a pixel's position is counted in, so it is put to devices that
# have a raster to index. The null device draws nothing and the
# bounding-box device keeps a box rather than pixels; neither has the
# limit and neither is asked.
#
#   $1  path to the built xpost binary
#   $2  path to page_extent_test.ps
set -u
xpost=$1
script=$2
. "$(dirname "$0")/verdict.sh"
. "$(dirname "$0")/device-fleet.sh"

if "$xpost" -h 2>/dev/null | grep -q -- '--no-sandbox'; then
    ns='--no-sandbox'
else
    ns=''
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fail=0
asked=0

one_device() {
    dev=$1
    out=$("$xpost" -q $ns -d "$dev" -o "$work/out.$dev" "$script" </dev/null 2>&1)
    st=$?
    printf '%s\n' "$out" | sed "s/^/$dev: /"
    verdict_run "$st" "$out" "the page-extent job on $dev" || return 1
    verdict_ok "$out" "the page-extent check on $dev" || return 1
    return 0
}

# The devices with a raster to index, which is the roster less the two
# that keep no pixels.
roster=
for dev in $DEVICE_FLEET_MARKING; do
    case "$dev" in
        null|bbox) continue ;;
    esac
    roster="$roster $dev"
done

# Every device with a raster to index answers this one: the roster above
# has already set aside the two that keep no pixels, and what is left
# reaches the limit the same way.
CANNOT_ANSWER=''

fleet_each one_device $roster || fail=1
fleet_hold_unasked "$CANNOT_ANSWER" || fail=1
asked=$fleet_asked

# A roster that answered for nothing reports as quietly as one that
# answered for everything.
if [ "$asked" -eq 0 ]; then
    echo "FAILURES: no device answered, so the limit was held against nothing"
    exit 1
fi
echo "page-extent: held on $asked device(s)"

[ "$fail" -eq 0 ] || exit 1
echo SUCCESS
