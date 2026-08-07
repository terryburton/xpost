#!/bin/sh
# Meson test wrapper: the interpreter must start and run a trivial job
# without emitting any diagnostic of its own. A self-check that reports
# on every ordinary run is worse than no self-check -- it trains the
# reader to ignore the channel real errors arrive on -- so a clean run
# must be silent on stderr as well as stdout.
#
#   $1  path to the built xpost binary
set -u
xpost=$1
. "$(dirname "$0")/verdict.sh"

if "$xpost" -h 2>/dev/null | grep -q -- '--no-sandbox'; then
    ns='--no-sandbox'
else
    ns=''
fi

work=$(mktemp -d)
printf '1 2 add pop\n' > "$work/t.ps"

fail=0
for mode in "" "--no-graphics"; do
    err=$("$xpost" -q $ns $mode -d null "$work/t.ps" </dev/null 2>&1 >/dev/null)
    status=$?
    verdict_run "$status" "$err" "a quiet run${mode:+ ($mode)}" || exit 1
    if [ -n "$err" ]; then
        echo "FAIL: a quiet run wrote to stderr${mode:+ ($mode)}:"
        printf '%s\n' "$err" | head -5
        fail=1
    fi
done

rm -rf "$work"
if [ "$fail" -ne 0 ]; then
    echo "FAILURES: startup is not silent"
    exit 1
fi
echo SUCCESS
exit 0
