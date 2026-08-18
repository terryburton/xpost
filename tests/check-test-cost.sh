#!/bin/sh
#
# Every test declares what it costs, once, where it is registered.
#
# The suite is selected along two axes -- what a test is about and what
# it costs -- and the second only works if every test answers it. A test
# registered without a cost joins whatever profile the selection leaves
# it in, which is the cheap one: `--no-suite slow --no-suite veryslow`
# subtracts, so a test tagged with neither is a test the quick profile
# runs. A minute-long check added six months from now would land there
# and nothing would say so; the profile would just quietly stop being
# quick. So the cost is not optional and not defaulted -- meson.build
# names one of fast, slow and veryslow on every registration, and this
# holds it to that.
#
# The population is read out of meson.build rather than listed here, so
# a test added tomorrow is inside the rule the day it is added, and a
# test removed takes itself out. There is no register of slow tests to
# fall out of step with the registrations: the registration is the
# register.
#
# Two failures are the point of it:
#
#   a registration naming no cost      -- the omission above
#   a registration naming two          -- an edit that added a cost
#                                         without taking the old one out,
#                                         after which the test is in both
#                                         profiles and neither answer is
#                                         the one in force
#
# and two more that would make the rule mean nothing:
#
#   a cost variable that does not carry the tag it is named for, so
#   `suite: cost_fast` puts a test in the slow suite and every reading
#   of meson.build says otherwise
#
#   the tags spelled outside the two files that have to spell them:
#   meson.build, where a test declares its cost, and the profile wrapper
#   that selects on it. A third is a second list of what is slow, and
#   the two would agree until they did not, after which the wrong one is
#   the one nothing runs. Looked for by the one name that means nothing
#   else -- "slow" is an ordinary English word and turns up in prose
#   wherever a cost is being described -- across the three directories a
#   second list could live in. The documentation says what the profiles
#   are and is not a second list: a name in prose selects nothing.
#
# and one that would make the profiles mean nothing:
#
#   a cost declared on a test that no profile can reach. The wrapper's
#   profiles are cost ranges, so every tag a registration may carry has
#   to be a tag some profile names; a fourth tag introduced and left out
#   of the wrapper would put its tests in no profile at all.
#
# All of that is the form of a tag. None of it compares a tag to a
# duration, so a tag can be wrong the day it is written and go on being
# wrong as the test it names gets faster or slower.
#
# Given a build directory as well, this reads that build's test record
# and says which tags the run disagrees with. That part is a report and
# not a rule: nothing it finds about a tag changes the exit status.
# Being unable to read a record it was pointed at does fail, because a
# report that answers with silence when it was handed nothing is the
# one way it could be worse than not existing.
#
# It is not a rule because of what a cost is. The taxonomy defines one
# as the time a test takes run alone on an otherwise idle machine, and
# an ordinary run is neither -- the suite runs several tests at once by
# default, and the machine is whatever its owner was already doing on
# it. A check that failed a test for measuring outside its band would
# fail hardest on the busiest machine, which is when a developer least
# wants to be arguing with it, and what it would teach is to tag for the
# worst run rather than for the cost. So the tags get a refresh instead:
# durations are taken from a run that was going to happen anyway, the
# disagreements are printed, and a human decides.
#
# What the report establishes:
#
#   that a test measured outside the band its tag names, in a record
#   whose tests did not overlap each other. That is a real disagreement
#   between the tag and the run.
#
# What it does not establish, said here because a report read for more
# than it says is worse than no report at all:
#
#   that the machine was idle. Nothing in the record says, and nothing
#   can. Load only ever adds to a duration, so the two directions are
#   not alike: a test reported under its band was under it whatever else
#   the machine was doing, and a test reported over its band may only
#   have been sharing. Confirm an over-band finding by running that test
#   alone before re-costing it.
#
#   anything about a test the record does not hold. A record left by a
#   profile covers that profile and no more, and a test that skipped
#   measured nothing at all. Both are counted out and both are said.
#
#   that a tag it passes over is right. Silence here is one measurement
#   that fell inside the band, not a property of the test.
#
#   $1  path to the source tree root
#   $2  optional: a build directory whose test record to read durations
#       from, made by `meson test -C <build> --num-processes 1`
set -u
src=${1:?usage: check-test-cost.sh <srcroot> [buildroot]}
build=${2:-}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_require_file "$src/meson.build" "the build description"

