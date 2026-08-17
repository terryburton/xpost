#!/bin/sh
# Guard: a test that costs nothing to run a second time goes on costing
# nothing.
#
# The sanitizers see memory that was allocated with malloc and never freed.
# The collector takes virtual memory that nothing names any more. Neither
# instrument can see the third case, which is the one that actually hurts a
# long-running context: memory allocated, still named, and named forever --
# a table nothing clears, a record something keeps appending to. It is not
# leaked, so nothing reports it; it is held, so no collection takes it; and
# it grows once per job for as long as the process serves jobs.
#
# The only instrument that finds it is repetition. Run the same work twice in
# one interpreter and ask what the second run cost. A one-off price -- a name
# interned for the first time, a face opened, a table sized on demand -- is
# paid by the first run and not the second. Anything the second run pays for
# again is paid for every job after it too.
#
# WHY THIS IS NOT ONE THRESHOLD OVER EVERY TEST. Measured over the whole
# directory: of 223 workloads, 61 cost exactly nothing on a second run, 46
# cost thousands of bytes for reasons that are mostly legitimate, and 114
# cannot be run twice in one interpreter at all -- a test that makes a
# structure inaccessible, or leaves a device installed, or depends on being
# the first thing to run, does not survive being run again, and a test that
# fails to run allocates nothing and would read as the cleanest of all. A
# single bound over that population would have to be wide enough to hide
# everything it was meant to catch.
#
# So the assertion is made where it costs nothing to make it exactly: a test
# measured at zero must stay at zero. That needs no tolerance and no
# assumption about the width of an object, which is what makes it hold on
# every build. The rest are registered by name in tests/vm_growth.golden
# with what is known about them, and the register is held in both
# directions: a test that has become free is reported too, because the
# population of tests making this assertion should only grow, and a line
# excusing something that no longer needs excusing is cover for the next
# thing to land in that state.
#
# The classes:
#
#   zero          Runs cleanly twice and the second run costs nothing. This
#                 is the one that is re-derived and enforced exactly: it
#                 fails if the workload ever costs anything, or stops
#                 running twice at all.
#
#   costs <why>   Runs cleanly twice and the second run costs something.
#
#   once <why>    Does not survive being run twice in one interpreter.
#
# What is enforced for the two non-zero classes is that they are still not
# free, so that either can be promoted the moment it becomes so and the
# population making the assertion only grows. The distinction BETWEEN them is
# recorded as it was observed and not held to: a workload that errors on its
# second run one day and merely costs something the next would make a gate
# out of its own flakiness, and nothing here needs that difference.
#
# Those two classes also are the blindness control, and it is not a separate
# contrivance: if the measurement ever stops seeing anything, every one of
# the forty-odd workloads that cost something reads as free at once, and each
# of them says so. There is no state of this guard in which it passes while
# weighing nothing.
#
#   $1  path to the source tree root
#   $2  path to the built xpost binary
set -u
src=${1:?usage: check-vm-growth.sh <srcroot> <xpost>}
xpost=${2:?usage: check-vm-growth.sh <srcroot> <xpost>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_require_file "$xpost" "the interpreter"
# Each workload is run from a directory of its own, so the interpreter has to
# be named from somewhere other than where this was invoked. A relative name
# would be resolved against the workload's directory and find nothing there,
# and a run that cannot start reads exactly like a workload that cannot be
# run twice.
case $xpost in
    /*) ;;
    *)  xpost=$(cd "$(dirname "$xpost")" && pwd)/$(basename "$xpost") ;;
esac
guard_require_file "$xpost" "the interpreter, named absolutely"
golden="$src/tests/vm_growth.golden"
guard_require_file "$golden" "the register of second-run costs"

guard_workdir
trap 'rm -rf "$work"' EXIT INT TERM

# ---- the register ----
grep -vE '^[[:space:]]*(#|$)' "$golden" | tr -d '\r' \
  | sed 's/[[:blank:]][[:blank:]]*/ /g; s/^ //; s/ $//' > "$work/recorded"

nrec=$(grep -c . < "$work/recorded" || true)
if [ "$nrec" -lt 50 ]; then
    echo "FAILURES: the register carries only $nrec workloads, which is fewer than"
    echo "      this tree has; it is not answering for the directory"
    exit 1
fi

# ---- measure every workload in the directory ----
#
# The population is the directory, not a list: a workload that arrives
# unregistered is the state a new test is in, and the register cannot be
# allowed to answer only for the ones somebody remembered.
: > "$work/measured"
nrun=0
for f in "$src"/tests/*.ps; do
    b=$(basename "$f")
    nrun=$((nrun + 1))
    # A fresh directory per workload. Several of these write files, and one
    # of them writes a great many; sharing one directory across the
    # directory's worth of runs leaves the later workloads walking whatever
    # the earlier ones left behind, which was enough to push a dozen of them
    # past the time limit and have them read as workloads that cannot be run
    # twice. The path is fixed and inside the work directory, not composed
    # from anything measured.
    rm -rf "$work/case"
    mkdir -p "$work/case" || exit 1
    # a workload that asks for the shared framework gets it, exactly as the
    # harness that normally runs it would
    case $(head -n 1 "$f" 2>/dev/null) in
        '%!testlib'*) cat "$src/tests/testlib.ps" "$f" > "$work/case/prep.ps" ;;
        *)            cp "$f" "$work/case/prep.ps" ;;
    esac

    # The reading is reuse, not a difference: the used figure never falls, so
    # a collection each time round is what makes what the last run gave back
    # available to the next one.
    cat > "$work/case/drive.ps" <<PS
/xg.used { vmstatus pop exch pop } bind def
/xg.tgt ($work/case/prep.ps) def
mark { xg.tgt run } stopped { (XGFAIL1\n) print } if cleartomark
1 vmreclaim
/xg.mark xg.used def
mark { xg.tgt run } stopped { (XGFAIL2\n) print } if cleartomark
1 vmreclaim
(XGCOST ) print xg.used xg.mark sub 20 string cvs print (\n) print
flush
PS
    out=$(cd "$work/case" && XPOST_DATA_DIR="$src/data" timeout 30 \
              "$xpost" -q --no-sandbox -d null "$work/case/drive.ps" </dev/null 2>/dev/null)
    st=$?
    cost=$(printf '%s\n' "$out" | sed -n 's/^XGCOST \(-*[0-9][0-9]*\)$/\1/p' | tail -1)
    erred=$(printf '%s\n' "$out" | grep -c 'XGFAIL[12]' || true)

    if [ "$st" -ne 0 ] || [ -z "$cost" ] || [ "$erred" != 0 ]; then
        printf '%s once\n' "$b" >> "$work/measured"
    elif [ "$cost" -eq 0 ]; then
        printf '%s zero\n' "$b" >> "$work/measured"
    else
        printf '%s costs %s\n' "$b" "$cost" >> "$work/measured"
    fi
done

if [ "$nrun" -lt 50 ]; then
    echo "FAILURES: only $nrun workloads were read out of $src/tests, which is"
    echo "      fewer than this tree has; the reading is broken, not the tree"
    exit 1
fi

# ---- hold each one to what the register says ----
fail=0
: > "$work/problems"

while read -r name class extra; do
    row=$(awk -v n="$name" '$1 == n { print; exit }' "$work/recorded")
    if [ -z "$row" ]; then
        printf '%s  is not in the register. Measured: %s%s\n' \
            "$name" "$class" "${extra:+ $extra}" >> "$work/problems"
        printf '        Add it to tests/vm_growth.golden in this commit.\n' \
            >> "$work/problems"
        continue
    fi
    want=$(printf '%s\n' "$row" | cut -d' ' -f2)
    why=$(printf '%s\n' "$row" | cut -d' ' -f3-)

    case $want in
    zero)
        if [ "$class" != zero ]; then
            if [ "$class" = costs ]; then
                printf '%s  cost nothing to run a second time and now costs %s bytes,\n' \
                    "$name" "$extra" >> "$work/problems"
                printf '        so something it does is held rather than given back\n' \
                    >> "$work/problems"
            else
                printf '%s  ran cleanly twice and no longer does, so it is not\n' \
                    "$name" >> "$work/problems"
                printf '        measuring anything here any more\n' >> "$work/problems"
            fi
        fi
        ;;
    costs|once)
        [ -n "$why" ] || printf '%s  is registered as %s with nothing said about why\n' \
            "$name" "$want" >> "$work/problems"
        if [ "$class" = zero ]; then
            printf '%s  is registered as %s and now costs nothing to run a\n' \
                "$name" "$want" >> "$work/problems"
            printf '        second time. Move it to zero, so it is held to that.\n' \
                >> "$work/problems"
        fi
        ;;
    *)
        printf '%s  has the unknown class %s in the register\n' \
            "$name" "$want" >> "$work/problems"
        ;;
    esac
done < "$work/measured"

# a register line for a workload this tree does not have
cut -d' ' -f1 "$work/recorded" | sort -u > "$work/recorded-names"
cut -d' ' -f1 "$work/measured" | sort -u > "$work/measured-names"
comm -23 "$work/recorded-names" "$work/measured-names" > "$work/stale"

if [ -s "$work/problems" ]; then
    echo "FAILURES: what a second run of a workload costs is not what the"
    echo "      register says it costs:"
    sed 's/^/      /' "$work/problems"
    fail=1
fi
if [ -s "$work/stale" ]; then
    echo "FAILURES: the register names a workload this tree does not have, so it"
    echo "      is answering for something that is gone:"
    sed 's/^/      /' "$work/stale"
    fail=1
fi

[ "$fail" -eq 0 ] || exit 1

nzero=$(awk '$2 == "zero"' "$work/measured" | grep -c . || true)
ncosts=$(awk '$2 == "costs"' "$work/measured" | grep -c . || true)
nonce=$(awk '$2 == "once"' "$work/measured" | grep -c . || true)
nwhy=$(awk '$2 == "costs" || $2 == "once" { if ($3 == "unexplained") u++ } END { print u + 0 }' \
           "$work/recorded")
printf 'SUCCESS (%s workloads: %s cost nothing on a second run and are held to it,' \
    "$nrun" "$nzero"
printf ' %s cost something, %s do not survive a second run; %s of those carry no\n' \
    "$ncosts" "$nonce" "$nwhy"
printf '         explanation yet)\n'
