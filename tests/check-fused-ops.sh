#!/bin/sh
# Guard the fused-execution coverage invariant: every operator the
# interpreter executes inline while walking a procedure must appear in
# tests/fused_equivalence_test.ps, which runs it both ways and requires
# identical behaviour.
#
# The fused copies re-implement operator semantics, so an uncovered one
# can drift from the operator it stands in for and no ordinary test will
# notice -- exactly how the fused array get and put came to skip their
# access checks. Adding a new operator to the fast path without its
# equivalence cases fails here.
#
# What counts as coverage is a case, not a mention. The check used to
# look for the operator's name anywhere in the file, so a comment naming
# it answered for it: the whole equivalence test could be replaced by one
# line listing the operators and this reported full coverage. A case in
# that file is a string of PostScript followed by `same`, which runs it
# both ways and compares; that is what is counted here.
#
# Usage: check-fused-ops.sh <src/lib directory> <fused_equivalence_test.ps>

set -eu

libdir=${1:?usage: check-fused-ops.sh <src/lib dir> <equivalence test>}
testps=${2:?usage: check-fused-ops.sh <src/lib dir> <equivalence test>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_dir "$libdir" "the library source directory"
guard_require_file "$testps" "the equivalence test"
guard_require_file "$libdir/xpost_interpreter.c" "the interpreter"

# The operators the interpreter inlines: reference-table entries reached by
# XPOST_OP_CODE, which yields an opcode to recognise the current element by.
# The entries the interpreter schedules instead of recognising are reached by
# XPOST_OP, which yields the operator object, and are not fused bodies. Four
# opcodes are used for other purposes still (re-entering the scanner,
# unwinding a wrapped-operator frame sealed or not, and finding the boundary
# of a call back into a program's procedure) and are listed here as exempt.
exempt='token wrapdone wrapsealed calloutdone'

fused=$(grep -oE 'XPOST_OP_CODE\(ctx, *[a-z]+\)' "$libdir/xpost_interpreter.c" \
        | sed -E 's/^.*, *//; s/\)$//' | sort -u)

# an empty list would make this check vacuously pass, so a rename of the
# reference-table accessor cannot be allowed to disarm it
if [ -z "$fused" ]; then
    echo "FAIL: no fused operators found in xpost_interpreter.c --"
    echo "      the reference accessor was renamed and this check disarmed"
    exit 1
fi

# The cases: a PostScript string on its own line, followed by `same`.
guard_workdir
trap 'rm -rf "$work"' EXIT
sed -nE 's/^[[:space:]]*(\(.*\))[[:space:]]+same[[:space:]]*$/\1/p' "$testps" \
    > "$work/cases"
ncases=$(grep -c . "$work/cases" || true)
if [ "$ncases" -eq 0 ]; then
    echo "FAIL: no cases found in $(basename "$testps") -- a case is a string"
    echo "      of PostScript followed by 'same', and there are none, so this"
    echo "      check would report full coverage of an empty test"
    exit 1
fi

fail=0
for op in $fused; do
    case " $exempt " in
        *" $op "*) continue ;;
    esac
    # opXXX -> XXX; the shortcut names carry an op prefix to avoid C
    # keywords (opif, opdef), the PostScript operator does not
    name=${op#op}
    if ! grep -qE "(^|[ (])$name([ )]|\$)" "$work/cases"; then
        echo "FAIL: $name is fused in xpost_interpreter.c but has no case in"
        echo "      $(basename "$testps") -- add equivalence cases for it"
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: a fused operator lacks equivalence coverage"
    exit 1
fi
echo "SUCCESS ($ncases equivalence cases)"
exit 0
