#!/bin/sh
# Meson test wrapper: the command line options the interpreter documents.
#
# -g takes a geometry as WIDTHxHEIGHT+XOFFSET+YOFFSET and sets the page
# size to it; a geometry that does not parse is an error rather than
# something to carry on past. -V, -L and -h report and exit.
#
#   $1  path to the built xpost binary
set -u
xpost=$1
# an absolute path may begin with a drive letter as well as a slash;
# prepending the working directory to one of those makes every
# invocation a path that does not exist
case $xpost in /* | ?:/* | ?:\\*) ;; *) xpost=$PWD/$xpost ;; esac
. "$(dirname "$0")/verdict.sh"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
printf 'showpage\nquit\n' > "$work/blank.ps"

fail=0
note() { echo "FAIL: $1"; fail=1; }

# A run the option was meant to be accepted by is read for the page it
# left and for how it left: an option taken and then rendered from a
# page that the interpreter died over leaves the page behind either way.
render() {  # $1 what to call it in a complaint, $2... the arguments
    r_who=$1
    shift
    r_out=$("$xpost" -q --no-sandbox "$@" "$work/blank.ps" </dev/null 2>&1)
    verdict_run "$?" "$r_out" "$r_who" || fail=1
}

# the reports each name the program and each says something of its own
"$xpost" --version 2>&1 | grep -q 'Xpost' || note "--version does not name the program"
"$xpost" --license 2>&1 | grep -qi 'redistribution\|license\|BSD' \
    || note "--license does not state the licence"
"$xpost" --help 2>&1 | grep -q -- '--geometry\|-g' \
    || note "--help does not list the geometry option"

# a geometry sets the page size: the raster carries its dimensions
render "a 200x100 geometry" -g 200x100+0+0 -d pgm -o "$work/g.pgm"
if [ -f "$work/g.pgm" ]; then
    dim=$(head -c 32 "$work/g.pgm" | tr '\n' ' ' | awk '{print $2"x"$3}')
    [ "$dim" = "200x100" ] || note "a geometry of 200x100 produced a page of $dim"
else
    note "a well-formed geometry produced no page at all"
fi

# a geometry that does not parse is refused, and refusing it means not
# rendering a page at some other size
"$xpost" -q --no-sandbox -g 200x100 -d pgm -o "$work/bad.pgm" "$work/blank.ps" \
    </dev/null >/dev/null 2>&1
status=$?
[ "$status" -eq 0 ] && [ -f "$work/bad.pgm" ] \
    && note "a geometry missing its offsets was accepted"

"$xpost" -q --no-sandbox -g nonsense -d pgm -o "$work/junk.pgm" "$work/blank.ps" \
    </dev/null >/dev/null 2>&1
status=$?
[ "$status" -eq 0 ] && [ -f "$work/junk.pgm" ] \
    && note "a geometry that is not a geometry was accepted"

# without -g the default page size stands
render "a run with no geometry" -d pgm -o "$work/def.pgm"
if [ -f "$work/def.pgm" ]; then
    dim=$(head -c 32 "$work/def.pgm" | tr '\n' ' ' | awk '{print $2"x"$3}')
    [ "$dim" = "612x792" ] || note "the default page is $dim, not 612x792"
else
    note "no page without a geometry"
fi

[ "$fail" = 0 ] || { echo "FAILURES: the options above"; exit 1; }
echo "SUCCESS"
