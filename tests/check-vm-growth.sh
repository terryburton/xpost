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
# WHAT THE NUMBER IS. Each round reports the difference in the second value
# vmstatus gives, taken before and after one run, with a reclaim asked for at
# the end of the run. That value is the bank's high-water mark. A collection
# alone does not move it: what a collection recovers goes on a free list and
# stays where it is. A reclaim a program asks for closes the gaps between
# what is left, so the mark comes back over everything the run gave up, and
# what remains is what the run is still holding.
#
# So a round measures one quantity: HOW MUCH MORE THE INTERPRETER IS HOLDING
# AFTER THIS RUN THAN BEFORE IT. A run that allocates a hundred megabytes and
# releases them all reads nothing; a run that holds one small object reads
# the size of that object, every pass, for ever. A run that leaves the arena
# smaller than it found it reads a negative number, which is a run that gave
# back more than it took and is counted with the ones that cost nothing.
#
# The number is read off the stack before the round prints anything, and that
# order is part of the measurement: anything allocated between the mark and
# the reading is charged to the workload, and the first print of a run
# allocates.
#
# WHY THIS IS NOT ONE THRESHOLD OVER EVERY TEST, AND WHY IT IS NOT ONE RUN.
# Two things were measured over the whole directory before any of it was
# written.
#
# First, roughly half the workloads cannot be run twice in one interpreter at
# all: one that makes a structure inaccessible, or leaves a device installed,
# or depends on being the first thing to run, does not come through a second
# time -- and one that fails to run allocates nothing, so it would read as
# the cleanest of all. A single bound over a population half of which is not
# measurable would have to be wide enough to hide everything it was for.
#
# Second, and less obvious: a cost on a SECOND run is mostly not a leak, and
# the reason is the allocator rather than the collector. Reclamation is not
# the limit -- asking for five collections between runs instead of one, and
# asking for both banks instead of one, gives the same figure to the byte.
# What limits reuse is size: a released block serves a later request only
# where it is at least as large as the request -- the admission test is a
# plain comparison of the two, searched best-fit upward from the request's
# own size class. A run asking for a spread of sizes therefore needs fresh
# space for every size larger than anything released so far, and comes to be
# served only once a block that big has been released. One workload here costs 1,536,488 bytes on its second run, 428,756 on
# its third, 49,236 on its fourth and exactly nothing from its fifth
# onwards. A gate reading the second run alone would have called that a leak
# of a megabyte and a half.
#
# So each workload is asked twice, and any that costs something is asked
# again after four more runs. What comes to nothing is held to coming to
# nothing; what still costs something after those runs is the worklist. Neither
# assertion needs a tolerance or an assumption about how wide an object is,
# which is what makes them hold on every build. Every class is recorded by
# name in tests/vm_growth.golden and held in both directions: a workload that
# has become free is reported too, because the population making the
# assertion should only grow, and a line excusing something that no longer
# needs excusing is cover for the next thing to land in that state.
#
# The classes:
#
#   zero          Runs cleanly twice and the second run costs nothing. This
#                 is the one that is re-derived and enforced exactly: it
#                 fails if the workload ever costs anything, or stops
#                 running twice at all.
#
#   fits          Costs something on a second run and nothing by the sixth.
#                 The collector is not at fault and this is not a leak: what
#                 the previous run released has been given back, but the
#                 allocator can serve a request out of a released block only
#                 where the block is at least as large as the request. So a
#                 run asking for a spread of sizes needs fresh space for
#                 every size larger than anything released so far, and comes
#                 to be served only once a block that big has been released. Measured directly: a body
#                 whose requests are all one size, or descend in size, costs
#                 NOTHING from its second run; the same body with ascending
#                 sizes costs 69,480 bytes on its second run, 33,096 on its
#                 third and 19,056 on its fourth. The assertion for this
#                 class is that it still reaches nothing.
#
#   costs <why>   Still costs something after five runs. That may be a
#                 retention, or it may be the same fit effect needing more
#                 passes than five to cover the sizes asked for; which it is
#                 has not been established per workload, and that is what
#                 makes this the worklist.
#
#   once <why>    Does not survive being run twice in one interpreter.
#
# Two classes carry an assertion: zero must stay free, and fits must still
# reach nothing. For costs and once what is enforced is only that they are still
# not free, so either can be promoted the moment it becomes so. The
# distinction between those two is recorded as observed and not held to: a
# workload that errors on its second run one day and merely costs something
# the next would make a gate out of its own flakiness, and nothing here needs
# that difference.
#
# The costs and once classes are also the blindness control, and not as a
# separate contrivance: if the measurement ever stops seeing anything, every
# workload that costs something reads as free at once and each of them says
# so. There is no state of this guard in which it passes while weighing
# nothing.
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

