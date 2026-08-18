#!/bin/sh
#
# Guard: an awk program in this tree parses under the awk every host
# running this suite has, not only under the one the author had.
#
# The suite's guards are shell and awk, so they are the part of the tree
# that runs before anything is built and on every host the build reaches.
# That makes their portability a property of the suite rather than of a
# particular checkout: a guard that will not parse does not report a
# finding, it reports itself, and the thing it was watching goes unread
# for as long as the parse error stands.
#
# The construct this refuses is a redirection target written as a
# concatenation without parentheses:
#
#     print x > dir "/f"          # refused
#     print x > (dir "/f")        # what to write instead
#
# POSIX leaves the target of an output redirection as an unparenthesised
# expression ambiguous with the relational operator, and the awks resolve
# it differently. The two carried by a typical Linux distribution take the
# concatenation and run; the awk in the base system of macOS refuses the
# program outright at parse time, naming the string as the error. The
# parenthesised form is accepted by all three and says which reading was
# meant, so it is the form to write whatever the local awk allows.
#
# The rule is worth a guard rather than a convention because of how it
# fails: the author's awk accepts it, every local run is green, and the
# first report comes from a host the author does not have in front of
# them -- which is the whole shape this suite exists to shorten.
#
# The awks are not the only tools whose lines an awk reads, so a second
# divergence is held at the end: the od in the base system of macOS
# closes -A n output with a line for the final offset, blank under that
# flag, where GNU od ends with the data. The raster-reading helpers are
# run here under an od that writes that extra line, so an assumption
# about od's line count is met on every host rather than only where
# that od lives.
#
# What is deliberately not here: a redirection to a plain name or to a
# quoted string is unambiguous and is left alone, and so is `>` used as
# a comparison, which is why the rule reads only lines that print.
#
#   $1  path to the source tree root
set -u
src=${1:?usage: check-awk-portability.sh <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_require_dir "$src/tests" "the test directory"

guard_workdir
fail=0

# The files that carry awk: the suite's own shell, the tools beside it,
# and any program held in a file of its own. Found rather than listed --
# a file this misses is a file the rule does not reach.
find "$src/tests" "$src/tools" -type f \
     \( -name '*.sh' -o -name '*.awk' \) 2>/dev/null | sort > "$work/files"

if [ ! -s "$work/files" ]; then
    echo "FAILURES: no shell or awk files found under tests or tools; the"
    echo "      rule would read a sound tree and an empty one alike"
    exit 1
fi

# Those files have to carry awk, not merely be named as though they
# might. A tree of empty files has the right shape and holds no program,
# and a rule reading it finds nothing for the same reason it finds
# nothing in a tree written correctly -- so the two would read alike, and
# the rule would be answering about a tree it never read. The count is
# well under what this suite carries, being a floor rather than a census.
carrying=0
while IFS= read -r f; do
    if grep -q 'awk' "$f"; then carrying=$((carrying + 1)); fi
done < "$work/files"
if [ "$carrying" -lt 20 ]; then
    echo "FAILURES: only $carrying of the files read carry an awk program,"
    echo "      so this tree cannot be told from one whose files are empty"
    echo "      and the rule would report success having read nothing"
    exit 1
fi

# A print or printf, then a redirection, then a target that begins with a
# name, a field or a string and carries straight on into another term.
# The three shapes are `> name "..."`, `> $1 "..."` and `> "..." name`.
concat_target='(print|printf)[^#]*>>?[[:space:]]*('\
'[A-Za-z_][A-Za-z_0-9]*[[:space:]]*"'\
'|\$[0-9A-Za-z_()]+[[:space:]]*"'\
'|"[^"]*"[[:space:]]*[A-Za-z_$])'

# A comment line carries no construct -- it is a comment in shell and in
# awk alike -- so it is emptied rather than dropped, which keeps the line
# numbers the report quotes the ones the file has.
: > "$work/found"
while IFS= read -r f; do
    sed 's/^[[:space:]]*#.*//' "$f" | grep -nE "$concat_target" | \
    while IFS= read -r hit; do
        echo "${f#$src/}:$hit"
    done >> "$work/found"
done < "$work/files"

if [ -s "$work/found" ]; then
    echo "FAILURES: an awk redirection target is a concatenation without"
    echo "      parentheses, which the awk in the base system of macOS"
    echo "      refuses at parse time, so the guard holding it reports"
    echo "      itself instead of what it watches:"
    sed 's/^/      /' "$work/found"
    echo "      write the target as (expr) instead."
    fail=1
fi

# The rule is put to a program that carries the construct, so that a
# pattern which finds nothing in a sound tree is told apart from a
# pattern which finds nothing at all.
printf '{ print $1 > %s "/x" }\n' out > "$work/fixture.awk"
if ! grep -qE "$concat_target" "$work/fixture.awk"; then
    echo "FAILURES: the rule does not find the construct in a program"
    echo "      written to carry it, so a tree that carries it reads the"
    echo "      same as one that does not"
    fail=1
fi

# And to one that writes it the way the rule asks for, so that the rule
# is not simply refusing every redirection.
printf '%s\n' \
    '{ print $1 > (out "/x") }' \
    '{ print $1 > "/dev/stderr" }' \
    '{ if (a > b) print a }' > "$work/clean.awk"
if grep -qE "$concat_target" "$work/clean.awk"; then
    echo "FAILURES: the rule finds the construct in a program that writes"
    echo "      the target parenthesised, so it refuses what it asks for"
    fail=1
fi

# The od divergence, met here rather than on the host that carries it.
# An od standing in for the macOS one runs the real od and closes with
# the extra line; the helpers that read rasters through od are held to
# their answers under it. A helper that takes every line for data grows
# a second line, and where its answer rides into an awk as a -v value
# the extra line is a newline inside a string, which the awk in that
# same base system refuses at parse time -- the guard then reports
# itself instead of what it watches. The pages are the two grounds the
# helpers know: a bilevel page with four bits set, and a grey page with
# two marked bytes.
realod=$(command -v od)
mkdir -p "$work/shim"
printf '#!/bin/sh\n%s "$@"\necho\n' "$realod" > "$work/shim/od"
chmod +x "$work/shim/od"
printf 'P4\n2 2\n\300\300' > "$work/probe.pbm"
printf 'P5\n2 2\n255\n\000\377\000\377' > "$work/probe.pgm"

check_od() {    # <which helper on which page> <the answer it owes> <the answer given>
    if [ "$3" != "$2" ]; then
        echo "FAILURES: $1 answered '$3' rather than '$2' under an od that"
        echo "      closes its output with a line for the final offset, as"
        echo "      the od in the base system of macOS does"
        fail=1
    fi
}
check_od "guard_pnm_bilevel on a bilevel page" "yes" \
    "$(PATH="$work/shim:$PATH" guard_pnm_bilevel "$work/probe.pbm")"
check_od "guard_pnm_bilevel on a grey page" "no" \
    "$(PATH="$work/shim:$PATH" guard_pnm_bilevel "$work/probe.pgm")"
check_od "guard_pnm_ink on a bilevel page" "ink 4" \
    "$(PATH="$work/shim:$PATH" guard_pnm_ink "$work/probe.pbm")"
check_od "guard_pnm_ink on a grey page" "ink 2" \
    "$(PATH="$work/shim:$PATH" guard_pnm_ink "$work/probe.pgm")"

exit $fail
