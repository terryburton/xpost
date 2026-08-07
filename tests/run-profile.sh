#!/bin/sh
# Run one test profile, having first checked that the selection is the
# one the profile names.
#
# A meson suite filter that matches nothing does not fail. `--suite`
# with a name no test carries prints "No suitable tests defined" and
# exits zero, and `--no-suite` with such a name excludes nothing and
# exits zero -- and a suite name is project-qualified, so the name shown
# in a test listing is not always a name a filter accepts: the listing
# writes several suites joined with '+', and a filter given that joined
# form matches no suite and is therefore inert. Both mistakes read as a
# green run. The cheap way round costs a developer the time the profile
# was supposed to save; the expensive way round runs a fraction of the
# suite under the name of the whole of it and calls the result tested.
#
# So the filter is not trusted to mean what it says. The profile is also
# a predicate over the suites each test carries, the listing is read for
# what that predicate picks, and the two must agree exactly before
# anything runs. A filter that selects nothing, selects everything, or
# stops excluding what it names fails here rather than passing quietly.
#
# The predicate and the filter are written from the same reading of the
# profile and checked against each other; they are not one expression
# used twice. That is the whole of the protection: an inert filter is
# inert in one of them and not the other.
#
# A test that skipped is the same silence read from the other side. A
# skip is how a test says it was given nothing to work on, meson counts
# it apart from a pass, and every summary line that reports no failures
# reports none whether the tests ran or not. So the run's own record is
# read afterwards for what skipped, every profile names them, and the
# profile whose whole claim is that everything ran refuses to pass while
# any did not. XPOST_ALLOW_SKIP excuses named tests there, per run and
# never in the tree, and is held from both ends: a name it excuses that
# turns out to have run is an excuse that has lapsed and fails too.
#
#   $1        profile: quick, full, corpus, vendor or everything
#   $2...     further arguments for meson test (-v, --num-processes, ...)
#
#   XPOST_ALLOW_SKIP  space-separated test names the `everything` profile
#                     may leave unrun, each named in its verdict as
#                     something the run does not speak for
#
# The build directory is MESON_BUILD_ROOT where meson set it (this runs
# as a build target), and the working directory otherwise.
set -u
profile=${1:?usage: run-profile.sh <quick|full|corpus|vendor|everything> [meson test args...]}
shift
build=${MESON_BUILD_ROOT:-$PWD}

if [ ! -f "$build/meson-info/meson-info.json" ]; then
    echo "FAILURES: $build is not a meson build directory"
    exit 1
fi

# What the profile selects, as a filter and as a predicate over the
# suites a test carries. `fast`, `slow` and `veryslow` are the cost
# axis, `corpus` and `vendor` are two values of the other, and the two
# cost profiles are cost ranges over what is left when those two are
# taken out.
#
# The two are taken out by name rather than left to the cost axis to
# exclude. Both need something the tree does not carry -- a fetched
# corpus, a checkout of a consumer -- so both skip on most machines, and
# a cost profile that reports a skip here and none there is a profile
# that means two things. Naming them also keeps the exclusion from
# resting on what cost they happen to be tagged with today.
#
# Two ranges and not three: a range whose bound falls where no test lies
# is a second name for the range below it. Nothing the tree runs out of
# itself is veryslow -- the tag is carried by two of the corpora, which
# these profiles take out by name -- so a profile drawn at the top of
# the slow band and a profile drawn above it select the same tests, and
# a reader choosing between them is choosing between two spellings.
# `full` is the one kept, because what it selects is what it says.
case $profile in
    quick)  filter='--suite fast --no-suite corpus --no-suite vendor'
            want='fast'; without='corpus vendor'
            what='the fast tests, no corpus or vendor suite' ;;
    full)   filter='--no-suite corpus --no-suite vendor'
            want='fast slow veryslow'; without='corpus vendor'
            what='every test the tree runs out of itself' ;;
    corpus) filter='--suite corpus'
            want='corpus'; without=''
            what='the differential corpus' ;;
    vendor) filter='--suite vendor'
            want='vendor'; without=''
            what='the downstream consumer suite' ;;
    everything)
            filter=''
            want='fast slow veryslow'; without=''
            what='every test the build defines, none of them skipped' ;;
    *)      echo "FAILURES: no such profile: $profile"
            echo "      one of quick, full, corpus, vendor, everything"
            exit 1 ;;
esac

work=$(mktemp -d 2>/dev/null) || work=
if [ -z "$work" ] || [ ! -d "$work" ]; then
    echo "FAILURES: could not make a scratch directory (is TMPDIR writable?)"
    exit 1
fi
trap 'rm -rf "$work"' EXIT

# A listing line is "<project>:<suite>+<suite> / <name>". Read the
# suites off it and apply the predicate.
if ! meson test -C "$build" --list > "$work/all" 2>"$work/err"; then
    echo "FAILURES: could not list the tests in $build"
    sed 's/^/      /' "$work/err"
    exit 1
fi
if [ ! -s "$work/all" ]; then
    echo "FAILURES: $build defines no tests at all"
    exit 1
fi

awk -v want="$want" -v without="$without" '
    {
        i = index($0, " / ")
        if (i == 0) next
        suites = substr($0, 1, i - 1)
        sub(/^[^:]*:/, "", suites)
        n = split(suites, s, "+")
        hit = 0; barred = 0
        for (j = 1; j <= n; j++) {
            if (index(" " want " ", " " s[j] " ")) hit = 1
            if (without != "" && index(" " without " ", " " s[j] " ")) barred = 1
        }
        if (hit && !barred) print $0
    }' "$work/all" | sort > "$work/want"

