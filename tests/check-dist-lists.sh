#!/bin/sh
# Guard the distribution lists: what `make dist` packs must be what the
# tree holds. A file the build needs and the lists do not name produces a
# release tarball that does not compile, and a line naming a file that
# has gone produces one that does not pack at all -- both invisible to
# every in-tree build and every CI job, which read the working copy.
#
# The check used to look at one directory through one list, src/lib
# against src/lib/Makefile.mk, in one direction. Everything else went
# unheld, and the tests directory was in no list at all: the guards, the
# helper they share and the registers they hold the tree to were absent
# from every tarball, so a release shipped a tree in which not one
# structural invariant could be checked, and nothing anywhere said so.
#
# Each directory below is held to its list both ways.
#
# Usage: check-dist-lists.sh <source tree root>

set -eu
src=${1:?usage: check-dist-lists.sh <source tree root>}
. "$(dirname "$0")/guard-paths.sh"
guard_require_srcroot "$src"
guard_workdir
trap 'rm -rf "$work"' EXIT
guard_mirror_tree "$src"
tree=$mirror

fail=0

# What a list holds: every "dir/file" token in it, whatever the line
# shape. Continuations, leading tabs and trailing backslashes are not
# part of a filename.
#
# A build target named in the same file -- src/lib/libxpost.la, the
# programs under src/bin -- is not a distributed file, so the comparison
# is confined to the kinds of file the directory contributes.
listed() {          # <makefile> <dir> <kinds>
    [ -f "$1" ] || return 0
    tr -d '\r' < "$1" | sed 's/#.*//' \
      | tr ' \t\\' '\n\n\n' \
      | grep -E "^$2/[^/]" | grep -E "$3" || true
}

# Compare a directory's contents with the list that is supposed to name
# them, both ways round.
hold() {            # <label> <listfile> <dir> <have-file> <kinds>
    label=$1; listfile=$2; dir=$3; have=$4; kinds=$5
    shown=${listfile#"$tree"/}
    if [ ! -f "$listfile" ]; then
        echo "FAILURES: $label has no distribution list at $shown"
        exit 1
    fi
    listed "$listfile" "$dir" "$kinds" | LC_ALL=C sort -u > "$work/listed"
    grep -E "$kinds" "$have" | LC_ALL=C sort -u > "$work/have"
    if [ ! -s "$work/have" ]; then
        echo "FAILURES: no $label files found under $src/$dir; this check"
        echo "      is reading the wrong tree"
        exit 1
    fi
    if [ ! -s "$work/listed" ]; then
        echo "FAILURES: $shown names no $dir file; the list was emptied or"
        echo "      its shape changed and this check no longer reads it"
        exit 1
    fi
    LC_ALL=C comm -23 "$work/have" "$work/listed" > "$work/unlisted"
    LC_ALL=C comm -13 "$work/have" "$work/listed" > "$work/stale"
    if [ -s "$work/unlisted" ]; then
        echo "FAIL: present in the tree, absent from $shown"
        echo "      (make dist would omit these):"
        sed 's/^/      /' "$work/unlisted"
        fail=1
    fi
    if [ -s "$work/stale" ]; then
        echo "FAIL: named by $shown, not in the tree"
        echo "      (make dist would fail on these):"
        sed 's/^/      /' "$work/stale"
        fail=1
    fi
}

# ---- the library: every source and header it is built from ----
( cd "$tree" && ls src/lib/*.c src/lib/*.h 2>/dev/null ) > "$work/lib"
hold "library source" "$tree/src/lib/Makefile.mk" src/lib "$work/lib" '\.[ch]$'

# ---- the programs: sources, and the Windows resources they need ----
( cd "$tree" && ls src/bin/*.c src/bin/*.h src/bin/*.rc src/bin/*.ico 2>/dev/null ) \
    > "$work/bin"
hold "program source" "$tree/src/bin/Makefile.mk" src/bin "$work/bin" \
    '\.([ch]|rc|ico)$'

# ---- the interpreter's PostScript: what meson installs ----
sed -n "/xpost_data_src = files(\[/,/\])/p" "$tree/data/meson.build" \
  | grep -oE "'[^']+'" | tr -d "'" | sed 's|^|data/|' > "$work/data"
hold "interpreter data" "$tree/data/Makefile.mk" data "$work/data" '\.ps$'

# ---- the test suite, guards and registers included ----
( cd "$tree" && find tests -type f -print ) > "$work/tests"
hold "test suite" "$tree/tests/Makefile.mk" tests "$work/tests" '.'

# ---- the other build system, which the tarball has to carry too ----
#
# The tests are registered in meson and CI builds with it, so a release
# that ships only the autotools half ships a tree in which nothing here
# can be run -- including the guards the lists above now distribute.
( cd "$src" && find . -name 'meson.build' -o -name 'meson_options.txt' ) \
  | sed 's|^\./||' | LC_ALL=C sort -u > "$work/meson-have"
tr -d '\r' < "$tree/Makefile.am" | sed 's/#.*//' | tr ' \t\\' '\n\n\n' \
  | grep -E '(^|/)meson(\.build|_options\.txt)$' | LC_ALL=C sort -u \
  > "$work/meson-listed"
LC_ALL=C comm -3 "$work/meson-have" "$work/meson-listed" > "$work/meson-diff"
if [ -s "$work/meson-diff" ]; then
    echo "FAIL: the meson build description and Makefile.am's list of it disagree:"
    sed 's/^\t/      only in Makefile.am: /; s/^\([^ ]\)/      not distributed: \1/' \
        "$work/meson-diff"
    fail=1
fi

# ---- and the lists are actually included by the build ----
for mk in src/lib/Makefile.mk src/bin/Makefile.mk data/Makefile.mk \
          tests/Makefile.mk; do
    if ! grep -q "^include $mk\$" "$tree/Makefile.am"; then
        echo "FAIL: Makefile.am does not include $mk, so nothing in it is"
        echo "      distributed however complete the list is"
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "FAILURES: the distribution lists and the tree disagree"
    exit 1
fi
echo SUCCESS
exit 0
