#!/bin/sh
# Meson test wrapper: run the PLRM-example conformance suite (data/test.ps)
# in the freshly built interpreter and pass iff it reports SUCCESS -- i.e.
# the suite's internal failcount reached zero.
#   $1  path to the built xpost binary
#   $2  path to test.ps
#
# Two things a test written for this harness has to get right, both of
# which fail by reporting success rather than by reporting anything:
#
#   A verdict reached inside save/restore cannot be recorded there.
#   restore reverts local VM, and the failcount lives in userdict, so a
#   failure detected between a save and its restore is erased before it
#   can be printed -- the run reports SUCCESS with the failing assertion
#   already forgotten. Leave the verdict on the operand stack instead,
#   where a boolean survives the restore, and judge after restoring.
#
#   A test that reaches into an internal dictionary must fail when it
#   cannot find what it is looking for, not skip. A lookup guarded by
#   `known` that falls back to an empty result turns a moved member into
#   a differently-shaped test that still passes, or -- worse -- into a
#   run whose exemptions have quietly vanished and whose failures are
#   therefore inventions. Both have happened here.
set -u
xpost=$1
script=$2
# these conformance tests exercise the interpreter's own file operations, so
# run with the CLI file-access sandbox lifted
# capture the interpreter's exit status as well as its output: a run that
# reports SUCCESS and then dies during teardown -- a crash, an assertion,
# a sanitizer abort -- must not be recorded as a pass
out=$("$xpost" -q --no-sandbox -d null "$script" </dev/null 2>&1)
status=$?
printf '%s\n' "$out"
if [ "$status" -ne 0 ]; then
    echo "FAILURES: the interpreter exited with status $status"
    exit 1
fi
# a suite that printed any failure line has failed, whatever it concluded
if printf '%s\n' "$out" | grep -qE '^(FAIL:|FAILURES:|MISMATCH)'; then
    echo "FAILURES: the suite reported failures above"
    exit 1
fi
printf '%s\n' "$out" | grep -q '^SUCCESS$'
