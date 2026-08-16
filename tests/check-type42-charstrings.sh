#!/bin/sh
# A Type 42 font dictionary publishes a glyph complement, and it agrees
# with what the face can be asked to paint.
#
# PLRM Table 5.7 states CharStrings among the entries a Type 42 font
# dictionary has, with an entry whose key is .notdef. A TrueType file
# need not name its glyphs -- a version 3 post table names none of them
# -- and such a face is a Type 42 font all the same, so the dictionary
# owes the same entries.
#
# What the entries are worth is the second half, and the half a check on
# the keys alone would miss: a dictionary listing names the renderer
# cannot honour would be worse than no dictionary, because a program
# sizing a table from it, asking it whether a name is known, or building
# an encoding out of its keys would be told yes and then painted
# nothing. So membership is held against the page. Every sample name the
# dictionary publishes has to paint something other than what .notdef
# paints, and every sample name it does not publish has to paint exactly
# what .notdef paints. The comparison is of the rendered bytes, which is
# the only evidence that does not restate the dictionary the claim is
# about.
#
# Which faces are here is the host's business and not this test's. The
# faces are discovered by asking the font machinery rather than by
# naming files: any candidate that resolves to FontType 42 is driven,
# whether its file names its glyphs or not, and a host offering none
# skips and says so. A skip that read as a pass would report success
# having driven nothing, so the count of faces driven is checked and
# printed.
#
#   $1  path to the built xpost binary
#   $2  path to the PostScript probe
#   $3  the build directory, under which this makes its own scratch
set -u
xpost=${1:?usage: check-type42-charstrings.sh <xpost> <probe.ps> <builddir>}
probe=${2:?usage: check-type42-charstrings.sh <xpost> <probe.ps> <builddir>}
builddir=${3:?usage: check-type42-charstrings.sh <xpost> <probe.ps> <builddir>}
. "$(dirname "$0")/guard-paths.sh"

# What this was handed is refused rather than skipped over. A run given
# the wrong interpreter, or a probe that is not there, or somewhere to
# work that is not a directory, cannot ask its question -- and a skip
# reads as a pass, so it would report agreement having driven nothing.
# The one thing here that is genuinely a skip is a host with no face of
# the kind under test, which is the environment answering rather than
# the run being wired up wrongly.
if [ ! -f "$xpost" ] || [ ! -x "$xpost" ]; then
    echo "FAILURES: the interpreter is missing or not executable: $xpost"
    exit 1
fi
if [ ! -f "$probe" ]; then
    echo "FAILURES: the probe is not a file: $probe"
    exit 1
fi
guard_require_file "$probe" "the probe"
# named for a file the build writes, so that somewhere that merely
# exists is not taken for the build this is meant to run against
guard_require_dir "$builddir" "the build directory"
guard_require_file "$builddir/config.h" "the generated configuration header"

scratch=$builddir/type42-charstrings
mkdir -p "$scratch" || exit 1

# Families from the three platforms this runs on, since none of them can
# be assumed: a name that resolves nowhere costs one resolution and is
# passed over, and a name that resolves to another face type is not this
# test's subject. The list is names to try, not names to require.
candidates="Lato Roboto Carlito NotoMono NotoSans NotoSerif DejaVuSans
    DejaVuSerif DejaVuSansMono LiberationSans LiberationSerif FreeSans
    Arial ArialMT TimesNewRoman TimesNewRomanPSMT CourierNew Verdana
    Tahoma Georgia Calibri Cambria Consolas SegoeUI Helvetica
    HelveticaNeue Menlo Monaco Geneva AppleSDGothicNeo"

driven=0
fail=0
checked=0
ambiguous=0

