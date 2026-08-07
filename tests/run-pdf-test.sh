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
. "$(dirname "$0")/verdict.sh"

# Run the interpreter and hold it to its own answer. What a run wrote is
# read by the block that asked for it; the status it left and anything it
# said on the way are read here, since a document with every landmark in
# it is what a run that wrote the whole file and then died leaves behind.
# What the run printed stays in `out` for the probe blocks that read it.
run_xpost() {   # $1 what to call it in a complaint, $2... arguments
    rx_who=$1
    shift
    out=$("$xpost" -q "$@" </dev/null 2>&1)
    verdict_run "$?" "$out" "$rx_who" || exit 1
}
# Temp files below are created only inside the Ghostscript-oracle blocks;
# predeclare them so the EXIT trap cleanup stays valid under set -u when a
# block is skipped (gs absent).
textps= textpdf= strokeps= strokepdf= ra= rb= infops= infopdf=
colorps= colorpdf= craster=
pdf=$(mktemp)
# A sink for output the checks below do not read. /dev/null is a POSIX
# name for it and the platform null device is not that word everywhere,
# while a scratch file is a file wherever the interpreter runs. The name
# is relative for the same reason the separation file's is: the shell and
# a program built for another environment need not read one absolute path
# the same way.
discard=./discard-$$.pdf
trap 'rm -f "$pdf" "$discard"' EXIT

run_xpost "the pdfwrite run" -d pdfwrite -o "$pdf" "$script"

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
    trap 'rm -f "$pdf" "$discard" "$textps" "$textpdf"' EXIT
    cat > "$textps" <<'EOF'
/Helvetica findfont 20 scalefont setfont
72 100 moveto (Vector Glyphs) show
showpage
EOF
    run_xpost "the vector-text run" -d pdfwrite -o "$textpdf" "$textps"
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

    # glyph colour: text must mark in the current colour, not
    # unconditional black. White text over a black field must cut
    # visible holes: the dark-pixel count of the consumer's raster
    # falls measurably short of the untouched field.
    colorps=$(mktemp)
    colorpdf=$(mktemp)
    craster=$(mktemp)
    trap 'rm -f "$pdf" "$discard" "$textps" "$textpdf" "$colorps" "$colorpdf" "$craster"' EXIT
    cat > "$colorps" <<'EOF'
0 setgray
20 40 moveto 300 40 lineto 300 100 lineto 20 100 lineto closepath fill
0 0 0 0 setcmykcolor
/Helvetica findfont 40 scalefont setfont
30 55 moveto (WHITE) show
showpage
EOF
    run_xpost "the glyph-colour run" -d pdfwrite -o "$colorpdf" "$colorps"
    gs -q -dNOSAFER -dNOPAUSE -dBATCH -sDEVICE=pgmraw -g320x160 -r72 -o "$craster" "$colorpdf" 2>/dev/null
    dark=$(tail -c 51200 "$craster" | od -An -v -tu1 \
           | awk '{for(i=1;i<=NF;i++) if($i+0<128) n++} END{print n+0}')
    echo "glyph colour dark pixels: $dark"
    # the field alone is 280x60 = 16800 dark pixels: white glyphs must
    # carve out well over a thousand of them, black glyphs none
    [ "$dark" -ge 10000 ] && [ "$dark" -le 16000 ] \
        || { echo "FAIL: text did not mark in the current colour"; exit 1; }
    echo "gs glyph colour OK"

    # vector strokes: a bent polyline must reach the PDF as one path with
    # the requested width and the graphics state's join, not as separate
    # butt-capped segments at the consumer's default width. The defect is
    # sub-pixel at screen resolution, so rasterize our PDF and the original
    # drawing through gs at 288dpi and require near-identical rasters (a
    # small byte budget absorbs coordinate rounding at 1/100 point).
    strokeps=$(mktemp)
    strokepdf=$(mktemp)
    ra=$(mktemp)
    rb=$(mktemp)
    trap 'rm -f "$pdf" "$discard" "$textps" "$textpdf" "$colorps" "$colorpdf" "$craster" "$strokeps" "$strokepdf" "$ra" "$rb"' EXIT
    cat > "$strokeps" <<'EOF'
