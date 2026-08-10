#!/bin/sh
# Meson wrapper for the encapsulated corpus: real illustrations written
# to be placed in another document, rendered through this interpreter
# and held to what they draw.
#
# This one is not a differential run and does not ask another engine
# anything. What it holds is what a program of this shape must do here
# whatever any other implementation does with it:
#
#   the run finishes, exits zero and reports no PostScript error;
#   it draws exactly the pages the corpus declares for the program;
#   and every page it draws carries ink.
#
# Ink rather than a hash of the bytes, because the programs are fetched
# from their own source and that source is free to revise them: a hash
# holds this tree to a file nobody here controls, and the first time
# upstream redraws a logo the gate goes red over a change that is not
# this tree's. What does not move is that a drawing draws something.
# "Carries ink" is read against a page this same interpreter and device
# produce for a program that paints nothing, so the comparison is to
# this build's own idea of a blank page rather than to a colour written
# down here.
#
# Four of the twelve carry no showpage at all -- the register beside
# them names which, and why -- so the only page they produce is the one
# the interpreter ends for a job that painted and stopped without
# asking. For those four the ink check above is a check of that
# behaviour and of nothing else: without it they render blank, and a
# corpus that let them render blank would report twelve programs
# reached and say nothing about the four.
#
#   - SKIP (exit 77) when the corpus has not been fetched. The programs
#     belong to their own source and are not committed, so this is never
#     a build-time dependency -- run tests/corpus/fetch.sh to make this
#     test do its work.
#   - FAIL (exit 1) on any of the above, and on a corpus whose registers
#     and directory disagree in either direction.
#   - PASS (exit 0) otherwise.
#
#   $1  path to the built xpost binary
set -u
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$here/../verdict.sh"
xpost=${1:?usage: run-eps-test.sh <path to xpost>}
d="$here/eps"

# The corpus is the files, so its absence is what a skip is for. The
# registers are ours and are committed, so their absence is a broken
# tree and not a corpus waiting to be fetched.
have=0
for p in "$d"/*.eps; do
    [ -f "$p" ] && { have=1; break; }
done
if [ "$have" = 0 ]; then
    echo "corpus: the encapsulated corpus is not present -- run"
    echo "        tests/corpus/fetch.sh eps, then re-run. Skipping."
    exit 77
fi

fail=0
for reg in pages unasked; do
    if [ ! -s "$d/$reg" ]; then
        echo "FAILURES: tests/corpus/eps/$reg is missing or empty, so the"
        echo "      corpus holds its programs to nothing"
        exit 1
    fi
done

work=$(mktemp -d) || {
    echo "FAILURES: could not make a scratch directory (is TMPDIR writable?)"
    exit 1; }
trap 'rm -rf "$work"' EXIT

# A budget separates a program that is slow from one that will never
# finish. It is spent where there is one to spend: a platform without
# the command runs without it rather than reporting every program as a
# failure to start.
if command -v timeout >/dev/null 2>&1; then
    budget='timeout 600'
else
    budget=''
fi

# A page with nothing on it, made by the interpreter under test on the
# device under test. Every page below is ink or is this.
printf '%%!PS\nshowpage\n' > "$work/blank.ps"
out=$("$xpost" -q -d ppm -o "$work/blank_%d.ppm" "$work/blank.ps" </dev/null 2>&1)
st=$?
verdict_run "$st" "$out" "the blank page the ink check reads against" || exit 1
if [ ! -s "$work/blank_1.ppm" ]; then
    echo "FAILURES: the blank page the ink check reads against was not"
    echo "      produced, so every page below would read as ink"
    exit 1
fi

# What a register says about one program, taken from the first line that
# names it and with a comment tail counting for nothing.
declared_pages() {   # basename
    awk -v b="$1" '{ sub(/#.*/, "") } $1 == b { print $2; exit }' "$d/pages"
}
named_in() {         # register basename
    awk -v b="$2" '{ sub(/#.*/, "") } $1 == b { found = 1 }
                    END { exit !found }' "$d/$1"
}

