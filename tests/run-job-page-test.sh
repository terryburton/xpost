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
#   $1  path to the built xpost binary
set -u
xpost=$1
. "$(dirname "$0")/verdict.sh"

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
    out=$("$xpost" -q $ns -d pgm -o "$work/$name.pgm" "$work/$name.ps" </dev/null 2>&1)
    st=$?
    verdict_run "$st" "$out" "the $name job" || { fail=1; return; }

    # A page that arrived is a raster with something in it. A page that did
    # not leaves the writer nothing to write, so the file stays as small as
    # an empty one -- which is what "nothing" is read as here.
    if [ -s "$work/$name.pgm" ]; then
        got=$(wc -c < "$work/$name.pgm")
    else
        got=0
    fi

    if [ "$expect" = page ]; then
        if [ "$got" -lt 1000 ]; then
            echo "FAILURES: $name left no page, and the program painted one"
            fail=1
        else
            echo "$name: page ($got bytes), as it should"
        fi
    else
        if [ "$got" -ge 1000 ]; then
            echo "FAILURES: $name left a page of $got bytes, and should have left none"
            fail=1
        else
            echo "$name: no page, as it should"
        fi
    fi
}

mark='0 0 moveto 40 0 lineto 40 40 lineto 0 40 lineto closepath fill'

# A program that painted and never asked for its page: the page is ended
# for it. This is the shape of every file written for inclusion.
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

[ "$fail" -eq 0 ] || exit 1
echo SUCCESS
