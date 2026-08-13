#!/bin/sh
#
# One device roster, spelled in several places, held to agreeing.
#
# A device name is a selection: -d names it on the command line and
# setpagedevice names it from the program, and they are two spellings of
# one thing. The names live in three files -- the option parser's list,
# the names the interpreter accepts, and the .devicemakers dictionary the
# page-device operator looks in -- and nothing made them agree, so five
# devices were selectable with -d and unreachable by name. That is worse
# than merely unreachable: a page-device request naming no device
# defaults to the running one, so on those five every setpagedevice
# raised rangecheck and the page could not even be resized.
#
# The fourth is tests/device-fleet.sh, the roster the test wrappers run.
# It is held to naming every device the interpreter can make, bar the
# platform exclusions declared below, and its cross-product subsets are
# held to naming only members of it.
#
# Sources are read by name rather than by scanning a directory: a built
# tree leaves object files beside them whose debug information matches
# every pattern here, so a directory scan reads green where nothing was
# built and red where something was.
#
#   $1  path to the source tree root
set -u
src=${1:?usage: check-device-roster.sh <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"

guard_workdir
trap 'rm -rf "$work"' EXIT
# read a tree whose lines end where the scans below expect them to
guard_mirror_tree "$src"
src=$mirror

main_c="$src/src/bin/xpost_main.c"
interp_c="$src/src/lib/xpost_interpreter.c"
init_ps="$src/data/init.ps"

guard_require_file "$main_c" "the option parser"
guard_require_file "$interp_c" "the interpreter"
guard_require_file "$init_ps" "the interpreter's PostScript"

fail=0

# The names the command line accepts.
awk '/_xpost_main_devices\[\] *=/ { in_t = 1; next }
     in_t && /NULL/ { in_t = 0 }
     in_t && /^ *"/ { gsub(/[",]/, ""); gsub(/^ +| +$/, ""); if ($0 != "") print }' \
    "$main_c" | sort -u > "$work/cmdline"

# The names the interpreter accepts as a device selection. What builds
# the device is the .devicemakers dictionary below; what this list
# answers is whether a name is a device at all, which is the question
# xpost_create answers to its caller before any run begins.
awk '/device_strings\[\] *=/ { in_t = 1; next }
     in_t && /NULL/ { in_t = 0 }
     in_t && /^ *"/ { sub(/^ *"/, ""); sub(/".*$/, ""); print }' \
    "$interp_c" | sort -u > "$work/maker"

# The names setpagedevice will make.
awk '/\.devicemakers *<</ { in_t = 1; next }
     in_t && />> *put/ { in_t = 0 }
     in_t && /^ *\/[a-z]/ { sub(/^ *\//, ""); sub(/ .*$/, ""); print }' \
    "$init_ps" | sort -u > "$work/pagedevice"

for f in cmdline maker pagedevice; do
    if [ ! -s "$work/$f" ]; then
        echo "FAILURES: no device names found for the $f roster"
        echo "      the shape the guard reads for has changed; fix the guard"
        exit 1
    fi
done

# $1 label for the roster under test, $2 its file, $3 the accepted list it
# is held to, $4 where each lives
report_diff() {
    missing=$(comm -23 "$3" "$2")
    extra=$(comm -13 "$3" "$2")
    if [ -n "$missing" ]; then
        echo "FAIL: the $1 roster does not name:"
        printf '%s\n' "$missing" | sed 's/^/      /'
        echo "      ($4)"
        fail=1
    fi
    if [ -n "$extra" ]; then
        echo "FAIL: the $1 roster names devices the interpreter cannot make:"
        printf '%s\n' "$extra" | sed 's/^/      /'
        echo "      ($4)"
        fail=1
    fi
}

cmp -s "$work/cmdline" "$work/maker" ||
    report_diff "command-line" "$work/cmdline" "$work/maker" \
        "src/bin/xpost_main.c against src/lib/xpost_interpreter.c"
cmp -s "$work/pagedevice" "$work/maker" ||
    report_diff "page-device" "$work/pagedevice" "$work/maker" \
        "data/init.ps .devicemakers against src/lib/xpost_interpreter.c"

# The fourth spelling: the roster the test wrappers run. It used to be a
# list per wrapper, which is how a whole device came to be built,
# selectable and never once exercised; it is one file now, and this is
# what holds it to the names the interpreter accepts.
#
# Excluded from the roster, with reasons rather than by omission:
#   gdi, gl  the Windows window devices: they need a platform that can
#            open a window, so the wrappers reach them by name where the
#            platform provides one rather than through the roster.
#   xcb      the X11 window device: it needs a display, and the wrappers
#            that can conjure a virtual one run it by name.
exclude='gdi gl xcb'

fleet="$src/tests/device-fleet.sh"
guard_require_file "$fleet" "the device roster"

# The roster is three shell assignments, so read them by running the file
# rather than by matching its text: a continued list, a comment or a
# respelling then reads the same here as it does in a wrapper.
( . "$fleet"
  for v in $DEVICE_FLEET_ALL; do echo "all $v"; done
  for v in $DEVICE_FLEET_LIFETIME; do echo "lifetime $v"; done
  for v in $DEVICE_FLEET_MARKING; do echo "marking $v"; done
  for v in $DEVICE_FLEET_OPTIONAL; do echo "optional $v"; done
) > "$work/fleet" 2>/dev/null
for set in all lifetime marking optional; do
    awk -v s="$set" '$1 == s { print $2 }' "$work/fleet" | sort -u \
        > "$work/fleet-$set"
    if [ ! -s "$work/fleet-$set" ]; then
        echo "FAILURES: DEVICE_FLEET_$(echo "$set" | tr a-z A-Z) is empty or unset"
        echo "      in tests/device-fleet.sh"
        exit 1
    fi
done

grep -vx -e gdi -e gl -e xcb "$work/maker" > "$work/headless"
cmp -s "$work/fleet-all" "$work/headless" ||
    report_diff "device-fleet" "$work/fleet-all" "$work/headless" \
        "tests/device-fleet.sh DEVICE_FLEET_ALL against src/lib/xpost_interpreter.c,\
 less $exclude"

# A subset names members of the roster. One that names something else is
# a device nothing makes, run nowhere and reported as covered.
for set in lifetime marking optional; do
    stray=$(comm -23 "$work/fleet-$set" "$work/fleet-all")
    if [ -n "$stray" ]; then
        echo "FAIL: DEVICE_FLEET_$(echo "$set" | tr a-z A-Z) names devices the roster does not:"
        printf '%s\n' "$stray" | sed 's/^/      /'
        fail=1
    fi
done

# The subsets leave devices out, so something has to run the whole
# roster. That is the smoke wrapper, and it has to reach it through the
# roster rather than through a list of its own.
smoke="$src/tests/run-devices-test.sh"
guard_require_file "$smoke" "the device smoke wrapper"
if ! grep -q 'DEVICE_FLEET_ALL' "$smoke"; then
    echo "FAIL: run-devices-test.sh does not render the whole roster;"
    echo "      the devices the cross-product subsets leave out are then"
    echo "      run nowhere"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: the device rosters disagree"
    exit 1
fi

echo "SUCCESS ($(wc -l < "$work/maker" | tr -d ' ') devices, one roster in four files)"
exit 0
