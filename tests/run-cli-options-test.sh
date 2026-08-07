#!/bin/sh
# Meson test wrapper: the command line options the interpreter documents.
#
# -g takes a geometry as WIDTHxHEIGHT+XOFFSET+YOFFSET and sets the page
# size to it; a geometry that does not parse is an error rather than
# something to carry on past. -V, -L and -h report and exit.
#
# -D and -I are the two options that hand the job something rather than
# configure the run: -Dname=token defines the name in userdict before the
# program starts, and -I adds a directory the resource machinery searches
# when findresource misses in VM. Each is asked for twice, in the form
# that carries its value attached to the letter and the form that carries
# it as the next argument, and each is asked once more with the option
# left out -- an assertion that a name is defined proves nothing unless
# the same program finds it undefined when nothing defined it.
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

# -D reports what it left in userdict, for a name given a token, a name
# given a string and a name given nothing at all
cat > "$work/defs.ps" <<'PSEOF'
/report { % /name  .  -
    dup dup 32 string cvs print (=) print
    userdict exch known { load == }{ (absent) = pop } ifelse
} bind def
/alpha report /beta report /gamma report
quit
PSEOF

# the run's output is left in got rather than handed back through a
# command substitution: a subshell that records a failure records it in a
# copy of the variable and the run it judged passes
run_defs() {  # $1 what to call it, $2... the options before the program
    d_who=$1
    shift
    got=$("$xpost" -q -d null "$@" "$work/defs.ps" </dev/null 2>&1)
    verdict_run "$?" "$got" "$d_who" || fail=1
}

run_defs "a run with three definitions" \
        -Dalpha=42 --define 'beta=(hi)' -Dgamma
printf '%s\n' "$got" | grep -q '^alpha=42$' \
    || note "-Dalpha=42 did not define alpha as 42"
printf '%s\n' "$got" | grep -q '^beta=(hi)$' \
    || note "--define beta=(hi) did not define beta as the string"
printf '%s\n' "$got" | grep -q '^gamma=null$' \
    || note "-Dgamma with no value did not define gamma as null"

# the same program with nothing defining them: the three names are the
# option's doing and not the interpreter's
run_defs "a run with no definitions"
printf '%s\n' "$got" | grep -q '^alpha=absent$' \
    || note "alpha is defined without -D"
printf '%s\n' "$got" | grep -q '^beta=absent$' \
    || note "beta is defined without --define"
printf '%s\n' "$got" | grep -q '^gamma=absent$' \
    || note "gamma is defined without -D"

# -I: an instance the program asks for by name, sitting in a resource
# tree that only the option knows about. Two directories are given so
# that the search reaches past the first, and the run keeps the sandbox
# so that a directory named this way is one the confinement lets in.
mkdir -p "$work/res1/ProcSet" "$work/res2/ProcSet"
printf '/CliProbe << /Greeting (INCLUDE-OK) >> /ProcSet defineresource pop\n' \
    > "$work/res2/ProcSet/CliProbe"
cat > "$work/incs.ps" <<'PSEOF'
{ /CliProbe /ProcSet findresource /Greeting get }
stopped { (include=absent) = }{ (include=) print print (\n) print } ifelse
quit
PSEOF

run_incs() {  # $1 what to call it, $2... the options before the program
    n_who=$1
    shift
    got=$("$xpost" -q -d null "$@" "$work/incs.ps" </dev/null 2>&1)
    verdict_run "$?" "$got" "$n_who" || fail=1
}

run_incs "a run with two include directories" \
        -I "$work/res1" "-I$work/res2"
printf '%s\n' "$got" | grep -q '^include=INCLUDE-OK$' \
    || note "-I did not put the instance's directory on the resource path"

run_incs "a run with no include directory"
printf '%s\n' "$got" | grep -q '^include=absent$' \
    || note "the instance is found without -I"

# an option whose value is the next argument and has no next argument is
# refused rather than taken as empty
for opt in --define --include; do
    "$xpost" -q -d null "$opt" </dev/null >/dev/null 2>&1
    status=$?
    [ "$status" -eq 0 ] && note "$opt with no value was accepted"
done

[ "$fail" = 0 ] || { echo "FAILURES: the options above"; exit 1; }
echo "SUCCESS"
