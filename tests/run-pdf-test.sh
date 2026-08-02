#!/bin/sh
# Meson/make-check wrapper: write a PDF through the pdfwrite device and check
# it. A self-contained structural check always runs (PDF header, a fill
# operator, EOF trailer). When Ghostscript is available it is used as an
# independent oracle: the bounding box gs reads from our PDF must equal the box
# gs reads from the original drawing (a round-trip through the PDF).
#   $1  path to the built xpost binary
#   $2  path to the input drawing (a fill; e.g. bbox_test.ps)
set -u
xpost=$1
script=$2
pdf=$(mktemp)
trap 'rm -f "$pdf"' EXIT

"$xpost" -q -d pdfwrite -o "$pdf" "$script" </dev/null >/dev/null 2>&1

# structural (no external dependency; the content stream may be compressed,
# so check the object structure rather than content-stream operators)
head -c 8 "$pdf" | grep -q '%PDF-1'    || { echo "FAIL: no PDF header";   exit 1; }
grep -q '/Type[ ]*/Page' "$pdf"        || { echo "FAIL: no page object";  exit 1; }
grep -q 'stream' "$pdf"                || { echo "FAIL: no content stream"; exit 1; }
tail -c 16 "$pdf" | grep -q '%%EOF'    || { echo "FAIL: no EOF trailer";  exit 1; }
echo "PDF structure OK"

# independent oracle: round-trip the bounding box through Ghostscript
if command -v gs >/dev/null 2>&1; then
    gsbb() { gs -q -dNOSAFER -dNOPAUSE -dBATCH -sDEVICE=bbox -o /dev/null "$1" 2>&1 \
             | grep '^%%BoundingBox:'; }
    a=$(gsbb "$pdf")
    b=$(gsbb "$script")
    echo "our PDF : $a"
    echo "original: $b"
    [ -n "$a" ] && [ "$a" = "$b" ] || { echo "FAIL: gs bbox round-trip mismatch"; exit 1; }
    echo "gs round-trip OK"

    # vector text: glyphs must reach the PDF as outlines that mark at the
    # pen position. The two interpreters may resolve the font name to
    # different faces, so compare the boxes coordinate-wise with a small
    # tolerance rather than exactly.
    textps=$(mktemp)
    textpdf=$(mktemp)
    trap 'rm -f "$pdf" "$textps" "$textpdf"' EXIT
    cat > "$textps" <<'EOF'
