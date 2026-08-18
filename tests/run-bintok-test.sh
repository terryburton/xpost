#!/bin/sh
# Meson wrapper: run the binary token conformance corpus through xpost and
# compare against the committed golden record. The golden is the conformance
# baseline (see binary_token_test.ps for its adjudication record), so this
# check is self-contained and needs no other tool.
#   $1  path to the built xpost binary
#   $2  path to binary_token_test.ps
#   $3  path to binary_token_test.expected
set -u
xpost=$1
corpus=$2
golden=$3
. "$(dirname "$0")/verdict.sh"

# scratch is named inside the PS program the interpreter runs, so keep it
# relative: a native interpreter under a POSIX shell need not share /tmp
scratch=bintok_scratch_$$
job=$(mktemp)
out=$(mktemp)
trap 'rm -f "$scratch" "$job" "$out"' EXIT INT TERM

{ echo "/SCRATCH ($scratch) def"; cat "$corpus"; echo quit; } > "$job"

# The golden is the whole of what the run wrote. A run with no terminal on
# its standard input is one the interpreter says nothing of its own to --
# no greeting, no page-boundary announcement, no prompt -- so the output
# channel carries the corpus's answers alone and the record is compared
# against it whole. Nothing is taken out on the way: a line of the
# interpreter's arriving here is a divergence from the record and is
# reported as one rather than being removed before the comparison.
"$xpost" -q -d null "$job" </dev/null 2>/dev/null > "$out"
status=$?
verdict_run "$status" "$(cat "$out")" "the corpus run" || exit 1

# --strip-trailing-cr: the golden may be checked out with CRLF on a host that
# translates line endings, while the interpreter emits LF
if ! diff -u --strip-trailing-cr "$golden" "$out"; then
    echo "FAIL: xpost diverges from the binary token golden record"
    exit 1
fi
echo "xpost matches golden ($(wc -l < "$golden") lines)"