# Whether a program asks for its page. The claim the register makes is
# about the text of the file, so the text is what answers it: comment
# tails are dropped and the remainder is searched for the operator's
# name standing alone. What survives a comment tail is what the
# interpreter would execute.
asks_for_page() {    # path
    sed 's/%.*//' "$1" | grep -qw showpage
}

progs=0
pages=0
inked=0
for p in "$d"/*.eps; do
    [ -f "$p" ] || continue
    b=$(basename "$p" .eps)
    progs=$((progs + 1))

    want=$(declared_pages "$b")
    case ${want:-} in
        ''|*[!0-9]*)
            echo "FAIL: $b has no declared page count, so a run of it could"
            echo "      draw any number of pages and report the same success"
            fail=1
            continue ;;
    esac
    pages=$((pages + want))

    # The register and the file, held to each other both ways.
    if named_in unasked "$b"; then
        if asks_for_page "$p"; then
            echo "FAIL: $b is named as asking for no page and asks for one;"
            echo "      the entry's reason has lapsed"
            fail=1
        fi
    elif ! asks_for_page "$p"; then
        echo "FAIL: $b asks for no page and is not named in the unasked"
        echo "      register, so what ends its page goes unrecorded"
        fail=1
    fi

    rm -f "$work"/pg_*.ppm
    # shellcheck disable=SC2086
    out=$($budget "$xpost" -q -d ppm -o "$work/pg_%d.ppm" "$p" </dev/null 2>&1)
    st=$?
    if [ "$st" = 124 ]; then
        echo "FAIL: $b did not finish inside its budget"
        fail=1
        continue
    fi
    verdict_run "$st" "$out" "$b" || { fail=1; continue; }
    # A PostScript error is reported and the run still ends cleanly, so
    # the status alone would pass a program that failed on its first
    # operator and drew nothing after it.
    if printf '%s\n' "$out" | grep -q 'Error:'; then
        echo "FAIL: $b reported a PostScript error:"
        printf '%s\n' "$out" | grep 'Error:' | sed 's/^/      /'
        fail=1
        continue
    fi

    got=$(ls "$work"/pg_*.ppm 2>/dev/null | wc -l)
    if [ "$got" != "$want" ]; then
        echo "FAIL: $b drew $got page(s) and the corpus declares $want"
        fail=1
        continue
    fi

    n=1
    while [ "$n" -le "$got" ]; do
        if cmp -s "$work/pg_$n.ppm" "$work/blank_1.ppm"; then
            echo "FAIL: $b page $n is the blank page, and a drawing draws"
            echo "      something"
            fail=1
        else
            inked=$((inked + 1))
        fi
        n=$((n + 1))
    done
done

# The other direction: a register naming a program the corpus does not
# hold is a register describing something that is not there, which goes
# on being read as a description of what is.
while read -r name rest; do
    case $name in ''|'#'*) continue ;; esac
    if [ ! -f "$d/$name.eps" ]; then
        echo "FAIL: the pages register names $name and the corpus holds no"
        echo "      such program"
        fail=1
    fi
done < "$d/pages"

nunasked=0
while read -r name rest; do
    case $name in ''|'#'*) continue ;; esac
    nunasked=$((nunasked + 1))
    if [ ! -f "$d/$name.eps" ]; then
        echo "FAIL: the unasked register names $name and the corpus holds no"
        echo "      such program"
        fail=1
    fi
done < "$d/unasked"

# A corpus none of whose programs leaves its page to the job end no
# longer covers the thing it was assembled to cover, and would go on
# reporting a full run of programs that all ask for their own pages.
if [ "$nunasked" = 0 ]; then
    echo "FAIL: no program of this corpus leaves its page for the job end,"
    echo "      so the corpus no longer reaches what it was assembled for"
    fail=1
fi

# and a run that measured nothing says nothing, whatever it exits with.
if [ "$progs" = 0 ] || [ "$inked" = 0 ]; then
    echo "FAILURES: the run reached $progs program(s) and found ink on"
    echo "      $inked page(s), so it measured nothing"
    exit 1
fi

echo "eps: $progs programs, $pages pages declared, $inked drawn with ink,"
echo "     $nunasked of them left for the job to end"
[ "$fail" -eq 0 ] || exit 1
echo SUCCESS
exit 0
