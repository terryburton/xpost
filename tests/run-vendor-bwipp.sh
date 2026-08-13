#!/bin/sh
# Meson test wrapper: run a downstream consumer's own test suite through
# the freshly built interpreter.
#
# The consumer is Barcode Writer in Pure PostScript, whose suite is a
# hundred and thirty programs of real-world PostScript -- deep procedure
# nesting, resource instances, packed procedures bound and rebound, long
# string and array work -- run to a verdict the consumer wrote. It
# exercises the language the way a program does rather than the way a
# conformance test does, which is why it catches what the operator suites
# here do not: a compatibility break against it went unseen for the whole
# of a campaign whose every other gate stayed green, because nothing ran
# it. A check that does not exist reports success as reliably as one that
# reports it wrongly, and is harder to notice.
#
# Everything it runs lives in a separate checkout, so this is never a
# build dependency and a machine without one gets a skip.
#
#   $1  path to the built xpost binary
#
# Environment:
#   BWIPP_DIR   the checkout to run. Two paths are tried when it is
#               unset; naming it is how a checkout anywhere else is run.
#   BWIPP_JOBS  how many of the consumer's test files to run at once.
#               Its suite otherwise takes one per core.
#
# A skip is exit 77 and never exit 0. The difference is the whole value
# of the thing: a run that found nothing to run and a run that ran
# everything both leave a green tick, and only the status separates them.
# The evaluator under tests/corpus exited zero when a tool it needed was
# absent, and read as a clean corpus run for as long as nobody looked.
#
# Three shapes of vacuous pass are specifically refused here, each of
# which the consumer's own runner will hand up as a zero exit:
#
#   It prints "SKIPPED xpost not available" and exits zero when the
#   interpreter it was given will not answer --version. Meson gives this
#   wrapper a binary it has just built, so that is a failed build, not an
#   absent tool.
#
#   It finds its test files by walking a directory, and a walk that finds
#   none reports nought passed, nought failed, and succeeds. So the files
#   are counted here, from the checkout, before the run; nought is a
#   failure and every name is held to producing a verdict.
#
#   It names files it did not reach -- a file skipped by its own skip
#   list, a worker that died before printing anything -- and the summary
#   line still adds up. Those are reported by name and fail, because a
#   gate that reports success over work it did not do says nothing about
#   the work it did not do.
#
# A fourth shape is not vacuous at all, which is what makes it worse: a
# verdict that is entirely true about code nobody is asking about. The
# suite runs the checkout's monolith, a file generated from its sources,
# and one that predates them is loaded as readily as one that does not.
# So the monolith is held to being no older than what it is generated
# from, and what generated it -- when, and from which revision -- is
# printed beside the verdict.
#
# The consumer's suite ships a shim that stands in for the Level 2
# resource operators on interpreters that lack them, guarded by a probe
# for a private name that this interpreter used to carry beside its own
# implementation. The name moved; the probe now answers "no resource
# operators" about an interpreter whose resource operators work, and the
# shim shadows them from userdict until the consumer's own PostScript
# fails typecheck. So the shim is not run: the suite's driver reads it
# from the directory the driver sits in, and this wrapper assembles a
# directory of its own in which that file is a stand-in that installs
# nothing and says so if the operators really are missing. Everything
# else in that directory is the consumer's, reached through symbolic
# links, so what runs is its runner and its tests and not a copy of them
# taken at the time this was written.
#
# The interpreter runs with the file-access sandbox lifted. The suite
# reads its programs from the assembled directory and its monolithic
# build from the checkout, neither of which is beneath the working
# directory the sandbox would confine it to, and the refusal arrives as
# an invalidfileaccess inside the consumer's PostScript, where it reads
# as the consumer's program failing.
#
# It also runs with XPOST_DATA_DIR named and checked. That variable is
# only the first place the interpreter looks for init.ps: unset or
# wrong, it falls through to data, ../data and ../../data relative to
# whatever directory the run started in. The suite runs from a scratch
# directory, so those relative paths land wherever the scratch directory
# happens to sit -- and a stale data directory two levels above one is
# enough to have the whole suite run against an interpreter's data files
# from somewhere else entirely and report a perfectly true result about
# a tree nobody asked about. That is not hypothetical: it is how the
# first green run of this wrapper was obtained.
set -u
xpost=${1:?usage: run-vendor-bwipp.sh <xpost binary>}
. "$(dirname "$0")/verdict.sh"

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$here/.." && pwd)
case $xpost in
    /*) ;;
    *)  xpost=$PWD/$xpost ;;
esac

skip() {
    echo "vendor-bwipp: $1 -- skipping"
    echo "vendor-bwipp: set BWIPP_DIR to run a checkout kept elsewhere"
    exit 77
}

# Where the consumer is checked out. The default pair is a guess at a
# developer's layout and nothing more; BWIPP_DIR is the answer for any
# other one, and the skip above says so wherever the guess misses.
bwipp=${BWIPP_DIR:-}
if [ -z "$bwipp" ]; then
    for d in "${HOME:-}/src/postscriptbarcode" "$here/../../postscriptbarcode"; do
        if [ -d "$d" ]; then
            bwipp=$d
            break
        fi
    done
fi
[ -n "$bwipp" ] || skip "no BWIPP checkout found"
[ -d "$bwipp" ] || skip "no BWIPP checkout at $bwipp"

runner=$bwipp/tests/xpost_tests/run
suite=$bwipp/tests/ps_tests
mono=${BWIPP_MONOLITHIC:-$bwipp/build/monolithic/barcode.ps}
[ -x "$runner" ] || skip "$runner is not there or not executable"
[ -d "$suite" ] || skip "$suite is not there"
[ -s "$suite/test_utils.ps" ] || skip "$suite/test_utils.ps is not there"
[ -s "$mono" ] || skip "$mono is not built (run make in $bwipp)"

# Past here nothing skips. A binary meson has just built and cannot run
# is a broken build, and an assembled directory that will not assemble is
# this wrapper's own failure; neither is an absent tool.
if [ ! -x "$xpost" ]; then
    echo "FAILURES: the interpreter to test is not executable: $xpost"
    exit 1
fi

# The monolith is generated from the checkout's sources, and nothing on
# either side notices one that predates them. The suite loads it, the
# encoders in it are the ones built last time, and every verdict is
# about those -- read, inevitably, as a verdict about the sources on
# disk. It is wrong in both directions at once: an edit that broke an
# encoder passes, and an edit that fixed one fails, each as convincingly
# as the truth. Ruling that out has already cost a bisect of this
# interpreter across four of its own commits, for a regression that was
# never in it.
#
# This fails rather than skips. A checkout whose monolith is out of date
# is present, runnable, and one command away from being right, which is
# not the absent tool the skips above are for. The command is named and
# not run: building someone else's tree as a side effect of a test here
# would be this gate deciding what that tree should contain.
stale=$(
    find "$bwipp/src" \
         \( -name '*.ps.src' -o -name 'ps.head' -o -name '*.upr' \) \
         -newer "$mono" 2>/dev/null
    find "$bwipp/CHANGES" -newer "$mono" 2>/dev/null
)
if [ -n "$stale" ]; then
    echo "FAILURES: the monolith is older than what it is generated from:"
    printf '%s\n' "$stale" | LC_ALL=C sort | while IFS= read -r f; do
        case $f in
            "$bwipp"/*) echo "      ${f#"$bwipp"/} is newer" ;;
            *)          echo "      $f is newer" ;;
        esac
    done
    echo "      $mono"
    echo "      would run the encoders built before those edits, so the"
    echo "      verdict would be about code that is no longer there."
    echo "      Rebuild it with: make -C $bwipp"
    exit 1
fi

XPOST_DATA_DIR=${XPOST_DATA_DIR:-$src/data}
export XPOST_DATA_DIR
if [ ! -s "$XPOST_DATA_DIR/init.ps" ]; then
    echo "FAILURES: no init.ps under $XPOST_DATA_DIR, and the interpreter"
    echo "      does not fail when its data directory is wrong -- it looks"
    echo "      beside the working directory instead and runs against"
    echo "      whatever it finds there"
    exit 1
fi

work=$(mktemp -d 2>/dev/null) || work=
if [ -z "$work" ] || [ ! -d "$work" ] || [ ! -w "$work" ]; then
    echo "FAILURES: could not make a scratch directory (is TMPDIR writable?)"
    exit 1
fi
trap 'case $work in /*) rm -rf "$work" ;; esac' EXIT

# What the checkout holds, counted before anything runs. This is the
# register the run is held to; reading it off the run's own output would
# be reading the answer from the thing being questioned.
: > "$work/listed"
for f in "$suite"/*.ps.test; do
    [ -f "$f" ] || continue
    b=${f##*/}
    printf '%s\n' "${b%.ps.test}" >> "$work/listed"
