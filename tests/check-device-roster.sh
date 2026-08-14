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
#   $2  the built interpreter, for the one roster that cannot be read
#       off the source (see the last section)
set -u
src=${1:?usage: check-device-roster.sh <srcroot> <xpost>}
xpost=${2:?usage: check-device-roster.sh <srcroot> <xpost>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
if [ ! -x "$xpost" ]; then
    echo "FAILURES: the interpreter is not an executable: $xpost"
    exit 1
fi
# named from wherever the caller stood, and the run below stands
# somewhere else
case $xpost in
    /*) ;;
    *) xpost=$(cd "$(dirname "$xpost")" && pwd)/$(basename "$xpost") ;;
esac

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

# ---------------------------------------------------------------------
# The devices that band, which are written down three times
#
# Selecting one of these selects banding, and the selection is settled
# before any boot file is read -- so the list exists in C as well as in
# the recording class's own roster, and a third time in the test fleet
# that wrappers ask for the page-whole spelling through. Three copies of
# one fact is three chances for it to drift, and this is where they are
# held to each other.
( . "$fleet"; for v in $DEVICE_FLEET_BANDS; do echo "$v"; done ) \
    2>/dev/null | sort -u > "$work/bands-fleet"
if [ ! -s "$work/bands-fleet" ]; then
    echo "FAIL: DEVICE_FLEET_BANDS is empty or unset in tests/device-fleet.sh"
    fail=1
fi

awk '/^#define XPOST_BANDS_BY_DEFAULT\(X\)/ { inmacro = 1 }
     inmacro { print; if ($0 !~ /\\$/) exit }' \
    "$src/src/lib/xpost.h" \
    | grep -o 'X("[a-z0-9]*")' | sed 's/^X("//; s/")$//' | sort -u > "$work/bands-c"
if [ ! -s "$work/bands-c" ]; then
    echo "FAIL: no XPOST_BANDS_BY_DEFAULT list found in src/lib/xpost.h"
    fail=1
fi

sed -n '/\/\.playtargets </,/>>/p' "$src/data/recorddev.ps" \
    | sed -n 's|^  *//*\([a-z0-9]*\) /\..*$|\1|p' | sort -u \
    > "$work/bands-ps"
if [ ! -s "$work/bands-ps" ]; then
    echo "FAIL: no .playtargets roster found in data/recorddev.ps"
    fail=1
fi

for pair in 'bands-c:the list the C selection is compiled from' \
            'bands-ps:the recording class roster in data/recorddev.ps'; do
    other=${pair%%:*}
    what=${pair#*:}
    if [ -s "$work/bands-fleet" ] && [ -s "$work/$other" ] \
       && ! cmp -s "$work/bands-fleet" "$work/$other"; then
        echo "FAIL: DEVICE_FLEET_BANDS and $what name different devices:"
        diff "$work/bands-fleet" "$work/$other" | sed 's/^/      /' | head -8
        fail=1
    fi
done

# ---------------------------------------------------------------------
# And the declaration those three lists are lists of
#
# Three rosters agreeing says which devices are routed through the band
# loop. It says nothing about whether those are the devices whose pages
# can arrive that way, and that is a separate statement each device makes
# for itself: /BandedPage on its class. The two are a pair. A device
# declaring it and absent from .playtargets promises an arrival no budget
# will ever give it, whatever is named to --band-bytes; one named there
# and declaring nothing is played into a device expecting the page whole.
#
# It is asked of a running interpreter rather than read off the source,
# because a class is built and not written: the compiled drivers copy a
# class that declares it and then say their own thing about the copy, and
# one driver body here makes two classes and says a different thing for
# each. What a file spells and what a class ends up holding are therefore
# two questions, and this is the one that matters.
#
# Each device is installed by name and its running dictionary read. A
# device the build left out cannot be installed and is reported as such
# rather than counted either way.
#
# The record is not asked. It is not something a page is routed to but
# the thing that does the routing, and what it says about bands is
# whatever it copied from the class it was specialised from
# (data/recorddev.ps), so its answer is the target's answer read twice.
asked="$work/asked.ps"
{
    echo "["
    grep -vx record "$work/fleet-all" | sed 's|^|/|'
    cat <<'EOF'
]
{ /D exch def
  { << /OutputDevice D /PageSize [ 8 8 ] >> setpagedevice } stopped
  { (BANDS ) print D 60 string cvs print ( unmade\n) print }
  { (BANDS ) print D 60 string cvs print ( ) print
    DEVICE /BandedPage known { (yes) }{ (no) } ifelse print (\n) print }
  ifelse
} forall
EOF
} > "$asked"

# Started on the device that paints nothing, so that what a device says
# is read after a page-device request installed it and never off whatever
# device this build was configured with.
said=$( cd "$work" && "$xpost" -q -d null -o roster.scratch "$asked" \
        </dev/null 2>&1 )
if [ $? -ne 0 ] || [ -z "$said" ]; then
    echo "FAILURES: the interpreter could not be asked what its devices say"
    echo "      about taking a page a band at a time:"
    printf '%s\n' "$said" | sed 's/^/      /' | head -8
    exit 1
fi

printf '%s\n' "$said" | awk '$1 == "BANDS" && $3 == "yes" { print $2 }' \
    | sort -u > "$work/says-yes"
printf '%s\n' "$said" | awk '$1 == "BANDS" && $3 == "unmade" { print $2 }' \
    | sort -u > "$work/unmade"

# A device that could not be made said nothing, so it is held to nothing
# -- and the roster it would have been held to is narrowed to match,
# rather than the device being counted absent from it.
grep -vx record "$work/bands-ps" | { [ -s "$work/unmade" ] &&
    grep -vxF -f "$work/unmade" || cat; } | sort -u > "$work/routed"

if [ ! -s "$work/says-yes" ]; then
    echo "FAILURES: no device the interpreter can make says its page may"
    echo "      arrive a band at a time, and the roster names $(wc -l < "$work/routed" | tr -d ' ')."
    echo "      The question is being asked wrong."
    exit 1
fi

if ! cmp -s "$work/says-yes" "$work/routed"; then
    unrouted=$(comm -23 "$work/says-yes" "$work/routed")
    undeclared=$(comm -13 "$work/says-yes" "$work/routed")
    if [ -n "$unrouted" ]; then
        echo "FAIL: these devices declare BandedPage and are not routed through"
        echo "      the band loop:"
        printf '%s\n' "$unrouted" | sed 's/^/      /'
        echo "      (their class against .playtargets, data/recorddev.ps)"
        echo "      A page reaches such a device whole whatever --band-bytes"
        echo "      names, so the declaration describes an arrival that cannot"
        echo "      happen. Route it, or take the declaration back out where"
        echo "      the class makes it and say what stands in the way."
        fail=1
    fi
    if [ -n "$undeclared" ]; then
        echo "FAIL: these devices are routed through the band loop and do not"
        echo "      declare BandedPage:"
        printf '%s\n' "$undeclared" | sed 's/^/      /'
        echo "      (.playtargets, data/recorddev.ps, against their class)"
        echo "      A device that has not said this is handed the whole page it"
        echo "      expects everywhere else, and is being handed part of one"
        echo "      here."
        fail=1
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: the device rosters disagree"
    exit 1
fi

skipped=''
[ -s "$work/unmade" ] &&
    skipped=", $(wc -l < "$work/unmade" | tr -d ' ') not built into this interpreter"
echo "SUCCESS ($(wc -l < "$work/maker" | tr -d ' ') devices, one roster in four files;\
 $(wc -l < "$work/says-yes" | tr -d ' ') declaring a banded page and routed for one$skipped)"
exit 0
