#!/bin/sh
#
# Every filter is asked the same two questions, and answers them.
#
# The filter operator knows seventeen names. What separates them is not
# written down anywhere the language can be read from: two of them
# refuse to be built from a name alone and want a parameter dictionary
# first, and four of them are in a build only if it found a library.
# Both are differences a program meets -- a name that answers undefined
# is a name it cannot use -- and until this file nothing said which
# names those were, or why.
#
# The family matters more than any member. The last defect in these was
# a lazy end-of-data desynchronisation present in ALL FIVE stream
# decoders, because each was written by copying the one before it; a
# question asked of one member and not the others is exactly how a
# family acquires a defect in every member at once.
#
# So this asks all seventeen, in one interpreter, and holds three things
# to each other:
#
#   the names the source compares against, which is the membership
#   what tests/filter-facts says each one is
#   what the interpreter really answers when asked to build it
#
# Membership is derived rather than listed, so a filter added to the
# operator and to no register fails here rather than passing unexamined.
# The build symbol is derived too, from the conditional the source
# encloses the name in, so a register line claiming the wrong library --
# or a name that quietly stopped being conditional -- is a failure and
# not a comment nobody re-read.
#
#   $1  path to the source tree root
#   $2  path to the xpost binary
set -u
src=${1:?usage: check-filter-facts.sh <srcroot> <xpost>}
xpost=${2:?usage: check-filter-facts.sh <srcroot> <xpost>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_require_file "$src/src/lib/xpost_op_file.c" "the filter operator"
guard_require_file "$src/tests/filter-facts" "the filter register"

guard_workdir
trap 'rm -rf "$work"' EXIT
cr=$(printf '\r')

# ---- membership, and the build each name needs, read from the source
#
# The conditional a name sits inside is tracked as the file is scanned,
# so a name is reported with the innermost HAVE_ symbol enclosing it and
# with "always" where there is none. Only HAVE_ conditionals count: the
# file has others, and they are not statements about which library the
# build found.
tr -d "$cr" < "$src/src/lib/xpost_op_file.c" | awk '
    /^[ \t]*#[ \t]*ifdef[ \t]+HAVE_[A-Z0-9_]+/ {
        for (i = 1; i <= NF; i++)
            if ($i ~ /^HAVE_/) { depth++; sym[depth] = $i }
        next
    }
    /^[ \t]*#[ \t]*if/  { other++; next }
    /^[ \t]*#[ \t]*endif/ {
        if (other > 0) other--
        else if (depth > 0) depth--
        next
    }
    # A name is reported with the condition on the comparison that goes
    # on to BUILD it, not on every comparison that mentions it. The
    # parameter-taking paths compare the same names again and hand the
    # work back to the plain dispatch, so the condition there is not the
    # one that decides whether the build has the filter at all.
    /xpost_file_cons_filter_/ {
        if (pending != "") { seen[pending] = pendwhere; pending = "" }
    }
    {
        line = $0
        while (match(line, /strcmp\(cname, "[A-Za-z0-9]+(Encode|Decode)"\)/)) {
            piece = substr(line, RSTART, RLENGTH)
            line = substr(line, RSTART + RLENGTH)
            if (match(piece, /"[A-Za-z0-9]+"/)) {
                nm = substr(piece, RSTART + 1, RLENGTH - 2)
                mentioned[nm] = 1
                pending = nm
                pendwhere = (depth > 0) ? sym[depth] : "always"
            }
        }
    }
    END {
        # a name mentioned and never seen building is still a member --
        # it is a name the operator answers to -- and it is reported
        # unconditional, since nothing guarded a construction of it
        for (n in mentioned)
            print n, (n in seen) ? seen[n] : "always"
    }
' | LC_ALL=C sort > "$work/source"

if [ ! -s "$work/source" ]; then
    echo "FAILURES: no filter names were read from src/lib/xpost_op_file.c;"
    echo "      the operator was rewritten in a way this cannot follow, and"
    echo "      a check that finds no members proves nothing about them"
    exit 1
fi
awk '{ print $1 }' "$work/source" | LC_ALL=C sort > "$work/source-names"

# ---- what the register says
sed 's/#.*//' "$src/tests/filter-facts" | awk 'NF >= 4 && $1 != "entries"' \
    > "$work/reg"
awk '{ print $1 }' "$work/reg" | LC_ALL=C sort > "$work/reg-names"

fail=0

# ---- the membership, both ways
guard_held=0
guard_hold "$work/source-names" "$work/reg-names" \
    "compared against by the filter operator and not classified in
      tests/filter-facts. Say there whether it can be built from a name
      alone and which build carries it. A filter nobody classified is
      one the family was never asked about:" \
    "classified in tests/filter-facts and not a name the filter operator
      compares against. A line that has outlived its filter reads
      exactly like one that still holds:"
[ "$guard_held" -eq 0 ] || fail=1

