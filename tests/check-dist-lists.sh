#!/bin/sh
# Guard the distribution lists: every source and header the library is
# built from must also be listed in the autotools file lists, which are
# what `make dist` packs. A file present in the meson build but absent
# from those lists produces a release tarball that does not compile --
# a breakage invisible to every in-tree build and every CI job that
# builds from the working copy.
#
# Usage: check-dist-lists.sh <source tree root>

set -eu
src=${1:?usage: check-dist-lists.sh <source tree root>}

lib_mk="$src/src/lib/Makefile.mk"
data_mk="$src/data/Makefile.mk"
data_meson="$src/data/meson.build"

fail=0

# every .c and .h in src/lib must appear in src/lib/Makefile.mk
for f in "$src"/src/lib/*.c "$src"/src/lib/*.h; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    if ! grep -q "src/lib/$base\([ \\]\|$\)" "$lib_mk"; then
        echo "FAIL: src/lib/$base is not in src/lib/Makefile.mk (make dist would omit it)"
        fail=1
    fi
done

# every data file meson installs must also be in data/Makefile.mk
sed -n "/xpost_data_src = files(\[/,/\])/p" "$data_meson" \
  | grep -oE "'[^']+'" | tr -d "'" | while read -r d; do
    [ -n "$d" ] || continue
    if ! grep -q "data/$d\([ \\]\|$\)" "$data_mk"; then
        echo "FAIL: data/$d is installed by meson but not in data/Makefile.mk"
        exit 1
    fi
done || fail=1

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: a built file is missing from the distribution lists"
    exit 1
fi
echo SUCCESS
exit 0