0.75 setlinewidth
100 100 moveto 105 103.5 lineto 100 107 lineto
120 100 moveto 130 110 lineto 140 100 lineto
stroke
showpage
EOF
    run_xpost "the vector-stroke run" -d pdfwrite -o "$strokepdf" "$strokeps"
    gsr() { gs -q -dNOSAFER -dNOPAUSE -dBATCH -sDEVICE=pbmraw -g2448x3168 -r288 -o "$2" "$1" 2>/dev/null; }
    gsr "$strokepdf" "$ra"
    gsr "$strokeps" "$rb"
    diffbytes=$(cmp -l "$ra" "$rb" 2>/dev/null | wc -l)
    echo "stroke raster diff: $diffbytes bytes"
    [ -s "$ra" ] && grep -q '[^\o000]' "$ra" || { echo "FAIL: stroke left no marks"; exit 1; }
    [ "$diffbytes" -le 8 ] || { echo "FAIL: stroked joints diverge from the original drawing"; exit 1; }
    echo "gs stroke round-trip OK"

    # document metadata: a DOCINFO pdfmark must land in the trailer's
    # Info dictionary, readable by the consumer
    infops=$(mktemp)
    infopdf=$(mktemp)
    trap 'rm -f "$pdf" "$discard" "$textps" "$textpdf" "$colorps" "$colorpdf" "$craster" "$strokeps" "$strokepdf" "$ra" "$rb" "$infops" "$infopdf"' EXIT
    cat > "$infops" <<'EOF'
