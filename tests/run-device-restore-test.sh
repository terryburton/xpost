#!/bin/sh
# Meson test wrapper: a restore back past a setpagedevice makes the retired
# device current again, and painting on it must not follow its released
# buffer (device_restore_retired_test.ps), run against the lifetime
# roster of tests/device-fleet.sh.
#
# The devices that keep their raster in a malloc'd buffer are the ones that
# can follow a cleared handle here; the rest are run because the rule is the
# same for all of them and the sequence is an ordinary page-size change.
#
#   $1  path to the built xpost binary
#   $2  path to device_restore_retired_test.ps
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
devices=$DEVICE_FLEET_LIFETIME
fail=0
ran=0

# A roster that skipped from end to end leaves the loop having asked
# nothing and every verdict untaken, which reads exactly as a roster
# that answered. The floor is the roster less what a build may not have
# the library for.
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
    echo "FAILURES: $ran of the roster answered and $floor of it is made"
    echo "      without an optional library; the rest said they were not"
    echo "      built in, which is a build to fix rather than a run to pass"
    exit 1
fi
if [ "$fail" -ne 0 ]; then
    echo "FAILURES: a device retired by setpagedevice did not survive a restore"
    exit 1
fi
echo "SUCCESS ($ran devices)"
exit 0
