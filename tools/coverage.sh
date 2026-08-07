#!/bin/sh
# Measure how much of the C sources the test suite executes, and report it.
#
# Builds a separate instrumented tree, runs a named test profile in it, and
# writes a report: line and branch coverage per source file, the conditions
# whose refusing side nothing ever takes, and every function the suite never
# entered. The point of the last two is that a function nothing reaches, and
# a guard nothing ever makes refuse, are not "partly tested" -- they are
# untested, and untested code in this project has repeatedly turned out to
# be broken.
#
# Two things a coverage report must say and this one is built to say.
#
# Which tests ran. A selection is not "the suite": the corpus tests skip
# until the programs are fetched, so a run that names no profile measures
# the interpreter's own suite while reading as though it measured
# everything. The profile is an argument here and is printed in the report.
#
# Whether they passed. Coverage of a failed run is a measurement of what
# the sources did while getting an answer wrong, and reporting it under a
# heading about test quality is worse than not reporting it: a run with
# failures in it produces a report that looks exactly like a run without.
# So the profile's exit status is read, and a non-zero one ends this
# script with no report written at all.
#
# The report is written to standard output. To refresh the two checked-in
# baselines, one per object width:
#
#     tools/coverage.sh bcov full > doc/COVERAGE.md
#     meson setup bcovlarge -Db_coverage=true -Dlarge-object=true
#     tools/coverage.sh bcovlarge full > doc/COVERAGE-large.md
#
#   $1     build directory to use (default: bcov). A directory that is not
#          there is configured only under that default name, and only at the
#          default object width. One named for another configuration is
#          refused instead: silently given this one, it would head a
#          large-object report over small-object numbers, which is the shape
#          of mistake this whole script exists to refuse.
#   $2     test profile to measure (default: full). One of the profiles
#          tests/run-profile.sh spells: quick, full, corpus.
#   $3...  further arguments for meson test (--num-processes, -v, ...)
#
# Requires gcov (part of gcc). Nothing else.
set -u

builddir=${1:-bcov}
profile=${2:-full}
[ $# -gt 0 ] && shift
[ $# -gt 0 ] && shift

srcdir=$(cd "$(dirname "$0")/.." && pwd)

if ! command -v gcov >/dev/null 2>&1; then
    echo "coverage.sh: gcov is not on the path" >&2
    exit 1
fi

if [ ! -f "$builddir/build.ninja" ]; then
    if [ "$builddir" != bcov ]; then
        echo "coverage.sh: $builddir is not configured, and only bcov is set up" >&2
        echo "coverage.sh: here -- a directory named for another configuration" >&2
        echo "coverage.sh: would be given this one's. Configure it yourself:" >&2
        echo "coverage.sh:     meson setup $builddir -Db_coverage=true ..." >&2
        exit 1
    fi
    meson setup "$builddir" -Db_coverage=true >/dev/null || exit 1
fi

# counts from an earlier run would be added to this one
find "$builddir" -name '*.gcda' -delete 2>/dev/null

ninja -C "$builddir" >/dev/null 2>&1 || exit 1

builddir=$(cd "$builddir" && pwd)

# The run, and its verdict. run-profile.sh checks that the profile's filter
# selects what the profile names before running anything, so a selection
# that quietly shrank to nothing fails here too.
runlog=$(mktemp)
MESON_BUILD_ROOT="$builddir" "$srcdir/tests/run-profile.sh" "$profile" "$@" \
    > "$runlog" 2>&1
status=$?
if [ "$status" -ne 0 ]; then
    echo "coverage.sh: the $profile profile failed (exit $status)." >&2
    echo "coverage.sh: no report written -- coverage of a failed run says" >&2
    echo "coverage.sh: what the sources did while getting an answer wrong." >&2
    sed 's/^/    /' "$runlog" >&2
    rm -f "$runlog"
    exit 1
fi
# meson's own tally, so the report can say what passed rather than implying it
tally=$(sed -n 's/^Ok: *\([0-9]*\).*/\1/p' "$runlog" | tail -1)
selected=$(sed -n "s/^profile $profile: .* -- \([0-9]*\) of .*/\1/p" "$runlog" | tail -1)
rm -f "$runlog"
[ -n "$tally" ] || tally='?'
[ -n "$selected" ] || selected='?'

# Which personality was measured. The two object widths compile different
# halves of every alternative the sources choose between, so a report has
# to say which one it is looking at.
probe=$(mktemp)
cat > "$probe" <<'PROBEEOF'
2147483647 1 add type /integertype eq
    { (XPOSTWIDTH=wide) }{ (XPOSTWIDTH=narrow) } ifelse =
quit
PROBEEOF
width=$(XPOST_DATA_DIR="$srcdir/data" \
        "$builddir/src/bin/xpost" -q --no-sandbox -d null "$probe" \
        </dev/null 2>/dev/null \
        | grep -o 'XPOSTWIDTH=[a-z]*' | head -1 | cut -d= -f2)
rm -f "$probe"
case $width in
    wide)   widthname='large-object'; othername='small-object' ;;
    narrow) widthname='small-object'; othername='large-object' ;;
    *)      echo "coverage.sh: could not tell which object width $builddir has" >&2
            exit 1 ;;