done
LC_ALL=C sort -o "$work/listed" "$work/listed"
listed=$(wc -l < "$work/listed")
if [ "$listed" -eq 0 ]; then
    echo "FAILURES: $suite holds no *.ps.test files; the checkout is not"
    echo "      the one this expects, and a run of it would pass by"
    echo "      evaluating nothing"
    exit 1
fi

# The directory the consumer's driver will read itself out of: its runner
# and its tests by link, the resource shim replaced, and the interpreter
# behind a wrapper because the driver invokes it as one word.
root=$work/tree
mkdir -p "$root/tests/xpost_tests" "$root/tests/ps_tests" "$root/bin" || exit 1
ln -s "$runner" "$root/tests/xpost_tests/run" || exit 1
for f in "$suite"/*; do
    [ -e "$f" ] || continue
    ln -s "$f" "$root/tests/ps_tests/" || exit 1
done

cat > "$root/tests/xpost_tests/shim.ps" <<'PSEOF'
%!PS
% Stand-in for the vendor suite's resource-operator shim. This
% interpreter implements the resource operators, so nothing is installed
% over them; what the shim would have installed answers a narrower
% question than the tests ask, and a run through it would say nothing
% about the operators themselves.
%
% stopped pushes true on the error, so the first procedure is the one
% that runs when the operators are missing.
mark { /Generic /Category findresource pop } stopped
{ cleartomark (FAIL: no working resource operators to run the suite through\n)
  print flush }
{ cleartomark } ifelse
PSEOF

# The shim names a device before whatever the suite passes. A run with
# no device named takes the build's, which is whatever the libraries
# found allowed and on one of them is a window on the screen the run was
# started from -- a hundred and thirty of them, on a suite that renders
# nothing. A suite naming its own device names it after this one, and
# the name it gives is the one used.
cat > "$root/bin/xpost" <<XPEOF
#!/bin/sh
exec "$xpost" --no-sandbox -d null "\$@"
XPEOF
chmod +x "$root/bin/xpost" || exit 1

XPOST=$root/bin/xpost
MONOLITHIC=$mono
export XPOST MONOLITHIC
if [ -n "${BWIPP_JOBS:-}" ]; then
    JOBS=$BWIPP_JOBS
    export JOBS
fi

# What produced the verdict, said before it is given. The check above
# settles that the monolith is not older than its sources; it cannot say
# which sources, because a checkout is edited between builds and a
# monolith built from work that is not committed anywhere is the
# ordinary case while an encoder is being written. So the two facts that
# identify it are printed -- when it was generated, and what the
# checkout's revision is -- and a run's report can be traced to them
# afterwards, which is what the bisect above had no way to do.
#
# Both are decoration on the run, not conditions of it: a checkout that
# is not a repository, or a date this shell cannot format, leaves a
# gap in the line and nothing else.
when=$(date -r "$mono" '+%Y-%m-%d %H:%M' 2>/dev/null) ||
    when=$(LC_ALL=C ls -l "$mono" | awk '{ print $6, $7, $8 }')
rev=$(git --no-optional-locks -C "$bwipp" rev-parse --short HEAD 2>/dev/null) || rev=
n=$(git --no-optional-locks -C "$bwipp" status --porcelain -- src 2>/dev/null |
    grep -c .) || n=0
edited=
if [ "$n" -gt 0 ]; then
    edited=" plus $n uncommitted source edit(s)"
fi

echo "vendor-bwipp: $listed test files from $bwipp${rev:+ at $rev}$edited"
echo "vendor-bwipp: through $mono, generated ${when:-at an unknown time}"
out=$(cd "$root" && "$root/tests/xpost_tests/run" 2>&1)
status=$?
printf '%s\n' "$out"

bad=0
verdict_run "$status" "$out" "the BWIPP suite" || bad=1

# The runner's own escape hatch, which is a zero exit and no complaint.
if printf '%s\n' "$out" | grep -q 'SKIPPED xpost not available'; then
    echo "FAILURES: the suite would not run the interpreter it was given;"
    echo "      that is a build to fix, not a tool to skip over"
    bad=1
fi

# Every name in the register against every name the run reached a verdict
# on. A file the run skipped and a file it never mentioned are the same
# thing from here -- work the gate did not do -- and are told apart only
# in what is printed about them.
printf '%s\n' "$out" \
  | awk '$1 == "PASS" || $1 == "FAIL" { print $2 }' \
  | LC_ALL=C sort > "$work/judged"
printf '%s\n' "$out" \
  | awk '$1 == "SKIP" { print $2 }' \
  | LC_ALL=C sort > "$work/skipped"

LC_ALL=C comm -23 "$work/listed" "$work/judged" > "$work/missing"
LC_ALL=C comm -13 "$work/listed" "$work/judged" > "$work/unknown"

if [ -s "$work/missing" ]; then
    echo "FAILURES: $(wc -l < "$work/missing") of $listed test files reached no verdict:"
    while read -r name; do
        if LC_ALL=C grep -qxF "$name" "$work/skipped"; then
            echo "      $name (skipped by the suite)"
        else
            echo "      $name (never reported on)"
        fi
    done < "$work/missing"
    bad=1
fi

if [ -s "$work/unknown" ]; then
    echo "FAILURES: the run reported on files the checkout does not hold:"
    sed 's/^/      /' "$work/unknown"
    echo "      this wrapper and the run are reading different trees"
    bad=1
fi

if [ "$bad" -ne 0 ]; then
    echo "FAILURES: the BWIPP suite did not pass"
    exit 1
fi

echo "vendor-bwipp: $listed of $listed test files evaluated, all passed"
exit 0