XPOST_DATA_DIR="$src/data"
export XPOST_DATA_DIR

# A limit on one run, and it is set well above what any workload here needs
# rather than near it. What the limit is for is a workload that hangs; a
# limit a slow workload can reach on a slow host turns the measurement into a
# race, and the workload that loses it reads as one that cannot be run twice
# -- which is a claim about the host's speed dressed as a claim about the
# workload. The longest run in this directory takes twenty-seven seconds on
# the machine this was written on, so thirty seconds was a coin toss for it.
#
# The limit is honoured on the hosts that carry a command for it and on the
# ones that do not. The base system of macOS has no timeout(1), and a run
# that never starts reads here exactly like a workload that cannot be run
# a second time -- so a guard naming the command outright reports the
# whole directory as unmeasurable and says nothing about any of it. The
# limit is kept rather than dropped where the command is missing, because
# what it is for is a workload that hangs, and the shell can do the same
# job: the run in the background with a sleeper beside it, and whichever
# finishes first decides. The sleeper's output goes nowhere, so that the
# command substitution around a call does not wait on it as well.
if command -v timeout > /dev/null 2>&1; then
    xg_limit() { _lim=$1; shift; timeout "$_lim" "$@"; }
elif command -v gtimeout > /dev/null 2>&1; then
    xg_limit() { _lim=$1; shift; gtimeout "$_lim" "$@"; }
else
    xg_limit() {
        _lim=$1; shift
        "$@" &
        _job=$!
        ( sleep "$_lim"; kill -9 "$_job" ) > /dev/null 2>&1 &
        _watch=$!
        wait "$_job"; _rc=$?
        kill "$_watch" > /dev/null 2>&1
        return $_rc
    }
fi

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

    # The reading is reuse, not a difference: a collection each time round is
    # what makes what the last run gave back available to the next one.
    #
    # The figure is taken off the stack before anything is printed, and that
    # order is the measurement rather than a matter of taste. A collection the
    # program asks for closes the arena up, so the number this reads is what
    # is live and not a mark that only ever rises -- and anything allocated
    # between the mark and the reading is then counted against the workload.
    # The first print of a run allocates, so a reading taken after it charges
    # every workload in the directory the same few bytes for the guard's own
    # report.
    # The workload and the driving program are named relatively, and the run
    # is made from their directory. The scratch directory is the shell's, and
    # where the shell and the interpreter do not spell a path the same way --
    # a POSIX shell driving a native Windows binary -- an absolute name from
    # the one is a name the other cannot open. The workload then never runs,
    # and a workload that never runs reads here as one that cannot be run
    # twice: the whole directory reports as unmeasurable and the guard says
    # nothing about any of it.
    cat > "$work/case/drive.ps" <<PS
/xg.used { vmstatus pop exch pop } bind def
/xg.tgt (prep.ps) def
mark { xg.tgt run } stopped { (XGFAIL1\n) print } if cleartomark
1 vmreclaim
/xg.mark xg.used def
mark { xg.tgt run } stopped { (XGFAIL2\n) print } if cleartomark
1 vmreclaim
xg.used xg.mark sub
(XGCOST ) print 20 string cvs print (\n) print
flush
PS
    out=$(cd "$work/case" && xg_limit 120 \
              "$xpost" -q --no-sandbox -d null drive.ps </dev/null 2>/dev/null)
    st=$?
    # Read in the C locale. What a workload prints is bytes and not text --
    # a raster, a font program, whatever the run put on its output -- and a
    # sed asked to match a pattern against a byte that is not a character in
    # the locale it was started in refuses the input rather than failing to
    # match it. The figure is then unreadable and the workload reads as one
    # that cannot be run twice.
    cost=$(printf '%s\n' "$out" | LC_ALL=C sed -n 's/^XGCOST \(-*[0-9][0-9]*\)$/\1/p' | tail -1)
    erred=$(printf '%s\n' "$out" | LC_ALL=C grep -c 'XGFAIL[12]' || true)

    if [ "$st" -ne 0 ] || [ -z "$cost" ] || [ "$erred" != 0 ]; then
        printf '%s once\n' "$b" >> "$work/measured"
        continue
    fi
    # Nothing, or less than nothing. A collection the program asks for
    # closes the arena up, so the figure is what is live rather than a mark
    # that only rises, and a run that leaves the arena smaller than it found
    # it has given back more than it took. That is the opposite of the thing
    # this weighs, so it belongs with the workloads that cost nothing rather
    # than being reported as a cost of a negative number of bytes.
    if [ "$cost" -le 0 ]; then
        printf '%s zero\n' "$b" >> "$work/measured"
        continue
    fi

    # It cost something on the second run, which by itself does not mean it
    # holds anything: the allocator serves a request out of a released block
    # only where that block is at least as large as the request, so a run
    # asking for a spread of sizes needs fresh space for every size larger
    # than anything released so far.
    #
    # So the ones that cost anything are asked again, after four more runs,
    # by which point the released blocks cover the sizes asked for. Only
    # these workloads pay for the extra runs.
    cat > "$work/case/again.ps" <<PS
