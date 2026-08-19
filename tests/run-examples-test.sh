#!/bin/sh
# Meson test wrapper: run every example program in the examples
# directory and require that each one runs cleanly.
#
# The population is the directory, not a list kept beside it: every
# *.ps file under $2 is run, so an example is held by this test from
# the moment the file exists, and one removed takes itself out. The
# one file the glob leaves out is the teapot mesh, which is data that
# tea1.ps and teamath.ps read, not a program -- which is also why every
# run starts from the examples directory: a program's companion data
# resolves against the working directory.
#
# What a run needs is read out of the program, not out of this script:
#
#   %%SampleArguments: <values>   the program reads an ARGUMENTS array
#                                 of strings; a driver defining it from
#                                 the values on the marker runs the
#                                 program, the way doc/MANUAL tells a
#                                 reader to
#   %%Unbounded: <why>            the program runs until interrupted,
#                                 by design; it passes by still running
#                                 at a deadline, having produced output
#                                 of its own and no error, and fails by
#                                 stopping
#
# Every program runs under the null device with the sandbox off (the
# teapot readers open their mesh), stdin on /dev/null so the executive
# that follows a program file finds end-of-file rather than a terminal.
# A bounded program must exit 0; a runaway one is caught by the meson
# test timeout. Neither kind may raise an interpreter error: a raised
# error prints the %%[ Error: ... ]%% banner, and a program that
# trapped one itself is a program that needed to.
#
# A build without a face library answers a face-backed font request
# with invalidfont; that refusal is the build's contract, not the
# example's failure. In such a build an example that fails with
# invalidfont is reported and skipped -- what it draws needs a face,
# and every build that carries one holds it to a clean run -- and any
# other failure still fails.
#   $1  path to the built xpost binary
#   $2  path to the examples directory
set -u
xpost=$1
dir=$2
. "$(dirname "$0")/verdict.sh"

# runs start from the examples directory, so the binary's path must
# survive the cd
case $xpost in /*) ;; *) xpost=$(pwd)/$xpost ;; esac

tmp=${TMPDIR:-/tmp}/examples-test-$$
mkdir -p "$tmp" || exit 1
trap 'rm -rf "$tmp"' 0

# whether this build carries a face library: the faceless build seeds
# NOFACES into systemdict, and the skip below exists only for it. The
# probe is also the wrapper's clock: how long a whole interpreter run
# takes on this host, under whatever instrumentation the run carries,
# which is what the aliveness window below must stand clear of -- a
# window a slow host can exhaust at startup turns the check into a
# race about the host's speed.
probe_from=$(date +%s)
nofaces=$(printf 'systemdict /NOFACES known { (NOFACES) = } if\n' > "$tmp/probe.ps"
          "$xpost" -q -d null -o /dev/null --no-sandbox "$tmp/probe.ps" </dev/null 2>&1 | grep -c NOFACES)
probe_took=$(( $(date +%s) - probe_from ))

# How long an unbounded example is given to prove it is still going.
# It only needs to outlive startup, and startup is what the probe just
# measured: five of those plus a floor stands clear of the host's own
# speed, while an example that has already spoken passes the moment it
# has, so a fast host never waits the window out.
window=$((probe_took * 5 + 3))

# in a faceless build, a run that failed by the refusal contract is a
# skip: report it and count it, judge nothing else about it
faceless_refusal() {
    [ "$nofaces" -gt 0 ] || return 1
    grep -q '%%\[ Error: invalidfont' "$1"
}

fail=0
skipped=0
n=0
for f in "$dir"/*.ps; do
    [ -e "$f" ] || continue
    n=$((n + 1))
    name=$(basename "$f")

    # a program that reads ARGUMENTS says so; run it through a driver
    # that defines the array it documents
    sample=$(sed -n 's/^%%SampleArguments:[ 	]*//p' "$f" | sed 1q)
    if [ -n "$sample" ]; then
        run=$tmp/drv-$name
        printf '/ARGUMENTS [ %s ] def\n(%s) run\n' "$sample" "$name" > "$run"
    else
        run=$name
    fi

    if grep -q '^%%Unbounded' "$f"; then
        # runs until interrupted: alive at the deadline is the pass
        ( cd "$dir" && exec "$xpost" -q -d null -o /dev/null --no-sandbox \
              "$run" </dev/null >"$tmp/out-$name" 2>&1 ) &
        pid=$!
        t=0
        while [ "$t" -lt "$window" ] && kill -0 "$pid" 2>/dev/null; do
            # said something of its own already: the pass needs no more
            # of the window. The interpreter's startup banner is four
            # lines; anything past them is the program's.
            [ "$(grep -c . "$tmp/out-$name")" -gt 4 ] && break
            sleep 1
            t=$((t + 1))
        done
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null
            wait "$pid" 2>/dev/null
            out=$(cat "$tmp/out-$name")
            verdict_run 0 "$out" "example $name" || fail=1
            # said something of its own: the interpreter's startup
            # banner is four lines, everything past them is the
            # program's
            if [ "$(grep -c . "$tmp/out-$name")" -le 4 ]; then
                echo "FAILURES: example $name was still running at ${window}s"
                echo "      (window scaled from a ${probe_took}s probe) but had"
                echo "      said nothing beyond the startup banner"
                fail=1
            fi
        else
            wait "$pid"
            est=$?
            if faceless_refusal "$tmp/out-$name"; then
                echo "SKIPPED: example $name needs a face this build does not carry"
                skipped=$((skipped + 1))
                continue
            fi
            cat "$tmp/out-$name"
            echo "FAILURES: example $name declares itself unbounded but stopped"
            echo "      (status $est)"
            fail=1
        fi
    else
        out=$( cd "$dir" && "$xpost" -q -d null -o /dev/null --no-sandbox \
                   "$run" </dev/null 2>&1 )
        status=$?
        printf '%s\n' "$out" > "$tmp/out-$name"
        if [ "$status" -ne 0 ] && faceless_refusal "$tmp/out-$name"; then
            echo "SKIPPED: example $name needs a face this build does not carry"
            skipped=$((skipped + 1))
            continue
        fi
        if ! verdict_run "$status" "$out" "example $name"; then
            printf '%s\n' "$out"
            fail=1
        fi
    fi

    # an error the program raised, or raised and trapped its own way
    # past, prints the interpreter's error banner
    if grep -q '%%\[ Error' "$tmp/out-$name"; then
        echo "FAILURES: example $name raised an interpreter error:"
        grep '%%\[ Error' "$tmp/out-$name" | sed 's/^/      /'
        fail=1
    fi
done

if [ "$n" -eq 0 ]; then
    echo "FAILURES: no example programs found under $dir; this test is"
    echo "      reading the wrong directory"
    exit 1
fi

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: $n examples run, some did not run cleanly"
    exit 1
fi
if [ "$skipped" -ne 0 ]; then
    echo "SUCCESS ($n examples, $skipped skipped: this build carries no face library)"
else
    echo "SUCCESS ($n examples)"
fi
exit 0
