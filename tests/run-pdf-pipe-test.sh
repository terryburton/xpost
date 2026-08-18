#!/bin/sh
# Meson test wrapper: pdfwrite must write a complete document to a
# destination that cannot be positioned.
#
# A named pipe answers no byte offset, so a writer that asks the file where
# it is fails at the first cross-reference entry, while one that counts the
# bytes it has emitted does not. The document written down the pipe must
# also match the one written to a regular file byte for byte, which is what
# catches a count that drifts from what was actually put out.
#
# That last reading compares one of this run's documents against another
# of them, so it says the two routes agree and cannot say the document
# was right. A fault in what both routes share passes here; the bytes a
# pdfwrite page has to be are the golden-render manifest's.
#
#   $1  path to the built xpost binary
#   $2  path to the PostScript workload
set -u
xpost=$1
script=$2
. "$(dirname "$0")/verdict.sh"

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT INT TERM

# A shell that emulates named pipes for its own programs can make one
# that a native program cannot open: mkfifo succeeding says the shell has
# them, not that the interpreter under test can be handed one.
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*)
        echo "SKIP: the named pipes here are the shell's, not the platform's"
        exit 77 ;;
esac

fifo=$dir/pipe.pdf
mkfifo "$fifo" || { echo "SKIP: no named pipe support"; exit 77; }

# drain the pipe while the interpreter fills it, or the writer blocks
cat "$fifo" > "$dir/frompipe.pdf" &
drain=$!

out=$("$xpost" -q --no-sandbox -d pdfwrite -o "$fifo" "$script" </dev/null 2>&1)
status=$?
wait "$drain"

verdict_run "$status" "$out" "the interpreter" || exit 1

head -c 8 "$dir/frompipe.pdf" | LC_ALL=C grep -q '%PDF-1' || {
    echo "FAIL: no PDF header -- nothing reached the pipe"
    exit 1
}
tail -c 16 "$dir/frompipe.pdf" | LC_ALL=C grep -q '%%EOF' || {
    echo "FAIL: no EOF trailer -- the document was cut short"
    exit 1
}

# The offset the trailer points at must be where the table really starts.
# A document holds bytes that are not text, and the tools reading it here
# are told to work in bytes: one that decodes its input as characters
# stops at the first byte that is not one, and reports nothing found
# rather than saying why.
xoff=$(LC_ALL=C tr -d '\000' < "$dir/frompipe.pdf" \
    | LC_ALL=C sed -n '/^startxref$/{n;p;}' | tail -1)
case "$xoff" in
    ''|*[!0-9]*) echo "FAIL: no startxref offset in the trailer"; exit 1 ;;
esac
found=$(tail -c "+$((xoff + 1))" "$dir/frompipe.pdf" | head -c 4)
[ "$found" = "xref" ] || {
    echo "FAIL: startxref says $xoff, which holds '$found' and not the table"
    exit 1
}

# and the same document sent to a regular file must come out identical
out=$("$xpost" -q --no-sandbox -d pdfwrite -o "$dir/tofile.pdf" "$script" </dev/null 2>&1)
status=$?
verdict_run "$status" "$out" "the interpreter writing to a file" || exit 1

cmp -s "$dir/tofile.pdf" "$dir/frompipe.pdf" || {
    echo "FAIL: the pipe and the file disagree on the document"
    exit 1
}

echo "SUCCESS"
