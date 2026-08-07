#!/bin/sh
# Render each corpus through xpost and Ghostscript and report the
# per-page difference. A corpus whose directory is absent or empty is
# skipped, so this is never a build dependency. Ghostscript is used as
# the differential reference; read the difference as a lead, not a
# verdict (see README.md).
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
        timeout 240 "$XPOST" -d $dev -o "$work/x_%d.$dev" "$src" \
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
        if [ "$ng" = 0 ]; then echo "  $b  reference produced no page"; exit 0; fi
        if [ "$nx" = 0 ]; then echo "  $b  XPOST FAILED${xerr:+: $xerr}"; exit 0; fi
        i=1
        while [ "$i" -le "$ng" ]; do
            gp="$work/g_$i.$dev"; xp="$work/x_$i.$dev"
            [ -f "$xp" ] || { echo "  $b p$i  no xpost page"; i=$((i+1)); continue; }
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
            i=$((i+1))
        done
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
    echo "=== $corpus"
    cwork="$work/$corpus"
    mkdir -p "$cwork" || return

    # Name the programs to render, in order, and hold out the ones the
    # corpus lists as too slow for the per-file timeout.
    n=0
    held=0
    nondet=
    : > "$cwork/list"
    for p in "$@"; do
        [ -f "$p" ] || continue
        b=$(basename "$p" | sed 's/\.[Pp][Ss]$//;s/\.[Ee][Pp][Ss]$//')
        # a corpus may list basenames (one per line) in a "slow" file: programs
        # that render correctly but too slowly to fit the per-file timeout, held
        # out until the underlying performance work lands
        if [ -f "$dir/slow" ] && grep -qxF "$b" "$dir/slow"; then
            echo "  $b  held out (see $corpus/slow)"
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
    while read -r c && read -r b && read -r p && read -r d; do
        if [ -s "$d.out" ]; then
            cat "$d.out"
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
    note=
    [ "$held" = 0 ] || note=", $held held out"
    [ -z "$nondet" ] || note="$note, nondeterministic:$nondet"
    if [ "$seen" = "$n" ]; then
        echo "$corpus: $n programs evaluated$note"
    else
        echo "$corpus: NOT EVALUATED -- $seen of $n programs reported$note"
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
