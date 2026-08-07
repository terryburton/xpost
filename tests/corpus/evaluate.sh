#!/bin/sh
# Render each corpus through xpost and Ghostscript and report the
# per-page difference. A corpus whose directory is absent or empty is
# skipped, as is one whose programs need a prelude that is populated
# rather than committed and has not been, so this is never a build
# dependency. Ghostscript is used as the differential reference; read
# the difference as a lead, not a verdict (see README.md).
#
#   evaluate.sh                 evaluate every corpus present
#   evaluate.sh ghostscript     evaluate one
#   XPOST=/path/to/xpost evaluate.sh    use a specific build
#
# The corpora are meant to be evaluated at once: the build registers one
# test per corpus so that they overlap. So nothing written here may sit
# at a path a second run would arrive at as well. A run makes a working
# directory of its own under .work, each corpus takes a directory under
# that, and each program a numbered directory under that -- three levels
# because all three names repeat. Two corpora both number their programs
# from one, and a corpus evaluated twice at once is two runs of the same
# name; sharing any of it means one program's renders standing where
# another's are read, a list truncated while it is being walked, and a
# report that is neither run's.
#
set -u
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
XPOST=${XPOST:-"$root/build/src/bin/xpost"}
GS=${GS:-gs}
jobs=${CORPUS_JOBS:-$(nproc 2>/dev/null || echo 4)}

for tool in "$XPOST" "$GS"; do
    command -v "$tool" >/dev/null 2>&1 || [ -x "$tool" ] || {
        echo "evaluate: missing $tool -- skipping all" >&2; exit 0; }
done
command -v compare >/dev/null 2>&1 || {
    echo "evaluate: ImageMagick 'compare' not found -- skipping all" >&2; exit 0; }

# device and metric for one page, by corpus and file name. The Adobe
# halftone and pattern-screen pages are bilevel; everything else is
# colour.
device_for() {   # corpus base -> "ppm" | "pbm"
    case "$1/$2" in
        adobe/ht_*|adobe/bb_1[2-5]) echo pbm;;
        *) echo ppm;;
    esac
}

# One program, rendered by both engines in a directory of its own so that
# any number of these may run at once.
#   $1 corpus  $2 base name  $3 path to the program  $4 work directory
evaluate_one() {
    corpus=$1
    b=$2
    p=$3
    work=$4
    mkdir -p "$work" || return
    # Beside the report, what this program produced no page for and how
    # many pages it did get compared. The reader below holds both
    # against what the corpus declares, and it reads these rather than
    # the report, so that the wording of a line is not also a protocol.
    : > "$work.miss"
    echo 0 > "$work.cmp"
    (
        dev=$(device_for "$corpus" "$b")
        gsdev=${dev}raw
        rm -f "$work"/g_*.* "$work"/x_*.*
        # an optional compatibility prelude, prepended to both engines so
        # the input stays identical; used where a corpus assumes operators
        # outside the language the reference provides as extensions
        src="$p"
        if [ -f "$here/$corpus/prelude" ]; then
            cat "$here/$corpus/prelude" "$p" > "$work/src.ps"
            src="$work/src.ps"
        fi
        "$GS" -q -sDEVICE=$gsdev -sPAPERSIZE=letter -r72 -dNOSAFER \
              -dBATCH -dNOPAUSE -o "$work/g_%d.$dev" "$src" >/dev/null 2>&1
        # The budget separates a program that is slow from one that will
        # never finish, and it is spent on a machine this evaluator is
        # itself loading: every corpus renders its programs several at a
        # time and several corpora run at once, so a program can be
        # sharing a core with a handful of its own kind before the rest
        # of the suite is counted. The longest program here takes about
        # a minute alone, seventy-five seconds with the whole suite
        # beside it, and a hundred and sixty on a machine already busy
        # with other work -- and it has still been killed at four
        # minutes on a busier one than any of those. A gate that fails
        # for the machine's reasons rather than the renderer's teaches
        # its reader to discount it, so the budget is four times the
        # point at which that happened.
        timeout 960 "$XPOST" -d $dev -o "$work/x_%d.$dev" "$src" \
                </dev/null >"$work/xlog" 2>&1
        xstatus=$?
        xerr=$(grep -m1 -oE 'Error: [a-zA-Z.]+' "$work/xlog" | sed 's/Error: //')
        # a signal death or a timeout is a hard regression, distinct from a
        # controlled PostScript error (which just yields no page, below)
        if [ "$xstatus" -ge 128 ]; then
            echo "  $b  XPOST CRASHED (signal $((xstatus - 128)))"; exit 0
        fi
        if [ "$xstatus" = 124 ]; then
            echo "  $b  XPOST TIMED OUT"; exit 0
        fi
        ng=$(ls "$work"/g_*.$dev 2>/dev/null | wc -l)
        nx=$(ls "$work"/x_*.$dev 2>/dev/null | wc -l)
        # A program either engine drew nothing for is compared not at
        # all, so a run of them answers none of what it was asked. Record
        # the absence under the program's name for the reader to hold
        # against what the corpus declares; which engine came up empty
        # is the reason for it, and reasons live in that file.
        if [ "$nx" = 0 ] || [ "$ng" = 0 ]; then
            printf '%s\n' "$b" >> "$work.miss"
            if [ "$nx" = 0 ] && [ "$ng" = 0 ]; then
                echo "  $b  no page from either engine"
            elif [ "$nx" = 0 ]; then
                echo "  $b  XPOST FAILED${xerr:+: $xerr}"
            else
                echo "  $b  reference produced no page ($nx from xpost)"
            fi
            exit 0
        fi
        i=1
        compared=0
        while [ "$i" -le "$ng" ]; do
            gp="$work/g_$i.$dev"; xp="$work/x_$i.$dev"
            # a program that stops partway leaves the pages after it
            # unwritten, and each of those is an absence of its own:
            # keyed by its page, so the corpus declares it by the page
            [ -f "$xp" ] || { echo "  $b p$i  no xpost page"
                              printf '%s p%s\n' "$b" "$i" >> "$work.miss"
                              i=$((i+1)); continue; }
            if [ "$dev" = pbm ]; then
                convert "$gp" -resize 12.5% "$work/a.png" 2>/dev/null
                convert "$xp" -resize 12.5% "$work/b.png" 2>/dev/null
                m=$(compare -metric RMSE "$work/a.png" "$work/b.png" null: 2>&1 \
                    | grep -oE '\([0-9.]+\)' | tr -d '()')
                printf "  %-16s p%-2s  tintRMSE %s\n" "$b" "$i" "${m:-?}"
            else
                m=$(compare -metric AE -fuzz 5% "$gp" "$xp" null: 2>&1 | grep -oE '^[0-9]+')
                printf "  %-16s p%-2s  AE %s\n" "$b" "$i" "${m:-?}"
            fi
            compared=$((compared + 1))
            i=$((i+1))
        done
        printf '%s\n' "$compared" > "$work.cmp"
    ) > "$work.out"
    rm -rf "$work"
}