esac

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
: > "$work/branches"

while read -r gcda; do
    primary=$(basename "$gcda" .gcda)
    case $primary in *.c) ;; *) continue ;; esac

    ( cd "$work" && gcov -f -b -n "$gcda" ) > "$work/out" 2>/dev/null || continue

    # Per-file totals come from the "File '...'" blocks. Only this build's own
    # sources count: a header's numbers differ per translation unit, and the
    # C files are what the suite is measured against.
    #
    # A block runs Lines, Branches, Taken at least once, Calls -- with the
    # middle two replaced by "No branches" where there are none -- so the
    # record is written when the last of them is in hand rather than at the
    # first line, which is where a lines-only reading stopped.
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
            lpct = b[1]; lines = b[2]
            next
        }
        /^Branches executed:/ && want { next }
        /^Taken at least once:/ && want {
            split($0, a, ":"); split(a[2], b, "% of ")
            print path "|" lpct "|" lines "|" b[1] "|" b[2] >> out
            want = 0
            next
        }
        /^No branches/ && want {
            print path "|" lpct "|" lines "|0.00|0" >> out
            want = 0
            next
        }
    ' "$work/out"

    # Which outcome of which condition, and how often the condition was
    # reached. gcov resolves the source names in a .gcda against the build
    # directory, so it is read from there; --stdout keeps the annotated
    # listing out of the tree.
    ( cd "$builddir" && gcov -b -c -t "$gcda" ) 2>/dev/null \
        | awk -v out="$work/branches" '
        /^ *-: *0:Source:/ {
            path = $0
            sub(/^.*0:Source:/, "", path)
            sub(/^\.\.\//, "", path)
            want = (path ~ /^src\/.*\.c$/)
            next
        }
        $1 == "branch" && want {
            # "taken N", or "never executed" where the block did not run
            taken = ($3 == "taken") ? $4 + 0 : 0
            print path "|" lineno "|" $2 "|" count "|" taken "|" text >> out
            next
        }
        {
            # "<count>:<line>:<source>", count "-" on a line that is not
            # code and "#####" on one that is code nothing ran
            i = index($0, ":"); if (i == 0) next
            c = substr($0, 1, i - 1)
            rest = substr($0, i + 1)
            j = index(rest, ":"); if (j == 0) next
            n = substr(rest, 1, j - 1) + 0
            if (n == 0) next
            gsub(/ /, "", c)
            if (c == "-") next
            count = (c ~ /^[0-9]/) ? c + 0 : 0
            lineno = n
            text = substr(rest, j + 1)
            sub(/^[ \t]+/, "", text)
            sub(/[ \t]+$/, "", text)
        }
    '

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

# One condition is compiled into as many objects as include it, so the
# outcome is keyed by where it is written and the counts are added up.
# What is wanted is the outcome nothing ever produces at a condition the
# suite does reach: a condition never reached at all is already in the
# line figures and in the list of functions nothing enters.
: > "$work/never"
awk -F'|' -v never="$work/never" -v totals="$work/branchtot" '
    { k = $1 "|" $2 "|" $3
      if (!(k in ev)) { ev[k] = 0; taken[k] = 0; outcomes[$1 "|" $2]++ }
      ev[k] += $4; taken[k] += $5
      if ($4 > reached[$1 "|" $2]) reached[$1 "|" $2] = $4
      # the condition itself is the last field and C writes "||" in it, so
      # it is taken as the remainder rather than as one split field
      t = $0; sub(/^([^|]*\|){5}/, "", t)
      src[$1 "|" $2] = t }
    END {
        nout = 0; nnever = 0
        for (k in ev) {
            nout++
            if (taken[k] == 0) {
                split(k, a, "|")
                nnever++
                blind[a[1] "|" a[2]]++
            }
        }
        for (line in outcomes)
            if (blind[line] > 0 && reached[line] > 0)
                printf "%d|%s|%d|%d|%s\n",
                    reached[line], line, blind[line], outcomes[line], src[line] > never
        printf "%d|%d\n", nout, nnever > totals
    }
' "$work/branches"
[ -s "$work/branchtot" ] || printf '0|0\n' > "$work/branchtot"
nbranch=$(cut -d'|' -f1 "$work/branchtot")
nnever=$(cut -d'|' -f2 "$work/branchtot")
nguard=$(wc -l < "$work/never" | tr -d ' ')

case $width in
    wide) record='doc/COVERAGE-large.md'; setup='meson setup bcovlarge -Db_coverage=true -Dlarge-object=true'
          bdir='bcovlarge' ;;
    *)    record='doc/COVERAGE.md'; setup='meson setup bcov -Db_coverage=true'
          bdir='bcov' ;;
