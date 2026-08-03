#!/bin/sh
# Meson test wrapper: %statementedit reads a whole PostScript statement
# from the standard input, however many lines it takes.
#
# The special file gathers input until what it has is syntactically
# complete: a statement that opens a procedure, a string or a hexadecimal
# string is not finished at the end of the line, so it keeps reading. The
# nesting it tracks is the only thing that decides where the statement
# ends, so an escaped parenthesis must not close a string.
#
# Nothing else in the suite supplies standard input, so none of this had
# ever run.
#
#   $1  path to the built xpost binary
set -u
xpost=$1
case $xpost in /*) ;; *) xpost=$PWD/$xpost ;; esac

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The statement is written out as its own bytes, so the comparison below
# needs no escaping on either side.
cat > "$work/read.ps" <<PSEOF
/f (%statementedit) (r) file def
/s 400 string def
f s readstring pop
/o ($work/got.txt) (w) file def
o exch writestring
o closefile
quit
PSEOF

fail=0
check() { # description  input  expected
    rm -f "$work/got.txt"
    printf '%b' "$2" | "$xpost" -q --no-sandbox -d null "$work/read.ps" >/dev/null 2>&1
    printf '%b' "$3" > "$work/want.txt"
    if ! cmp -s "$work/got.txt" "$work/want.txt"; then
        echo "FAIL: $1"
        echo "      want: $(od -c < "$work/want.txt" | head -2 | tr '\n' ' ')"
        echo "      got:  $(od -c < "$work/got.txt" 2>/dev/null | head -2 | tr '\n' ' ')"
        fail=1
    fi
}

check "a statement on one line is that line" \
      '1 2 add\n' '1 2 add'
check "a procedure is read until its brace closes" \
      '{ 1 2\nadd }\n' '{ 1 2\nadd }'
check "nested procedures close from the inside out" \
      '{ { 1 }\n2 }\n' '{ { 1 }\n2 }'
check "a string is read until its parenthesis closes" \
      '(abc\ndef)\n' '(abc\ndef)'
# The escape matters at the end of a line: an escaped parenthesis leaves
# the string open, so reading continues onto the next line. Taken as a
# closing parenthesis it would end the statement there instead.
check "an escaped parenthesis leaves the string open across a line" \
      '(a\\)\nb)\n' '(a\\)\nb)'
check "a hexadecimal string is read until its bracket closes" \
      '<0102\n0304>\n' '<0102\n0304>'
check "input that ends mid-statement yields what there was" \
      '{ 1 2' '{ 1 2'

[ "$fail" = 0 ] || { echo "FAILURES: the statements above"; exit 1; }
echo "SUCCESS"