guard_workdir
guard_mirror cost "$src/meson.build"
meson="$mirror/meson.build"

fail=0
costs='fast slow veryslow'

# ---- the cost variables ----
#
# Read from the assignment rather than assumed, because the whole rule
# rests on them: cost_fast is what a registration says, and what it
# means is whatever list this line holds.
for c in $costs; do
    line=$(grep -c "^cost_$c = " "$meson")
    if [ "$line" -ne 1 ]; then
        echo "FAIL: meson.build defines cost_$c $line times; it needs exactly one"
        fail=1
        continue
    fi
    if ! grep "^cost_$c = " "$meson" | grep -q "'$c'"; then
        echo "FAIL: cost_$c does not put a test in the $c suite:"
        grep "^cost_$c = " "$meson" | sed 's/^/      /'
        fail=1
    fi
    # and not in either of the others
    for other in $costs; do
        [ "$other" = "$c" ] && continue
        if grep "^cost_$c = " "$meson" | grep -q "'$other'"; then
            echo "FAIL: cost_$c also puts a test in the $other suite"
            fail=1
        fi
    done
done

# ---- the bands the costs stand for ----
#
# The three costs are ranges of duration, and where one ends and the
# next begins is stated once, in the taxonomy in meson.build. The report
# below needs them as numbers, which is a second statement of the same
# thing, so the second is held to the first: reword the taxonomy and
# this fails until the numbers come with it. That is the whole reason
# the wording is matched exactly rather than loosely -- a match that
# tolerated a reword would tolerate the reword that moved a bound.
band_fast_max=5
band_slow_max=50
for want in 'fast      under five seconds' \
            'slow      five seconds to fifty' \
            'veryslow  longer than fifty'
do
    if ! grep -qx "#   $want" "$meson"; then
        echo "FAIL: the cost taxonomy in meson.build no longer reads"
        echo "      \"#   $want\""
        echo "      This guard restates those bounds as $band_fast_max and"
        echo "      $band_slow_max seconds to compare them against measured"
        echo "      durations; change the two together or they say"
        echo "      different things"
        fail=1
    fi
done