esac

printf '# Test coverage (%s build)\n\n' "$widthname"
printf 'How much of the C sources the test suite executes, and what it never\n'
printf 'makes them do. Regenerate with\n\n'
printf '    %s\n' "$setup"
printf '    tools/coverage.sh %s %s > %s\n\n' "$bdir" "$profile" "$record"
printf '(needs gcov; takes a few minutes, since it builds an instrumented tree\n'
printf 'and runs the profile in it. The setup line is not optional: only a\n'
printf 'default `bcov` is configured on the caller'"'"'s behalf, and a directory\n'
printf 'named for some other configuration is refused rather than quietly given\n'
printf 'this one.)\n\n'

printf '## What was measured\n\n'
printf 'The **%s** profile: %s of the tests defined in that build, all of\n' "$profile" "$selected"
printf 'which passed (%s ok, 0 failed). A coverage report over a run with\n' "$tally"
printf 'failures in it is a measurement of what the sources did while getting an\n'
printf 'answer wrong, and it looks exactly like a report over a run without, so\n'
printf 'the generator reads the exit status and writes nothing at all when the\n'
printf 'profile fails.\n\n'
printf 'The profile is named rather than left to default. `meson test` with no\n'
printf 'selection takes in the corpus tests, which render real programs fetched\n'
printf 'into `tests/corpus` and skip where they have not been fetched: the\n'
printf 'numbers then come from the interpreter suite alone while reading as\n'
printf 'though everything had run. The `full` profile is every test but the\n'
printf 'corpus, so what is excluded is excluded on purpose and says so here.\n\n'
printf 'These numbers are one platform and one object width: this run measured\n'
printf 'the **%s** build. Code chosen at build time for anything else --\n' "$widthname"
printf 'the Windows halves of the compatibility layer, the portable path\n'
printf 'confinement used where the kernel has no openat2, and the %s\n' "$othername"
printf 'half of every WANT_LARGE_OBJECT alternative -- cannot run here and reads\n'
printf 'as uncovered whatever the other CI lanes do with it.\n\n'

printf '## The two numbers\n\n'
awk -F'|' -v nbranch="$nbranch" -v nnever="$nnever" '
    { lpct[$1] = $2; lines[$1] = $3 }
    END {
        tot = 0; cov = 0; n = 0
        for (f in lines) { tot += lines[f]; cov += lines[f] * lpct[f] / 100; n++ }
        if (tot > 0)
            printf "**%.1f%% of %d lines** and **%.1f%% of %d branch outcomes**, across\n%d files. %d outcomes are never taken.\n\n",
                cov * 100 / tot, tot, (nbranch - nnever) * 100 / nbranch, nbranch, n, nnever
    }
