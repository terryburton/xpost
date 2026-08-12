#!/bin/sh
# Meson test wrapper: a page written a band at a time by a device that
# keeps its raster in a buffer of its own is the page it writes whole,
# and what it holds while it does it is the band.
#
# The devices here are the compiled writers -- PNG, PNG with an alpha
# channel, and JPEG. Their rasters are outside virtual memory, so nothing
# the interpreter reports about its own memory says anything about them,
# and their output is encoded, so a page can differ from another byte for
# byte while standing for the same picture. Three separate things are
# therefore asked, and none of them implies the others:
#
#   The bytes. The file a band loop produces is compared with the file
#   the same device produces holding the whole page. These writers are
#   deterministic -- the same rows through the same settings in the same
#   order -- so the two are the same bytes, and that is the sharpest
#   comparison available.
#
#   The pixels. The two files are decoded and their pixels compared,
#   which the PostScript side does through this interpreter's own
#   filters. A file that decoded to the same picture through different
#   bytes would pass the second and fail the first, and one that agreed
#   in length while standing for a different picture would pass the
#   first and fail the second.
#
#   The bound. A loop that held the whole page anyway writes exactly the
#   same bytes, so no comparison of pages can see it. What can see it is
#   the memory the process took, which is measured here for each route
#   at a short page and a tall one, so that what is compared is how each
#   grows with the page rather than what either costs once. The
#   whole-page route is required to grow by a row of pixels for every
#   row of page, since a measurement that saw nothing would pass
#   everything, and the banded route by a small fraction of that.
#
# The seams are what the band heights are chosen for. A PNG row is
# filtered against the row before it and a JPEG scanline goes into a unit
# of eight or sixteen rows, so a writer that lost what it held between
# one band and the next would be wrong at exactly the rows where bands
# meet. Bands that divide the page are run, bands that do not, and a band
# of one row -- at which every row of the page is a seam.
#
# And what each class says about taking its page in bands is read back,
# for the devices that say yes and for the two that say no. These classes
# are dict copies of one that says yes, so the way the rule breaks is by
# inheritance, silently, and the roster is checked rather than assumed.
#
#   $1  path to the built xpost binary
#   $2  path to band_writer_test.ps
set -u
xpost=$1
script=$2
. "$(dirname "$0")/verdict.sh"