if [ ! -s "$work/want" ]; then
    echo "FAILURES: the $profile profile names no test"
    echo "      no test carries any of: $want"
    exit 1
fi

# shellcheck disable=SC2086
if ! meson test -C "$build" $filter --list > "$work/raw" 2>"$work/err"; then
    echo "FAILURES: the $profile profile's filter was refused: $filter"
    sed 's/^/      /' "$work/err"
    exit 1
fi
sort < "$work/raw" > "$work/got"

if ! cmp -s "$work/want" "$work/got"; then
    echo "FAILURES: the $profile profile's filter does not select what the"
    echo "      profile names. Filter: $filter"
    missing=$(comm -23 "$work/want" "$work/got")
    extra=$(comm -13 "$work/want" "$work/got")
    if [ -n "$missing" ]; then
        echo "      it leaves out $(printf '%s\n' "$missing" | wc -l) test(s) the profile names, among them:"
        printf '%s\n' "$missing" | head -5 | sed 's/^/        /'
    fi
    if [ -n "$extra" ]; then
        echo "      it takes in $(printf '%s\n' "$extra" | wc -l) test(s) the profile excludes, among them:"
        printf '%s\n' "$extra" | head -5 | sed 's/^/        /'
    fi
    echo "      a suite name that matches nothing is silently inert in a"
    echo "      meson filter; check the spelling against the listing"
    exit 1
fi

selected=$(wc -l < "$work/want")
echo "profile $profile: $what -- $selected of $(wc -l < "$work/all") tests"

# The run is read out of the record meson writes rather than out of the
# summary it prints. The summary counts a skip apart from a pass and
# then reports "Fail: 0" either way, so a reader watching the failure
# count sees the same number whether the tests ran or not. The record
# says per test which it was.
#
# It is removed first, because a record left by an earlier run is a
# record of some other selection: read after a run that never started,
# it answers for tests this one did not touch.
record="$build/meson-logs/testlog.json"
rm -f "$record"

# shellcheck disable=SC2086
meson test -C "$build" $filter "$@"
status=$?

if [ ! -f "$record" ]; then
    echo "FAILURES: the $profile profile ran nothing -- meson wrote no record"
    echo "      at $record"
    exit 1
fi

# One JSON object per line, its name first and its result after the
# output it captured. Neither key can occur inside that output: a quote
# in captured text is escaped, so an unescaped '"result": "' is the key
# and nothing else.
awk '
    {
        name = ""; res = ""
        if (match($0, /^\{"name": "[^"]*"/))
            name = substr($0, RSTART + 10, RLENGTH - 11)
        if (match($0, /, "result": "[A-Z]+"/)) {
            res = substr($0, RSTART, RLENGTH)
            sub(/^, "result": "/, "", res)
            sub(/"$/, "", res)
        }
        if (name != "" && res != "") print name "\t" res
    }' "$record" > "$work/results"

ran=$(wc -l < "$work/results")
if [ "$ran" -ne "$selected" ]; then
    echo "FAILURES: the $profile profile named $selected tests and the record"
    echo "      holds $ran. A run that reports on a fraction of what it"
    echo "      selected agrees with whatever the rest would have said."
    exit 1
fi

awk -F'\t' '$2 == "SKIP" { print $1 }' "$work/results" | sort > "$work/skipped"
nskip=$(wc -l < "$work/skipped")

# A skipped test is named whatever the profile, because a profile that
# reports no failures over tests that never ran is reporting on less
# than it says. Only `everything` refuses to pass for it: the others
# select part of the suite by design and say which part in their own
# verdict line, while `everything` has nothing left to stand for it.
if [ "$nskip" -ne 0 ]; then
    echo "profile $profile: $nskip of $selected tests did not run:"
    sed 's/^/      /' "$work/skipped"
fi

if [ "$profile" = everything ]; then
    # A name may be given as meson writes it or as the test is called.
    : > "$work/excused"
    : > "$work/lapsed"
    for name in ${XPOST_ALLOW_SKIP:-}; do
        if awk -F' / ' -v n="$name" '$0 == n || $NF == n { found = 1 }
                                     END { exit !found }' "$work/skipped"; then
            awk -F' / ' -v n="$name" '$0 == n || $NF == n' "$work/skipped" \
                >> "$work/excused"
        else
            printf '%s\n' "$name" >> "$work/lapsed"
        fi
    done
    sort -u "$work/excused" > "$work/excused.u"
    comm -23 "$work/skipped" "$work/excused.u" > "$work/unexcused"

    if [ -s "$work/lapsed" ]; then
        echo "FAILURES: XPOST_ALLOW_SKIP excuses a test that this run did not"
        echo "      skip, so the excuse no longer describes anything:"
        sed 's/^/      /' "$work/lapsed"
        exit 1
    fi
    if [ -s "$work/unexcused" ]; then
        echo "FAILURES: the everything profile is the claim that every test"
        echo "      ran, and these did not:"
        sed 's/^/      /' "$work/unexcused"
        echo "      give each what it is waiting for -- tests/corpus/fetch.sh"
        echo "      for a corpus, a checkout for the consumer suite -- or name"
        echo "      it in XPOST_ALLOW_SKIP, which puts it in this verdict as"
        echo "      something the run does not speak for"
        exit 1
    fi
    if [ -s "$work/excused.u" ]; then
        echo "profile everything: this run does not speak for $(wc -l < "$work/excused.u") excused test(s)"
    fi
fi

exit $status
