#!/bin/sh
# Meson test wrapper: the error report the standard handler writes when
# a program asks for a binary one.
#
# The report is a binary object sequence rather than text, so it has to
# be checked on the bytes rather than from inside the interpreter: a
# four-element array under tag 250 carrying the class of error, the
# error's own name, what was executing, and a boolean. It is written
# only when the program both asks for it and has binary encoding
# enabled; with either missing the report stays human-readable.
#
# Each program catches its own error and calls the handler itself, so
# the interpreter exits normally and a crash is told apart from a
# report that merely came out wrong.
#
#   $1  path to the built xpost binary
set -u
xpost=$1
case $xpost in /*) ;; *) xpost=$PWD/$xpost ;; esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

# $1 name, $2 preamble
run_case() {
    cat > "$work/$1.ps" <<PSEOF
$2
{ 1 0 div } stopped pop
errordict /handleerror get exec
PSEOF
    "$xpost" -q --no-sandbox -d null "$work/$1.ps" </dev/null \
        >"$work/$1.out" 2>&1
    status=$?
    if [ "$status" -ne 0 ]; then
        echo "FAIL: $1 exited with status $status"
        fail=1
        return 1
    fi
    return 0
}

# asked for, and encoding enabled: a sequence, and no text report
if run_case binary '1 setobjectformat
$error /binary true put'; then
    if grep -q '%%\[ Error:' "$work/binary.out"; then
        echo "FAIL: a binary report was asked for and text was written"
        fail=1
    fi
    # tag 250 is 0xFA, matched as a byte of its own rather than as a pair
    # of hex digits that happen to meet across two other bytes
    if ! od -An -tx1 "$work/binary.out" | tr '\n' ' ' | grep -qw 'fa'; then
        echo "FAIL: no tag 250 in the binary report"
        fail=1
    fi
    for want in Error undefinedresult div; do
        if ! grep -q "$want" "$work/binary.out"; then
            echo "FAIL: the binary report does not name $want"
            fail=1
        fi
    done
fi

# asked for, but encoding disabled: the report stays text
if run_case noenc '$error /binary true put'; then
    grep -q '%%\[ Error: undefinedresult' "$work/noenc.out" || {
        echo "FAIL: with encoding disabled the report was not text"
        fail=1
    }
fi

# not asked for: the report stays text even with encoding enabled
if run_case plain '1 setobjectformat'; then
    grep -q '%%\[ Error: undefinedresult' "$work/plain.out" || {
        echo "FAIL: without being asked the report was not text"
        fail=1
    }
fi

[ "$fail" = 0 ] || { echo "FAILURES: the reports above"; exit 1; }
echo "SUCCESS"
