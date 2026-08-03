#!/bin/sh
# Measure how much of the C sources the test suite executes, and report it.
#
# Builds a separate instrumented tree, runs the whole suite in it, and writes
# a report: line coverage per source file, and every function the suite never
# entered. The point of the second list is that a function nothing reaches is
# not "partly tested" -- it is untested, and untested code in this project has
# repeatedly turned out to be broken.
#
# The report is written to standard output. To refresh the checked-in
# baseline:
#
#     tools/coverage.sh > doc/COVERAGE.md
#
#   $1  build directory to use (default: bcov). It is configured if absent.
#
# Requires gcov (part of gcc). Nothing else.
set -u

builddir=${1:-bcov}

if ! command -v gcov >/dev/null 2>&1; then
    echo "coverage.sh: gcov is not on the path" >&2
    exit 1
fi

if [ ! -f "$builddir/build.ninja" ]; then
    meson setup "$builddir" -Db_coverage=true >/dev/null || exit 1
fi

# counts from an earlier run would be added to this one
find "$builddir" -name '*.gcda' -delete 2>/dev/null

ninja -C "$builddir" >/dev/null 2>&1 || exit 1
meson test -C "$builddir" >/dev/null 2>&1

builddir=$(cd "$builddir" && pwd)

# gcov writes its .gcov files beside itself, so give it a directory of its own
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

find "$builddir" -name '*.gcda' | sort > "$work/gcda"
if [ ! -s "$work/gcda" ]; then
    echo "coverage.sh: the run produced no coverage data" >&2
    exit 1
fi

: > "$work/files"
: > "$work/zero"

while read -r gcda; do
    primary=$(basename "$gcda" .gcda)
    case $primary in *.c) ;; *) continue ;; esac

    ( cd "$work" && gcov -f -n "$gcda" ) > "$work/out" 2>/dev/null || continue

    # Per-file totals come from the "File '...'" blocks. Only this build's own
    # sources count: a header's numbers differ per translation unit, and the
    # C files are what the suite is measured against.
    awk -v out="$work/files" '
        /^File / {
            path = $0
            sub(/^File ./, "", path)    # the word, its space and the quote
            sub(/.$/, "", path)         # the closing quote
            sub(/^\.\.\//, "", path)    # gcov names it relative to the build
            want = (path ~ /^src\/.*\.c$/)
            next
        }
        /^Lines executed:/ && want {
            split($0, a, ":"); split(a[2], b, "% of ")
            print path "|" b[1] "|" b[2] >> out
            want = 0
        }
    ' "$work/out"

    # A function's own block precedes the file blocks, so attribute the ones
    # at zero to the object's primary source -- the first file gcov names. A
    # static inline from a header that no caller reaches lands here too.
    primary=$(grep -m1 '^File ' "$work/out" | sed "s/^File .//; s/.\$//; s|^\.\./||")
    case $primary in src/*) ;; *) continue ;; esac

    awk -v out="$work/zero" -v primary="$primary" '
        /^Function / {
            fn = $0
            sub(/^Function ./, "", fn)
            sub(/.$/, "", fn)
            infn = 1
            next
        }
        /^Lines executed:/ && infn {
            split($0, a, ":"); split(a[2], b, "%")
            if (b[1] + 0 == 0) print primary "|" fn >> out
            infn = 0
        }
    ' "$work/out"
done < "$work/gcda"

sort -u "$work/files" > "$work/files.u"
sort -u "$work/zero" > "$work/zero.u"

printf '# Test coverage\n\n'
printf 'How much of the C sources the test suite executes. Regenerate with\n'
printf '`tools/coverage.sh > doc/COVERAGE.md` (needs gcov; takes a few minutes,\n'
printf 'since it builds an instrumented tree and runs the whole suite in it).\n\n'
printf 'Coverage is a floor, not a score: a covered line is one that ran, not one\n'
printf 'whose behaviour anything asserted. Read the second table as the list of\n'
printf 'places where there is nothing to argue about.\n\n'

awk -F'|' '
    { pct[$1] = $2; lines[$1] = $3 }
    END {
        tot = 0; cov = 0; n = 0
        for (f in lines) { tot += lines[f]; cov += lines[f] * pct[f] / 100; n++ }
        if (tot > 0)
            printf "**%.1f%% of %d lines**, across %d files.\n\n", cov * 100 / tot, tot, n
    }
' "$work/files.u"

printf '## By file, most uncovered lines first\n\n'
printf 'Uncovered lines, not percentage, is what picks the next thing to test: a\n'
printf 'small file at 50%% hides less than a large one at 85%%.\n\n'
printf '| File | Covered | Lines | Uncovered |\n|---|---:|---:|---:|\n'
awk -F'|' '{ printf "%d|%s|%s|%s\n", $3 - ($3 * $2 / 100) + 0.5, $1, $2, $3 }' "$work/files.u" \
    | sort -rn -t'|' -k1 \
    | awk -F'|' '{ printf "| `%s` | %s%% | %s | %s |\n", $2, $3, $4, $1 }'

printf '\n## Functions the suite never enters\n\n'
nzero=$(wc -l < "$work/zero.u" | tr -d ' ')
printf 'The blind spots: %s functions nothing in the suite reaches.\n\n' "$nzero"
awk -F'|' '
    { if ($1 != last) { if (last != "") printf "\n"; printf "**`%s`**\n\n", $1; last = $1 }
      printf "- `%s`\n", $2 }
' "$work/zero.u"