evaluate_corpus() {
    corpus=$1
    dir="$here/$corpus"
    set -- "$dir"/*.ps "$dir"/*.eps
    have=0
    for p in "$@"; do [ -f "$p" ] && have=1; done
    if [ "$have" = 0 ]; then
        echo "$corpus: absent -- skipped (fetch.sh $corpus)"
        return
    fi
    # A corpus whose programs assume a prelude cannot run without one:
    # with nothing prepended, every program of it fails and the run
    # compares no page at all. Where the prelude is committed that is a
    # broken tree, and the programs failing is the report of it. Where
    # the prelude is populated alongside the programs -- generated and
    # large, and so kept out of the tree as they are -- its absence
    # means only that the corpus is half fetched, which is a skip. The
    # two cases look alike from the missing file, so the corpus says
    # which it is: a committed "prelude.fetched" beside the prelude
    # declares that the prelude is populated rather than committed, and
    # says how to obtain it. A corpus without that file is one whose
    # prelude is part of the tree, and its absence is not skipped over.
    if [ -f "$dir/prelude.fetched" ] && [ ! -s "$dir/prelude" ]; then
        echo "$corpus: prelude absent -- skipped ($corpus/prelude is not committed)"
        grep -v '^[[:space:]]*#' "$dir/prelude.fetched" \
            | grep -v '^[[:space:]]*$' | sed 's/^/  /'
        return
    fi
    echo "=== $corpus"
    cwork="$work/$corpus"
    mkdir -p "$cwork" || return

    # Name the programs to render, in order, and hold out the ones the
    # corpus lists.
    n=0
    held=0
    nondet=
    : > "$cwork/list"
    for p in "$@"; do
        [ -f "$p" ] || continue
        b=$(basename "$p" | sed 's/\.[Pp][Ss]$//;s/\.[Ee][Pp][Ss]$//')
        # a corpus may list basenames (one per line) in a "heldout" file:
        # the programs it holds out of the run, each recorded there with
        # the reason it is held. The reason is the entry's whole value --
        # a name in this list is a program nothing measures again until
        # someone reads why it is there -- so the file carries it and
        # this only reads the names
        if [ -f "$dir/heldout" ] && grep -qxF "$b" "$dir/heldout"; then
            echo "  $b  held out (see $corpus/heldout)"
            held=$((held + 1))
            continue
        fi
        # a corpus may also list basenames in a "nondeterministic" file:
        # programs whose own output differs between two runs of the same
        # build, so a difference against anything says nothing about the
        # renderer. They are evaluated and labelled by default, since the
        # rest of what they exercise is still worth running; SKIP_NONDET=1
        # holds them out for a comparison that needs every difference to
        # mean something.
        if [ -f "$dir/nondeterministic" ] && grep -qxF "$b" "$dir/nondeterministic"; then
            if [ "${SKIP_NONDET:-0}" != 0 ]; then
                echo "  $b  held out (see $corpus/nondeterministic)"
                held=$((held + 1))
                continue
            fi
            nondet="$nondet $b"
        fi
        n=$((n + 1))
        printf '%s\n%s\n%s\n%s\n' "$corpus" "$b" "$p" "$cwork/$n" >> "$cwork/list"
    done
    [ "$n" = 0 ] && return

    # Render them concurrently -- each engine run is a separate process over
    # its own directory -- then report in the order they were named, so the
    # output does not depend on which finished first. The list is what said
    # which programs those were, so walking it again is what reports them,
    # and a program whose report is not there is named as one rather than
    # passed over: a run that evaluates a fraction of a corpus and says
    # nothing about the rest agrees with whatever the rest would have said.
    xargs -P "$jobs" -n4 "$0" --one < "$cwork/list" >/dev/null 2>&1
    seen=0
    pages=0
    : > "$cwork/missing"
    : > "$cwork/ran"
    while read -r c && read -r b && read -r p && read -r d; do
        if [ -s "$d.out" ]; then
            cat "$d.out"
            [ -f "$d.miss" ] && cat "$d.miss" >> "$cwork/missing"
            [ -s "$d.cmp" ] && pages=$((pages + $(cat "$d.cmp")))
            printf '%s\n' "$b" >> "$cwork/ran"
            # a program that differs from itself between two runs differs
            # from anything, so say so beside its numbers rather than
            # leaving them to be read as the renderer's doing
            case " $nondet " in
                *" $b "*) echo "  $b  nondeterministic: its own two runs differ (see $corpus/nondeterministic)";;
            esac
            seen=$((seen + 1))
        else
            echo "  $b  NOT EVALUATED (no report)"
        fi
    done < "$cwork/list"

    # What produced no page, against what the corpus says produces none.
    # A program neither engine drew is compared not at all, and so is a
    # page of one that stopped before reaching it; either way the run
    # answers nothing about it, so the corpus has to have said so first,
    # in a "nopage" file that names it and records why. An entry is a
    # basename for a whole program, or a basename and " pN" for one page
    # of it -- the keys the evaluator wrote above.
    #
    # The comparison runs both ways, because both directions are news. An
    # absence nobody declared is a run comparing less than it was asked
    # to and reporting as though it had: the count of programs evaluated
    # is honest and says nothing about whether any of them drew, so a
    # corpus in which everything failed reads exactly like one in which
    # everything worked. A declared absence that turns out to have
    # rendered is the other way round -- the entry's reason has lapsed,
    # and the line is now telling its next reader something untrue.
    : > "$cwork/declared"
    if [ -f "$dir/nopage" ]; then
        sed -e 's/#.*//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' \
            "$dir/nopage" | grep -v '^$' > "$cwork/declared"
    fi
    undeclared=0
    lapsed=0
    while read -r u; do
        grep -qxF "$u" "$cwork/declared" && continue
        echo "  $u  no page, and $corpus/nopage does not say it makes none"
        undeclared=$((undeclared + 1))
    done < "$cwork/missing"
    while read -r u; do
        # an entry for a program this run did not render -- held out, or
        # not fetched -- is not one this run can speak to either way
        grep -qxF "${u%% *}" "$cwork/ran" || continue
        grep -qxF "$u" "$cwork/missing" && continue
        echo "  $u  declared in $corpus/nopage as making no page, but it rendered"
        lapsed=$((lapsed + 1))
    done < "$cwork/declared"

    note=
    [ "$held" = 0 ] || note=", $held held out"
    [ -z "$nondet" ] || note="$note, nondeterministic:$nondet"
    # the count of programs is what was reached; the count of pages is
    # what was actually compared, which is the number a run that reached
    # everything and drew nothing cannot inflate
    note="$note, $pages pages compared"
    if [ "$seen" != "$n" ]; then
        echo "$corpus: NOT EVALUATED -- $seen of $n programs reported$note"
    elif [ "$undeclared" != 0 ] || [ "$lapsed" != 0 ]; then
        echo "$corpus: NO-PAGE SET DIFFERS -- $undeclared undeclared, $lapsed lapsed; $n programs evaluated$note"
    else
        echo "$corpus: $n programs evaluated$note"
    fi
    rm -rf "$cwork"
}

# the per-program entry point xargs re-invokes this script through. It is
# told the directory to work in, so it makes none of its own and this has
# to come before the run's directory is made.
if [ "${1:-}" = "--one" ]; then
    shift
    evaluate_one "$@"
    exit 0
fi

mkdir -p "$here/.work" 2>/dev/null
work=$(mktemp -d "$here/.work/run.XXXXXX" 2>/dev/null) || work=
if [ -z "$work" ] || [ ! -d "$work" ] || [ ! -w "$work" ]; then
    echo "evaluate: could not make a working directory under $here/.work" >&2
    exit 1
fi
# and taken away however the run ends: a signal reaches the trap below,
# which exits, which reaches the one on EXIT.
trap 'rm -rf "$work"' EXIT
trap 'exit 1' INT TERM HUP

for name in ${*:-ghostscript casselman bwipp adobe}; do
    evaluate_corpus "$name"
done
