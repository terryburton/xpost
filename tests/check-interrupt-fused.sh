#!/bin/sh
# An interrupt request must reach a procedure running in the fused element
# loop, not only one running at the interpreter loop. A tail-recursive
# procedure -- one whose last element re-invokes a procedure by name --
# runs its elements without returning to the interpreter loop, so if the
# request is read only there it spins unabortably short of killing the
# process. Start such a program, send it the interrupt, and require it to
# stop; a plain loop, read at the interpreter loop, is the control.
#
# The control also calibrates the platform: if a plain loop does not take
# the interrupt the way this test delivers it, the test cannot tell the
# defect from a platform that delivers interrupts differently, so it skips.
#   $1  path to the built xpost binary
set -u
xpost=$1

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM

# Return 0 if the program stops within a few seconds of the interrupt.
interruptible() {   # $1 program text
    printf '%s\n' "$1" > "$work/p.ps"
    "$xpost" -q -d null "$work/p.ps" </dev/null >/dev/null 2>&1 &
    ip_pid=$!
    sleep 1
    if ! kill -0 "$ip_pid" 2>/dev/null; then
        wait "$ip_pid" 2>/dev/null
        return 2                       # exited on its own -- not a spin
    fi
    kill -INT "$ip_pid" 2>/dev/null
    ip_i=0
    while [ "$ip_i" -lt 4 ]; do
        kill -0 "$ip_pid" 2>/dev/null || { wait "$ip_pid" 2>/dev/null; return 0; }
        sleep 1
        ip_i=$((ip_i + 1))
    done
    kill -KILL "$ip_pid" 2>/dev/null
    wait "$ip_pid" 2>/dev/null
    return 1
}

interruptible '{ } loop'
case $? in
    0) : ;;   # control takes the interrupt: the test can run
    *) echo "check-interrupt-fused: skipped (the interpreter-loop control did not take the interrupt here)"
       exit 77 ;;
esac

interruptible '/r {r} def r'
case $? in
    0) echo "check-interrupt-fused: ok"; exit 0 ;;
    2) echo "FAIL: the tail-recursive program did not run (test cannot exercise the fused loop)"; exit 1 ;;
    *) echo "FAIL: a tail-recursive procedure ignored the interrupt -- the fused loop does not read it"; exit 1 ;;
esac