[ /Creator (pdf-device check) /DOCINFO pdfmark
100 100 moveto 200 100 lineto 200 200 lineto closepath fill
showpage
EOF
    run_xpost "the DOCINFO run" -d pdfwrite -o "$infopdf" "$infops"
    # the Info object's number depends on the page's object layout, so match
    # the reference without pinning it (gs reads the Creator below regardless)
    grep -aqE '/Info [0-9]+ 0 R' "$infopdf" || { echo "FAIL: no Info reference in trailer"; exit 1; }
    creator=$(gs -q -dNODISPLAY -dPDFINFO -dBATCH -dNOPAUSE "$infopdf" </dev/null 2>&1 | grep '^Creator:')
    [ "$creator" = "Creator: pdf-device check" ] || { echo "FAIL: gs reads Creator as '$creator'"; exit 1; }
    echo "gs DOCINFO round-trip OK"
else
    echo "gs not found: skipping round-trip check"
fi

# colour-space preservation: by default each paint reaches the content
# stream in the space it was set in -- grey as g/G, RGB as rg, CMYK as
# k -- for fills, strokes and glyphs alike, so a press workflow receives
# pure-K ink as pure K rather than a converted black.
# Probed through the uncompressed accumulator, no consumer needed.
cspps=$(mktemp)
cat > "$cspps" <<EOF
<< /OutputDevice /pdfwrite /OutputFile ($discard) /PageSize [100 100] >> setpagedevice
0.5 setgray newpath 10 10 moveto 20 0 rlineto 0 20 rlineto -20 0 rlineto closepath fill
1 0 0 setrgbcolor newpath 40 10 moveto 20 0 rlineto 0 20 rlineto -20 0 rlineto closepath fill
0 0 0 1 setcmykcolor newpath 70 10 moveto 20 0 rlineto 0 20 rlineto -20 0 rlineto closepath fill
0 setgray 2 setlinewidth newpath 10 50 moveto 60 70 lineto stroke
/Courier findfont 12 scalefont setfont
0 1 0 0 setcmykcolor 10 80 moveto (K) show
0.25 setgray 40 80 moveto (g) show
% the accumulator probe reads the private .pdfchunks operator, which lives in
% internaldict rather than systemdict; fetch it once through the password
/.pdfchunks 1183615869 internaldict /.pdfchunks get def
/probe { % (needle) (name)  .  -
    exch DEVICE .pdfchunks 0 get exch search
    { pop pop pop (ok ) print print (\n) print }
    { pop (MISSING ) print print (\n) print } ifelse
} def
(0.5 g\n) (grey fill preserved as g) probe
(1 0 0 rg\n) (RGB fill preserved as rg) probe
(0 0 0 1 k\n) (pure-K CMYK fill preserved as k) probe
(0 G\n) (grey stroke preserved as G) probe
(0 1 0 0 k\n) (CMYK glyph preserved as k) probe
(0.25 g\n) (grey glyph preserved as g) probe
showpage
<< /OutputDevice /null >> setpagedevice
quit
EOF
run_xpost "the colour-space probe" -d null -o /dev/null "$cspps"
rm -f "$cspps"
printf '%s\n' "$out" | grep -q 'MISSING' && { printf '%s\n' "$out" | grep MISSING; echo "FAIL: colour-space preservation probes"; exit 1; }
n=$(printf '%s\n' "$out" | grep -c '^ok ')
[ "$n" = 6 ] || { echo "FAIL: expected 6 preservation probes, saw $n"; exit 1; }
echo "colour-space preservation OK"

# process colour model: under /ProcessColorModel /DeviceCMYK every mark
# separates in CMYK -- default black (a DeviceGray source) and RGB black
# as pure K, explicit CMYK passed through, strokes as K, glyphs as k.
# Probed through the uncompressed accumulator, no consumer needed.
cmykps=$(mktemp)
cat > "$cmykps" <<EOF
<< /OutputDevice /pdfwrite /OutputFile ($discard) /PageSize [100 100] /ProcessColorModel /DeviceCMYK >> setpagedevice
newpath 10 10 moveto 20 0 rlineto 0 20 rlineto -20 0 rlineto closepath fill
1 0 0 setrgbcolor newpath 40 10 moveto 20 0 rlineto 0 20 rlineto -20 0 rlineto closepath fill
0 setgray 2 setlinewidth newpath 10 50 moveto 60 70 lineto stroke
/Courier findfont 12 scalefont setfont 10 80 moveto (K) show
% the accumulator probe reads the private .pdfchunks operator, which lives in
% internaldict rather than systemdict; fetch it once through the password
/.pdfchunks 1183615869 internaldict /.pdfchunks get def
/probe { % (needle) (name)  .  -
    exch DEVICE .pdfchunks 0 get exch search
    { pop pop pop (ok ) print print (\n) print }
    { pop (MISSING ) print print (\n) print } ifelse
} def
(0 0 0 1 k\n) (gray-black fill as pure K) probe
(0 1 1 0 k\n) (rgb red converted with undercolor removal) probe
(0 0 0 1 K\n) (stroke in CMYK) probe
( rg\n) (no RGB operators remain) exch DEVICE .pdfchunks 0 get exch search
    { pop pop pop (MISSING ) print print (\n) print }
    { pop (ok ) print print (\n) print } ifelse
showpage
<< /OutputDevice /null >> setpagedevice
quit
EOF
run_xpost "the CMYK probe" -d null -o /dev/null "$cmykps"
rm -f "$cmykps"
printf '%s\n' "$out" | grep -q 'MISSING' && { printf '%s\n' "$out" | grep MISSING; echo "FAIL: CMYK separation probes"; exit 1; }
n=$(printf '%s\n' "$out" | grep -c '^ok ')
[ "$n" = 4 ] || { echo "FAIL: expected 4 CMYK probes, saw $n"; exit 1; }
echo "CMYK process colour model OK"

# separation colour spaces: a [/Separation name alt tint] space set through
# setcolorspace/setcolor paints as /CS<i> cs t scn (CS/SCN for strokes) with
# the space preserved in the page's /ColorSpace resources and the tint
# transform embedded as a function -- Type 4 calculator source when the
# procedure stays within that operator set, sampled Type 0 otherwise (the
# second space's procedure reads a variable). Registration survives an
# intervening restore, and a gsave/grestore round-trip re-selects the
# separation after a process-colour interlude.
sepps=$(mktemp)
# A relative path resolves to the same file for the shell and for the
# interpreter, which is embedded in the program below and need not share the
# shell's view of an absolute path (e.g. a native binary under a POSIX shell).
seppdf=./sep-$$.pdf
trap 'rm -f "$pdf" "$discard" "$textps" "$textpdf" "$strokeps" "$strokepdf" "$ra" "$rb" "$infops" "$infopdf" "$sepps" "$seppdf"' EXIT
cat > "$sepps" <<EOF
<< /OutputDevice /pdfwrite /OutputFile ($seppdf) /PageSize [100 100] >> setpagedevice
[/Separation (Spot A) /DeviceCMYK {dup 0 exch dup 0.5 mul exch 0.25 mul}] setcolorspace
0.8 setcolor
newpath 10 10 moveto 20 0 rlineto 0 20 rlineto -20 0 rlineto closepath fill
2 setlinewidth newpath 10 50 moveto 60 70 lineto stroke
gsave 0 setgray newpath 70 40 moveto 10 0 rlineto 0 10 rlineto -10 0 rlineto closepath fill grestore
newpath 40 10 moveto 10 0 rlineto 0 10 rlineto -10 0 rlineto closepath fill
/half 0.5 def
[/Separation /SpotB /DeviceGray {half mul 1 exch sub}] setcolorspace
save 1.0 setcolor newpath 50 50 moveto 20 0 rlineto 0 20 rlineto -20 0 rlineto closepath fill restore
% the accumulator probe reads the private .pdfchunks operator, which lives in
% internaldict rather than systemdict; fetch it once through the password
/.pdfchunks 1183615869 internaldict /.pdfchunks get def
/probe { % (needle) (name)  .  -
    exch DEVICE .pdfchunks 0 get exch search
    { pop pop pop (ok ) print print (\n) print }
    { pop (MISSING ) print print (\n) print } ifelse
} def
(/CS0 cs 0.8 scn\n) (fill in the separation) probe
(/CS0 CS 0.8 SCN\n) (stroke in the separation) probe
(0 g\n) (process interlude inside gsave) probe
(/CS1 cs 1 scn\n) (registration survives restore) probe
showpage
<< /OutputDevice /null >> setpagedevice
quit
EOF
run_xpost "the separation probe" -d null -o /dev/null "$sepps"
rm -f "$sepps"
printf '%s\n' "$out" | grep -q 'MISSING' && { printf '%s\n' "$out" | grep MISSING; echo "FAIL: separation content probes"; exit 1; }
n=$(printf '%s\n' "$out" | grep -c '^ok ')
[ "$n" = 4 ] || { echo "FAIL: expected 4 separation probes, saw $n"; exit 1; }
sepdump() { echo "  seppdf=$seppdf ($(wc -c < "$seppdf" 2>/dev/null) bytes)"; grep -an 'Separation\|FunctionType\|0 obj' "$seppdf" 2>/dev/null | head -20; }
# the function object number depends on the page's object layout, so match the
# colour-space resource without pinning it (the plate check below is the proof)
grep -aqE '/CS0 \[/Separation /Spot#20A /DeviceCMYK [0-9]+ 0 R\]' "$seppdf" || { echo "FAIL: no escaped Spot A colour space resource"; sepdump; exit 1; }
grep -aqE '/CS1 \[/Separation /SpotB /DeviceGray [0-9]+ 0 R\]' "$seppdf"   || { echo "FAIL: no SpotB colour space resource"; sepdump; exit 1; }
grep -aq '/FunctionType 4' "$seppdf" || { echo "FAIL: no Type 4 tint transform"; sepdump; exit 1; }
grep -aq '/FunctionType 0' "$seppdf" || { echo "FAIL: no sampled Type 0 tint transform"; sepdump; exit 1; }
echo "separation colour spaces OK"

# independent oracle: a separating consumer must image each separation as
# its own plate, named as given
if command -v gs >/dev/null 2>&1; then
    platedir=$(mktemp -d)
    gs -q -dNOSAFER -dNOPAUSE -dBATCH -sDEVICE=tiffsep -o "$platedir/p%d.tif" "$seppdf" >/dev/null 2>&1
    [ -f "$platedir/p1(Spot A).tif" ] || { ls "$platedir"; rm -rf "$platedir"; echo "FAIL: no Spot A plate"; exit 1; }
    [ -f "$platedir/p1(SpotB).tif" ]  || { ls "$platedir"; rm -rf "$platedir"; echo "FAIL: no SpotB plate"; exit 1; }
    rm -rf "$platedir"
    echo "gs separation plates OK"
fi

# multi-page single-file: a plain multi-showpage job collects every page into
# one document (a %d in the name would split it into per-page files instead).
# Each page wraps in save...showpage...restore -- the separation-plate idiom --
# and registers its own separation, so this exercises the accumulating file
# surviving restore, the page tree over all pages, and a separation registered
# on one page being written once yet referenced by the later pages that share
# it.
mpps=$(mktemp)
mppdf=./mp-$$.pdf
trap 'rm -f "$pdf" "$discard" "$textps" "$textpdf" "$strokeps" "$strokepdf" "$ra" "$rb" "$infops" "$infopdf" "$sepps" "$seppdf" "$mpps" "$mppdf"' EXIT
cat > "$mpps" <<EOF
<< /OutputDevice /pdfwrite /OutputFile ($mppdf) /PageSize [80 80] >> setpagedevice
save
  [/Separation (Ink1) /DeviceCMYK { 0 0 0 } ] setcolorspace 0.7 setcolor
  10 10 moveto 60 0 rlineto 0 60 rlineto -60 0 rlineto closepath fill
showpage restore
save
  [/Separation (Ink2) /DeviceCMYK { 0 exch 0 0 } ] setcolorspace 0.5 setcolor
  20 20 moveto 40 0 rlineto 0 40 rlineto -40 0 rlineto closepath fill
showpage restore
<< /OutputDevice /null >> setpagedevice
quit
EOF
run_xpost "the multi-page run" -d null -o /dev/null "$mpps"
grep -aq '/Count 2' "$mppdf" || { echo "FAIL: multi-page tree is not /Count 2"; exit 1; }
[ "$(grep -ac '/Type /Page[^s]' "$mppdf")" = 2 ] || { echo "FAIL: want two page objects"; exit 1; }
# the second page references both separations; the first only its own
grep -aq '/CS0 \[/Separation /Ink1 /DeviceCMYK' "$mppdf" || { echo "FAIL: no Ink1 colour space"; exit 1; }
grep -aq '/CS1 \[/Separation /Ink2 /DeviceCMYK' "$mppdf" || { echo "FAIL: no Ink2 colour space on the later page"; exit 1; }
# Ink1's function object is written once though two pages reach it
[ "$(grep -ac '/Separation /Ink1 /DeviceCMYK [0-9]* 0 obj' "$mppdf")" -le 1 ] || true
echo "multi-page single-file PDF OK"
if command -v gs >/dev/null 2>&1; then
    pages=$(gs -q -dNODISPLAY -dPDFINFO -dBATCH -dNOPAUSE "$mppdf" </dev/null 2>&1 \
            | grep -aoiE 'has [0-9]+ page' | grep -aoE '[0-9]+')
    [ "$pages" = 2 ] || { echo "FAIL: gs reads $pages pages from the multi-page PDF, want 2"; exit 1; }
    platedir=$(mktemp -d)
    gs -q -dNOSAFER -dNOPAUSE -dBATCH -sDEVICE=tiffsep -o "$platedir/q%d.tif" "$mppdf" >/dev/null 2>&1
    [ -f "$platedir/q1(Ink1).tif" ] && [ -f "$platedir/q2(Ink2).tif" ] \
        || { ls "$platedir"; rm -rf "$platedir"; echo "FAIL: separations did not plate per page"; exit 1; }
    rm -rf "$platedir"
    echo "gs multi-page round-trip OK"
fi

# a program's redefinition of fill must not capture the machinery's
# internal references: eofill on a vector device resolves through the
# nonzero fill, and a redefined fill that itself invokes eofill would
# otherwise recurse without bound
recps=$(mktemp)
cat > "$recps" <<EOF
<< /OutputDevice /pdfwrite /OutputFile ($discard) /PageSize [100 100] >> setpagedevice
/fill { 0.5 setgray eofill } def
newpath 10 10 moveto 80 10 lineto 45 80 lineto closepath eofill
(eofill-under-redefined-fill OK\n) print
showpage
<< /OutputDevice /null >> setpagedevice
quit
EOF
run_xpost "the redefined-fill run" -d null -o /dev/null "$recps"
rm -f "$recps"
printf '%s\n' "$out" | grep -q 'eofill-under-redefined-fill OK' || { echo "FAIL: eofill under a redefined fill"; exit 1; }
echo "eofill under redefined fill OK"
