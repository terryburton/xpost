#!/bin/sh
# Give this checkout the corpora another checkout has already fetched.
#
#   share.sh                      take them from the default source below
#   share.sh /path/to/xpost       take them from that checkout
#   share.sh /path/to/xpost/tests/corpus   or from that corpus directly
#
# A corpus is fetched, never committed (see README.md and .gitignore),
# so a fresh git worktree has the directories and none of the programs,
# and every corpus test in it skips. That is the right answer for a
# checkout nobody has fetched into, and the wrong one for a worktree cut
# from a checkout that has: the programs are already on the disk.
#
# Fetching again is what this exists to avoid. The programs belong to
# other people and are served from their machines; asking for a second
# copy of a file already on this disk, once per worktree, is a cost
# borne by someone who gets nothing for it. So a second checkout takes
# its corpus from the first.
#
# It copies rather than links. A link would be cheaper and is the wrong
# trade: a worktree is a place work happens and gets thrown away, and a
# link points a harness at another checkout's files, where a stray write
# lands on the corpus everything else is measured against. A copy cannot
# reach back. The corpora are small enough that this is not a real cost.
#
# What is copied is what .gitignore already covers, so nothing copied
# can be committed by accident.
#
# It is not fetch.sh and does not replace it: fetch.sh is how a corpus
# first arrives, this is how a second checkout sees one that already has.
set -u

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

src=${1:-$HOME/src/xpost/tests/corpus}
# a checkout rather than a corpus is an easy thing to pass; take either
[ -d "$src/tests/corpus" ] && src=$src/tests/corpus

if [ "$src" = "$here" ]; then
    echo "share.sh: source and destination are the same corpus: $here" >&2
    exit 2
fi
if [ ! -d "$src" ]; then
    echo "share.sh: no corpus at $src" >&2
    echo "share.sh: pass the checkout to take one from, e.g." >&2
    echo "         $0 /home/tez/src/xpost" >&2
    exit 2
fi

copied=0
present=0
empty=0

for dir in "$src"/*/; do
    name=${dir%/}
    name=${name##*/}
    [ -d "$here/$name" ] || continue

    got=0
    for f in "$dir"*; do
        [ -f "$f" ] || continue
        base=${f##*/}
        # only what the corpus does not commit: a tracked file is this
        # checkout's own and must not be overwritten by another's copy
        case $base in
            *.ps|*.eps|*.ppm|*.pbm|*.pgm|*.png|prelude) ;;
            *) continue ;;
        esac
        got=$((got + 1))
        if [ -e "$here/$name/$base" ]; then
            present=$((present + 1))
            continue
        fi
        # to a temporary name first, so an interrupted copy cannot leave
        # a short file that reads as a program of the corpus -- the same
        # reason fetch.sh removes what a failed download left behind
        cp -- "$f" "$here/$name/.$base.part" || exit 1
        mv -- "$here/$name/.$base.part" "$here/$name/$base" || exit 1
        copied=$((copied + 1))
    done
    [ "$got" -eq 0 ] && { empty=$((empty + 1)); echo "share.sh: $name is unfetched at the source"; }
done

echo "share.sh: copied $copied, already present $present, unfetched at the source $empty"

# An unfetched source is not this script's failure to report as one --
# it copied everything there was. But a run that copied nothing and
# found nothing is a caller who thinks they have a corpus and does not.
if [ "$copied" -eq 0 ] && [ "$present" -eq 0 ]; then
    echo "share.sh: nothing to copy; run fetch.sh at $src first" >&2
    exit 1
fi
exit 0