/xg.used { vmstatus pop exch pop } bind def
/xg.tgt (prep.ps) def
/xg.one { mark { xg.tgt run } stopped pop cleartomark 1 vmreclaim } bind def
xg.one xg.one xg.one xg.one xg.one
/xg.mark xg.used def
xg.one
xg.used xg.mark sub
(XGAGAIN ) print 20 string cvs print (\n) print
flush
PS
    wout=$(cd "$work/case" && xg_limit 360 \
               "$xpost" -q --no-sandbox -d null again.ps </dev/null 2>/dev/null)
    again=$(printf '%s\n' "$wout" | LC_ALL=C sed -n 's/^XGAGAIN \(-*[0-9][0-9]*\)$/\1/p' | tail -1)
    if [ -z "$again" ]; then
        printf '%s costs %s\n' "$b" "$cost" >> "$work/measured"
    elif [ "$again" -le 0 ]; then
        printf '%s fits %s\n' "$b" "$cost" >> "$work/measured"
    else
        printf '%s costs %s\n' "$b" "$again" >> "$work/measured"
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
        case $class in
        zero) ;;
        costs|fits)
            printf '%s  cost nothing to run a second time and now costs %s bytes,\n' \
                "$name" "$extra" >> "$work/problems"
            printf '        so something it does is held rather than given back\n' \
                >> "$work/problems" ;;
        *)  printf '%s  ran cleanly twice and no longer does, so it is not\n' \
                "$name" >> "$work/problems"
            printf '        measuring anything here any more\n' >> "$work/problems" ;;
        esac
        ;;
    fits)
        # The assertion for this class is convergence: it may cost something
        # while the free list is still settling, and must come to nothing
        # once it has. A workload that stops converging is holding something.
        case $class in
        zero|fits) ;;
        costs)
            printf '%s  used to reach nothing by its sixth run and now still costs %s\n' \
                "$name" "$extra" >> "$work/problems"
            printf '        bytes after five runs, so what it takes is no longer\n' \
                >> "$work/problems"
            printf '        explained by which sizes the allocator can reuse\n' \
                >> "$work/problems" ;;
        *)  printf '%s  ran cleanly twice and no longer does, so it is not\n' \
                "$name" >> "$work/problems"
            printf '        measuring anything here any more\n' >> "$work/problems" ;;
        esac
        ;;
    costs|once)
        [ -n "$why" ] || printf '%s  is registered as %s with nothing said about why\n' \
            "$name" "$want" >> "$work/problems"
        case $class in
        zero)
            printf '%s  is registered as %s and now costs nothing to run a\n' \
                "$name" "$want" >> "$work/problems"
            printf '        second time. Move it to zero, so it is held to that.\n' \
                >> "$work/problems" ;;
        fits)
            printf '%s  is registered as %s and now reaches nothing by its sixth run.\n' \
                "$name" "$want" >> "$work/problems"
            printf '        Move it to fits, so it is held to reaching nothing.\n' \
                >> "$work/problems" ;;
        esac
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
nfits=$(awk '$2 == "fits"' "$work/measured" | grep -c . || true)
ncosts=$(awk '$2 == "costs"' "$work/measured" | grep -c . || true)
nonce=$(awk '$2 == "once"' "$work/measured" | grep -c . || true)
nwhy=$(awk '$2 == "costs" || $2 == "once" { if ($3 == "unexplained") u++ } END { print u + 0 }' \
           "$work/recorded")
printf 'SUCCESS (%s workloads: %s free on a second run, %s reaching nothing by the sixth --\n' \
    "$nrun" "$nzero" "$nfits"
printf '         both held to it -- %s still costing after five runs, %s not\n' \
    "$ncosts" "$nonce"
printf '         survivable twice; %s carry no explanation yet)\n' "$nwhy"
