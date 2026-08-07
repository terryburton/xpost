#!/bin/sh
# Meson wrapper: the pngalpha device must write RGBA where the erased
# page is transparent, marks are opaque, and an explicit white fill
# stays opaque (distinct from the page background); the png device must
# stay plain RGB. Colour types come from the IHDR; pixel semantics are
# checked when python3 is available.
#   $1  path to the built xpost binary
set -u
xpost=$1
. "$(dirname "$0")/verdict.sh"

ps=$(mktemp)
outa=$(mktemp)
outrgb=$(mktemp)
trap 'rm -f "$ps" "$outa" "$outrgb"' EXIT

cat > "$ps" <<'EOF'
newpath 20 20 moveto 100 20 lineto 100 60 lineto 20 60 lineto closepath fill
1 setgray newpath 120 20 moveto 200 20 lineto 200 60 lineto 120 60 lineto closepath fill
% anti-aliased text: the glyph edges are blended against the page, which
% is the device's blending method rather than its rectangle fill
0 setgray /Helvetica findfont 24 scalefont setfont
20 90 moveto (Ag) show
showpage
quit
EOF

# the two images are read below; how each run left is read here, since a
# device whose teardown faults has already written the image the checks
# below open
out=$("$xpost" -q -d pngalpha -o "$outa" "$ps" </dev/null 2>&1)
verdict_run "$?" "$out" "the pngalpha run" || exit 1
out=$("$xpost" -q -d png -o "$outrgb" "$ps" </dev/null 2>&1)
verdict_run "$?" "$out" "the png run" || exit 1

# IHDR colour type: byte 25 of the file (2 = RGB, 6 = RGBA)
ct() { od -An -j25 -N1 -tu1 "$1" | tr -d ' '; }
[ "$(ct "$outa")" = 6 ]   || { echo "FAIL: pngalpha colour type $(ct "$outa"), want 6"; exit 1; }
[ "$(ct "$outrgb")" = 2 ] || { echo "FAIL: png colour type $(ct "$outrgb"), want 2"; exit 1; }
echo "colour types OK (RGBA=6, RGB=2)"

if command -v python3 >/dev/null 2>&1; then
    python3 - "$outa" <<'PYEOF'
import struct, sys, zlib
d = open(sys.argv[1],'rb').read()
pos, idat, meta = 8, b'', {}
while pos < len(d):
    ln, typ = struct.unpack('>I4s', d[pos:pos+8])
    if typ == b'IHDR':
        meta['w'], meta['h'] = struct.unpack('>II', d[pos+8:pos+16])
    if typ == b'IDAT':
        idat += d[pos+8:pos+8+ln]
    pos += 12 + ln
raw = zlib.decompress(idat)
W, H = meta['w'], meta['h']
stride = W*4
out, prev, i = bytearray(), bytearray(stride), 0
for y in range(H):
    f = raw[i]; i += 1
    line = bytearray(raw[i:i+stride]); i += stride
    for x in range(stride):
        a = line[x-4] if x>=4 else 0
        b = prev[x]
        c = prev[x-4] if x>=4 else 0
        if f==1: line[x]=(line[x]+a)&255
        elif f==2: line[x]=(line[x]+b)&255
        elif f==3: line[x]=(line[x]+(a+b)//2)&255
        elif f==4:
            pp=a+b-c; pa,pb,pc=abs(pp-a),abs(pp-b),abs(pp-c)
            line[x]=(line[x]+(a if (pa<=pb and pa<=pc) else (b if pb<=pc else c)))&255
    out += line; prev = line
def pix(x,y):
    o=(y*W+x)*4; return tuple(out[o:o+4])
# A glyph rendered with anti-aliasing has edge pixels only partly
# covered: their alpha lies strictly between transparent and opaque.
# A device that painted glyphs without blending would give every pixel
# one or the other.
alphas = {out[(y*W+x)*4 + 3] for y in range(H) for x in range(W)}
partial = {a for a in alphas if 0 < a < 255}

checks = [
    (pix(2,2)[3] == 0,               "erased page is transparent"),
    (pix(60,H-40) == (0,0,0,255),    "ink is opaque"),
    (pix(160,H-40) == (255,255,255,255), "an explicit white fill is opaque"),
    (255 in alphas,                  "the text rendered at all"),
    (len(partial) > 0,               "anti-aliased glyph edges are partly covered"),
]
bad = [msg for ok,msg in checks if not ok]
for m in bad: print("FAIL:", m)
sys.exit(1 if bad else 0)
PYEOF
    [ $? -eq 0 ] || exit 1
    echo "alpha semantics OK"
else
    echo "python3 not found: pixel semantics not checked"
fi
