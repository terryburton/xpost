#!/bin/sh
# Guard: no clipPath identity is issued twice in one SVG document.
#
# A clip the interpreter cannot reduce to a rectangle is written out as a
# clipPath element and referenced by the fill it cuts. The reference is by
# identity, and an identity naming two elements is resolved to the first
# of them wherever it appears -- so a fill is cut by a shape belonging to
# some other clip. Nothing raises. The document is well formed, every
# element is there, and the page is wrong.
#
# The counter naming those elements is virtual memory, and the document
# is not: bytes already written cannot be taken back. A restore past the
# clip that raised the counter winds the counter back while the elements
# it named stay in the file, and the numbers are handed out again over
# the top of them.
#
# So the program below sets a clip, restores past it, and sets more --
# and every identity the document carries is counted against the number
# of distinct ones. Counting is the point: this defect raises nothing, so
# a run that only asked whether the interpreter succeeded would pass over
# it. Both directions are read, because a document with no clipPath in it
# also has no repeated identity: the elements are counted first, and too
# few of them is a failure in its own right.
#   $1  path to the built xpost binary
set -u
xpost=$1
. "$(dirname "$0")/verdict.sh"

# relative: the OutputFile path is named inside the PS program the
# interpreter runs, and a native interpreter under a POSIX shell need not
# share the shell's view of an absolute path
tmp=svgclipid-$$
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp"

# Three clips that are not rectangles, so each is written as a clipPath.
# The second is set inside a save and the save is restored, which is what
# winds the counter back under the elements already written.
cat > "$tmp/t.ps" <<PSEOF
<< /OutputDevice /svgwrite /OutputFile ($tmp/a.svg) /PageSize [612 792] >> setpagedevice
/tri  { newpath 100 100 moveto 300 150 lineto 200 350 lineto closepath } bind def
/tri2 { newpath 400 400 moveto 560 460 lineto 470 700 lineto closepath } bind def
/tri3 { newpath 60 500 moveto 260 560 lineto 160 760 lineto closepath } bind def

tri clip
gsave 0 0 1 setrgbcolor newpath 0 0 612 792 rectfill grestore

/sv save def
initclip
tri2 clip
gsave 1 0 0 setrgbcolor newpath 0 0 612 792 rectfill grestore
sv restore

initclip
tri3 clip
gsave 0 1 0 setrgbcolor newpath 0 0 612 792 rectfill grestore

showpage
<< /OutputDevice /null >> setpagedevice
quit
PSEOF

out=$("$xpost" -q --no-sandbox -d null -o /dev/null "$tmp/t.ps" </dev/null 2>&1)
status=$?

fail() { echo "FAIL: $1"; exit 1; }

# how the run left is read before the document is, since a device that
# wrote every element and then died on the way out leaves a document that
# looks complete
verdict_run "$status" "$out" "the svg clip identity run" || exit 1

a=$tmp/a.svg
[ -s "$a" ] || fail "no output"

# the identities the document carries, in the order written
ids=$(sed -n 's/.*<clipPath id="\([^"]*\)".*/\1/p' "$a")
n=$(printf '%s\n' "$ids" | grep -c . || true)
u=$(printf '%s\n' "$ids" | grep . | sort -u | grep -c . || true)

# A document with no clipPath in it has no repeated identity either, so
# the count is read before the comparison. Three clips are set above and
# none of them is a rectangle, so three elements are what this expects;
# fewer means the run stopped short or the shapes were reduced, and the
# comparison below would then be answering about a document that never
# drove the case.
if [ "$n" -lt 3 ]; then
    fail "only $n clipPath element(s) written, so the case was not driven"
fi

if [ "$n" -ne "$u" ]; then
    echo "FAIL: $n clipPath element(s) carry only $u distinct identities,"
    echo "      so a reference resolves to an element belonging to another clip:"
    printf '%s\n' "$ids" | sort | uniq -d | sed 's/^/        repeated: /'
    exit 1
fi

echo "SUCCESS ($n clipPath elements, $u distinct identities)"
exit 0