for family in $candidates; do
    out="$scratch/t42-$family"
    rm -f "$out"-*.ppm "$out.txt"
    # the run must not be given a terminal to read from, and a family
    # that resolves to nothing at all leaves the run in its error
    # handler rather than at a report this can read
    if ! timeout 120 "$xpost" -d ppm -o "$out-%d.ppm" -DFONT="/$family" \
            "$probe" </dev/null >"$out.txt" 2>&1; then
        continue
    fi

    ftype=$(sed -n 's/^FONTTYPE //p' "$out.txt" | head -1)
    [ "$ftype" = "42" ] || continue

    driven=$((driven + 1))

    csknown=$(sed -n 's/^CSKNOWN //p' "$out.txt" | head -1)
    if [ "$csknown" != "YES" ]; then
        echo "FAIL: $family is a Type 42 font with no /CharStrings"
        fail=1
        continue
    fi
    checked=$((checked + 1))

    if [ "$(sed -n 's/^NOTDEF //p' "$out.txt" | head -1)" != "YES" ]; then
        echo "FAIL: $family publishes /CharStrings with no .notdef entry"
        fail=1
    fi

    # A face named only through the standard names reaches the ones that
    # sit at their Latin-1 position, which is fewer than the font's own
    # Encoding holds. The shortfall is said out loud rather than held
    # against the run: the names it covers are the ones the face cannot
    # be asked for at all, so the dictionary leaving them out is what
    # keeps it honest, and a run that hid the number would be hiding how
    # much of the encoding a program can re-encode from.
    encmissing=$(sed -n 's/^ENCMISSING //p' "$out.txt" | head -1)
    if [ -n "$encmissing" ] && [ "$encmissing" -gt 0 ]; then
        echo "note: $family publishes no glyph for $encmissing of the names" \
             "its own Encoding holds"
    fi

    blank="$out-1.ppm"
    ref="$out-2.ppm"
    if [ ! -f "$blank" ] || [ ! -f "$ref" ]; then
        echo "FAIL: $family painted no reference page"
        fail=1
        continue
    fi
    # A face whose .notdef carries no ink paints one page for a glyph it
    # does not have and for one it has that is blank, so those two
    # cannot be told apart by the page alone. The pairs below say when
    # they have reached that case rather than guessing at it.
    if cmp -s "$blank" "$ref"; then
        notdefblank=yes
        echo "note: $family has a .notdef that paints nothing"
    else
        notdefblank=no
    fi

    # pages 3 onward are the sample in the order the probe reported it
    page=2
    while read -r name verdict; do
        page=$((page + 1))
        pf="$out-$page.ppm"
        if [ ! -f "$pf" ]; then
            echo "FAIL: $family painted no page for /$name"
            fail=1
            continue
        fi
        if cmp -s "$ref" "$pf"; then
            same=yes
        else
            same=no
        fi
        case "$verdict:$same" in
        IN:yes)
            echo "FAIL: $family publishes /$name, which paints what .notdef paints"
            fail=1 ;;
        OUT:no)
            echo "FAIL: $family does not publish /$name, which paints a glyph anyway"
            fail=1 ;;
        esac
        checked=$((checked + 1))
    done <<EOF
$(sed -n 's/^NAME //p' "$out.txt")
EOF

    # The pairs. The second name of a pair writes the character as its
    # code point, which reaches the glyph through the character map
    # whatever the dictionary holds, so its page says whether the face
    # has the glyph at all. That is what each pair is judged against,
    # rather than against a count of how many a host's faces happen to
    # carry.
    while read -r name verdict uniform; do
        page=$((page + 1))
        pf="$out-$page.ppm"
        page=$((page + 1))
        uf="$out-$page.ppm"
        if [ ! -f "$pf" ] || [ ! -f "$uf" ]; then
            echo "FAIL: $family painted no page for /$name or /$uniform"
            fail=1
            continue
        fi
        if cmp -s "$ref" "$uf"; then
            # Nothing distinguishes absent from present-and-blank on a
            # face whose .notdef paints nothing, so such a pair is
            # counted as unasked rather than judged either way. Every
            # character here carries ink in a face that has it, so this
            # is a bound on what the run can see and not a hole a face
            # could hide a wrong answer in.
            if [ "$notdefblank" = yes ]; then
                ambiguous=$((ambiguous + 1))
                continue
            fi
            # the face has no glyph for this character
            if [ "$verdict" = IN ]; then
                echo "FAIL: $family publishes /$name, for which it has no glyph"
                fail=1
            elif ! cmp -s "$ref" "$pf"; then
                echo "FAIL: $family paints a glyph for /$name, which it does not have"
                fail=1
            fi
        else
            # the face has the glyph, so the name owes it
            if [ "$verdict" != IN ]; then
                echo "FAIL: $family has the glyph for /$name but does not publish it"
                fail=1
            elif ! cmp -s "$pf" "$uf"; then
                echo "FAIL: $family paints /$name and /$uniform differently," \
                     "so the name is bound to the wrong character"
                fail=1
            fi
        fi
        checked=$((checked + 1))
    done <<EOF
$(sed -n 's/^PAIR //p' "$out.txt")
EOF
done

if [ "$driven" -eq 0 ]; then
    echo "SKIP: this host resolves no candidate family to a Type 42 face"
    exit 77
fi

[ "$fail" -eq 0 ] || exit 1
if [ "$ambiguous" -gt 0 ]; then
    echo "note: $ambiguous pair(s) went unasked, the face's .notdef" \
         "carrying no ink to tell an absent glyph from a blank one"
fi
echo "SUCCESS ($checked claims held across $driven Type 42 face(s))"