# ---- one cost per registration ----
#
# A test() call runs from its opening line to the line where its
# parentheses balance, so the whole call is read whatever shape it was
# written in -- one line or ten, with lists and dictionaries inside it.
# Whole-line comments are dropped first; a trailing one is not, because
# a '#' inside a string is not a comment and this file has several.
awk '
function nch(s, c,   i, n) {
    n = 0
    for (i = length(s); i > 0; i--) if (substr(s, i, 1) == c) n++
    return n
}
/^[[:space:]]*#/ { next }
# A registration inside a foreach declares its cost in the call or in
# the list the loop walks, so the header counts as part of it. The one
# that does this is the corpus, whose four members do not all cost the
# same and whose cost therefore rides in the pair alongside the name.
/^[[:space:]]*foreach[[:space:]]/ { head = $0 }
/^[[:space:]]*endforeach/ { head = "" }
{
    if (!collecting) {
        if ($0 !~ /^[[:space:]]*test\(/) next
        collecting = 1; buf = head; depth = 0; start = FNR
    }
    buf = buf " " $0
    depth += nch($0, "(") - nch($0, ")")
    if (depth <= 0) {
        collecting = 0
        n = 0; what = ""
        if (index(buf, "cost_fast") || index(buf, "\047fast\047")) {
            n++; what = what " fast"
        }
        if (index(buf, "cost_slow") || index(buf, "\047slow\047")) {
            n++; what = what " slow"
        }
        if (index(buf, "cost_veryslow") || index(buf, "\047veryslow\047")) {
            n++; what = what " veryslow"
        }
        name = buf
        sub(/^.*test\([[:space:]]*/, "", name)
        sub(/[,)].*$/, "", name)
        print start "\t" n "\t" name what
    }
}' "$meson" > "$work/calls"

if [ ! -s "$work/calls" ]; then
    echo "FAILURES: no test registrations found in meson.build"
    echo "      the shape the guard reads for has changed; fix the guard"
    exit 1
fi
total=$(wc -l < "$work/calls" | tr -d ' ')
if [ "$total" -lt 100 ]; then
    echo "FAILURES: only $total test registrations found in meson.build;"
    echo "      the scan is reading a fraction of the file"
    exit 1
fi

none=$(awk -F'\t' '$2 == 0' "$work/calls")
if [ -n "$none" ]; then
    echo "FAIL: these registrations name no cost, so they run in every"
    echo "      profile including the quick one:"
    printf '%s\n' "$none" | awk -F'\t' '{ print "      meson.build:" $1 ": " $3 }'
    echo "      name cost_fast, cost_slow or cost_veryslow on each"
    fail=1
fi

many=$(awk -F'\t' '$2 > 1' "$work/calls")
if [ -n "$many" ]; then
    echo "FAIL: these registrations name more than one cost:"
    printf '%s\n' "$many" | awk -F'\t' '{ print "      meson.build:" $1 ": " $3 }'
    fail=1
fi

# ---- every cost is reachable through a profile ----
#
# The profiles are cost ranges over the same three names. A tag that no
# profile names is a tag whose tests run under no profile, which is the
# same silence as a test carrying no tag at all.
profile="$src/tests/run-profile.sh"
guard_require_file "$profile" "the profile wrapper"
guard_mirror prof "$profile"
for c in $costs; do
    if ! grep -qE "want='[^']*$c" "$mirror/run-profile.sh"; then
        echo "FAIL: no profile in tests/run-profile.sh names the $c cost;"
        echo "      the tests carrying it run under none of them"
        fail=1
    fi
done

# ---- the tags are spelt where they are declared and where they are
#      selected on, and nowhere else ----
#
# Anywhere else that names them is a second answer to the same question.
# meson.build is where a test declares its cost and run-profile.sh is
# where a profile selects on it; this guard names them because it is
# what checks them; the documentation describes the profiles a developer
# types and selects nothing by doing so.
elsewhere=$(cd "$src" && grep -rl veryslow data src tests 2>/dev/null \
            | grep -vE '^tests/(check-test-cost|run-profile)\.sh$')
if [ -n "$elsewhere" ]; then
    echo "FAIL: these name a cost suite outside meson.build and the profile"
    echo "      wrapper, which is a second statement of what is slow:"
    printf '%s\n' "$elsewhere" | sed 's/^/      /'
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: the cost declarations do not hold"
    exit 1
fi

echo "SUCCESS ($total registrations, each naming one cost)"

if [ -z "$build" ]; then
    echo "      the form of the tags only -- what the tests measure was"
    echo "      not read; pass a build directory to compare the two"
    exit 0
fi

# ---- what the tests measured, against what they declare ----
#
# What this finds about a tag is a report: it prints and does not fail,
# for the reasons in the header. Being unable to read the record it was
# pointed at is not a finding about a tag, and does fail.
#
# The record is meson's own, one JSON object a line, and is read as the
# profile wrapper reads it: a key is the unescaped form of its name, and
# an unescaped quote cannot occur inside the captured output beside it,
# so the first match on a line is the field and not a mention of it. It
# is read where it lies rather than mirrored: it is written by the build
# rather than checked out, so the line endings are the ones this machine
# makes.
#
# The suites are read off the record too, not off meson.build. What a
# test was tagged when it ran is what the run is evidence about; a tag
# edited since is a tag no measurement here speaks for. How a record
# names a test is what tests/listing.awk reads, which is the same
# reading the gate and the profile wrapper are handed.
guard_require_dir "$build" "the build directory"
record="$build/meson-logs/testlog.json"
guard_require_file "$record" "the test record"
listing="$(dirname "$0")/listing.awk"
guard_require_file "$listing" "the listing reader"

cat > "$work/measure.awk" <<'AWK'
{
    name = ""; res = ""; dur = ""; st = ""
    if (match($0, /^\{"name": "[^"]*"/)) {
        name = substr($0, RSTART, RLENGTH)
        sub(/^\{"name": "/, "", name); sub(/"$/, "", name)
    }
    if (match($0, /, "result": "[A-Z]+"/)) {
        res = substr($0, RSTART, RLENGTH)
        sub(/^, "result": "/, "", res); sub(/"$/, "", res)
    }
    if (match($0, /, "duration": [0-9]+\.?[0-9]*/)) {
        dur = substr($0, RSTART, RLENGTH); sub(/^, "duration": /, "", dur)
    }
    if (match($0, /, "starttime": [0-9]+\.?[0-9]*/)) {
        st = substr($0, RSTART, RLENGTH); sub(/^, "starttime": /, "", st)
    }
    if (name == "" || res == "" || dur == "" || st == "") next
    tag = ""
    n = split(listing_suites(name), s, "+")
    for (j = 1; j <= n; j++)
        if (s[j] == "fast" || s[j] == "slow" || s[j] == "veryslow")
            tag = (tag == "" ? s[j] : "?")
    print listing_name(name) "\t" tag "\t" res "\t" st "\t" dur
}
AWK
awk -f "$listing" -f "$work/measure.awk" "$record" > "$work/measured"

# Counts in against counts out. A record whose shape has moved on would
# leave the scan reading a fraction of the run and reporting on it as
# though it were the run, which is the one way a report like this turns
# into a lie rather than into silence.
#
# It is the one thing here that fails. Everything below it is a report on
# the tags, which a machine's load can make wrong; this is the reading
# itself having stopped working, and a guard that names its own defect
# and then exits as though it had looked is a guard whose caller is told
# nothing went wrong.
held=$(grep -c . "$record")
read_back=$(grep -c . "$work/measured")
if [ "$read_back" -eq 0 ] || [ "$read_back" -ne "$held" ]; then
    echo "FAILURES: $read_back of $held records could be read from $record;"
    echo "      the shape of the record has moved and nothing is reported"
    echo "      from it -- fix the scan above"
    exit 1
fi

# Whether the run was one test at a time, which is half of what the
# taxonomy asks for and the half a record can answer. Durations taken
# while other tests had the machine are not costs, so a record made in
# parallel is not read at all rather than read with a caveat: a caveat
# on every line is a caveat nobody reads.
tab=$(printf '\t')
overlap=$(sort -t "$tab" -k4,4n "$work/measured" | awk -F'\t' '
    NR > 1 && end > $4 + 0.05 { n++ }
    { end = $4 + $5 }
    END { print n + 0 }')
if [ "$overlap" -ne 0 ]; then
    echo "      the record in $build was left by a run of more than one"
    echo "      test at a time ($overlap of them overlapping another), and a"
    echo "      duration measured while the rest of the suite had the"
    echo "      machine is not what a cost is. Nothing is reported from it."
    echo "      Make one this can read with:"
    echo "        meson test -C $build --num-processes 1"
    exit 0
fi

awk -F'\t' -v fmax="$band_fast_max" -v smax="$band_slow_max" '
    $3 == "SKIP" { skipped++; next }
    $2 == "" || $2 == "?" { untagged++; next }
    {
        d = $5 + 0
        measured++
        want = (d < fmax) ? "fast" : (d <= smax) ? "slow" : "veryslow"
        if (want != $2) {
            drift++
            printf "L      %-30s %7.2f s  tagged %-8s measures %s\n", \
                   $1, d, $2, want
        }
    }
    END {
        printf "C\t%d\t%d\t%d\t%d\n", measured + 0, drift + 0, \
               skipped + 0, untagged + 0
    }' "$work/measured" > "$work/drift"

counts=$(awk -F'\t' '$1 == "C" { print $2, $3, $4, $5 }' "$work/drift")
if [ -z "$counts" ]; then
    echo "      the comparison above wrote no tally, so how much of the"
    echo "      record it covered cannot be told and nothing is reported"
    exit 0
fi
# shellcheck disable=SC2086
set -- $counts
measured=$1; ndrift=$2; skipped=$3; untagged=$4

if [ "$ndrift" -eq 0 ]; then
    echo "cost drift: none of the $measured tests this record measured is"
    echo "      outside the band its tag names"
else
    echo "cost drift: $ndrift of the $measured tests this record measured are"
    echo "      outside the band their tags name:"
    sed -n 's/^L//p' "$work/drift"
    echo "      Re-cost each in meson.build, or leave it and say in the"
    echo "      comment above it why the measurement is not the cost."
fi

# The report's own edges, said every time rather than only when it finds
# something: a reader who takes a quiet run for a clean bill of costs has
# read it for more than it says.
echo "      Read in one direction only: load adds to a duration and never"
echo "      takes away, so a test named under its band is under it whatever"
echo "      else the machine was doing, while one named over its band may"
echo "      only have been sharing -- run that one alone before re-costing."
if [ "$skipped" -ne 0 ] || [ "$untagged" -ne 0 ]; then
    echo "      $skipped test(s) skipped and so measured nothing; $untagged carried"
    echo "      no single cost in the record. Neither is reported on."
fi
echo "      The record holds $held test(s). One the run that left it did not"
echo "      select is not among them, and nothing here speaks for its tag."
exit 0
