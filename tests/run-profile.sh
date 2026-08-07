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
#   $1        profile: quick, check, full or corpus
#   $2...     further arguments for meson test (-v, --num-processes, ...)
#
# The build directory is MESON_BUILD_ROOT where meson set it (this runs
# as a build target), and the working directory otherwise.
set -u
profile=${1:?usage: run-profile.sh <quick|check|full|corpus> [meson test args...]}
shift
build=${MESON_BUILD_ROOT:-$PWD}

if [ ! -f "$build/meson-info/meson-info.json" ]; then
    echo "FAILURES: $build is not a meson build directory"
    exit 1
fi

# What the profile selects, as a filter and as a predicate over the
# suites a test carries. `fast`, `slow` and `veryslow` are the cost
# axis and `corpus` is one value of the other, so every profile is a
# cost range with or without the corpus.
case $profile in
    quick)  filter='--suite fast --no-suite corpus'
            want='fast'; without='corpus'
            what='the fast tests, no corpus' ;;
    check)  filter='--suite fast --suite slow --no-suite corpus'
            want='fast slow'; without='corpus'
            what='the fast and slow tests, no corpus' ;;
    full)   filter='--no-suite corpus'
            want='fast slow veryslow'; without='corpus'
            what='every test but the corpus' ;;
    corpus) filter='--suite corpus'
            want='corpus'; without=''
            what='the differential corpus' ;;
    *)      echo "FAILURES: no such profile: $profile"
            echo "      one of quick, check, full, corpus"
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

echo "profile $profile: $what -- $(wc -l < "$work/want") of $(wc -l < "$work/all") tests"
# shellcheck disable=SC2086
meson test -C "$build" $filter "$@"
status=$?
exit $status
