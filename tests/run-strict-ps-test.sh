#!/bin/sh
# Meson test wrapper: run a PostScript test and require that, apart from the
# interpreter's fixed startup banner and prompt, the output is exactly
# "SUCCESS" -- every assertion must hold AND nothing else may print. Guards
# machinery that must stay silent: a stray diagnostic from a device method
# interleaves with a page stream written to standard output and corrupts it.
#   $1  path to the built xpost binary
#   $2  path to the test script
set -u
xpost=$1
script=$2
# capture stdout only: the silence requirement is about the page-stream
# channel; the log channel (stderr) is judged by other tests
out=$("$xpost" -q --no-sandbox -d null "$script" </dev/null 2>/dev/null)
printf '%s\n' "$out"
filtered=$(printf '%s\n' "$out" \
    | grep -v '^Xpost ' \
    | grep -v '^Copyright (C)' \
    | grep -v '^This software is supplied' \
    | grep -v '^see the file COPYING' \
    | sed 's/^PS> *$//' \
    | grep -v '^$')
test "$filtered" = "SUCCESS"
