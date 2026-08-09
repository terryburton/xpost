#!/bin/sh
# Meson test wrapper: rendering one document per job must not spend global
# memory per document.
#
# Global memory is never collected, so anything a document allocates there is
# spent for good. A writer that builds its across-page bookkeeping per
# document therefore leaks tens of kilobytes a document, which a long-running
# context pays for until it is restarted. The workload installs and retires
# the PDF writer repeatedly and the script reports what each document cost.
#
#   $1  path to the built xpost binary
#   $2  path to the PostScript workload
set -u
xpost=$1
script=$2
. "$(dirname "$0")/verdict.sh"

pdf=$(mktemp)
trap 'rm -f "$pdf"' EXIT

out=$("$xpost" -q --no-sandbox -d pdfwrite -o "$pdf" "$script" </dev/null 2>&1)
status=$?

verdict_run "$status" "$out" "the interpreter" || exit 1

echo "$out" | grep -q '^SUCCESS' || {
    echo "$out" | sed -n 's/^/  /p' | tail -3
    echo "FAIL: global memory grows with the number of documents"
    exit 1
}

# the run must actually have produced documents, or the measurement is of
# nothing happening
head -c 8 "$pdf" | grep -q '%PDF-1' || {
    echo "FAIL: no document was written -- the workload did not run"
    exit 1
}

echo "$out" | sed -n 's/^  global memory/  global memory/p'
echo "SUCCESS"
