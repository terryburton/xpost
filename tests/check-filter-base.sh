#!/bin/sh
# Guard the filter base: what every filter shares is declared once, what a
# filter is a filter over is a property it states rather than a name on a
# list, and what happens when an encode filter is closed is decided in one
# place rather than in each coding.
#
# Nineteen filters once re-spelled the same leading members -- the method
# table, the stream beneath, the pushback byte, the end-of-data latch --
# in nineteen structs, and the machinery that reads them cast to a base
# that only happened to have the same shape. Which files were filters at
# all was a hand-maintained list of the method tables, in one boolean
# expression with arms for the optional libraries: a twentieth filter left
# off that list had its source freed while it still held the pointer, with
# no diagnostic, no assertion and no test.
#
# The eight encoders separately wrote out the same three statements about
# being closed -- refuse further bytes, write the end-of-data once, give up
# what the coding holds -- which is eight places for one rule to differ.
#
# So: every filter struct begins with one of the bases and adds only what
# its own coding needs; the base is declared once; the wrapping is stated
# in the one call every filter is constructed through, which asks for the
# stream by name, so a filter cannot be written that does not say what it
# is over; and the encode family has one method table, with a coding
# supplying only how a byte is encoded, what its end-of-data is, and what
# it has to release.
#
# Sources are read by name. A built tree leaves object files beside them
# and their debug information matches source patterns, so a sweep of the
# directory answers about the build rather than the source.
#
#   $1  path to the source tree root
set -u
src=${1:?usage: check-filter-base.sh <source root>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_workdir
# read a tree whose lines end where the scans below expect them to
guard_mirror_tree "$src"
src=$mirror
lib=$src/src/lib
f=$lib/xpost_file.c
h=$lib/xpost_file.h
guard_require_file "$f" "the file layer"
guard_require_file "$h" "the file layer's header"
fail=0

count() { grep -c "$1" "$2" 2>/dev/null || true; }

want_one() { # pattern description
    n=$(count "$1" "$f")
    if [ "$n" != 1 ]; then
        echo "check-filter-base: $2 is declared $n times in xpost_file.c, expected once"
        fail=1
    fi
}

# ---- what every filter shares, declared once ----
want_one '^    Xpost_File \*source;$'   "a decode filter's source"
want_one '^    int pushback;'           "a decode filter's pushback byte"
want_one '^    int eod;$'               "a decode filter's end-of-data latch"
want_one '^    Xpost_File \*target;$'   "an encode filter's target"
want_one '^    int closed;$'            "an encode filter's closed latch"

# The bit reader and the bit writer are two things, one over a source and
# one over a target, and each is declared once.
for m in 'unsigned int bitbuf;' 'int bitcnt;'; do
    n=$(count "^    $m\$" "$f")
    if [ "$n" != 2 ]; then
        echo "check-filter-base: '$m' is declared $n times in xpost_file.c,"
        echo "      expected 2 (the bit reader and the bit writer)"
        fail=1
    fi
done

# Only the two bases begin with the file itself; every other struct in the
# module begins with a base. A struct that re-spelled the shared members
# would have to start by re-spelling this one.
n=$(count '^    Xpost_File methods;$' "$f")
if [ "$n" != 2 ]; then
    echo "check-filter-base: $n structs in xpost_file.c begin with the file itself,"
    echo "      expected 2 (the decode base and the encode base)"
    fail=1
fi

# ---- the wrapping is read off the file, not off a list of filters ----
body=$(awk '/^static Xpost_File \*_filter_underlying_stream\(Xpost_File \*f\)$/ { on = 1 }
            on { print }
            on && /^}$/ { exit }' "$f")
if [ -z "$body" ]; then
    echo "check-filter-base: _filter_underlying_stream is not where it was"
    fail=1
else
    if ! printf '%s\n' "$body" | grep -q 'f->wraps'; then
        echo "check-filter-base: _filter_underlying_stream no longer reads the"
        echo "      wrapping off the file"
        fail=1
    fi
    if printf '%s\n' "$body" | grep -q '_methods'; then
        echo "check-filter-base: _filter_underlying_stream names method tables again --"
        echo "      the allowlist of which files are filters has come back:"
        printf '%s\n' "$body" | grep -n '_methods' | sed 's/^/      /'
        fail=1
    fi
fi

# ---- one birth point, and it asks what the filter is over ----
n=$(count '_filter_object_cons(mem,' "$f")
if [ "$n" != 2 ]; then
    echo "check-filter-base: $n calls to _filter_object_cons in xpost_file.c,"
    echo "      expected 2 (the decode constructor and the encode constructor)"
    fail=1
fi
for w in SOURCE TARGET; do
    n=$(count "XPOST_FILE_WRAPS_$w," "$f")
    if [ "$n" != 1 ]; then
        echo "check-filter-base: XPOST_FILE_WRAPS_$w is stated as a construction"
        echo "      argument $n times, expected once"
        fail=1
    fi
done
if ! grep -q 'f->wraps = XPOST_FILE_WRAPS_NOTHING;' "$f"; then
    echo "check-filter-base: a file that is a stream in its own right no longer"
    echo "      states that it is over nothing"
    fail=1
fi

# ---- one place decides what a closed encoder does ----
# Every coding once wrote out the same three statements: a closed filter
# takes no more bytes, the end-of-data goes out once and only on the first
# close, and what the coding holds is released there. Eight copies of a
# rule is eight places for it to differ.
n=$(count '^static struct Xpost_File_Methods enc_methods =$' "$f")
if [ "$n" != 1 ]; then
    echo "check-filter-base: the encode family has $n method tables, expected 1"
    fail=1
fi
stray=$(grep -nE '^[a-z0-9]+enc_(writech|close)\(' "$f" || true)
if [ -n "$stray" ]; then
    echo "check-filter-base: a coding has taken back the decision about being"
    echo "      closed, which the encode base makes for all of them:"
    printf '%s\n' "$stray" | sed 's/^/      /'
    fail=1
fi
# and no coding leaves a hook out for the base to test for
holes=$(awk '/^static const Xpost_Enc_Coding /,/^};$/ { if (/NULL/) print }' "$f")
if [ -n "$holes" ]; then
    echo "check-filter-base: a coding leaves one of its three hooks empty;"
    echo "      the shared ones exist so the base never tests for absence:"
    printf '%s\n' "$holes" | sed 's/^/      /'
    fail=1
fi

# ---- and the cast the whole arrangement rests on is asserted ----
if ! grep -q 'offsetof(Xpost_FilterBase, methods) == 0' "$f"; then
    echo "check-filter-base: nothing asserts that the base begins with the file,"
    echo "      which every cast in the module depends on"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "check-filter-base: the filter base is no longer the single declaration."
    exit 1
fi
echo "check-filter-base: ok (one base per family, one birth point, no allowlist,"
echo "                       one decision about a closed encoder)"
exit 0
