#!/bin/sh
# Meson test wrapper: hold the xpost_dsc program to its own report.
#
# The program parses a document's structuring comments through
# libxpost_dsc and prints what the header and the page table say. Three
# things are held here: a conforming document is reported faithfully
# (version, title, page count, bounding box, and a start for every page
# the count promises); a header whose optional comments are absent is
# reported with empty fields rather than through undefined behaviour;
# and the two refusals -- no argument, and a file that cannot be opened
# -- refuse with a nonzero status instead of an answer.
#
#   $1  path to the built xpost_dsc binary
set -u
dsc=$1
. "$(dirname "$0")/verdict.sh"

verdict_workdir

fail=0

# a conforming two-page document, every header comment it reports set
cat > "$work/doc.ps" <<'PS'
%!PS-Adobe-3.0
%%Title: probe
%%Creator: dsc-tool-test
%%CreationDate: 2026-08-19
%%For: the test
%%Pages: 2
%%BoundingBox: 0 0 100 200
%%EndComments
%%EndProlog
%%Page: one 1
showpage
%%Page: two 2
showpage
%%EOF
PS
out=$("$dsc" "$work/doc.ps" 2>&1)
if ! verdict_run "$?" "$out" "the conforming document's report"; then
    printf '%s\n' "$out"
    fail=1
fi
for want in 'result : good' 'version : 3.0' 'title : probe' \
            'creator : dsc-tool-test' 'pages : 2' \
            'bounding box : 0 0 100 200' 'label: one' 'label: two'; do
    if ! printf '%s\n' "$out" | grep -qF "$want"; then
        echo "FAILURES: the report does not say \"$want\""
        fail=1
    fi
done

# the optional header comments absent: the fields are reported empty
cat > "$work/bare.ps" <<'PS'
%!PS-Adobe-3.0
%%Pages: 1
%%BoundingBox: 0 0 10 10
%%EndComments
%%EndProlog
%%Page: 1 1
showpage
%%EOF
PS
out=$("$dsc" "$work/bare.ps" 2>&1)
if ! verdict_run "$?" "$out" "the bare header's report"; then
    printf '%s\n' "$out"
    fail=1
fi
for line in 'title : ' 'creator : ' 'creation date : '; do
    if ! printf '%s\n' "$out" | grep -q "^$line\$"; then
        echo "FAILURES: an absent header comment did not report as an"
        echo "      empty \"$line\" field"
        fail=1
    fi
done

# the refusals: no argument, and a file that is not there
"$dsc" > "$work/usage.out" 2>&1
if [ $? -eq 0 ]; then
    echo "FAILURES: a run with no argument answered success"
    fail=1
fi
grep -q '^Usage:' "$work/usage.out" || {
    echo "FAILURES: a run with no argument did not print its usage"
    fail=1
}
"$dsc" "$work/absent.ps" > "$work/absent.out" 2>&1
if [ $? -eq 0 ]; then
    echo "FAILURES: a file that cannot be opened was answered with success"
    fail=1
fi

[ "$fail" -ne 0 ] && exit 1
echo "SUCCESS"
exit 0