/Helvetica findfont 20 scalefont setfont
72 100 moveto (Vector Glyphs) show
showpage
EOF
    "$xpost" -q -d pdfwrite -o "$textpdf" "$textps" </dev/null >/dev/null 2>&1
    a=$(gsbb "$textpdf")
    b=$(gsbb "$textps")
    echo "our text PDF : $a"
    echo "original text: $b"
    if [ -n "$a" ] && [ -n "$b" ]; then
        set -- $(echo "$a" | tr -d '%:' | cut -d' ' -f2-)
        a1=$1 a2=$2 a3=$3 a4=$4
        set -- $(echo "$b" | tr -d '%:' | cut -d' ' -f2-)
        ok=1
        for pair in "$a1 $1" "$a2 $2" "$a3 $3" "$a4 $4"; do
            d=$(( ${pair% *} - ${pair#* } ))
            [ "$d" -ge -6 ] && [ "$d" -le 6 ] || ok=0
        done
        [ "$ok" = 1 ] || { echo "FAIL: text bbox diverges beyond face substitution"; exit 1; }
        echo "gs text round-trip OK"
    else
        echo "FAIL: text left no marks"; exit 1
    fi

    # document metadata: a DOCINFO pdfmark must land in the trailer's
    # Info dictionary, readable by the consumer
    infops=$(mktemp)
    infopdf=$(mktemp)
    trap 'rm -f "$pdf" "$textps" "$textpdf" "$infops" "$infopdf"' EXIT
    cat > "$infops" <<'EOF'
[ /Creator (pdf-device check) /DOCINFO pdfmark
100 100 moveto 200 100 lineto 200 200 lineto closepath fill
showpage
EOF
    "$xpost" -q -d pdfwrite -o "$infopdf" "$infops" </dev/null >/dev/null 2>&1
    grep -aq '/Info 5 0 R' "$infopdf" || { echo "FAIL: no Info reference in trailer"; exit 1; }
    creator=$(gs -q -dNODISPLAY -dPDFINFO -dBATCH -dNOPAUSE "$infopdf" </dev/null 2>&1 | grep '^Creator:')
    [ "$creator" = "Creator: pdf-device check" ] || { echo "FAIL: gs reads Creator as '$creator'"; exit 1; }
    echo "gs DOCINFO round-trip OK"

    # process colour model: /ProcessColorModel /DeviceCMYK makes every mark
    # separate in CMYK -- an explicit CMYK colour passes through, a gray or
    # RGB source converts (RGB with undercolor removal), strokes emit K and
    # glyph outlines emit k. The content stream is deflate-compressed, so
    # decompress with mutool to read the colour operators when it is present.
    cmykps=$(mktemp)
    cmykpdf=$(mktemp)
    cmykdec="$cmykpdf.dec.pdf"   # mutool clean picks its output format by extension
    trap 'rm -f "$pdf" "$textps" "$textpdf" "$infops" "$infopdf" "$cmykps" "$cmykpdf" "$cmykdec"' EXIT
    cat > "$cmykps" <<'EOF'
<< /PageSize [100 100] /ProcessColorModel /DeviceCMYK >> setpagedevice
0.1 0.2 0.3 0.4 setcmykcolor newpath 10 10 moveto 30 0 rlineto 0 30 rlineto -30 0 rlineto closepath fill
1 0 0 setrgbcolor newpath 50 10 moveto 30 0 rlineto 0 30 rlineto -30 0 rlineto closepath fill
0 setgray 2 setlinewidth newpath 10 60 moveto 80 80 lineto stroke
/Courier findfont 12 scalefont setfont 10 80 moveto (K) show
showpage
EOF
    "$xpost" -q -d pdfwrite -o "$cmykpdf" "$cmykps" </dev/null >/dev/null 2>&1
    cbb=$(gsbb "$cmykpdf")
    [ -n "$cbb" ] || { echo "FAIL: process-CMYK output left no marks"; exit 1; }
    if command -v mutool >/dev/null 2>&1; then
        mutool clean -d "$cmykpdf" "$cmykdec" >/dev/null 2>&1
        grep -aq ' k$' "$cmykdec"       || { echo "FAIL: no CMYK fill (k) in process-CMYK output"; exit 1; }
        grep -aq ' K$' "$cmykdec"       || { echo "FAIL: no CMYK stroke (K) in process-CMYK output"; exit 1; }
        grep -aq '0 1 1 0 k' "$cmykdec" || { echo "FAIL: RGB red not converted with undercolor removal"; exit 1; }
        grep -aq ' rg$' "$cmykdec"      && { echo "FAIL: an RGB operator leaked into process-CMYK output"; exit 1; }
    fi
    echo "CMYK process colour model OK"

    # separation colour spaces: a [/Separation name alt tint] space set through
    # setcolorspace/setcolor paints as /CS<i> cs t scn (CS/SCN for strokes) with
    # the space preserved in the page's /ColorSpace resources (grep-able in the
    # uncompressed page object) and the tint transform embedded as a function --
    # Type 4 calculator source when the procedure stays within that operator set,
    # sampled Type 0 otherwise. A gsave/grestore round-trip re-selects the
    # separation after a process-colour interlude; registration survives an
    # intervening save/restore.
    sepps=$(mktemp)
    seppdf=$(mktemp)
    sepdec="$seppdf.dec.pdf"
    trap 'rm -f "$pdf" "$textps" "$textpdf" "$infops" "$infopdf" "$cmykps" "$cmykpdf" "$cmykdec" "$sepps" "$seppdf" "$sepdec"' EXIT
    cat > "$sepps" <<'EOF'
<< /PageSize [100 100] >> setpagedevice
[/Separation (Spot A) /DeviceCMYK {dup 0 exch dup 0.5 mul exch 0.25 mul}] setcolorspace
0.8 setcolor
newpath 10 10 moveto 20 0 rlineto 0 20 rlineto -20 0 rlineto closepath fill
2 setlinewidth newpath 10 50 moveto 60 70 lineto stroke
gsave 0 setgray newpath 70 40 moveto 10 0 rlineto 0 10 rlineto -10 0 rlineto closepath fill grestore
newpath 40 10 moveto 10 0 rlineto 0 10 rlineto -10 0 rlineto closepath fill
/half 0.5 def
[/Separation /SpotB /DeviceGray {half mul 1 exch sub}] setcolorspace
save 1.0 setcolor newpath 50 50 moveto 20 0 rlineto 0 20 rlineto -20 0 rlineto closepath fill restore
showpage
EOF
    "$xpost" -q -d pdfwrite -o "$seppdf" "$sepps" </dev/null >/dev/null 2>&1
    grep -aq '/CS0 \[/Separation /Spot#20A /DeviceCMYK 5 0 R\]' "$seppdf" || { echo "FAIL: no escaped Spot A colour space resource"; exit 1; }
    grep -aq '/CS1 \[/Separation /SpotB /DeviceGray 6 0 R\]' "$seppdf"    || { echo "FAIL: no SpotB colour space resource"; exit 1; }
    grep -aq '/FunctionType 4' "$seppdf" || { echo "FAIL: no Type 4 tint transform"; exit 1; }
    grep -aq '/FunctionType 0' "$seppdf" || { echo "FAIL: no sampled Type 0 tint transform"; exit 1; }
    if command -v mutool >/dev/null 2>&1; then
        mutool clean -d "$seppdf" "$sepdec" >/dev/null 2>&1
        grep -aq '/CS0 cs 0.8 scn' "$sepdec" || { echo "FAIL: fill not painted in the separation"; exit 1; }
        grep -aq '/CS0 CS 0.8 SCN' "$sepdec" || { echo "FAIL: stroke not painted in the separation"; exit 1; }
        grep -aq '^0 g$' "$sepdec"           || { echo "FAIL: no process-colour interlude inside gsave"; exit 1; }
        grep -aq '/CS1 cs 1 scn' "$sepdec"   || { echo "FAIL: registration did not survive restore"; exit 1; }
    fi
    echo "separation colour spaces OK"

    # independent oracle: a separating consumer images each separation as its
    # own plate, named as given
    platedir=$(mktemp -d)
    gs -q -dNOSAFER -dNOPAUSE -dBATCH -sDEVICE=tiffsep -o "$platedir/p%d.tif" "$seppdf" >/dev/null 2>&1
    if [ -f "$platedir/p1(Spot A).tif" ] && [ -f "$platedir/p1(SpotB).tif" ]; then
        echo "gs separation plates OK"
    else
        ls "$platedir"; rm -rf "$platedir"; echo "FAIL: gs did not image the separations as plates"; exit 1
    fi
    rm -rf "$platedir"

    # colour-space preservation: by default each paint reaches the content
    # stream in the space it was set in -- grey as g/G, RGB as rg, CMYK as k --
    # for fills, strokes and glyphs alike, so a press workflow receives pure-K
    # ink as pure K rather than a converted black. The content stream is
    # deflate-compressed, so decompress with mutool to read the operators.
    if command -v mutool >/dev/null 2>&1; then
        cspps=$(mktemp)
        csppdf=$(mktemp)
        cspdec="$csppdf.dec.pdf"
        trap 'rm -f "$pdf" "$textps" "$textpdf" "$infops" "$infopdf" "$cmykps" "$cmykpdf" "$cmykdec" "$sepps" "$seppdf" "$sepdec" "$cspps" "$csppdf" "$cspdec"' EXIT
        cat > "$cspps" <<'EOF'
<< /PageSize [100 100] >> setpagedevice
0.5 setgray newpath 10 10 moveto 20 0 rlineto 0 20 rlineto -20 0 rlineto closepath fill
1 0 0 setrgbcolor newpath 40 10 moveto 20 0 rlineto 0 20 rlineto -20 0 rlineto closepath fill
0 0 0 1 setcmykcolor newpath 70 10 moveto 20 0 rlineto 0 20 rlineto -20 0 rlineto closepath fill
0 setgray 2 setlinewidth newpath 10 50 moveto 60 70 lineto stroke
/Courier findfont 12 scalefont setfont
0 1 0 0 setcmykcolor 10 80 moveto (K) show
0.25 setgray 40 80 moveto (g) show
showpage
EOF
        "$xpost" -q -d pdfwrite -o "$csppdf" "$cspps" </dev/null >/dev/null 2>&1
        mutool clean -d "$csppdf" "$cspdec" >/dev/null 2>&1
        for probe in '0.5 g' '1 0 0 rg' '0 0 0 1 k' '0 G' '0 1 0 0 k' '0.25 g'; do
            grep -aq "^$probe\$" "$cspdec" || { echo "FAIL: colour-space not preserved: $probe"; exit 1; }
        done
        echo "colour-space preservation OK"
    fi
else
    echo "gs not found: skipping round-trip check"
fi
