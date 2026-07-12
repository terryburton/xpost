#!/bin/sh
# Meson wrapper: run the binary token conformance corpus and compare
# against the golden record (the PLRM's answer, adjudicated; see the
# header of binary_token_test.ps). When gs is available it is diffed
# against the same golden outside its documented deviations, so a stale
# golden or a drifting interpreter fails too.
#   $1  path to the built xpost binary
#   $2  path to binary_token_test.ps
#   $3  path to binary_token_test.expected
set -u
xpost=$1
corpus=$2
golden=$3

# scratch is named inside the PS program the interpreter runs, so keep it
# relative: a native interpreter under a POSIX shell need not share /tmp
scratch=bintok_scratch_$$
job=$(mktemp)
out=$(mktemp)
trap 'rm -f "$scratch" "$job" "$out"' EXIT

{ echo "/SCRATCH ($scratch) def"; cat "$corpus"; echo quit; } > "$job"

"$xpost" -q -d null "$job" </dev/null 2>/dev/null \
    | grep -v '^Xpost\|^Copyright\|WARRANTY\|COPYING\|^PS' > "$out"

# --strip-trailing-cr: the golden may be checked out with CRLF on a host that
# translates line endings, while the interpreter emits LF
if ! diff -u --strip-trailing-cr "$golden" "$out"; then
    echo "FAIL: xpost diverges from the binary token golden record"
    exit 1
fi
echo "xpost matches golden ($(wc -l < "$golden") lines)"

if command -v gs >/dev/null 2>&1; then
    # gs deviates from the adjudicated golden on the lines the corpus
    # header documents; the comparison holds everywhere else
    gsdev='^(fixed ieee be|fixed native|fixed ieee le|bseq exec int|bseq exec real|bseq exec bool):'
    { echo "/SCRATCH ($scratch) def"; cat "$corpus"; } \
        | gs -q -dNOSAFER -dNOPAUSE -dBATCH -sDEVICE=nullpage - 2>/dev/null \
        | grep -vE "$gsdev" > "$out"
    if ! grep -vE "$gsdev" "$golden" | diff -u - "$out"; then
        echo "FAIL: the golden record is stale against gs"
        exit 1
    fi
    echo "golden matches gs outside the documented deviations"
else
    echo "gs not found: golden staleness not checked"
fi
