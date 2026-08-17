#!/bin/sh
# Meson test wrapper: run the PLRM-example conformance suite (data/test.ps)
# in the freshly built interpreter and pass iff it reports SUCCESS -- i.e.
# the suite's internal failcount reached zero.
#   $1  path to the built xpost binary
#   $2  path to test.ps
#   $3  optional: a name=value definition to hand the suite, as any
#       caller would hand one in. A suite that needs to know something
#       about the run it is part of -- where it was started from, say --
#       is told by whoever started it, since what the interpreter itself
#       settled about the run is kept where a program cannot read it.
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
define=${3:-}
. "$(dirname "$0")/verdict.sh"
# these conformance tests exercise the interpreter's own file operations, so
# run with the CLI file-access sandbox lifted
# capture the interpreter's exit status as well as its output: a run that
# reports SUCCESS and then dies during teardown -- a crash, an assertion,
# a sanitizer abort -- must not be recorded as a pass
# The shared framework (tests/testlib.ps) is prepended to a suite whose
# first line asks for it, and to no other. It has to be asked for: some
# suites here assert that a program's own dictionary starts empty or count
# the dictionary stack, and a framework defined into userdict for one of
# those would be the thing it reports -- and six read their own source
# through currentfile, which prepending would shift. So the opt-in is a
# marker in the file, where a reader of the file can see it, rather than a
# list kept over here.
run=$script
lib=$(dirname "$0")/testlib.ps
case $(head -n 1 "$script" 2>/dev/null) in
    '%!testlib'*)
        if [ ! -f "$lib" ]; then
            echo "FAILURES: $script asks for the test framework and"
            echo "      $lib is not there"
            exit 1
        fi
        run=$(mktemp "${TMPDIR:-/tmp}/xpost-testlib-XXXXXX.ps") || exit 1
        trap 'rm -f "$run"' EXIT INT TERM
        cat "$lib" "$script" > "$run" || exit 1 ;;
esac
# A definition is passed only when one was asked for: every other suite
# here asserts that a program's own dictionary starts empty, and a
# definition made for one of them would be the thing that filled it.
if [ -n "$define" ]; then
    out=$("$xpost" -q --no-sandbox -d null "-D$define" "$run" </dev/null 2>&1)
else
    out=$("$xpost" -q --no-sandbox -d null "$run" </dev/null 2>&1)
fi
status=$?
printf '%s\n' "$out"
if [ "$status" -ne 0 ]; then
    echo "FAILURES: the interpreter exited with status $status"
    exit 1
fi
verdict_ok "$out" "the suite"
