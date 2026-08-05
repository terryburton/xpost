#!/bin/sh
#
# One device roster, spelled in several places, held to agreeing.
#
# A device name is a selection: -d names it on the command line and
# setpagedevice names it from the program, and they are two spellings of
# one thing. The names live in three files -- the option parser's list,
# the interpreter's maker table, and the .devicemakers dictionary the
# page-device operator looks in -- and nothing made them agree, so five
# devices were selectable with -d and unreachable by name. That is worse
# than merely unreachable: a page-device request naming no device
# defaults to the running one, so on those five every setpagedevice
# raised rangecheck and the page could not even be resized.
#
# The device test wrappers carry rosters of their own, which is how a
# whole device came to be built, selectable and never once exercised.
# They are held to naming every device the interpreter can make, bar the
# platform exclusions declared below.
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

# The names the interpreter can build a maker call for.
awk '/device_strings\[\]\[3\] *=/ { in_t = 1; next }
     in_t && /{ *NULL/ { in_t = 0 }
     in_t && /^ *{ *"/ { sub(/^ *{ *"/, ""); sub(/".*$/, ""); print }' \
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

# $1 label for the roster under test, $2 its file, $3 the maker table it
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

# The wrappers that run a workload once per device. Each names its own
# set; every device the interpreter can make must appear in it, so a new
# device cannot be added and left unexercised.
#
# Excluded, with reasons rather than by omission:
#   gdi, gl  the Windows window devices: only the teardown wrapper runs
#            them, and only where the platform can open a window, so the
#            other two wrappers have nothing to run.
exclude='gdi gl'

for w in run-device-contract-test.sh run-device-destroy-test.sh \
         run-device-features-test.sh; do
    wrapper="$src/tests/$w"
    guard_require_file "$wrapper" "the device wrapper $w"
    # the names in the devices= assignment, plus any device named
    # literally on a command line elsewhere in the wrapper (the window
    # device runs under its own display)
    { awk '
        /devices=/ && !in_a {
            sub(/^.*devices=/, "")
            q = substr($0, 1, 1)
            if (q == "\"" || q == "'\''") { $0 = substr($0, 2); in_a = 1 }
            else { print; next }
        }
        in_a {
            i = index($0, q)
            if (i > 0) { print substr($0, 1, i - 1); in_a = 0 }
            else print
        }' "$wrapper"
      sed -n 's/.*-d \([a-z0-9]*\).*/\1/p' "$wrapper"
    } | tr ' \n' '\n\n' | sed 's/:.*//' | grep -v '^$' | sort -u > "$work/w"
    for dev in $(cat "$work/maker"); do
        case " $exclude " in *" $dev "*) continue ;; esac
        if ! grep -qx "$dev" "$work/w"; then
            echo "FAIL: $w never runs the $dev device"
            fail=1
        fi
    done
done

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: the device rosters disagree"
    exit 1
fi

echo "SUCCESS ($(wc -l < "$work/maker") devices, one roster in three files)"
exit 0
