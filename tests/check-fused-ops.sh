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
# Usage: check-fused-ops.sh <src/lib directory> <fused_equivalence_test.ps>

set -eu

libdir=${1:?usage: check-fused-ops.sh <src/lib dir> <equivalence test>}
testps=${2:?usage: check-fused-ops.sh <src/lib dir> <equivalence test>}

# The operators the interpreter inlines: opcode_shortcuts members compared
# against the current element in the fused walker. Shortcuts used for other
# purposes (re-entering the scanner, unwinding a wrapped-operator frame) are
# not fused operator bodies and are listed here as exempt.
exempt='token wrapdone'

fused=$(grep -oE 'opcode_shortcuts\.[a-z]+' "$libdir/xpost_interpreter.c" \
        | sed 's/^opcode_shortcuts\.//' | sort -u)

# an empty list would make this check vacuously pass, so a rename of the
# shortcut structure cannot be allowed to disarm it
if [ -z "$fused" ]; then
    echo "FAIL: no fused operators found in xpost_interpreter.c --"
    echo "      the shortcut structure was renamed and this check disarmed"
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
    if ! grep -qE "[( ]$name[) ]" "$testps"; then
        echo "FAIL: $name is fused in xpost_interpreter.c but has no case in"
        echo "      $(basename "$testps") -- add equivalence cases for it"
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: a fused operator lacks equivalence coverage"
    exit 1
fi
echo "SUCCESS"
exit 0