' "$work/files.u"
printf 'Branch coverage is the harder of the two and the one worth reading. A\n'
printf 'line is covered when control reaches it; an outcome is covered when the\n'
printf 'condition actually comes out that way. Every guard in this tree is a\n'
printf 'condition whose refusing outcome is the whole point of it, and a line\n'
printf 'figure counts such a guard as tested the first time it lets something\n'
printf 'through.\n\n'

printf '## Coverage is not detection\n\n'
printf 'A mutation study over the VM core at this line coverage measured **35%%\n'
printf 'fault detection**: of 219 compiling mutants, 142 survived the suite --\n'
printf 'on lines gcov records as executed. Executing a line and pinning down\n'
printf 'what it does are different measurements, and only the second is a\n'
printf 'quality figure. Read what follows as a floor: it says where there is\n'
printf 'nothing to argue about, not that the rest is settled.\n\n'

printf '## Where the gaps matter most\n\n'
printf 'A ranking by consequence rather than by line count. It is a reading of\n'
printf 'the tables below and is revised with them.\n\n'
printf '**Guards nothing has ever made refuse.** The first table is the one to\n'
printf 'act on: conditions the suite evaluates by the hundred million whose\n'
printf 'refusing side it never once produces. Among them the write bound in\n'
printf '`xpost_memory_put`, the entity-number ceiling in\n'
printf '`xpost_memory_table_alloc`, the free-list validation in `xpost_free.c`\n'
printf 'that discards a corrupt list rather than hand out an entity two owners\n'
printf 'share, and the index bounds in `xpost_stack.c`. These are the last\n'
printf 'thing between a mis-sized entity and a write outside the arena, and\n'
printf 'nothing in the suite has yet made one say no. The dictionary-growth\n'
printf 'use-after-free came out of this family, which is reason enough to want\n'
printf 'the refusals driven on purpose rather than waited for. Read the table\n'
printf 'past the `CHECK_VALID_ENT` rows, which are discounted below.\n\n'
printf '**Global VM is barely exercised.** Across every collection this run\n'
printf 'performs, the global half is never the one collected: the `isglobal`\n'
printf 'arms in `xpost_garbage.c` never come out true. The relocation recheck\n'
printf 'against the global bank in the fused `def` and array fast paths in\n'
printf '`xpost_interpreter.c` is never true either, while the local-bank twin\n'
printf 'beside it fires a handful of times in twelve million. Two banks of\n'
printf 'memory, one of them tested.\n\n'
printf '**Documented options with no coverage at all.** `_xpost_main_list_add`\n'
printf 'is never entered, so `-D`/`--define` and `-I`/`--include` -- both in the\n'
printf 'usage text the binary prints -- are untested.\n\n'
printf '**Reachable from ordinary PostScript, never entered.** Among the\n'
printf 'functions listed further down: `filter_flush`, `filter_writech` and\n'
printf '`a85_writech`, the whole memory-file method set\n'
printf '(`memory_writech`/`seek`/`tell`/`flush`/`purge`), `enc_readch` and\n'
printf '`enc_unreadch`, `rsd_flush` and `rsd_unreadch`, `_filter_cons_abandon`,\n'
printf '`dct_skip_input_data` and all four JPEG error handlers,\n'
printf '`_outline_conicto` -- no TrueType quadratic is ever traced to a path --\n'
printf '`program_take`, `_strike_resample`, and `_xpost_glyph_name_to_unicode`.\n\n'
printf '**Discount as unreachable in this configuration**, so that nobody\n'
printf 'spends effort on them: the Windows halves of `xpost_compat_posix.c` and\n'
printf 'the portable path-confinement fallback, `xpost_dev_xcb.c` (it needs a\n'
printf 'display), the mmap and file-backed VM paths, `xpost_log.c`, `evalquit`,\n'
printf 'the 4 GiB growth clamp, and the `CHECK_VALID_ENT` refusals.\n\n'
printf '**Largest raw uncovered but lower consequence**, for completeness:\n'
printf '`xpost_file.c` and `xpost_op_font.c` hold the two largest blocks of\n'
printf 'unexecuted lines in the tree, and `xpost_dsc_parse.c` and\n'
printf '`xpost_font.c` the lowest branch coverage of any file not discounted\n'
printf 'above. None of the four guards memory, which is why they are last here\n'
printf 'rather than first.\n\n'

