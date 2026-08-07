#!/bin/sh
# Meson test wrapper: render one trivial page through EVERY built output
# device and require each to succeed. The rest of the suite runs under
# -d null, which never loads the graphics/device stack, so a device that
# fails to initialise or emit a page is invisible to it.
#
# This is where "every" is spelt: the roster in tests/device-fleet.sh,
# which check-device-roster.sh holds to the interpreter's maker table.
# The cross-product wrappers run representative subsets of it, so a
# device that leaves one of those is still rendered here.
#
# Two device classes:
#   file  - writes a raster/vector page to the -o path; must emit bytes.
#   buf   - leaves nothing at that path: the two whose raster is a buffer
#           the library hands back rather than a file, which the CLI
#           cannot capture, and the two that paint nothing at all. Each
#           is required only to render to completion with no error, which
#           still exercises the full graphics + device init and fillrect
#           path -- what a device regression breaks.
#
#   $1  path to the built xpost binary
set -u
xpost=$1
. "$(dirname "$0")/device-fleet.sh"

# Reach the interpreter's data directory, which lives outside any sandbox
# root. When this build has a file-access sandbox, disable it for the test;
# earlier builds have no such option and need nothing. Detect it from the
# usage text rather than assuming, so the one test is valid at every point
# in the series.
if "$xpost" -h 2>/dev/null | grep -q -- '--no-sandbox'; then
    ns='--no-sandbox'
else
    ns=''
fi

work=$(mktemp -d)
prog="$work/page.ps"
printf 'newpath 10 10 moveto 90 90 lineto stroke showpage\n' > "$prog"

# the devices that leave nothing at the -o path, and everything else in
# the roster
buf_devices='bgr raster null bbox'
file_devices=
for dev in $DEVICE_FLEET_ALL; do
    case " $buf_devices " in
        *" $dev "*) continue ;;
    esac
    file_devices="$file_devices $dev"
done

fail=0

run_dev() {   # $1=device
    dev=$1
    out="$work/out.$dev"
    rm -f "$out"
    # a device this build did not compile in prints "wrong device"; skip it.
    err=$("$xpost" -q $ns -d "$dev" -o "$out" "$prog" </dev/null 2>&1)
    case "$err" in
        *"wrong device"*) echo "SKIP $dev (not built in)"; return 2 ;;
    esac
    if printf '%s' "$err" | grep -q '%%\[ Error'; then
        echo "FAIL $dev: $(printf '%s' "$err" | grep '%%\[ Error' | head -1)"
        return 1
    fi
    return 0
}

for dev in $file_devices; do
    run_dev "$dev"; rc=$?
    [ "$rc" -eq 2 ] && continue
    if [ "$rc" -ne 0 ]; then fail=1; continue; fi
    if [ -f "$out" ]; then sz=$(wc -c < "$out"); else sz=0; fi
    if [ "${sz:-0}" -le 0 ]; then
        echo "FAIL $dev: produced no output"; fail=1
    else
        echo "OK   $dev ($sz bytes)"
    fi
done

for dev in $buf_devices; do
    run_dev "$dev"; rc=$?
    [ "$rc" -eq 2 ] && continue
    if [ "$rc" -ne 0 ]; then fail=1; continue; fi
    echo "OK   $dev (rendered, leaves no file)"
done

rm -rf "$work"
if [ "$fail" -ne 0 ]; then
    echo "FAILURES: at least one device did not render"
    exit 1
fi
echo "SUCCESS"
exit 0
