/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the Xpost software product nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef XPOST_STRBUF_H
#define XPOST_STRBUF_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xpost_error.h"

/* A growable byte buffer: the one allocation discipline behind every
   builder that assembles a byte stream of unknown final size -- the
   vector devices' page content, the font module's font programs, a
   compressed stream, a rereadable file's captured bytes. Growth doubles
   the capacity, so a build is linear in its output.

   Every function answers the operator convention the rest of the tree
   answers: 0 for no error, and otherwise the error code to raise. The
   only error a byte buffer has is VMerror, so a caller may return the
   result of a call directly. A call that answers VMerror leaves the
   buffer exactly as it found it, still holding its bytes and still
   usable; the caller releases it with xpost_strbuf_free.

   A zero-filled Xpost_String_Buffer is a valid empty buffer, so a holder
   that arrives by byte copy rather than by construction needs no
   initialiser. */
typedef struct
{
    char *s;
    size_t len;
    size_t cap;
} Xpost_String_Buffer;

static inline int
xpost_strbuf_init(Xpost_String_Buffer *b, size_t initial)
{
    if (initial < 16)
        initial = 16;
    b->s = (char *)malloc(initial);
    b->len = 0;
    b->cap = b->s ? initial : 0;
    return b->s ? 0 : VMerror;
}

/* make room for extra more bytes past the current length */
static inline int
xpost_strbuf_reserve(Xpost_String_Buffer *b, size_t extra)
{
    size_t need, cap;
    char *ns;

    if (extra > (size_t)-1 - b->len)
        return VMerror;
    need = b->len + extra;
    if (need <= b->cap)
        return 0;
    cap = b->cap ? b->cap : 16;
    while (cap < need)
    {
        if (cap > ((size_t)-1) / 2)
        {
            cap = need;   /* the last doubling would wrap */
            break;
        }
        cap *= 2;
    }
    ns = (char *)realloc(b->s, cap);
    if (!ns)
        return VMerror;
    b->s = ns;
    b->cap = cap;
    return 0;
}

static inline int
xpost_strbuf_append(Xpost_String_Buffer *b, const void *p, size_t n)
{
    int ret;

    if (!n)
        return 0;
    ret = xpost_strbuf_reserve(b, n);
    if (ret)
        return ret;
    memcpy(b->s + b->len, p, n);
    b->len += n;
    return 0;
}

/* append the formatted text, sized before it is written so the buffer
   grows once and the format runs at most twice */
static inline int
xpost_strbuf_appendf(Xpost_String_Buffer *b, const char *fmt, ...)
{
    va_list ap;
    int n, ret;

    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0)
        return VMerror;
    ret = xpost_strbuf_reserve(b, (size_t)n + 1);
    if (ret)
        return ret;
    va_start(ap, fmt);
    vsnprintf(b->s + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
    return 0;
}

static inline void
xpost_strbuf_free(Xpost_String_Buffer *b)
{
    free(b->s);
    b->s = NULL;
    b->len = b->cap = 0;
}

#endif
