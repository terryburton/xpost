#!/bin/sh
# Meson test wrapper: assert that every guard which derives a path from an
# argument refuses a path that is not what it was promised.
#
# The guards take a source root and read data/ or tests/ beneath it. Handed
# the wrong root, they do not fail: XPOST_DATA_DIR is only the first
# candidate the interpreter tries, and it falls through to the shared
# library's directory and to relative paths, so the guard finds the working
# tree anyway and reports a true result about a tree nobody asked about.
# Every number in that report is real, which is what makes it dangerous.
#
# tests/guard-paths.sh carries the refusal. This holds each guard to
# calling it, so a new guard cannot quietly skip the check, and it also
# runs each one against a wrong path to confirm the refusal actually fires
# rather than merely being present in the text.
#
#   $1  path to the source tree root
set -u
src=${1:?usage: check-guard-paths.sh <srcroot>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# a directory that is emphatically not a source root, and looks like the
# mistake actually made: the data directory itself
decoy="$work/decoy"
mkdir -p "$decoy"
: > "$decoy/init.ps"

fail=0
checked=0

for g in "$src"/tests/check-*.sh; do
    base=$(basename "$g")
    [ "$base" = "check-guard-paths.sh" ] && continue
    # only the guards that derive a path from a source-root argument
    grep -qE '\$\{?src\}?/(data|tests)' "$g" || continue
    checked=$((checked + 1))

    if ! grep -q 'guard_require_srcroot' "$g"; then
        echo "FAIL: $base derives a path from its argument without validating it"
        fail=1
        continue
    fi

    # and the refusal must fire: hand it the decoy in the source-root
    # position. Which position that is differs, so try the shapes in use.
    if sh "$g" "$decoy" >/dev/null 2>&1 \
       || sh "$g" "$src/build/src/bin/xpost" "$decoy" >/dev/null 2>&1 \
       || sh "$g" "$src/build/src/bin/xpost" "$decoy" "$src/tests/wrapped_bind.golden" >/dev/null 2>&1
    then
        echo "FAIL: $base accepted a path that is not a source root"
        fail=1
    fi
done

if [ "$checked" -lt 5 ]; then
    echo "FAILURES: found only $checked path-deriving guards; the check is unusable"
    exit 1
fi

[ "$fail" = 0 ] || exit 1
echo "SUCCESS ($checked path-deriving guards refuse a path that is not a source root)"
exit 0