# ---- the build symbol, held to what encloses the name
while read -r name where; do
    said=$(awk -v n="$name" '$1 == n { print $3; exit }' "$work/reg")
    [ -n "$said" ] || continue          # already reported as unclassified
    if [ "$said" != "$where" ]; then
        echo "FAIL: tests/filter-facts says $name is $said and the source"
        echo "      builds it under $where."
        echo "      The register is meant to say which builds carry the"
        echo "      filter; one that names the wrong condition sends a"
        echo "      reader to a library that has nothing to do with it."
        fail=1
    fi
done < "$work/source"

# ---- and what the interpreter answers when asked
#
# Each name is offered a file of the direction it wants and nothing
# else, and what comes back is ok or the error. An encoding filter is
# given a writable file, since one over a readable file is refused
# before the name is even looked at, and its output goes to a scratch
# file rather than to standard output, which this reads.
printf 'x' > "$work/in.tmp"
{
    printf '/inpath (%s/in.tmp) def\n' "$work"
    printf '/outpath (%s/out.tmp) def\n' "$work"
    cat <<'PSEOF'
/isenc { 80 string cvs dup length 6 sub 6 getinterval (Encode) eq } def
/ask {                                  % /Name  .  -
    /nm exch def
    nm 40 string cvs print ( ) print
    nm isenc {
        { outpath (w) file nm filter } stopped
    }{
        { inpath (r) file nm filter } stopped
    } ifelse
    { $error /errorname get 40 string cvs }{ pop (ok) } ifelse
    print (\n) print
    clear
} def
PSEOF
    awk '{ printf "/%s ask\n", $1 }' "$work/reg-names"
} > "$work/probe.ps"

XPOST_DATA_DIR="$src/data" "$xpost" -q --no-sandbox -d null -o /dev/null \
    "$work/probe.ps" </dev/null 2>/dev/null \
    | tr -d "$cr" | awk 'NF == 2 { print }' | LC_ALL=C sort > "$work/answers"

nasked=$(grep -c . "$work/answers" || true)
nmembers=$(grep -c . "$work/reg-names" || true)
if [ "$nasked" -ne "$nmembers" ]; then
    echo "FAILURES: $nmembers filters were asked and $nasked answered."
    echo "      A member that does not answer is one this check passes over"
    echo "      in silence, which is the state it exists to prevent."
    exit 1
fi

while read -r name answer; do
    said=$(awk -v n="$name" '$1 == n { print $2; exit }' "$work/reg")
    where=$(awk -v n="$name" '$1 == n { print $3; exit }' "$work/reg")
    [ -n "$said" ] || continue
    case "$said:$answer" in
        plain:ok|needsdict:undefined)
            ;;
        plain:undefined)
            # a conditional filter absent from this build answers the
            # same way, and that is the one reading this cannot tell
            # apart, so it is accepted and counted
            if [ "$where" = always ]; then
                echo "FAIL: tests/filter-facts says $name builds from a name"
                echo "      alone and the interpreter answers undefined. It is"
                echo "      in no build condition, so there is nothing that"
                echo "      could have left it out."
                fail=1
            else
                absent="$absent $name"
            fi ;;
        needsdict:ok)
            echo "FAIL: tests/filter-facts says $name needs a parameter"
            echo "      dictionary and the interpreter built it from the name"
            echo "      alone. Either it grew defaults for the parameters it"
            echo "      had none for -- say so there -- or it is building"
            echo "      something it has not been told the shape of."
            fail=1 ;;
        *)
            echo "FAIL: $name answered $answer, which is neither the ok that"
            echo "      would mean it built nor the undefined that would mean"
            echo "      the operator does not know the name."
            fail=1 ;;
    esac
done < "$work/answers"

# ---- the count, so retiring a filter is two edits
entries=$(awk '/^entries /{ print $2; found = 1 } END { if (!found) print "" }' \
    "$src/tests/filter-facts")
case $entries in
    ''|*[!0-9]*)
        echo "FAILURES: tests/filter-facts has no 'entries <n>' line"
        fail=1 ;;
    *)  if [ "$entries" -ne "$nmembers" ]; then
            echo "FAILURES: tests/filter-facts records $entries filters and"
            echo "      holds $nmembers"
            fail=1
        fi ;;
esac

# ---- and every line says why
while read -r name kind where rest; do
    if [ -z "$rest" ]; then
        echo "FAIL: the line for $name gives no reason. A member classified"
        echo "      without one is a member nobody examined."
        fail=1
    fi
    case $kind in
        plain|needsdict) ;;
        *)  echo "FAIL: $name is '$kind', which is neither plain nor needsdict"
            fail=1 ;;
    esac
done < "$work/reg"

[ "$fail" = 0 ] || exit 1
absent=${absent:-}
if [ -n "$absent" ]; then
    printf 'SUCCESS (%s filters, each classified and each asked; not in this build:%s)\n' \
        "$nmembers" "$absent"
else
    printf 'SUCCESS (%s filters, each classified and each asked, all present in this build)\n' \
        "$nmembers"
fi
