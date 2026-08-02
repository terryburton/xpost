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

/* A growable byte buffer: the one allocation discipline behind every
   builder that assembles a byte stream of unknown final size. Growth
   doubles the capacity, so a build is linear in its output. All
   functions return 0 on success and -1 when memory runs out, leaving
   the buffer valid either way; the caller frees s. */
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
    b->s = malloc(initial);
    b->len = 0;
    b->cap = b->s ? initial : 0;
    return b->s ? 0 : -1;
}

static inline int
xpost_strbuf_reserve(Xpost_String_Buffer *b, size_t extra)
{
    size_t need = b->len + extra;
    size_t cap = b->cap ? b->cap : 16;
    char *ns;

    if (need <= b->cap)
        return 0;
    while (cap <= need)
        cap *= 2;
    ns = realloc(b->s, cap);
    if (!ns)
        return -1;
    b->s = ns;
    b->cap = cap;
    return 0;
}

static inline int
xpost_strbuf_append(Xpost_String_Buffer *b, const void *p, size_t n)
{
    if (xpost_strbuf_reserve(b, n + 1))
        return -1;
    memcpy(b->s + b->len, p, n);
    b->len += n;
    return 0;
}

static inline int
xpost_strbuf_appendf(Xpost_String_Buffer *b, const char *fmt, ...)
{
    va_list ap;
    int n;

    for (;;)
    {
        va_start(ap, fmt);
        n = vsnprintf(b->s + b->len, b->cap - b->len, fmt, ap);
        va_end(ap);
        if (n < 0)
            return -1;
        if (b->len + (size_t)n < b->cap)
        {
            b->len += (size_t)n;
            return 0;
        }
        if (xpost_strbuf_reserve(b, (size_t)n + 1))
            return -1;
    }
}

static inline void
xpost_strbuf_free(Xpost_String_Buffer *b)
{
    free(b->s);
    b->s = NULL;
    b->len = b->cap = 0;
}

#endif
