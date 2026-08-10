#!/bin/sh
# Meson test wrapper: what becomes of the page a job leaves behind.
#
# A program that paints and never asks for its page still has one, and a
# file written to be included in another document is exactly that: it
# carries no showpage, because the including document supplies it. Such a
# file must not render blank.
#
# The converse matters as much. A program that did transmit a page has
# said where its pages end, so anything painted after the last one is a
# page it chose not to finish and is left unfinished; a page that was
# erased has nothing left to end; and a job that painted nothing has no
# page at all. Each of those is a way for this to go wrong by producing a
# page too many, which is worse than the blank it replaces.
#
# Read from the device rather than from the interpreter's own account: a
# page either arrived or it did not, and the raster is where that shows.
# A page is "arrived" here when the output file carries ink, so the
# comparison is against a run of the same shape that should produce none.
#
# Every marking device, not one of them. The rule lives in the page
# machinery and every device reaches it the same way, so a device that
# answered differently would be a device that had gone around it -- which
# is how a behaviour drifts between the devices written in PostScript and
# the ones written in C. Asking one device holds the rule; asking all of
# them holds the rule and the agreement.
#
#   $1  path to the built xpost binary
set -u
xpost=$1
. "$(dirname "$0")/verdict.sh"
. "$(dirname "$0")/device-fleet.sh"

if "$xpost" -h 2>/dev/null | grep -q -- '--no-sandbox'; then
    ns='--no-sandbox'
else
    ns=''
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fail=0

# Each case is a name, what it should leave (page|nothing), and a program.
# The mark is a filled rectangle rather than a stroke, so what is being
# read back is unambiguous ink rather than a hairline.
run_case() { # name expect program
    name=$1; expect=$2; prog=$3
    printf '%s\n' "$prog" > "$work/$name.ps"
    out=$("$xpost" -q $ns -d "$dev" -o "$work/$name.$dev" "$work/$name.ps" </dev/null 2>&1)
    st=$?
    verdict_run "$st" "$out" "the $name job on $dev" || { fail=1; return; }

    # A page that arrived is a raster with something in it. A page that did
    # not leaves the writer nothing to write, so the file stays as small as
    # an empty one -- which is what "nothing" is read as here.
    if [ -s "$work/$name.$dev" ]; then
        got=$(wc -c < "$work/$name.$dev")
    else
        got=0
    fi

    if [ "$expect" = page ]; then
        if [ "$got" -le "$empty" ]; then
            echo "FAILURES: $dev: $name left no page, and the program painted one"
            fail=1
        fi
    else
        if [ "$got" -gt "$empty" ]; then
            echo "FAILURES: $dev: $name left a page of $got bytes, and should have left none"
            fail=1
        fi
    fi
}

mark='0 0 moveto 40 0 lineto 40 40 lineto 0 40 lineto closepath fill'

asked=0

for dev in $DEVICE_FLEET_MARKING; do
    before=$fail

    # Whether this device can be asked at all, established from the
    # device rather than assumed. Two jobs are run whose answers are not
    # in doubt: one paints and transmits, one paints nothing. A device
    # whose output tells those two apart can be asked about the cases
    # that follow; a device that answers the same either way -- one that
    # keeps its raster for whoever embedded the interpreter, or opens its
    # file when it is made rather than when a page arrives -- cannot, and
    # says so instead of being held to a reading its output does not
    # carry.
    printf '%%!PS\n%s\nshowpage\n' "$mark" > "$work/cal-page.ps"
    printf '%%!PS\n%% this job paints nothing\n' > "$work/cal-none.ps"
    out=$("$xpost" -q $ns -d "$dev" -o "$work/cal-page.$dev" "$work/cal-page.ps" </dev/null 2>&1)
    verdict_run "$?" "$out" "the calibration page on $dev" || { fail=1; continue; }
    out=$("$xpost" -q $ns -d "$dev" -o "$work/cal-none.$dev" "$work/cal-none.ps" </dev/null 2>&1)
    verdict_run "$?" "$out" "the calibration blank on $dev" || { fail=1; continue; }

    sz_page=0; [ -e "$work/cal-page.$dev" ] && sz_page=$(wc -c < "$work/cal-page.$dev")
    sz_none=0; [ -e "$work/cal-none.$dev" ] && sz_none=$(wc -c < "$work/cal-none.$dev")

    if [ "$sz_page" -le "$sz_none" ]; then
        echo "$dev: its output does not tell a transmitted page from none; not asked"
        continue
    fi
    # anything above what the no-page job left is a page having arrived
    empty=$sz_none

    run_case unasked page "%!PS
$mark"

# A program that asked: one page, and no second one for the asking.
    run_case asked page "%!PS
$mark
showpage"

# Painted again after the last page was transmitted: that page was never
# finished, and is not finished here either.
    run_case trailing page "%!PS
$mark
showpage
$mark"

# Transmitted by copypage rather than showpage: a page has still been
# transmitted, so what follows is in the same position as above.
    run_case copied page "%!PS
$mark
copypage
$mark"

# Erased before the job ended: nothing remains to end.
    run_case erased nothing "%!PS
$mark
erasepage"

# Painted nothing at all.
    run_case blank nothing "%!PS
% this program draws nothing"


    asked=$((asked + 1))
    if [ "$fail" -eq "$before" ]; then
        echo "$dev: the page a job leaves behind is what it should be"
    fi
done

# A roster that answered for nothing reports as quietly as one that
# answered for everything.
if [ "$asked" -eq 0 ]; then
    echo "FAILURES: no device answered, so the rule was held against nothing"
    exit 1
fi
echo "job-page: held on $asked device(s)"

[ "$fail" -eq 0 ] || exit 1
echo SUCCESS
