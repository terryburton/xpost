#!/bin/sh
# Guard the library's exported symbol set against accidental growth.
#
# A symbol becomes exported by losing its `static`, which a declaration
# interposed between the keyword and the function does silently: the
# compiler reports only "useless storage class specifier in empty
# declaration", and the linkage change is invisible to every test.
#
# tests/exported_symbols.golden lists what the library exports. A new
# entry is a deliberate act and is added to the register in the same
# commit; a symbol that disappears breaks a consumer.
#
# Names beginning with an underscore are reserved at file scope (C99
# 7.1.3), so an exported one is flagged whether or not it is in the
# register.
#
# Usage: check-exported-symbols.sh <path to libxpost.so> <golden file>

set -u
lib=${1:?usage: check-exported-symbols.sh <library> <golden>}
golden=${2:?usage: check-exported-symbols.sh <library> <golden>}

if ! command -v nm >/dev/null 2>&1; then
    echo "SKIP: nm is not available"
    exit 77
fi
if [ ! -f "$lib" ]; then
    echo "SKIP: $lib is not a shared library on this platform"
    exit 77
fi
if [ ! -s "$golden" ]; then
    echo "FAILURES: no usable register at $golden"
    exit 1
fi

work=$(mktemp -d)
nm -D --defined-only "$lib" 2>/dev/null | awk '$2 ~ /^[TDBR]$/ { print $3 }' \
    | sort -u > "$work/have"

fail=0

added=$(comm -13 "$golden" "$work/have")
removed=$(comm -23 "$golden" "$work/have")

if [ -n "$added" ]; then
    echo "FAIL: newly exported symbols not in the register:"
    printf '%s\n' "$added" | sed 's/^/      /'
    fail=1
fi
if [ -n "$removed" ]; then
    echo "FAIL: symbols in the register no longer exported:"
    printf '%s\n' "$removed" | sed 's/^/      /'
    fail=1
fi

rm -rf "$work"
if [ "$fail" -ne 0 ]; then
    echo "FAILURES: the exported symbol set changed"
    exit 1
fi
echo SUCCESS
exit 0
