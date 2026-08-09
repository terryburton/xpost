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
# The memacct and vmlimit tests are the cases that are neither. The
# memacct five weigh the memory a run holds rather than the answers it
# gives, so a build whose sanitizer holds freed memory in quarantine and
# carries shadow maps of its own leaves them nothing to measure and all
# five fail. The vmlimit two run the interpreter under an address-space
# limit tight enough to deny an accumulator its next doubling, and a
# runtime that reserves an address space larger than that limit stops the
# process from starting at all, which the wrapper reports as a skip.
# Neither is something the run was given nothing to work on -- a skip
# would say that, and would then have to be excused every time -- and
# neither is a fault in the tree. Both are properties of the build,
# recorded in the build directory, which is why they are read off the
# build and why no profile selects those tests where they hold. Never selected is what keeps them
# from being silently absent: they leave the count the profile reports,
# every profile says so in its verdict, and `everything` says in its own
# terms that the run does not speak for them. A declared list in the tree
# would be the alternative and is the thing the excuse channel above
# deliberately refuses -- a register of permitted absences goes on
# excusing something after it stops being absent, and a fact derived from
# the build cannot: reconfigure and the same run selects them again with
# nothing edited.
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

# Whether this build carries a sanitizer runtime, which is what puts
# both the memacct measurement and the vmlimit limit out of reach.
#
# Two memacct tests weigh the process through getrusage and three run it
# under valgrind, so what puts that measurement out of reach is a runtime
# that intercepts the allocator or maps shadow memory of its own: the
# address, leak, memory and thread sanitizers do, and valgrind will not
# run over any of them either. The vmlimit two impose an address-space
# limit of two hundred megabytes, which the same runtimes reserve past
# before the interpreter reaches its first line. `undefined` does none of
# it -- it instruments arithmetic and casts, leaves the heap where it was
# and reserves nothing -- and all seven pass under it, so it is the one
# sanitizer they are still answerable in and it is not read as displacing
# them.
#
# The value is taken from between "value" and "section" in the
# introspection, which is where the choices list -- which always names
# every sanitizer -- is not. It is a plain string in the meson this tree
# requires and a list in later ones, so it is split on anything that is
# not a letter and read name by name. Anything left after `none` and
# `undefined` are taken out displaces the measurement, which means a
# sanitizer this tree has not met is read as displacing rather than as
# harmless.
displaces=no
displacer=''
sanval=$(meson introspect "$build" --buildoptions 2>/dev/null | tr '{' '\n' |
    awk '/"name": "b_sanitize"/ {
             i = index($0, "\"value\":")
             j = index($0, ", \"section\"")
             if (i && j > i) print substr($0, i + 8, j - i - 8)
         }')
if [ -z "$sanval" ]; then
    # An answer that could not be read takes nothing out. Selecting the
    # five where the build does displace them costs five failures, which
    # is loud and is the behaviour without this paragraph at all; dropping
    # them from a build that could have run them is the silence the whole
    # of this wrapper exists to prevent.
    echo "profile $profile: could not read b_sanitize from $build, so the"
    echo "      memacct and vmlimit tests are selected; they cannot pass"
    echo "      under a sanitizer that carries a runtime"
elif printf '%s\n' "$sanval" | tr -c 'a-z' ' ' | tr ' ' '\n' |
     grep -vx '' | grep -vx none | grep -qvx undefined; then
    displaces=yes
    displacer='this build carries a sanitizer runtime'
    filter="$filter --no-suite memacct --no-suite vmlimit"
    without="$without memacct vmlimit"
fi

# A memory checker run as a wrapper displaces the same two measurements
# as a sanitizer built into the binary, and for the same reasons: it
# intercepts the allocator, so the memacct five weigh its arena rather
# than the interpreter's -- and it does not return memory to the system,
# so a context that has been destroyed cannot leave the process any
# smaller -- and it reserves an address space past the limit the vmlimit
# two impose. It is not recorded in the build, since it is not part of
# it: it arrives on this run's own command line, which is where it is
# read from.
if [ "$displaces" = no ]; then
    for arg in "$@"; do
        case $arg in
            *valgrind*)
                displaces=yes
                displacer='this run is wrapped in a memory checker'
                filter="$filter --no-suite memacct --no-suite vmlimit"
                without="$without memacct vmlimit"
                break ;;
        esac
    done
fi

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