printf '## Conditions whose refusing side nothing takes\n\n'
printf '%s conditions are reached by the suite and have an outcome it never\n' "$nguard"
printf 'produces. The count is how many times the condition was evaluated, so\n'
printf 'the top of this list is code the suite leans on constantly without ever\n'
printf 'testing what it is there for. The 40 most-evaluated:\n\n'
printf '| Evaluations | Site | Never taken | Condition |\n|---:|---|---:|---|\n'
sort -rn -t'|' -k1 "$work/never" | head -40 | awk -F'|' '
    {
        text = $0; sub(/^([^|]*\|){5}/, "", text)
        if (length(text) > 62) text = substr(text, 1, 59) "..."
        gsub(/\|/, "\\|", text)
        gsub(/`/, "'"'"'", text)
        n = $1; grouped = ""
        while (length(n) > 3) {
            grouped = "," substr(n, length(n) - 2) grouped
            n = substr(n, 1, length(n) - 3)
        }
        printf "| %s | `%s:%s` | %s of %s | `%s` |\n", n grouped, $2, $3, $4, $5, text
    }'

printf '\n## By file, most uncovered lines first\n\n'
printf 'Uncovered lines, not percentage, is what picks the next thing to test: a\n'
printf 'small file at 50%% hides less than a large one at 85%%.\n\n'
printf '| File | Lines | Line %% | Branch %% | Uncovered |\n|---|---:|---:|---:|---:|\n'
awk -F'|' '{ printf "%d|%s|%s|%s|%s|%s\n", $3 - ($3 * $2 / 100) + 0.5, $1, $2, $3, $4, $5 }' "$work/files.u" \
    | sort -rn -t'|' -k1 \
    | awk -F'|' '{ printf "| `%s` | %s | %s%% | %s | %s |\n", $2, $4, $3, ($6 + 0 == 0 ? "--" : $5 "%"), $1 }'

# A function defined in a header is compiled into every object that
# includes it, and gcov counts each copy separately: the copy in an object
# that never calls it reads as zero even when another object's copy runs.
# Those are not blind spots, so name them apart.
: > "$work/inline"
while IFS='|' read -r f fn; do
    if grep -rlE "(^|[ *])$fn[[:space:]]*\\(" "$srcdir"/src/lib/*.h >/dev/null 2>&1; then
        printf '%s|%s\n' "$f" "$fn" >> "$work/inline"
    fi
done < "$work/zero.u"
sort -u "$work/inline" > "$work/inline.u" 2>/dev/null || : > "$work/inline.u"
comm -23 "$work/zero.u" "$work/inline.u" > "$work/zero.real"

printf '\n## Functions the suite never enters\n\n'
nzero=$(wc -l < "$work/zero.real" | tr -d ' ')
ninline=$(wc -l < "$work/inline.u" | tr -d ' ')
printf 'The blind spots: %s functions nothing in the suite reaches.\n\n' "$nzero"
printf '(A further %s are defined in headers, so every object that includes\n' "$ninline"
printf 'one carries its own copy and the copies that are not called read as\n'
printf 'zero. They are listed at the end rather than here.)\n\n'
awk -F'|' '
    { if ($1 != last) { if (last != "") printf "\n"; printf "**`%s`**\n\n", $1; last = $1 }
      printf "- `%s`\n", $2 }
' "$work/zero.real"

printf '\n## Header-defined functions with an uncalled copy\n\n'
printf 'Not blind spots: each is compiled into every object that includes its\n'
printf 'header, and only the copies nothing calls are counted here.\n\n'
awk -F'|' '{ printf "- `%s` (in `%s`)\n", $2, $1 }' "$work/inline.u"
