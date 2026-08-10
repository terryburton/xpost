#!/bin/sh
# Meson test wrapper: a page the device cannot build must leave the
# interpreter able to build the next one (oversize_page_test.ps), run
# against the marking roster of tests/device-fleet.sh.
#
# The question is about the page a device builds, so it goes to the
# roster that marks. The devices whose page is the interpreter's own
# virtual memory answer the whole of it; the rest answer the part that
# does not depend on the raster being virtual memory, and the test picks
# which by the key the device carries rather than by its name.
#
#   $1  path to the built xpost binary
#   $2  path to oversize_page_test.ps
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
devices=$DEVICE_FLEET_MARKING
fail=0
ran=0

# A roster that skipped from end to end leaves the loop having asked
# nothing and every verdict untaken, which reads exactly as a roster that
# answered. The floor is the roster less what a build may not have the
# library for.
floor=0
for dev in $devices; do
    case " $DEVICE_FLEET_OPTIONAL " in *" $dev "*) continue ;; esac
    floor=$((floor + 1))
done

for dev in $devices; do
    out=$("$xpost" -q $ns -d "$dev" -o "$work/out.$dev" "$script" </dev/null 2>&1)
    st=$?
    case "$out" in
        *"wrong device"*) echo "SKIP $dev (not built in)"; continue ;;
    esac
    ran=$((ran + 1))
    if [ "$st" -ne 0 ]; then
        echo "FAIL $dev: the interpreter exited with status $st"
        printf '%s\n' "$out" | tail -3
        fail=1
        continue
    fi
    if verdict_ok "$out" "$dev"; then
        echo "OK   $dev"
    else
        fail=1
    fi
done

rm -rf "$work"
if [ "$ran" -lt "$floor" ]; then
    echo "FAILURES: $ran of the roster's devices answered, and $floor of them"
    echo "      are made without an optional library; the rest said they were"
    echo "      not built in, which is a build to fix rather than a run to pass"
    exit 1
fi
if [ "$fail" -ne 0 ]; then
    echo "FAILURES: a page the device could not build did not leave the"
    echo "      interpreter able to build the next one"
    exit 1
fi
echo "SUCCESS ($ran devices)"
exit 0
