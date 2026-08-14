/* The one growable byte buffer (src/lib/xpost_strbuf.h): the single
   allocation discipline behind every builder in the tree that assembles a
   byte stream of unknown final size -- the vector devices' content
   accumulator and the font module's font-program emitters.

   Pin the contract the callers rely on:
   - the return convention is the codebase's mutator convention, 0 for no
     error and the error code to raise otherwise, so a caller may return the
     result directly;
   - a zero-initialised buffer is a valid empty buffer, since the vector
     devices carry theirs through a byte-for-byte copy;
   - content survives an arbitrary number of doublings byte for byte,
     including embedded zero bytes, which font programs and compressed
     content streams both contain;
   - a request no allocator can satisfy answers the error rather than
     wrapping the capacity computation around, and leaves the buffer
     usable. */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xpost_error.h"
#include "xpost_strbuf.h"

#include "xpost_test.h"

/* the mirror the buffer is compared against */
static char *model = NULL;
static size_t modellen = 0, modelcap = 0;

/* Accumulate the bytes the buffer is expected to be holding.

   A growth arrives in a pointer of its own and the model is moved onto
   it only once it has arrived, so a growth there is no memory for
   leaves the bytes already accumulated still reachable through the
   model, and leaves the capacity still saying what the model holds.

   A model with no buffer grows whatever length is asked of it, so past
   the growth there is always a buffer to address. That is one test in
   place of reading the arithmetic below it for the same answer: an
   accumulation of nothing onto nothing leaves the length no greater
   than the capacity, and the copy would then be addressing a
   destination that was never allocated. */
static void model_add(const void *p, size_t n)
{
    if (!model || modellen + n > modelcap)
    {
        size_t cap = (modellen + n) * 2 + 1024;
        char *grown = (char *)realloc(model, cap);

        if (!grown)
            exit(2);
        model = grown;
        modelcap = cap;
    }
    memcpy(model + modellen, p, n);
    modellen += n;
}

int main(void)
{
    Xpost_String_Buffer b;
    Xpost_String_Buffer z;
    char bin[256];
    size_t precap;
    char *presave;
    int i;

    /* the convention, stated by the successful call */
    check(xpost_strbuf_init(&b, 16) == 0, "init answers 0 for no error");
    check(b.len == 0, "a fresh buffer is empty");
    check(xpost_strbuf_append(&b, "ab", 2) == 0, "append answers 0 for no error");
    check(xpost_strbuf_appendf(&b, "%d", 12) == 0, "appendf answers 0 for no error");
    check(xpost_strbuf_reserve(&b, 4) == 0, "reserve answers 0 for no error");
    check(b.len == 4 && memcmp(b.s, "ab12", 4) == 0, "the two appends land in order");
    xpost_strbuf_free(&b);
    check(b.s == NULL && b.len == 0 && b.cap == 0, "free empties the buffer");

    /* a zero-initialised buffer is a valid empty buffer: the vector devices
       load theirs out of a byte string with no constructor run */
    memset(&z, 0, sizeof z);
    check(xpost_strbuf_append(&z, "", 0) == 0,
          "appending nothing to a zeroed buffer answers 0");
    check(z.s == NULL && z.len == 0 && z.cap == 0,
          "appending nothing allocates nothing");
    check(xpost_strbuf_append(&z, "x", 1) == 0, "append to a zeroed buffer answers 0");
    check(z.len == 1 && z.s && z.s[0] == 'x', "the zeroed buffer took the byte");
    xpost_strbuf_free(&z);

    /* content survives many doublings byte for byte */
    for (i = 0; i < 256; i++)
        bin[i] = (char)i;
    check(xpost_strbuf_init(&b, 16) == 0, "init for the growth run");
    for (i = 0; i < 20000; i++)
    {
        size_t n;

        if (xpost_strbuf_appendf(&b, "<%d:%s>", i, "abcdefghij"))
        {
            check(0, "appendf across the growth run");
            break;
        }
        {
            char t[64];
            int k = sprintf(t, "<%d:%s>", i, "abcdefghij");
            model_add(t, (size_t)k);
        }
        n = (size_t)(i % 256);
        if (xpost_strbuf_append(&b, bin, n))
        {
            check(0, "append across the growth run");
            break;
        }
        model_add(bin, n);
    }
    check(b.len == modellen, "the grown buffer has the exact accumulated length");
    check(b.len == modellen && memcmp(b.s, model, modellen) == 0,
          "every byte survives the doublings, embedded zero bytes included");
    check(b.cap >= b.len, "capacity covers the content");

    /* the failure path: a request no allocator can satisfy answers the
       error, leaves the buffer as it was, and does not wrap the capacity
       computation around into a spin. The bound is the largest object
       rather than the largest size_t, so a length one byte past it is
       refused with the same answer and the same buffer left behind. */
    presave = b.s;
    precap = b.cap;
    check(xpost_strbuf_reserve(&b, (size_t)-1 - b.len - 8) == VMerror,
          "an unsatisfiable reserve answers VMerror");
    check(b.s == presave && b.len == modellen && b.cap == precap,
          "a failed reserve leaves the buffer as it was");
    check(xpost_strbuf_reserve(&b, (size_t)PTRDIFF_MAX + 1 - b.len) == VMerror,
          "a reserve one byte past the largest object answers VMerror");
    check(b.s == presave && b.len == modellen && b.cap == precap,
          "a reserve refused at the bound leaves the buffer as it was");
    check(xpost_strbuf_append(&b, "tail", 4) == 0,
          "the buffer still takes bytes after a failed reserve");
    check(b.len == modellen + 4 && memcmp(b.s + modellen, "tail", 4) == 0,
          "the bytes after a failed reserve land correctly");
    xpost_strbuf_free(&b);
    free(model);

    return verdict();
}
