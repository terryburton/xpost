#!/bin/sh
# Meson test wrapper: run the filter suite under a leak checker and
# require the process to end holding nothing it allocated for a filter.
#
# What a filter does with the stream beneath it -- take a reference on it,
# give that reference up when it closes, and close and free the stream
# outright when the machinery made it for this filter alone -- has no
# expression in PostScript at all. The program's own assertions say the
# bytes came through; only the checker can say the streams went away.
#
# Where there is no checker the question cannot be asked and the test
# skips, rather than passing without having tested anything.
#
#   $1  path to the built xpost binary
#   $2  path to the PostScript program
#   $3  path to the valgrind suppression file
set -u
xpost=$1
script=$2
supp=$3

# the run happens in a scratch directory, so every path must survive the
# change of directory
# an absolute path may begin with a drive letter as well as a slash;
# prepending the working directory to one of those makes every
# invocation a path that does not exist
case $xpost in /* | ?:/* | ?:\\*) ;; *) xpost=$PWD/$xpost ;; esac
case $script in /*) ;; *) script=$PWD/$script ;; esac
case $supp in /*) ;; *) supp=$PWD/$supp ;; esac

if ! command -v valgrind >/dev/null 2>&1; then
    echo "SKIP: no leak checker on this platform"
    exit 77
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

log=$work/valgrind.log
out=$(
    cd "$work" || exit 1
    valgrind --leak-check=full --show-leak-kinds=definite,indirect \
             --error-exitcode=9 --log-file="$log" \
             --suppressions="$supp" \
             "$xpost" -q --no-sandbox -d null "$script" </dev/null 2>&1
)
status=$?
printf '%s\n' "$out"

if [ "$status" -ne 0 ]; then
    echo "FAILURES: the interpreter exited with status $status"
    [ -f "$log" ] && cat "$log"
    exit 1
fi
if printf '%s\n' "$out" | grep -qE '^(FAIL:|FAILURES:)'; then
    echo "FAILURES: the suite reported failures above"
    exit 1
fi
if ! printf '%s\n' "$out" | grep -q '^SUCCESS$'; then
    echo "FAILURES: the suite did not report success"
    exit 1
fi

# what the checker found: anything lost outright, or lost through
# something that was, is a stream or a coding state nobody gave up
lost=$(sed -n 's/^==[0-9]*==  *\(definitely\|indirectly\) lost: \([0-9,]*\) bytes.*/\2/p' \
       "$log" | tr -d ',')
if [ -z "$lost" ]; then
    echo "FAILURES: the checker reported no leak summary"
    cat "$log"
    exit 1
fi
for n in $lost; do
    if [ "$n" -ne 0 ]; then
        echo "FAILURES: the run ended holding memory it allocated for a filter"
        cat "$log"
        exit 1
    fi
done

echo SUCCESS
exit 0
