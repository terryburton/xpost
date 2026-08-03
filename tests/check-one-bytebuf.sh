#!/bin/sh
# Guard the single growable byte buffer. Every builder in the tree that
# assembles a byte stream of unknown final size -- the vector devices'
# page content, the font module's font programs and glyph fragments, a
# deflated stream, a rereadable file's captured source -- grows it
# through src/lib/xpost_strbuf.h, and every one of them reads the same
# answer from it.
#
# A second grow loop, a buffer carried as a raw pointer/length/capacity
# triple, or a caller reading the answer as a success flag means the
# tree is growing byte buffers more than one way again.
#
#   $1  path to the source tree root
set -u
src=${1:?usage: check-one-bytebuf.sh <source root>}
lib=$src/src/lib
fail=0

# the buffer type is declared in exactly one place
hits=$(grep -l '^} Xpost_String_Buffer;' "$lib"/*.h "$lib"/*.c 2>/dev/null || true)
if [ "$hits" != "$lib/xpost_strbuf.h" ]; then
    echo "check-one-bytebuf: expected the only growable byte buffer in src/lib/xpost_strbuf.h, found:"
    printf '%s\n' "${hits:-none}"
    fail=1
fi

# it answers one convention: 0 for no error, the error code otherwise
if grep -qE '^ *return (-1|1);' "$lib/xpost_strbuf.h"; then
    echo "check-one-bytebuf: xpost_strbuf.h answers a value that is neither 0 nor an error code:"
    grep -nE '^ *return (-1|1);' "$lib/xpost_strbuf.h"
    fail=1
fi
if ! grep -q 'return VMerror;' "$lib/xpost_strbuf.h"; then
    echo "check-one-bytebuf: xpost_strbuf.h no longer answers VMerror for failure"
    fail=1
fi

# no byte buffer is threaded through a call as a raw pointer/length/capacity
# triple rather than as the buffer itself
hits=$(grep -rnE 'char \*\*[A-Za-z_]+, *size_t \*' "$lib" "$src/src/bin" 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "check-one-bytebuf: a byte buffer is passed as a raw pointer/length/capacity triple:"
    printf '%s\n' "$hits"
    fail=1
fi

# the byte-stream builders grow nothing of their own: every reallocation
# left in them sizes an array of typed elements
for f in xpost_op_font.c xpost_dev_generic.c xpost_file.c; do
    if ! grep -q '#include "xpost_strbuf.h"' "$lib/$f"; then
        echo "check-one-bytebuf: $f does not reach the shared byte buffer"
        fail=1
    fi
    hits=$(grep -n 'realloc(' "$lib/$f" | grep -v 'sizeof' | grep -v 'write_capacity' || true)
    if [ -n "$hits" ]; then
        echo "check-one-bytebuf: $f grows a byte buffer of its own:"
        printf '%s\n' "$hits"
        fail=1
    fi
done

# nobody reads the answer as a success flag
hits=$(grep -rnE 'if \(!(xpost_strbuf_|xpost_dev_pdf_append)' "$lib" 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "check-one-bytebuf: a caller reads a buffer answer as a success flag:"
    printf '%s\n' "$hits"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "check-one-bytebuf: the tree grows byte buffers more than one way."
    exit 1
fi
echo "check-one-bytebuf: ok (one buffer, one convention)"
exit 0
