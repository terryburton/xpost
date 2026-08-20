#!/bin/sh
# A DOCINFO pdfmark's key and value are written into the PDF's Info
# dictionary, so a program that supplies them must not be able to write
# raw PDF bytes through them. Drive pdfwrite with a DOCINFO whose key
# spells the bytes that end a name and begin a new object, and require the
# written file to carry those bytes escaped -- inside the one name they
# belong to -- rather than as the object they spell. Then drive it with an
# ordinary DOCINFO and require the key and value to arrive intact, so the
# escaping is not simply dropping metadata.
#   $1  path to the built xpost binary
set -u
xpost=$1
. "$(dirname "$0")/verdict.sh"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM

# 1. Injection attempt: a key that, unescaped, would close the name with
#    ">>", end the Info object, and open "50 0 obj << /Injected (pwned) >>".
cat > "$work/inj.ps" <<'PS'
[ (Title>>endobj
50 0 obj
<</Injected(pwned)>>
endobj
51 0 obj<<) cvn (v) /DOCINFO pdfmark
0 0 moveto 10 10 lineto stroke showpage
PS
out=$("$xpost" -q -d pdfwrite -o "$work/inj.pdf" "$work/inj.ps" </dev/null 2>&1)
verdict_run "$?" "$out" "the injection run" || exit 1

# The spelled object must not appear as a real one: no object header at the
# left margin, and no injected key. The escaped form keeps the same bytes
# as #XX inside the /Title... name, where they mark nothing.
if grep -qE '^50 0 obj' "$work/inj.pdf"; then
    echo "FAIL: a DOCINFO key wrote a raw object header into the file"
    exit 1
fi
if grep -q '/Injected' "$work/inj.pdf"; then
    echo "FAIL: a DOCINFO key injected an unescaped key into the file"
    exit 1
fi
echo "docinfo key injection is escaped"

# 2. An ordinary DOCINFO is still written, key and both value kinds intact:
#    a string value wrapped, a name value written as a name.
cat > "$work/ok.ps" <<'PS'
[ /Title (My Document) /Trapped /True /DOCINFO pdfmark
0 0 moveto 10 10 lineto stroke showpage
PS
out=$("$xpost" -q -d pdfwrite -o "$work/ok.pdf" "$work/ok.ps" </dev/null 2>&1)
verdict_run "$?" "$out" "the ordinary run" || exit 1

if ! grep -q '/Title (My Document)' "$work/ok.pdf"; then
    echo "FAIL: an ordinary string-valued Title was not written intact"
    exit 1
fi
if ! grep -q '/Trapped /True' "$work/ok.pdf"; then
    echo "FAIL: a name-valued key was not written as a PDF name"
    exit 1
fi
echo "ordinary docinfo is written intact"

echo "check-pdf-docinfo: ok"
exit 0