# The runs below are started in the directory the pages are written to,
# so what they were handed has to name the same thing from there.
case $xpost in /*) ;; *) xpost=$PWD/$xpost ;; esac
case $script in /*) ;; *) script=$PWD/$script ;; esac

if "$xpost" -h 2>/dev/null | grep -q -- '--no-sandbox'; then
    ns='--no-sandbox'
else
    ns=''
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0
ran=0

note() {
    echo "FAILURES: $1"
    shift
    for n_line in "$@"; do
        echo "      $n_line"
    done
    fail=1
}

# One run of the test program: the device, and the definitions the
# program reads its page and band height out of. Sets out.
run() {  # $1 device; $2... -Dname=value
    r_dev=$1
    shift
    out=$( cd "$work" && "$xpost" -q $ns -d "$r_dev" -o /dev/null "$@" \
           "$script" </dev/null 2>&1 )
    r_st=$?
    verdict_run "$r_st" "$out" "the $r_dev run" || return 1
    verdict_ok "$out" "the $r_dev run" || return 1
    return 0
}

field() { printf '%s\n' "$1" | tr -s '-' '\n' | sed -n "s/^$2 //p"; }

# ---- what a class says about taking its page in bands ----
# A device that has not thought about it must say nothing, and these
# classes are copies of one that says yes -- so every one of them has to
# have said something, and this is where what it said is read.
decl=$work/decl.ps
cat > "$decl" <<'EOF'
(DECL ) print DEVICE /BandedPage known { (yes) }{ (no) } ifelse print
(\n) print
(MOVE ) print DEVICE /.moveband known { (yes) }{ (no) } ifelse print
(\n) print
quit
EOF

says() {  # $1 device; prints yes/no, or nothing where the device is absent
    s_out=$("$xpost" -q $ns -d "$1" -o /dev/null "$decl" </dev/null 2>&1) \
        || return 1
    printf '%s\n' "$s_out" | tr -s '-' '\n' | sed -n 's/^DECL //p' | head -1
}

for c in png:yes pngalpha:yes jpeg:yes raster:no bgr:no; do
    dev=${c%%:*}; want=${c#*:}
    got=$(says "$dev" || true)
    if [ -z "${got:-}" ]; then
        echo "SKIP the $dev device is not in this build"
        continue
    fi
    if [ "$got" != "$want" ]; then
        note "$dev says $got to taking its page in bands and this expects" \
             "$want; a device that has not thought about it must say" \
             "nothing, and every one of these is a dict copy of a class" \
             "that says yes, so silence is the one answer it cannot have"
    else
        echo "OK   $dev takes its page in bands: $got"
    fi
done

# ---- the bytes and the pixels ----
# Four pages: the five marking kinds with text, a sampled image, marks
# confined to the middle of the page so that the bands they never reach
# are passed over and the rows there go out as the ground, and marks
# over part of the page with no clearing of it -- which is the one that
# can see a run of rows coming up carrying what the run before painted,
# every other page here covering itself before it draws.
for dev in png pngalpha jpeg; do
    if [ -z "$(says "$dev" || true)" ]; then
        continue
    fi
    for tag in 1 2 3 5; do
        rm -f "$work"/*.out "$work"/*.out2
        run "$dev" -DTAG=$tag -DBAND=0 || {
            note "$dev could not write page $tag whole"
            continue
        }
        whole=$(field "$out" WHOLE | head -1)
        for band in 1 5 8 13; do
            # one of them writes a second page through the same device,
            # which is what a job's second showpage does: a page arriving
            # in bands is finished once, and the page after it begins at
            # the move onto its first run of rows.
            second=''
            [ "$band" = 8 ] && second='-DSECOND=1'
            run "$dev" -DTAG=$tag -DBAND=$band $second || {
                note "$dev could not write page $tag in bands of $band"
                continue
            }
            ran=$((ran + 1))
            set -- $(field "$out" BANDS | head -1)
            nbands=${1:-0}; passed=${2:-0}; inked=${3:-0}
            if [ "$((nbands + passed))" -lt 2 ]; then
                note "$dev wrote page $tag in $nbands band(s); a page one" \
                     "band covers has no seam on it and every arrangement" \
                     "of marks passes on one"
            fi
            if [ "$inked" != "${whole:-}" ]; then
                note "$dev took $inked row(s) of page $tag over its bands" \
                     "and ${whole:-no} row(s) holding the page whole; the" \
                     "bands between them did not receive the page"
            fi
            a=$work/$tag-0-128-96.out
            b=$work/$tag-$band-128-96.out
            if [ ! -s "$a" ] || [ ! -s "$b" ]; then
                note "$dev produced nothing for page $tag at band $band"
                continue
            fi
            if cmp -s "$a" "$b"; then
                echo "OK   $dev page $tag in bands of $band is the page" \
                     "written whole"
            else
                note "$dev page $tag in bands of $band is not the page" \
                     "written whole"
                cmp "$a" "$b" 2>&1 | sed 's/^/      /' | head -2
            fi
            if [ -n "$second" ]; then
                if [ ! -s "$b"2 ]; then
                    note "$dev wrote no second page in bands of $band; a" \
                         "device that has finished a page has to begin the" \
                         "next one"
                elif cmp -s "$b" "$b"2; then
                    echo "OK   $dev writes a second banded page beside the" \
                         "first rather than nothing or the same file twice"
                else
                    note "$dev wrote a second banded page that is not the" \
                         "first, from the same marks"
                fi
            fi
        done
    done
    rm -f "$work"/*.out "$work"/*.out2
done

# ---- and the one writer that goes over the page more than once ----
# An interlaced PNG is written in seven passes over the page, so no row
# of it can be given up before the last band is painted: such a device
# holds every row of the page and writes them all at the call that finds
# nothing held. It bounds nothing, and it still has to produce the page
# -- including over the rows no band ever reached, which carry the
# ground rather than the white a fresh raster starts on.
if [ -n "$(says png || true)" ]; then
    for tag in 1 3; do
        rm -f "$work"/*.out "$work"/*.out2
        if run png -Dpng_interlaced=1 -DTAG=$tag -DBAND=0; then
            for band in 1 8; do
                run png -Dpng_interlaced=1 -DTAG=$tag -DBAND=$band || {
                    note "png could not write interlaced page $tag in bands"
                    continue
                }
                if cmp -s "$work/$tag-0-128-96.out" \
                          "$work/$tag-$band-128-96.out"; then
                    echo "OK   png interlaced page $tag in bands of $band is" \
                         "the page written whole"
                else
                    note "png interlaced page $tag in bands of $band is not" \
                         "the page written whole"
                fi
            done
        else
            note "png could not write an interlaced page whole"
        fi
    done
    rm -f "$work"/*.out "$work"/*.out2
fi

if [ "$ran" -eq 0 ]; then
    echo "SKIP no compiled writer in this build takes its page in bands"
    exit 77
fi

# ---- the bound ----
# These devices keep their raster in a buffer of their own, outside the
# memory the interpreter reports on, so what is weighed is the process:
# the peak resident size of a run, read from the timer, at a short page
# and a tall one. The page is made wide for these runs so that a row of
# the raster is large beside what a process's resident size moves about
# by on its own. A machine without that timer is told so rather than
# passed.
if /usr/bin/time -f '%M' true >/dev/null 2>&1; then
    peak() {  # $1 device; $2 height; $3 band
        p_out=$( cd "$work" && /usr/bin/time -f '%M' \
                 "$xpost" -q $ns -d "$1" -o /dev/null \
                 -DTAG=4 -DCHECK=0 -DPW=1000 -DPH="$2" -DBAND="$3" "$script" \
                 </dev/null 2>&1 >/dev/null )
        printf '%s\n' "$p_out" | tail -1
    }
    for dev in png pngalpha jpeg; do
        [ -n "$(says "$dev" || true)" ] || continue
        rm -f "$work"/*.out "$work"/*.out2
        lo=1000; hi=4000
        wlo=$(peak "$dev" "$lo" 0);   whi=$(peak "$dev" "$hi" 0)
        blo=$(peak "$dev" "$lo" 64);  bhi=$(peak "$dev" "$hi" 64)
        case "$wlo$whi$blo$bhi" in
            *[!0-9]*|'') note "the $dev runs did not report what they took"
                         continue ;;
        esac
        rows=$((hi - lo))
        # what each route took for each further row of page, in bytes;
        # the timer reports kibibytes, and the scaling keeps a slope
        # below one byte a row a number here
        wslope=$((1000 * (whi - wlo) * 1024 / rows))
        bslope=$((1000 * (bhi - blo) * 1024 / rows))
        echo "OK   held: $dev whole ${wlo} -> ${whi} KiB over $lo -> $hi rows"
        echo "OK   held: $dev band  ${blo} -> ${bhi} KiB over $lo -> $hi rows"
        # The measurement has to be able to see a page at all. A row of
        # this raster is three or four bytes a pixel over a page a
        # thousand wide, so most of that must show up here or the
        # instrument is not reading the raster.
        if [ "$wslope" -lt $((700 * 3 * 1000)) ]; then
            note "holding the whole $dev page grew by $wslope/1000 bytes a" \
                 "row where a row of it is at least $((3 * 1000)) bytes;" \
                 "the measurement is not seeing the raster, so it cannot" \
                 "say the band bounds it"
        else
            echo "OK   holding the whole $dev page grows by $wslope/1000" \
                 "bytes a row"
        fi
        # ... and the banded route must not. Nothing it holds follows
        # the page's height: the raster is the band, and where the file
        # has reached is a number.
        if [ $((bslope * 10)) -ge "$wslope" ]; then
            note "the $dev band route grew by $bslope/1000 bytes a row" \
                 "against the whole page's $wslope/1000; what it holds is" \
                 "following the page's height, so the band is not" \
                 "bounding it"
        else
            echo "OK   the $dev band route grows by $bslope/1000 bytes a" \
                 "row, under a tenth of it"
        fi
    done
else
    echo "SKIP the peak resident size of a run cannot be read on this" \
         "machine, so what the bands hold is not weighed here"
fi

[ "$fail" -eq 0 ] || exit 1
echo "SUCCESS"
exit 0
