#!/bin/sh
#
# That the start of a raster is found in one place.
#
# A test that wants to know what reached the page renders one and counts
# what it finds, and to reach the pixels it must first step over a header:
# a magic number, the dimensions, and a maximum value for every format but
# the bilevel one, separated by any whitespace and interruptible by a
# comment. Nine scripts here had each written that walk out for itself.
#
# The reason to write it once is not the nine copies. It is that getting it
# wrong is silent: a walk that steps over one token too many still yields a
# raster, just one shifted by a few bytes, and every count taken from it is
# plausible and wrong. A guard reporting a wrong number goes green.
#
# So the population is taken from the DIRECTORY rather than from a list:
# every script that pulls bytes out of a page must call the shared reader,
# unless it is named in tests/raster_reading.exempt with a reason. Holding
# one list against another would be blind to whatever is absent from both,
# which is exactly the state a newly written script is in.
#
#   $1  path to the source tree root
set -u
src=${1:?usage: check-raster-reading.sh <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"

reg="$src/tests/raster_reading.exempt"
guard_require_file "$reg" "the register of scripts that read a page themselves"

guard_workdir

fail=0

# The scripts that take bytes out of a rendered page: they name a page file
# or a binary PNM magic number, and they run one of the byte extractors
# over it. Derived by what a script DOES, not by what it is called.
for f in "$src"/tests/*.sh; do
    b=${f##*/}
    [ "$b" = guard-paths.sh ] && continue
    [ "$b" = check-raster-reading.sh ] && continue
    grep -qE '\.(pgm|pbm|ppm|pnm)\b|[^A-Za-z0-9]P[456][^0-9]' "$f" || continue
    grep -qE 'od -An|tail -c[ ]*[+"$]|[^a-z]dd if=' "$f" || continue
    echo "$b" >> "$work/reads"
done
[ -f "$work/reads" ] || : > "$work/reads"

# Those that go through the shared reader are satisfied.
: > "$work/hand"
while read -r b; do
    if grep -q 'guard_pnm_pixels\|guard_pnm_ink' "$src/tests/$b"; then
        continue
    fi
    echo "$b" >> "$work/hand"
done < "$work/reads"

# The register, and the reason each entry gives.
awk 'NF && $1 !~ /^#/ { print $1 }' "$reg" | LC_ALL=C sort -u > "$work/exempt"

# An entry naming a script that is not there is a failure of its own.
while read -r b; do
    [ -n "$b" ] || continue
    if [ ! -f "$src/tests/$b" ]; then
        echo "FAILURES: tests/raster_reading.exempt names $b, which is not in"
        echo "          tests/. An exemption that outlives its script covers a"
        echo "          later script of the same name without anyone deciding to"
        fail=1
    elif ! grep -qE "^$b[[:space:]]+[^[:space:]]" "$reg"; then
        echo "FAILURES: tests/raster_reading.exempt names $b with no reason"
        fail=1
    fi
done < "$work/exempt"

LC_ALL=C sort -u "$work/hand" > "$work/hand.s"
guard_held=0
guard_hold "$work/hand.s" "$work/exempt" \
    "these scripts read a page's bytes without the shared reader and are not
     in the register. Call guard_pnm_pixels or guard_pnm_ink, or add the
     script to tests/raster_reading.exempt with the reason it cannot" \
    "these scripts are excused from the shared reader and no longer read a
     page by hand. Remove them from tests/raster_reading.exempt, so that the
     register says what is true today"
[ "$guard_held" -eq 0 ] || fail=1

[ "$fail" -eq 0 ] && echo "PASS: every page is read through the one reader"
exit $fail
