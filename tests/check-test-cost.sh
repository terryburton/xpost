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
#   else -- "slow" is an ordinary English word and the corpus has a file
#   called it -- across the three directories a second list could live
#   in. The documentation says what the profiles are and is not a second
#   list: a name in prose selects nothing.
#
# and one that would make the profiles mean nothing:
#
#   a cost declared on a test that no profile can reach. The wrapper's
#   profiles are cost ranges, so every tag a registration may carry has
#   to be a tag some profile names; a fourth tag introduced and left out
#   of the wrapper would put its tests in no profile at all.
#
#   $1  path to the source tree root
set -u
src=${1:?usage: check-test-cost.sh <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_require_file "$src/meson.build" "the build description"

guard_workdir
trap 'rm -rf "$work"' EXIT
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
total=$(wc -l < "$work/calls")
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
exit 0
