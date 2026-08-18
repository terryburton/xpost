#!/bin/sh
# Meson test wrapper: boot the interpreter twice and judge what the two
# boots' virtual memory has in common.
#
# The question is whether an image of virtual memory written at start-up
# would be a function of the language or of the run that produced it,
# and the first half of that is whether two runs produce the same memory
# at all. Two of the three pairs below are two processes, because one
# difference at issue is a difference between processes: what a process
# holds in virtual memory that names the process itself.
#
# That pair is run twice over. The plain pair is what the machine gives:
# two processes loaded wherever the system chose to put them, which on a
# system that relocates them is where the host addresses in virtual
# memory come apart. The second pair is run with relocation turned off
# where the platform offers a way -- the two processes are then loaded
# at the same address, every host address in virtual memory agrees, and
# what is required of the two images is that they match to the byte.
# That is the stronger of the two answers and the one worth having, so
# it is not left to a machine that happens to have relocation off: it is
# asked for.
#
# Where the platform offers no such way that pair is not run, and this
# says so rather than passing quietly: what is missing is the stronger
# half of the answer, and a run that gave only the weaker half should
# read as having done so.
#
# The third pair is the two boots in one process, one context after
# another. It asks the other half of the question -- whether a boot from
# a heap an earlier context has already been through arrives where a
# first boot does -- and it asks it with nothing excused: one process is
# one load of this code at one address, so every host address in the two
# images is the same address, and the two are required to agree to the
# byte on every platform. It needs no tool and is not conditional.
#
#   $1  path to the vm-image test executable
set -u
exe=${1:?usage: run-vm-image-test.sh <vm_image_test executable>}
. "$(dirname "$0")/verdict.sh"

verdict_workdir
if [ -z "$work" ] || [ ! -d "$work" ]; then
    echo "FAILURES: could not make a scratch directory (is TMPDIR writable?)"
    exit 1
fi

fail=0
pairs=0
a="$work/a.img"
b="$work/b.img"

# What both pairs end with: the two images, compared and reported under
# the name of the pair that wrote them.
judge() {               # <what to call it>
    what=$1

    # Both boots have to have produced something. A comparison of two
    # files that are not there is a comparison of nothing, and the
    # reading below would refuse them -- but say it here, where what
    # went wrong is that a boot wrote no image.
    if [ ! -s "$a" ] || [ ! -s "$b" ]; then
        echo "FAILURES: the $what pair did not both write an image of"
        echo "      virtual memory"
        return 1
    fi

    out=$("$exe" compare "$a" "$b" 2>&1)
    status=$?
    printf '%s\n' "$out" | sed "s/^/[$what] /"
    verdict_run "$status" "$out" "the $what comparison" || return 1
    return 0
}

# One pair of processes: two boots, then the comparison of what they
# wrote. The prefix is what runs each boot -- nothing, or the command
# that turns relocation off.
pair() {                # <what to call it> [prefix...]
    what=$1
    shift
    rm -f "$a" "$b"

    out=$("$@" "$exe" write "$a" 2>&1)
    verdict_run "$?" "$out" "the first boot of the $what pair" || return 1
    out=$("$@" "$exe" write "$b" 2>&1)
    verdict_run "$?" "$out" "the second boot of the $what pair" || return 1

    judge "$what"
}

# The pair in one process: both boots in the one run, which writes both
# images itself.
inprocess() {           # <what to call it>
    what=$1
    rm -f "$a" "$b"

    out=$("$exe" write2 "$a" "$b" 2>&1)
    verdict_run "$?" "$out" "the two boots of the $what pair" || return 1

    judge "$what"
}

pair "as loaded" || fail=1
pairs=$((pairs + 1))

# Relocation off. setarch is asked whether it can before it is used for
# an answer: a platform without it, or with one that refuses, leaves the
# stronger half unasked rather than failing for the want of a tool.
if command -v setarch >/dev/null 2>&1 && setarch -R true >/dev/null 2>&1; then
    pair "loaded alike" setarch -R || fail=1
    pairs=$((pairs + 1))
else
    echo "NOTE: this platform offers no way to load two processes at the"
    echo "      same address, so this run asked only what two processes"
    echo "      loaded wherever the system put them have in common, and"
    echo "      not whether they are otherwise identical to the byte"
fi

inprocess "one process" || fail=1
pairs=$((pairs + 1))

[ "$fail" = 0 ] || { echo "FAILURES: virtual memory is not the same from one boot to the next"; exit 1; }
echo "SUCCESS ($pairs pair(s) of boots compared)"