# How many tests carry a named suite: in the build, and in what this
# profile selected. Both are counted off the listing rather than from a
# number written here, so neither can drift from what the tree carries,
# and both read the suite field rather than the line, so a test whose
# name happens to say memacct is not one of them.
count_suite() {
    awk -v want="$2" '{
        i = index($0, " / ")
        if (i == 0) next
        suites = substr($0, 1, i - 1)
        sub(/^[^:]*:/, "", suites)
        n = split(suites, s, "+")
        for (j = 1; j <= n; j++) if (s[j] == want) { c++; break }
    } END { print c + 0 }' "$1"
}
nmemacct=$(count_suite "$work/all" memacct)
nmemacct_sel=$(count_suite "$work/want" memacct)
nvmlimit=$(count_suite "$work/all" vmlimit)
nvmlimit_sel=$(count_suite "$work/want" vmlimit)

# Said only by the profiles that would otherwise hold them: the corpus
# and the consumer suite never carry the tag, so an exclusion notice
# there would name an absence that was never this profile's to report.
case $profile in
  quick|full|everything)
    if [ "$displaces" = yes ]; then
        echo "profile $profile: $displacer, so the"
        echo "      $nmemacct memacct test(s) are outside it -- they weigh the memory a"
        echo "      run holds, which a runtime that keeps what it is given and"
        echo "      maps memory of its own puts out of reach -- and so are the"
        echo "      $nvmlimit vmlimit test(s), whose address-space limit the same"
        echo "      runtime reserves past before the interpreter starts"
    elif [ "$nmemacct" -gt 0 ] && [ "$nmemacct_sel" -eq 0 ]; then
        # The other side of the same claim. Where the heap is left alone
        # the five are answerable and a profile over their costs has to
        # hold them; an exclusion that misfired, or a suite tag that went
        # missing, would take them out of every run with the filter and
        # the predicate agreeing about it and nothing left to notice.
        echo "FAILURES: the $profile profile selects none of the $nmemacct"
        echo "      memacct test(s) in a build that leaves the heap alone,"
        echo "      so the tests that weigh a run's memory would go unrun"
        echo "      with nothing in the verdict saying so"
        exit 1
    elif [ "$nvmlimit" -gt 0 ] && [ "$nvmlimit_sel" -eq 0 ]; then
        # The same claim about the other exclusion.
        echo "FAILURES: the $profile profile selects none of the $nvmlimit"
        echo "      vmlimit test(s) in a build that reserves no address space"
        echo "      of its own, so the tests that run under an address-space"
        echo "      limit would go unrun with nothing in the verdict saying so"
        exit 1
    fi ;;
esac

# The run is read out of the record meson writes rather than out of the
# summary it prints. The summary counts a skip apart from a pass and
# then reports "Fail: 0" either way, so a reader watching the failure
# count sees the same number whether the tests ran or not. The record
# says per test which it was.
#
# The record is removed first, because one left by an earlier run is a
# record of some other selection: read after a run that never started, it
# answers for tests this one did not touch.
#
# What it is called is meson's to choose. A plain run leaves testlog.json
# and a run under a wrapper leaves a name carrying the wrapper's, so
# rather than predict which, every record is cleared and the one the run
# leaves is the one read. A wrapper this has not met is then read the
# same as none.
logdir="$build/meson-logs"
for old in "$logdir"/testlog*.json; do
    [ -f "$old" ] && rm -f "$old"
done

# shellcheck disable=SC2086
meson test -C "$build" $filter "$@"
status=$?

record=''
for left in "$logdir"/testlog*.json; do
    [ -f "$left" ] || continue
    if [ -n "$record" ]; then
        echo "FAILURES: the $profile profile left more than one record in"
        echo "      $logdir, so which run each answers for cannot be told"
        exit 1
    fi
    record=$left
done

if [ -z "$record" ]; then
    echo "FAILURES: the $profile profile ran nothing -- meson wrote no record"
    echo "      in $logdir"
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
    # Said again in this profile's own terms, because this is the profile
    # whose claim is that every test ran: where the heap is displaced that
    # claim stops short of the five, and a green line here has to carry it.
    if [ "$displaces" = yes ]; then
        echo "profile everything: this run does not speak for the $nmemacct memacct"
        echo "      test(s) -- this build's sanitizer displaces the memory they weigh"
        echo "profile everything: nor for the $nvmlimit vmlimit test(s) -- the same"
        echo "      runtime reserves past the address-space limit they impose"
    fi
fi

exit $status
